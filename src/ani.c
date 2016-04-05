/*	ani.c
	Copyright (C) 2005-2016 Mark Tyler and Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/

#include <stdio.h>
#include <sys/types.h>
#include <errno.h>

#include "global.h"
#undef _
#define _(X) X

#include "mygtk.h"
#include "memory.h"
#include "vcode.h"
#include "ani.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "canvas.h"
#include "viewer.h"
#include "layer.h"
#include "spawn.h"
#include "inifile.h"
#include "mtlib.h"
#include "wu.h"

typedef struct {
	int frame1, frame2;
	int nlayer, lnum, layer;
	int lock;
	char *pos, *cyc; // text buffers
	void **posw, **cycw;
	void **save, **preview, **frames;
} anim_dd;

typedef struct {
	void **awin;
	int fix;
	int frame[3];
	void **fslider;
} apview_dd;

///	GLOBALS

int ani_state;

int	ani_frame1 = 1, ani_frame2 = 1, ani_gif_delay = 10;
static int ani_play_state, ani_timer_state;



///	FORM VARIABLES

ani_cycle ani_cycle_table[MAX_CYC_SLOTS];

static char ani_output_path[PATHBUF], ani_file_prefix[ANI_PREFIX_LEN+2];
static int ani_use_gif;



static void ani_win_read_widgets(void **wdata);




static void ani_widget_changed(anim_dd *dt)	// Widget changed so flag the layers as changed
{
	if (!dt->lock) layers_changed = 1;
}



static void set_layer_from_slot( int layer, int slot )		// Set layer x, y, opacity from slot
{
	ani_slot *ani = layer_table[layer].image->ani_.pos + slot;
	layer_table[layer].x = ani->x;
	layer_table[layer].y = ani->y;
	layer_table[layer].opacity = ani->opacity;
}

static void set_layer_inbetween( int layer, int i, int frame, int effect )		// Calculate in between value for layer from slot i & (i+1) at given frame
{
	MT_Coor c[4], co_res, lenz;
	float p1, p2;
	int f0, f1, f2, f3, ii[4] = {i-1, i, i+1, i+2}, j;
	ani_slot *ani = layer_table[layer].image->ani_.pos;


	f1 = ani[i].frame;
	f2 = ani[i + 1].frame;

	if (i > 0) f0 = ani[i - 1].frame;
	else
	{
		f0 = f1;
		ii[0] = ii[1];
	}

	if ((i >= MAX_POS_SLOTS - 2) || !(f3 = ani[i + 2].frame))
	{
		f3 = f2;
		ii[3] = ii[2];
	}

		// Linear in between
	p1 = ( (float) (f2-frame) ) / ( (float) (f2-f1) );	// % of (i-1) slot
	p2 = 1-p1;						// % of i slot

	layer_table[layer].x = rint(p1 * ani[i].x + p2 * ani[i + 1].x);
	layer_table[layer].y = rint(p1 * ani[i].y + p2 * ani[i + 1].y);
	layer_table[layer].opacity = rint(p1 * ani[i].opacity +
		p2 * ani[i + 1].opacity);


	if ( effect == 1 )		// Interpolated smooth in between - use p2 value
	{
		for ( i=0; i<4; i++ ) c[i].z = 0;	// Unused plane
		lenz.x = f1 - f0;
		lenz.y = f2 - f1;			// Frames for each line
		lenz.z = f3 - f2;

		if ( lenz.x<1 ) lenz.x = 1;
		if ( lenz.y<1 ) lenz.y = 1;
		if ( lenz.z<1 ) lenz.z = 1;

		// Set up coords
		for ( j=0; j<4; j++ )
		{
			c[j].x = ani[ii[j]].x;
			c[j].y = ani[ii[j]].y;
		}
		co_res = MT_palin(p2, 0.35, c[0], c[1], c[2], c[3], lenz);

		layer_table[layer].x = co_res.x;
		layer_table[layer].y = co_res.y;
	}
}

static void ani_set_frame_state(int frame)
{
	int i, j, k, v, c, f, p;
	ani_slot *ani;
	ani_cycle *cc;
	unsigned char *cp;

// !!! Maybe make background x/y settable here too?
	for (k = 1; k <= layers_total; k++)
	{
		/* Set x, y, opacity for layer */
		ani = layer_table[k].image->ani_.pos;

		/* Find first frame in position list that excedes or equals 'frame' */
		for (i = 0; i < MAX_POS_SLOTS; i++)
		{
			/* End of list */
			if (ani[i].frame <= 0) break;
			/* Exact match or one exceding it found */
			if (ani[i].frame >= frame) break;
		}

		/* If no slots have been defined
		 * leave the layer x, y, opacity as now */
		if (ani[0].frame <= 0);
		/* All position slots < 'frame'
		 * Set layer pos/opac to last slot values */
		else if ((i >= MAX_POS_SLOTS) || !ani[i].frame)
			set_layer_from_slot(k, i - 1);
		/* If closest frame = requested frame, set all values to this
		 * ditto if i=0, i.e. no better matches exist */
		else if ((ani[i].frame == frame) || !i)
			set_layer_from_slot(k, i);
		/* i is currently pointing to slot that excedes 'frame',
		 * so in between this and the previous slot */
		else set_layer_inbetween(k, i - 1, frame, ani[i - 1].effect);

		/* Set visibility for layer by processing cycle table */
		/* !!! Table must be sorted by cycle # */
		v = -1; // Leave alone by default
		cp = layer_table[k].image->ani_.cycles;
		for (i = c = 0; i < MAX_CYC_ITEMS; i++ , c = j)
		{
			if (!(j = *cp++)) break; // Cycle # + 1
			p = *cp++;
			cc = ani_cycle_table + j - 1;
			if (!(f = cc->frame0)) continue; // Paranoia
			if (f > frame) continue; // Not yet active
			/* Special case for enabling/disabling en-masse */
			if (cc->frame1 == f)
			{
				if (j != c) v = !p; // Ignore entries after 1st
			}
			/* Inside a normal cycle */
			else if (cc->frame1 >= frame)
			{
				if (j != c) v = 0; // Hide initially
				v |= (frame - f) % cc->len == p; // Show if matched
			}
		}
		if (v >= 0) layer_table[k].visible = v;
	}
}

#define ANI_CYC_ROWLEN (MAX_CYC_ITEMS * 2 + 1)
#define ANI_CYC_TEXT_MAX (128 + MAX_CYC_ITEMS * 10)

/* Convert cycle header & layers list to text */
static void ani_cyc_sprintf(char *txt, ani_cycle *chead, unsigned char *cdata)
{
	int j, l, b;
	char *tmp = txt + sprintf(txt, "%i\t%i\t", chead->frame0, chead->frame1);

	l = *cdata++;
	if (!l); // Empty group
	else if (chead->frame0 == chead->frame1) // Batch toggle group
	{
		while (TRUE)
		{
			tmp += sprintf(tmp, "%s%i", cdata[0] ? "-" : "", cdata[1]);
			if (--l <= 0) break;
			*tmp++ = ',';
			cdata += 2;
		}
	}
	else // Regular cycle
	{
		b = -1;
		while (TRUE)
		{
			j = cdata[0] - b;
			while (--j > 0) *tmp++ = '0' , *tmp++ = ',';
			b = cdata[0];
			if (--l <= 0) break;
			if ((cdata[2] == b) && (j >= 0)) *tmp++ = '(';
			tmp += sprintf(tmp, "%i%s,", cdata[1],
				(cdata[2] != b) && (j < 0) ? ")" : "");
			cdata += 2;
		}
		tmp += sprintf(tmp, "%i%s", cdata[1], j < 0 ? ")" : "");
		j = chead->len - cdata[0];
		while (--j > 0) *tmp++ = ',' , *tmp++ = '0';
	}
	strcpy(tmp, "\n");
}

/* Parse text into cycle header & layers list */
static int ani_cyc_sscanf(char *txt, ani_cycle *chead, unsigned char *cdata)
{
	char *tail;
	unsigned char *cntp;
	int j, l, f, f0, f1, b;

	while (*txt && (*txt < 32)) txt++;	// Skip non ascii chars
	if (!*txt) return (FALSE);
	f0 = f1 = -1;		// Default state if invalid
	l = 0; sscanf(txt, "%i\t%i\t%n", &f0, &f1, &l);
	chead->frame0 = f0;
	chead->frame1 = f1;
	chead->len = *cdata = 0;
	if (!l) return (TRUE); // Invalid cycle is a cycle still
	txt += l;

	cntp = cdata++;
	f = b = 0;
	while (cdata - cntp < ANI_CYC_ROWLEN)
	{
		while (*txt && (*txt <= 32)) txt++;	// Skip whitespace etc
		if (*txt == '(')
		{
			b = 1; txt++;
			while (*txt && (*txt <= 32)) txt++;	// Skip whitespace etc
		}
		j = strtol(txt, &tail, 0);
		if (tail - txt <= 0) break; // No number there
		if (f0 == f1) if ((f = j < 0)) j = -j; // Batch toggle group
		if ((j < 0) || (j > MAX_LAYERS)) j = 0; // Out of range
		*cdata++ = f; // Position
		*cdata++ = j; // Layer
		cntp[0]++;
		txt = tail;
		while (*txt && (*txt <= 32)) txt++;	// Skip whitespace etc
		if (*txt == ')')
		{
			b = 0; txt++;
			while (*txt && (*txt <= 32)) txt++;	// Skip whitespace etc
		}
		if (*txt++ != ',') break;	// Stop if no comma
		f += 1 - b;
	}
	if (*cntp) chead->len = *(cdata - 2) + 1;

	return (TRUE);
}

static int cmp_frames(const void *f1, const void *f2)
{
	int n = (int)((unsigned char *)f1)[0] - ((unsigned char *)f2)[0];
	return (n ? n :
		(int)((unsigned char *)f1)[1] - ((unsigned char *)f2)[1]);
}

/* Assemble cycles info from per-layer lists into per-cycle ones */
static void ani_cyc_get(unsigned char *buf)
{
	unsigned char *ptr, *cycles;
	int i, j, k, l;

	memset(buf, 0, MAX_CYC_SLOTS * ANI_CYC_ROWLEN); // Clear
	buf -= ANI_CYC_ROWLEN; // 1-based
	for (i = 1; i <= layers_total; i++)
	{
		cycles = layer_table[i].image->ani_.cycles;
		for (j = 0; j < MAX_CYC_ITEMS; j++)
		{
			if (!(k = *cycles++)) break; // Cycle # + 1
			l = *cycles++; // Position
			if ((k > MAX_CYC_SLOTS) || (l >= MAX_CYC_ITEMS))
				continue;
			ptr = buf + ANI_CYC_ROWLEN * k;
			if (ptr[0] >= MAX_CYC_ITEMS) continue;
			k = ptr[0]++;
			ptr += k * 2 + 1;
			ptr[0] = l; // Position
			ptr[1] = i; // Layer
		}
	}
	for (i = 1; i <= MAX_CYC_SLOTS; i++) // Sort by position & layer
	{
		ptr = buf + ANI_CYC_ROWLEN * i;
		if (*ptr > 1) qsort(ptr + 1, *ptr, 2, cmp_frames);
	}
}

/* Distribute out cycles info from per-cycle to per-layer lists */
static void ani_cyc_put(unsigned char *buf)
{
	unsigned char *ptr, *cycles;
	int i, j, k, l, cnt[MAX_LAYERS + 1];

	memset(cnt, 0, sizeof(cnt)); // Clear
	buf -= ANI_CYC_ROWLEN; // 1-based
	for (i = 1; i <= MAX_CYC_SLOTS; i++)
	{
		ptr = buf + ANI_CYC_ROWLEN * i;
		j = *ptr++;
		while (j-- > 0)
		{
			l = *ptr++; // Position
			k = *ptr++; // Layer
			if (!k || (k > layers_total)) continue;
			if (cnt[k] >= MAX_CYC_ITEMS) continue;
			cycles = layer_table[k].image->ani_.cycles + cnt[k]++ * 2;
			cycles[0] = i; // Cycle # + 1
			cycles[1] = l; // Position
		}
	}
	for (i = 0; i <= layers_total; i++) // Mark end of list
		if (cnt[i] < MAX_CYC_ITEMS)
			layer_table[i].image->ani_.cycles[cnt[i] * 2] = 0;
}

#define ANI_POS_TEXT_MAX 256

static void ani_pos_sprintf(char *txt, ani_slot *ani)
{
	sprintf(txt, "%i\t%i\t%i\t%i\t%i\n",
		ani->frame, ani->x, ani->y, ani->opacity, ani->effect);
}

static int ani_pos_sscanf(char *txt, ani_slot *ani)
{
	ani_slot data = { -1, -1, -1, -1, -1 };

	while (*txt && (*txt < 32)) txt++;	// Skip non ascii chars
	if (!*txt) return (FALSE);
	// !!! Ignoring parse errors
	sscanf(txt, "%i\t%i\t%i\t%i\t%i", &data.frame, &data.x, &data.y,
		&data.opacity, &data.effect);
	*ani = data;
	return (TRUE);
}




static void ani_read_layer_data()	// Save current layer state
{
	memcpy(layer_table_p = layer_table + (MAX_LAYERS + 1), layer_table,
		(MAX_LAYERS + 1) * sizeof(layer_table[0]));
}

static void ani_write_layer_data(int final)	// Restore layer state
{
	memcpy(layer_table, layer_table_p,
		(MAX_LAYERS + 1) * sizeof(layer_table[0]));
	if (final) layer_table_p = layer_table;
}

static char *ani_cyc_txt()	// Text for the cycle text widget
{
	memx2 mem;
	char txt[ANI_CYC_TEXT_MAX];
	unsigned char buf[MAX_CYC_SLOTS * ANI_CYC_ROWLEN];
	int i;

	memset(&mem, 0, sizeof(mem));
	ani_cyc_get(buf);
	for (i = 0; i < MAX_CYC_SLOTS; i++)
	{
		if (!ani_cycle_table[i].frame0) break;
		ani_cyc_sprintf(txt, ani_cycle_table + i,
			buf + ANI_CYC_ROWLEN * i);
		addstr(&mem, txt, 1);
	}
	return (mem.buf);
}

static char *ani_pos_txt(int idx)	// Text for the position text widget
{
	memx2 mem;
	char txt[ANI_POS_TEXT_MAX];
	int j;
	ani_slot *ani;

	memset(&mem, 0, sizeof(mem));
	if (idx <= 0) return ""; // Must no be for background layer or negative => PANIC!

	for (j = 0; j < MAX_POS_SLOTS; j++)
	{
		ani = layer_table[idx].image->ani_.pos + j;
		if (ani->frame <= 0) break;
		// Add a line if one exists
		ani_pos_sprintf(txt, ani);
		addstr(&mem, txt, 1);
	}
	return (mem.buf);
}

void ani_init()			// Initialize variables/arrays etc. before loading or on startup
{
	int j;

	ani_frame1 = 1;
	ani_frame2 = 100;
	ani_gif_delay = 10;

	for (j = 0; j <= layers_total; j++)
		memset(&layer_table[j].image->ani_, 0, sizeof(ani_info));
	memset(ani_cycle_table, 0, sizeof(ani_cycle_table));

	strcpy(ani_output_path, "frames");
	strcpy(ani_file_prefix, "f");

	ani_use_gif = TRUE;
}



///	EXPORT ANIMATION FRAMES WINDOW


static void create_frames_ani();

static void ani_btn(anim_dd *dt, void **wdata, int what, void **where)
{
	ani_win_read_widgets(wdata);
	ani_write_layer_data(what == op_EVT_CANCEL);

	if (what == op_EVT_CANCEL)
	{
		run_destroy(wdata);

		ani_state = ANI_NONE;
		update_stuff(UPD_ALLV);
		return;
	}

	where = origin_slot(where);

	if (where == dt->preview) ani_but_preview(wdata);

	else if (where == dt->frames)
	{
		cmd_showhide(GET_WINDOW(wdata), FALSE);
		create_frames_ani();
		cmd_showhide(GET_WINDOW(wdata), TRUE);
	}

	else layer_press_save(); // "Save"
}


/* Read current positions in text input and store */
static void ani_parse_store_positions(char *tx, int layer)
{
	char *txt, *tmp;
	ani_slot *ani = layer_table[layer].image->ani_.pos;
	int i;

	tmp = tx;
	for (i = 0; i < MAX_POS_SLOTS; i++)
	{
		if (!(txt = tmp)) break;
		tmp = strchr(txt, '\n');
		if (tmp) *tmp++ = '\0';
		if (!ani_pos_sscanf(txt, ani + i)) break;
	}
	if (i < MAX_POS_SLOTS) ani[i].frame = 0;	// End delimeter
}

/* Read current cycles in text input and store */
static void ani_parse_store_cycles(char *tx)
{
	unsigned char buf[MAX_CYC_SLOTS * ANI_CYC_ROWLEN];
	char *txt, *tmp;
	int i;

	tmp = tx;
	memset(buf, 0, sizeof(buf));
	for (i = 0; i < MAX_CYC_SLOTS; i++)
	{
		if (!(txt = tmp)) break;
		tmp = strchr(txt, '\n');
		if (tmp) *tmp++ = '\0';
		if (!ani_cyc_sscanf(txt, ani_cycle_table + i,
			buf + ANI_CYC_ROWLEN * i)) break;
	}
	if (i < MAX_CYC_SLOTS) ani_cycle_table[i].frame0 = 0;	// End delimeter
	ani_cyc_put(buf);
}

static void ani_win_read_widgets(void **wdata)	// Read all widgets and set up relevant variables
{
	anim_dd *dt = GET_DDATA(wdata);
	char *tmp;
	int a, b;

	run_query(wdata);

	ani_parse_store_positions(dt->pos, dt->layer);
	ani_parse_store_cycles(dt->cyc);
	/* Update 2 text widgets */
	dt->lock = TRUE;
	cmd_setv(dt->posw, tmp = ani_pos_txt(dt->layer), 0);
	free(tmp);
	cmd_setv(dt->cycw, tmp = ani_cyc_txt(), 0);
	free(tmp);
	dt->lock = FALSE;

	a = dt->frame1;
	b = dt->frame2;
	ani_frame1 = a < b ? a : b;
	ani_frame2 = a < b ? b : a;
}


static gboolean ani_play_timer_call(gpointer data)
{
	if (!ani_play_state)
	{
		ani_timer_state = 0;
		return FALSE;			// Stop animating
	}
	else
	{
		apview_dd *dt = data;
		int i;

		cmd_read(dt->fslider, dt);
		i = dt->frame[0] + 1;
		if (i > dt->frame[2]) i = dt->frame[1];
		cmd_set(dt->fslider, i);
		return TRUE;
	}
}


///	PREVIEW WINDOW CALLBACKS

static void ani_frame_slider_moved(apview_dd *dt, void **wdata, int what,
	void **where)
{
	int x = 0, y = 0, w = mem_width, h = mem_height;

	cmd_read(where, dt);
	ani_set_frame_state(dt->frame[0]);

	if (layer_selected)
	{
		x = layer_table[0].x - layer_table[layer_selected].x;
		y = layer_table[0].y - layer_table[layer_selected].y;
		w = layer_table[0].image->image_.width;
		h = layer_table[0].image->image_.height;
	}

	vw_update_area(x, y, w, h);	// Update only the area we need
}

static void ani_play_btn(apview_dd *dt, void **wdata, int what, void **where)
{
	if (what == op_EVT_CANCEL)
	{
		void **awin = dt->awin; // for after run_destroy()

		ani_play_state = FALSE;	// Stop animation playing if necessary

		run_destroy(wdata);

		if (awin) cmd_showhide(GET_WINDOW(awin), TRUE);
		else
		{
			ani_write_layer_data(TRUE);
			ani_state = ANI_NONE;
			update_stuff(UPD_ALLV);
		}
	}
	else if (what == op_EVT_CHANGE) // Play toggle
	{
		cmd_read(where, dt);
		if (ani_play_state && !ani_timer_state) // Start timer
			ani_timer_state = threads_timeout_add(ani_gif_delay * 10,
				ani_play_timer_call, dt);
	}
	else /* if (what == op_EVT_CLICK) */ // Fix
	{
		ani_read_layer_data();
		layers_notify_changed();
		update_stuff(UPD_RENDER);
	}
}

#define WBbase apview_dd
static void *apview_code[] = {
	WPWHEREVER, WINDOWm(_("Animation Preview")),
	WXYWH("ani_prev", 200, 0),
	HBOXB, // !!! Originally the window was that way, and had no vbox
	UTOGGLEv(_("Play"), ani_play_state, ani_play_btn),
	BORDER(SPINSLIDE, 0),
	REF(fslider), MINWIDTH(200), XSPINSLIDEa(frame),
	EVENT(CHANGE, ani_frame_slider_moved), TRIGGER,
	IF(fix), UBUTTON(_("Fix"), ani_play_btn),
	UCANCELBTN(_("Close"), ani_play_btn),
	WSHOW
};
#undef WBbase

void ani_but_preview(void **awin)
{
	apview_dd tdata = { awin, !awin, { ani_frame1, ani_frame1, ani_frame2 } };

	ani_read_layer_data();

	view_show();	// Ensure the view window is shown

	ani_play_state = FALSE;	// Stopped

	if (awin) cmd_showhide(GET_WINDOW(awin), FALSE);
	else
	{
		ani_state = ANI_PLAY;
		update_stuff(UPD_ALLV);
	}

	run_create(apview_code, &tdata, sizeof(tdata));
}

static void create_frames_ani()
{
	image_info *image;
	ls_settings settings;
	png_color pngpal[256], *trans;
	unsigned char *layer_rgb, *irgb;
	char output_path[PATHBUF], *command, *wild_path;
	int a, b, k, i, tr, cols, layer_w, layer_h, npt, l = 0;


	layer_press_save();		// Save layers data file

	command = strrchr(layers_filename, DIR_SEP);
	if (command) l = command - layers_filename + 1;
	wjstrcat(output_path, PATHBUF, layers_filename, l, ani_output_path, NULL);
	l = strlen(output_path);

	if (!ani_output_path[0]); // Reusing layers file directory
#ifdef WIN32
	else if (mkdir(output_path))
#else
	else if (mkdir(output_path, 0777))
#endif
	{
		if ( errno != EEXIST )
		{
			alert_box(_("Error"), _("Unable to create output directory"), NULL);
			return;			// Failure to create directory
		}
	}

		// Create output path and pointer for first char of filename

	a = ani_frame1 < ani_frame2 ? ani_frame1 : ani_frame2;
	b = ani_frame1 < ani_frame2 ? ani_frame2 : ani_frame1;

	image = layer_selected ? &layer_table[0].image->image_ : &mem_image;

	layer_w = image->width;
	layer_h = image->height;
	layer_rgb = malloc(layer_w * layer_h * 4);	// Primary layer image for RGB version
	if (!layer_rgb)
	{
		memory_errors(1);
		return;
	}
	irgb = layer_rgb + layer_w * layer_h * 3;	// For indexed or alpha

	/* Prepare settings */
	init_ls_settings(&settings, NULL);
	settings.mode = FS_COMPOSITE_SAVE;
	settings.width = layer_w;
	settings.height = layer_h;
	settings.colors = 256;
	settings.silent = TRUE;
	if (ani_use_gif)
	{
		settings.ftype = FT_GIF;
		settings.img[CHN_IMAGE] = irgb;
		settings.bpp = 1;
		settings.pal = pngpal;
	}
	else
	{
		if (!comp_need_alpha(FT_PNG)) irgb = NULL;
		settings.ftype = FT_PNG;
		settings.img[CHN_IMAGE] = layer_rgb;
		settings.img[CHN_ALPHA] = irgb;
		settings.bpp = 3;
		/* Background transparency */
		settings.xpm_trans = tr = image->trans;
		settings.rgb_trans = tr < 0 ? -1 : PNG_2_INT(image->pal[tr]);
	}

	progress_init(_("Creating Animation Frames"), 1);
	for ( k=a; k<=b; k++ )			// Create each frame and save it as a PNG or GIF image
	{
		if (progress_update(b == a ? 0.0 : (k - a) / (float)(b - a)))
			break;

		ani_set_frame_state(k);		// Change layer positions
		memset(layer_rgb, 0, layer_w * layer_h * 4);	// Init for RGBA compositing
		view_render_rgb( layer_rgb, 0, 0, layer_w, layer_h, 1 );	// Render layer

		snprintf(output_path + l, PATHBUF - l, DIR_SEP_STR "%s%05d.%s",
			ani_file_prefix, k, ani_use_gif ? "gif" : "png");

		if ( ani_use_gif )	// Prepare palette
		{
			cols = mem_cols_used_real(layer_rgb, layer_w, layer_h, 258, 0);
							// Count colours in image

			if ( cols <= 256 )	// If <=256 convert directly
				mem_cols_found(pngpal);	// Get palette
			else			// If >256 use Wu to quantize
			{
				cols = 256;
				if (wu_quant(layer_rgb, layer_w, layer_h, cols,
					pngpal)) goto failure2;
			}

			// Create new indexed image
			if (mem_dumb_dither(layer_rgb, irgb, pngpal,
				layer_w, layer_h, cols, FALSE)) goto failure2;

			settings.xpm_trans = -1;	// Default is no transparency
			if (image->trans >= 0)	// Background has transparency
			{
				trans = image->pal + image->trans;
				npt = PNG_2_INT(*trans);
				for (i = 0; i < cols; i++)
				{	// Does it exist in the composite frame?
					if (PNG_2_INT(pngpal[i]) != npt) continue;
					// Transparency found so note it
					settings.xpm_trans = i;
					break;
				}
			}
		}
		else if (irgb)
		{
			collect_alpha(irgb, layer_w, layer_h);
			mem_demultiply(layer_rgb, irgb, layer_w * layer_h, 3);
		}

		if (save_image(output_path, &settings) < 0)
		{
			alert_box(_("Error"), _("Unable to save image"), NULL);
			goto failure2;
		}
	}

	if ( ani_use_gif )	// all GIF files created OK so lets give them to gifsicle
	{
		wild_path = wjstrcat(NULL, 0, output_path, l,
			DIR_SEP_STR, ani_file_prefix, "?????.gif", NULL);
		snprintf(output_path + l, PATHBUF - l, DIR_SEP_STR "%s.gif",
			ani_file_prefix);

		run_def_action(DA_GIF_CREATE, wild_path, output_path, ani_gif_delay);
		run_def_action(DA_GIF_PLAY, output_path, NULL, 0);
		free(wild_path);
	}

failure2:
	progress_end();
	free( layer_rgb );
}

void pressed_remove_key_frames()
{
	int i, j;

	i = alert_box(_("Warning"), _("Do you really want to clear all of the position and cycle data for all of the layers?"),
		_("No"), _("Yes"), NULL);
	if ( i==2 )
	{
		for (j = 0; j <= layers_total; j++)
			memset(&layer_table[j].image->ani_, 0, sizeof(ani_info));
		memset(ani_cycle_table, 0, sizeof(ani_cycle_table));
	}
}

static void ani_set_key_frame(int key)		// Set key frame postions & cycles as per current layers
{
	unsigned char buf[MAX_CYC_SLOTS * ANI_CYC_ROWLEN];
	unsigned char *cp;
	ani_slot *ani;
	ani_cycle *cc;
	int i, j, k, l;


// !!! Maybe make background x/y settable here too?
	for ( k=1; k<=layers_total; k++ )	// Add current position for each layer
	{
		ani = layer_table[k].image->ani_.pos;
		// Find first occurence of 0 or frame # < 'key'
		for ( i=0; i<MAX_POS_SLOTS; i++ )
		{
			if (ani[i].frame > key || ani[i].frame == 0) break;
		}

		if ( i>=MAX_POS_SLOTS ) i=MAX_POS_SLOTS-1;

		//  Shift remaining data down a slot
		for ( j=MAX_POS_SLOTS-1; j>i; j-- )
		{
			ani[j] = ani[j - 1];
		}

		//  Enter data for the current state
		ani[i].frame = key;
		ani[i].x = layer_table[k].x;
		ani[i].y = layer_table[k].y;
		ani[i].opacity = layer_table[k].opacity;
		ani[i].effect = 0;			// No effect
	}

	// Find first occurence of 0 or frame # < 'key'
	for ( i=0; i<MAX_CYC_SLOTS; i++ )
	{
		if ( ani_cycle_table[i].frame0 > key ||
			ani_cycle_table[i].frame0 == 0 )
				break;
	}

	if ( i>=MAX_CYC_SLOTS ) i=MAX_CYC_SLOTS-1;

	// Shift remaining data down a slot
	l = MAX_CYC_SLOTS - 1 - i;
	ani_cyc_get(buf);
	cp = buf + ANI_CYC_ROWLEN * i;
	memmove(cp + ANI_CYC_ROWLEN, cp, l * ANI_CYC_ROWLEN);
	cc = ani_cycle_table + i;
	memmove(cc + 1, cc, l * sizeof(ani_cycle));

	// Enter data for the current state
	l = layers_total;
	if (l > MAX_CYC_ITEMS) l = MAX_CYC_ITEMS;
	cc->frame0 = cc->frame1 = key;
	cc->len = *cp++ = l;
	for (j = 1; j <= l; j++)
	{
		*cp++ = !layer_table[j].visible; // Position
		*cp++ = j; // Layer
	}

	// Write back
	ani_cyc_put(buf);
}

static void ani_layer_select(anim_dd *dt, void **wdata, int what, void **where)
{
	char *tmp;
	int j;

	cmd_read(where, dt);
// !!! Allow background here when/if added to the list (no +1 then)
	j = dt->nlayer + 1;

	if (j != dt->layer)	// If switching out of layer
	{
		cmd_read(dt->posw, dt);
		// Parse & store text inputs
		ani_parse_store_positions(dt->pos, dt->layer);
	}

	dt->layer = j;
	/* Refresh the text in the widget */
	dt->lock = TRUE;
	cmd_setv(dt->posw, tmp = ani_pos_txt(j), 0);
	free(tmp);
	dt->lock = FALSE;
}

static int do_set_key_frame(spin1_dd *dt, void **wdata)
{
	run_query(wdata);
	ani_set_key_frame(dt->n[0]);
	layers_notify_changed();

	return (TRUE);
}

void pressed_set_key_frame()
{
	spin1_dd tdata = {
		{ _("Set Key Frame"), spin1_code, FW_FN(do_set_key_frame) },
		{ ani_frame1, ani_frame1, ani_frame2 } };
	run_create(filterwindow_code, &tdata, sizeof(tdata));
}

#define WBbase anim_dd
static void *anim_code[] = {
	WPWHEREVER, WINDOWm(_("Configure Animation")),
	WXYWH("ani", 200, 200),
	NBOOK,
	PAGE(_("Output Files")),
	BORDER(TABLE, 0), BORDER(ENTRY, 0),
	XTABLE(2, 5),
	TSPIN(_("Start frame"), frame1, 1, MAX_FRAME),
	EVENT(CHANGE, ani_widget_changed),
	TSPIN(_("End frame"), frame2, 1, MAX_FRAME),
	EVENT(CHANGE, ani_widget_changed),
	TSPINv(_("Delay"), ani_gif_delay, 1, MAX_DELAY),
	EVENT(CHANGE, ani_widget_changed),
	TPENTRYv(_("Output path"), ani_output_path, PATHBUF),
	EVENT(CHANGE, ani_widget_changed),
	TPENTRYv(_("File prefix"), ani_file_prefix, ANI_PREFIX_LEN),
	EVENT(CHANGE, ani_widget_changed),
	WDONE,
	CHECKv(_("Create GIF frames"), ani_use_gif),
	EVENT(CHANGE, ani_widget_changed),
	WDONE,
///	LAYERS TABLES
	PAGE(_("Positions")),
	XHBOX, // !!! Originally the page was an hbox
	BORDER(SCROLL, 0),
	SCROLL(0, 1), // never/auto
	WLIST,
// !!! Maybe allow background here too, for x/y? (set base=0 here then)
	IDXCOLUMN(1, 1, 40, 1), // center
	TXTCOLUMNv(layer_table[1].name, sizeof(layer_table[1]), 0, 0), // left
	WIDTH(150),
	LISTCCr(nlayer, lnum, ani_layer_select), TRIGGER,
	XVBOX, // !!! what for?
	REF(posw), TEXT(pos),
	EVENT(CHANGE, ani_widget_changed),
	WDONE, WDONE, WDONE,
///	CYCLES TAB
	PAGE(_("Cycling")),
	REF(cycw), TEXT(cyc),
	EVENT(CHANGE, ani_widget_changed),
	WDONE,
	WDONE, /* NBOOK */
///	MAIN BUTTONS
// !!! Maybe better make buttons equal, with EQBOX
	HBOX,
	CANCELBTN(_("Close"), ani_btn),
	REF(save), BUTTON(_("Save"), ani_btn),
	REF(preview), BUTTON(_("Preview"), ani_btn),
	REF(frames), BUTTON(_("Create Frames"), ani_btn),
	WDONE,
	WSHOW
};
#undef WBbase

void pressed_animate_window()
{
	anim_dd tdata;


	if ( layers_total < 1 )					// Only background layer available
	{
		alert_box(_("Error"), _("You must have at least 2 layers to create an animation"), NULL);
		return;
	}

	if (!layers_filename[0])
	{
		alert_box(_("Error"), _("You must save your layers file before creating an animation"), NULL);
		return;
	}

	delete_layers_window();	// Lose the layers window if its up

	ani_read_layer_data();

	memset(&tdata, 0, sizeof(tdata));
	tdata.frame1 = ani_frame1;
	tdata.frame2 = ani_frame2;
	tdata.cyc = ani_cyc_txt();
// !!! Maybe allow background here too, for x/y? (set base=0 here then)
	tdata.lnum = layers_total;
	tdata.nlayer = layers_total - 1; // last layer in list
	tdata.layer = layers_total; // regular index of same

	run_create(anim_code, &tdata, sizeof(tdata));
	free(tdata.cyc);

	ani_state = ANI_CONF;	// Don't show all layers in main window - too messy

	update_stuff(UPD_ALLV);
}



///	FILE HANDLING

void ani_read_file( FILE *fp )			// Read data from layers file already opened
{
	unsigned char buf[MAX_CYC_SLOTS * ANI_CYC_ROWLEN];
	char tin[2048];
	int i, j, k, tot;

	ani_init();
	do
	{
		if (!fgets(tin, 2000, fp)) return;		// BAILOUT - invalid line
		string_chop( tin );
	} while ( strcmp( tin, ANIMATION_HEADER ) != 0 );	// Look for animation header

	i = read_file_num(fp, tin);
	if ( i<0 ) return;				// BAILOUT - invalid #
	ani_frame1 = i;

	i = read_file_num(fp, tin);
	if ( i<0 ) return;				// BAILOUT - invalid #
	ani_frame2 = i;

	if (!fgets(tin, 2000, fp)) return;		// BAILOUT - invalid line
	string_chop( tin );
	strncpy0(ani_output_path, tin, PATHBUF);

	if (!fgets(tin, 2000, fp)) return;		// BAILOUT - invalid #
	string_chop( tin );
	strncpy0(ani_file_prefix, tin, ANI_PREFIX_LEN + 1);

	i = read_file_num(fp, tin);
	if ( i<0 )
	{
		ani_use_gif = FALSE;
		ani_gif_delay = -i;
	}
	else
	{
		ani_use_gif = TRUE;
		ani_gif_delay = i;
	}

///	CYCLE DATA

	i = read_file_num(fp, tin);
	if ( i<0 || i>MAX_CYC_SLOTS ) return;			// BAILOUT - invalid #

	tot = i;
	memset(buf, 0, sizeof(buf));
	for ( j=0; j<tot; j++ )					// Read each cycle line
	{
		if (!fgets(tin, 2000, fp)) break;		// BAILOUT - invalid line

		ani_cyc_sscanf(tin, ani_cycle_table + j,
			buf + ANI_CYC_ROWLEN * j);
	}
	if ( j<MAX_CYC_SLOTS ) ani_cycle_table[j].frame0 = 0;	// Mark end
	ani_cyc_put(buf);

///	POSITION DATA

	for ( k=0; k<=layers_total; k++ )
	{
		i = read_file_num(fp, tin);
		if ( i<0 || i>MAX_POS_SLOTS ) return;			// BAILOUT - invalid #

		tot = i;
		for ( j=0; j<tot; j++ )					// Read each position line
		{
			if (!fgets(tin, 2000, fp)) break;		// BAILOUT - invalid line
			ani_pos_sscanf(tin, layer_table[k].image->ani_.pos + j);
		}
		if ( j<MAX_POS_SLOTS )
			layer_table[k].image->ani_.pos[j].frame = 0;	// Mark end
	}
}

void ani_write_file( FILE *fp )			// Write data to layers file already opened
{
	char txt[ANI_CYC_TEXT_MAX];
	unsigned char buf[MAX_CYC_SLOTS * ANI_CYC_ROWLEN];
	int gifcode = ani_gif_delay, i, j, k;

	if ( layers_total == 0 ) return;	// No layers memory allocated so bail out


	if ( !ani_use_gif ) gifcode = -gifcode;

	// HEADER

	fprintf( fp, "%s\n", ANIMATION_HEADER );
	fprintf( fp, "%i\n%i\n%s\n%s\n%i\n", ani_frame1, ani_frame2,
			ani_output_path, ani_file_prefix, gifcode );

	// CYCLE INFO

	// Count number of cycles, and output this data (if any)
	for ( i=0; i<MAX_CYC_SLOTS; i++ )
	{
		if (!ani_cycle_table[i].frame0) break;	// Bail out at 1st 0
	}

	fprintf( fp, "%i\n", i );

	ani_cyc_get(buf);
	for ( k=0; k<i; k++ )
	{
		ani_cyc_sprintf(txt, ani_cycle_table + k,
			buf + ANI_CYC_ROWLEN * k);
		fputs(txt, fp);
	}

	// POSITION INFO

	// NOTE - we are saving data for layer 0 even though its never used during animation.
	// This is because the user may shift this layer up/down and bring it into play

	for ( k=0; k<=layers_total; k++ )		// Write position table for each layer
	{
		ani_slot *ani = layer_table[k].image->ani_.pos;
		for ( i=0; i<MAX_POS_SLOTS; i++ )	// Count how many lines are in the table
		{
			if (ani[i].frame == 0) break;
		}
		fprintf( fp, "%i\n", i );		// Number of position lines for this layer
		for (j = 0; j < i; j++)
		{
			ani_pos_sprintf(txt, ani + j);
			fputs(txt, fp);
		}
	}
}
