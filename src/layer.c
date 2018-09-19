/*	layer.c
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

#include "global.h"
#undef _
#define _(X) X

#include "mygtk.h"
#include "memory.h"
#include "vcode.h"
#include "ani.h"
#include "png.h"
#include "layer.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "canvas.h"
#include "inifile.h"
#include "viewer.h"
#include "channels.h"
#include "icons.h"


int	layers_total,		// Layers currently being used
	layer_selected,		// Layer currently selected in the layers window
	layers_changed;		// 0=Unchanged

char layers_filename[PATHBUF];		// Current filename for layers file
int	show_layers_main,		// Show all layers in main window
	layer_overlay;			// Toggle overlays per layer


// !!! Always follow adding/changing layer's image_info by update_undo()
layer_node layer_table[(MAX_LAYERS + 1) * 2];	// Table of layer info & its backup
layer_node *layer_table_p = layer_table;	// Unmodified layer table


static void layer_clear_slot(int l, int visible)
{
	memset(layer_table + l, 0, sizeof(layer_node));
	layer_table[l].opacity = 100;
	layer_table[l].visible = visible;
}

void layers_init()
{
	layer_clear_slot(0, TRUE);
	strncpy0(layer_table[0].name, __("Background"), LAYER_NAMELEN);
	layer_table[0].image = calloc(1, sizeof(layer_image));
}

/* Allocate layer image, its channels and undo stack
 * !!! Must be followed by update_undo() after setting up image is done */
layer_image *alloc_layer(int w, int h, int bpp, int cmask, image_info *src)
{
	layer_image *lim;

	lim = calloc(1, sizeof(layer_image));
	if (!lim) return (NULL);
	if (init_undo(&lim->image_.undo_, mem_undo_depth) &&
		mem_alloc_image(src ? AI_COPY : 0, &lim->image_, w, h, bpp, cmask, src))
		return (lim);
	mem_free_image(&lim->image_, FREE_UNDO);
	free(lim);
	return (NULL);
}

/* Repaint layer in view/main window */
static void repaint_layer(int l)
{
	image_info *image = l == layer_selected ? &mem_image :
		&layer_table[l].image->image_;
	lr_update_area(l, 0, 0, image->width, image->height);
}

static void repaint_layers()
{
	update_stuff(show_layers_main ? UPD_ALLV : UPD_VIEW);
}


///	LAYERS WINDOW

typedef struct {
	int lock;
	int x, y, opacity, trans;
	int nlayer, lnum;
	char *lname;
	void **llist, **nmentry, **xspin, **yspin, **opslider, **trspin;
	void **ltb_new, **ltb_raise, **ltb_lower, **ltb_dup, **ltb_center,
		**ltb_del, **ltb_close;
} layers_dd;

static void **layers_box_, **layers_window_;


static void layers_update_titlebar()		// Update filename in titlebar
{
	char txt[300], txt2[PATHTXT];


	if (!layers_window_) return;	// Don't bother if window is not showing

	gtkuncpy(txt2, layers_filename, PATHTXT);
	snprintf(txt, 290, "%s %s %s", __("Layers"),
		layers_changed ? __("(Modified)") : "-",
		txt2[0] ? txt2 : __("Untitled"));
	cmd_setv(GET_WINDOW(layers_window_), txt, WINDOW_TITLE);
}

void layers_notify_changed()			// Layers have just changed - update vars as needed
{
	if ( layers_changed != 1 )
	{
		layers_changed = 1;
		layers_update_titlebar();
	}
}

static void layers_notify_unchanged()		// Layers have just been unchanged (saved) - update vars as needed
{
	if ( layers_changed != 0 )
	{
		layers_changed = 0;
		layers_update_titlebar();
	}
}


void layer_copy_from_main( int l )	// Copy info from main image to layer
{
	layer_image *lp = layer_table[l].image;

	lp->image_ = mem_image;
	lp->state_ = mem_state;
	lp->image_.undo_.size = 0; // Invalidate
	update_undo(&lp->image_); // Safety net
}

void layer_copy_to_main( int l )		// Copy info from layer to main image
{
	layer_image *lp = layer_table[l].image;

	if (!layer_overlay)
	{
		lp->state_.iover = mem_state.iover;
		lp->state_.aover = mem_state.aover;
	}
	mem_image = lp->image_;
	mem_state = lp->state_;
}

void shift_layer(int val)
{
	layers_dd *dt = GET_DDATA(layers_box_);
	layer_node temp;
	int newbkg, lv = layer_selected + val;

	if ((lv < 0) || (lv > layers_total)) return; // Cannot move

	/* Update source layer */
	if ((blend_src == SRC_LAYER + layer_selected) || (blend_src == SRC_LAYER + lv))
		blend_src ^= (SRC_LAYER + layer_selected) ^ (SRC_LAYER + lv);

	layer_copy_from_main(layer_selected);
	temp = layer_table[layer_selected];
	layer_table[layer_selected] = layer_table[lv];
	layer_table[lv] = temp;
	newbkg = (layer_selected == 0) || (lv == 0);

	cmd_setv(dt->llist, (void *)layer_selected, LISTCC_RESET_ROW);
	cmd_setv(dt->llist, (void *)lv, LISTCC_RESET_ROW);
	cmd_set(dt->llist, layer_selected = lv);
	layers_notify_changed();

	if (newbkg)	// Background layer changed
	{
		vw_realign();
		repaint_layers();
	}
	else repaint_layer(layer_selected);	// Regular layer shifted
}

void layer_show_new()
{
	layer_refresh_list(layers_total);
	layers_notify_changed();
}

int layer_add(int w, int h, int bpp, int cols, png_color *pal, int cmask)
{
	layer_image *lim;

	if (layers_total >= MAX_LAYERS) return (FALSE);

	lim = alloc_layer(w, h, bpp, cmask, NULL);
	if (!lim)
	{
		memory_errors(1);
		return (FALSE);
	}
	lim->state_.xbm_hot_x = lim->state_.xbm_hot_y = -1;
	lim->state_.channel = lim->image_.img[mem_channel] ? mem_channel : CHN_IMAGE;

	lim->image_.trans = -1;
	lim->image_.cols = cols;
	if (pal) mem_pal_copy(lim->image_.pal, pal);
	else mem_bw_pal(lim->image_.pal, 0, cols - 1);

	init_istate(&lim->state_, &lim->image_);
	update_undo(&lim->image_);

	layers_total++;
	layer_clear_slot(layers_total, TRUE);
	layer_table[layers_total].image = lim;

	/* Start with fresh animation data if new */
	if (layers_total == 1) ani_init();

	return (TRUE);
}

void layer_new(int w, int h, int bpp, int cols, png_color *pal, int cmask)
{
	if (layer_add(w, h, bpp, cols, pal, cmask)) layer_show_new();
}

/* !!! Same as above: modify structures, *then* show results - WJ */
void layer_press_duplicate()
{
	layer_image *lim, *ls;

	if (layers_total >= MAX_LAYERS) return;

	lim = alloc_layer(0, 0, 0, 0, &mem_image);
	if (!lim)
	{
		memory_errors(1);
		return;
	}

	// Copy layer info
	layer_copy_from_main(layer_selected);
	layers_total++;
	layer_table[layers_total] = layer_table[layer_selected];
	layer_table[layers_total].image = lim;
	ls = layer_table[layer_selected].image;

	lim->state_ = ls->state_;
	mem_pal_copy(lim->image_.pal, ls->image_.pal);
	lim->image_.cols = ls->image_.cols;
	lim->image_.trans = ls->image_.trans;
	update_undo(&lim->image_);

	// Copy across position data
	lim->ani_ = ls->ani_;

	layer_show_new();
}

void layer_delete(int item)
{
	layer_image *lp = layer_table[item].image;
	int i;

	mem_free_image(&lp->image_, FREE_ALL);
	free(lp);

	// If deleted item is not at the end shuffle rest down
	for (i = item; i < layers_total; i++)
		layer_table[i] = layer_table[i + 1];
	memset(layer_table + layers_total, 0, sizeof(layer_node));
	layers_total--;
}

void layer_refresh_list(int slot)
{
	layers_dd *dt = GET_DDATA(layers_box_);

	dt->nlayer = slot;
	dt->lnum = layers_total + 1;
	cmd_reset(dt->llist, dt);
	cmd_set(dt->llist, slot); // !!! For the rest of updates
}

void layer_press_delete()
{
	char txt[256];
	int i;

	if (!layer_selected) return; // Deleting background is forbidden
	snprintf(txt, 256, __("Do you really want to delete layer %i (%s) ?"),
		layer_selected, layer_table[layer_selected].name );

	i = alert_box(_("Warning"), txt, _("No"), _("Yes"), NULL);
	if ((i != 2) || (check_for_changes() == 1)) return;

	if (blend_src == SRC_LAYER + layer_selected) blend_src = SRC_NORMAL;
	layer_copy_from_main(layer_selected);
	layer_copy_to_main(--layer_selected);
	update_main_with_new_layer();
	layer_delete(layer_selected + 1);

	layer_refresh_list(layer_selected);
	layers_notify_changed();
}

static void layer_show_position()
{
	layers_dd *dt = GET_DDATA(layers_box_);
	layer_node *t = layer_table + layer_selected;

	dt->lock++;
	cmd_set(dt->xspin, t->x);
	cmd_set(dt->yspin, t->y);
	dt->lock--;
}

void layer_show_trans()
{
	layers_dd *dt = GET_DDATA(layers_box_);

	if (dt->trans != mem_xpm_trans)
	{
		dt->lock++;
		cmd_set(dt->trspin, mem_xpm_trans);
		dt->lock--;
	}
}

void layer_press_centre()
{
	if (!layer_selected) return; // Nothing to do
	layer_table[layer_selected].x = layer_table[0].x +
		layer_table[0].image->image_.width / 2 - mem_width / 2;
	layer_table[layer_selected].y = layer_table[0].y +
		layer_table[0].image->image_.height / 2 - mem_height / 2;
	layer_show_position();
	layers_notify_changed();
	repaint_layers();
}

/* Return 1 if some layers are modified, 2 if some are nameless, 3 if both,
 * 0 if neither */
static int layers_changed_tot()
{
	image_info *image;
	int j, k;

	for (j = k = 0; k <= layers_total; k++) // Check each layer for mem_changed
	{
		image = k == layer_selected ? &mem_image :
			&layer_table[k].image->image_;
		j |= !!image->changed + !image->filename * 2;
	}

	return (j);
}

int check_layers_for_changes()		// 1=STOP, 2=IGNORE, -10=NOT CHANGED
{
	if (!(layers_changed_tot() + layers_changed)) return (-10);
	return (alert_box(_("Warning"),
		_("One or more of the layers contains changes that have not been saved.  Do you really want to lose these changes?"),
		_("Cancel Operation"), _("Lose Changes"), NULL));
}

static void layer_update_filename( char *name )
{
	strncpy(layers_filename, name, PATHBUF);
	layers_changed = 1;		// Forces update of titlebar
	layers_notify_unchanged();
}

static void layers_free_all()
{
	layer_node *t;

	if (blend_src > SRC_LAYER + 0) blend_src = SRC_NORMAL;

	if (layers_total && layer_selected)	// Copy over layer 0
	{
		layer_copy_from_main(layer_selected);
		layer_copy_to_main(0);
		layer_selected = 0;
	}

	for (t = layer_table + layers_total; t != layer_table; t--)
	{
		mem_free_image(&t->image->image_, FREE_ALL);
		free(t->image);
	}
	memset(layer_table + 1, 0, sizeof(layer_node) * MAX_LAYERS);
	layers_total = 0;
	layers_filename[0] = 0;
	layers_changed = 0;
}

void string_chop( char *txt )
{
	char *cp = txt + strlen(txt) - 1;

	// Chop off unwanted non ASCII characters at end
	while ((cp - txt >= 0) && ((unsigned char)*cp < 32)) *cp-- = 0;
}

int read_file_num(FILE *fp, char *txt)
{
	int i;

	if (!fgets(txt, 32, fp)) return -987654321;
	sscanf(txt, "%i", &i);

	return i;
}

int load_layers( char *file_name )
{
	layer_node *t;
	layer_image *lim2;
	char tin[300], load_name[PATHBUF], *c;
	int i, j, k, kk;
	int layers_to_read = -1, /*layer_file_version = -1,*/ lfail = 0, lplen = 0;
	FILE *fp;

	c = strrchr(file_name, DIR_SEP);
	if (c) lplen = c - file_name + 1;

		// Try to save text file, return -1 if failure
	if ((fp = fopen(file_name, "r")) == NULL) goto fail;

	if (!fgets(tin, 32, fp)) goto fail2;

	string_chop( tin );
	if ( strcmp( tin, LAYERS_HEADER ) != 0 ) goto fail2;		// Bad header

	i = read_file_num(fp, tin);
	if ( i==-987654321 ) goto fail2;
//	layer_file_version = i;
	if ( i>LAYERS_VERSION ) goto fail2;		// Version number must be compatible

	i = read_file_num(fp, tin);
	if ( i==-987654321 ) goto fail2;
	layers_to_read = i < MAX_LAYERS ? i : MAX_LAYERS;

	/* !!! Can use lock field instead, but this is the original way */
	cmd_sensitive(GET_WINDOW(layers_box_), FALSE);

	if (layers_total) layers_free_all();	// Remove all current layers if any
	for ( i=0; i<=layers_to_read; i++ )
	{
		// Read filename, strip end chars & try to load (if name length > 0)
		fgets(tin, 256, fp);
		string_chop(tin);
		wjstrcat(load_name, PATHBUF, file_name, lplen, tin, NULL);
		k = 1;
		j = detect_image_format(load_name);
		if ((j > 0) && (j != FT_NONE) && (j != FT_LAYERS1))
			k = load_image(load_name, FS_LAYER_LOAD, j) != 1;

		if (k) /* Failure - skip this layer */
		{
			for ( j=0; j<7; j++ ) read_file_num(fp, tin);
			lfail++;
			continue;
		}

		/* Update image variables after load */
		t = layer_table + layers_total;
		lim2 = t->image;
		// !!! No old name so no fuss with saving it
		lim2->image_.filename = strdup(load_name);

		fgets(tin, 256, fp);
		string_chop(tin);
		strncpy0(t->name, tin, LAYER_NAMELEN);

		k = read_file_num(fp, tin);
		t->visible = k > 0;

		t->x = read_file_num(fp, tin);
		t->y = read_file_num(fp, tin);

		kk = read_file_num(fp, tin);
		k = read_file_num(fp, tin);
		lim2->image_.trans = kk <= 0 ? -1 : k < 0 ? 0 : k > 255 ? 255 : k;

		k = read_file_num(fp, tin);
		t->opacity = k < 1 ? 1 : k > 100 ? 100 : k;

		init_istate(&lim2->state_, &lim2->image_);
		if (!layers_total++) layer_copy_to_main(0); // Update mem_state
	}
	if (layers_total) layers_total--;

	/* Read in animation data - only if all layers loaded OK
	 * (to do otherwise is likely to result in SIGSEGV) */
	if (!lfail) ani_read_file(fp);
	fclose(fp);

	layer_refresh_list(layers_total);
	cmd_sensitive(GET_WINDOW(layers_box_), TRUE);
	layer_update_filename( file_name );

	if (lfail) /* There were failures */
	{
		snprintf(tin, 300, __("%d layers failed to load"), lfail);
		alert_box(_("Error"), tin, NULL);
	}

	return 1;		// Success
fail2:
	fclose(fp);
fail:
	return -1;
}

int load_to_layers(char *file_name, int ftype, int ani_mode)
{
	char *buf, *tail;
	image_frame *frm;
	image_info *image;
	image_state *state;
	layer_node *t;
	layer_image *lim;
	frameset fset;
	int anim = ani_mode > ANM_PAGE;
	int i, j, l, res, res0, lname;


	/* Create buffer for name mangling */
	lname = strlen(file_name);
	buf = malloc(lname + 64);
	if (!buf) return (FILE_MEM_ERROR);
	strcpy(buf, file_name);
	tail = buf + lname;

	/* !!! Can use lock field instead, but this is the original way */
	cmd_sensitive(GET_WINDOW(layers_box_), FALSE);

	/* Remove old layers, load new frames */
	if (layers_total) layers_free_all();	// Remove all current layers
	res = res0 = load_frameset(&fset, ani_mode, file_name, FS_LAYER_LOAD, ftype);

	if (!fset.cnt) /* Failure - we have no image */
	{
		if (res == FILE_LIB_ERROR) res = -1; // Failure is complete
	}
	else /* Got some frames - convert into layers */
	{
		l = 0; // Start from layer 0
		if (anim) /* Animation */
		{
			int x0, y0, x1, y1, x, y;

			frm = fset.frames;
			/* Calculate a bounding box for the anim */
			x1 = (x0 = frm->x) + frm->width;
			y1 = (y0 = frm->y) + frm->height;
			for (i = 1; i < fset.cnt; i++)
			{
				frm++;
				x = frm->x; if (x0 > x) x0 = x;
				x += frm->width; if (x1 < x) x1 = x;
				y = frm->y; if (y0 > y) y0 = y;
				y += frm->height; if (y1 < y) y1 = y;
			}
			/* Create an empty indexed background of that size */
			do_new_one(x1 - x0, y1 - y0, 256, mem_pal_def, 1, FALSE);
			/* Remember the offsets */
			layer_table[0].x = x0;
			layer_table[0].y = y0;
			l = 1; // Frames start from layer 1
		}

		for (i = 0; i < fset.cnt; i++ , l++)
		{
			frm = fset.frames + i;

			t = layer_table + l;
			res = FILE_MEM_ERROR;
			if (!l) // Layer 0 aka current image
			{
				if (mem_new(frm->width, frm->height, frm->bpp, 0))
					break;
				layer_copy_from_main(0);
			}
			else
			{
				if (!(t->image = alloc_layer(frm->width, frm->height,
					frm->bpp, 0, NULL))) break;
			}
			res = res0;
			lim = t->image;
			t->visible = !anim;
			t->opacity = 100;
			image = &lim->image_; state = &lim->state_;

			/* Move frame data to image */
			memcpy(image->img, frm->img, sizeof(chanlist));
			memset(frm->img, 0, sizeof(chanlist));
			image->trans = frm->trans;
			mem_pal_copy(image->pal, frm->pal ? frm->pal :
				fset.pal ? fset.pal : mem_pal_def);
			image->cols = frm->cols;
			t->x = frm->x; t->y = frm->y;
			update_undo(image);

			/* Create a name for this frame */
			sprintf(tail, ".%03d", i);
			// !!! No old name so no fuss with saving it
			image->filename = strdup(buf);

			init_istate(state, image);
			if (!l) layer_copy_to_main(0); // Update everything
		}
		layers_total = l ? l - 1 : 0;

		if (anim)
		{
// !!! These legacy things need be replaced by a per-layer field
			preserved_gif_delay = ani_gif_delay = fset.frames[0].delay;

			/* Clear stale cycles */
			memset(ani_cycle_table, 0, sizeof(ani_cycle_table));
			/* Build animation cycle for these layers */
			ani_frame1 = ani_cycle_table[0].frame0 = 1;
			ani_frame2 = ani_cycle_table[0].frame1 = l - 1;
			ani_cycle_table[0].len = l - 1;
			for (j = 1; j < l; j++)
			{
				unsigned char *cp = layer_table[j].image->ani_.cycles;
				cp[0] = 1; // Cycle # + 1
				cp[1] = j - 1; // Position
			}

			/* Display 1st layer in sequence */
			layer_table[1].visible = TRUE;
			layer_copy_from_main(0);
			layer_copy_to_main(layer_selected = 1);
		}
		update_main_with_new_layer();
	}
	mem_free_frames(&fset);

	layer_refresh_list(layer_selected);
	cmd_sensitive(GET_WINDOW(layers_box_), TRUE);

	/* Name change so that layers file would not overwrite the source */
	strcpy(tail, ".txt");
	layer_update_filename(buf);

	free(buf);
	return (res);
}

/* Convert absolute filename 'file' into one relative to prefix */
static void parse_filename(char *dest, char *prefix, char *file, int len)
{
	int i, k;

	/* # of chars that match at start */
	for (i = 0; (i < len) && (prefix[i] == file[i]); i++);

	if (!i || (i == len)) /* Complete match, or no match at all */
		strncpy(dest, file + i, PATHBUF);
	else	/* Partial match */
	{
		dest[0] = 0;
		/* Count number of DIR_SEP encountered on and after point i in
		 * 'prefix', add a '../' for each found */
		for (k = i; k < len; k++)
		{
			if (prefix[k] == DIR_SEP)
				strnncat(dest, ".." DIR_SEP_STR, PATHBUF);
		}
		/* nip backwards on 'file' from i to previous DIR_SEP or
		 * beginning and ... */
		for (k = i; (k >= 0) && (file[k] != DIR_SEP); k--);
		/* ... add rest of 'file' */
		strnncat(dest, file + k + 1, PATHBUF);
	}
}

int layer_save_composite(char *fname, ls_settings *settings)
{
	image_info *image;
	unsigned char *layer_rgb;
	int w, h, res = 0, tf = comp_need_alpha(settings->ftype);

	image = layer_selected ? &layer_table[0].image->image_ : &mem_image;
	w = image->width;
	h = image->height;
	layer_rgb = calloc(1, w * h * (3 + !!tf));
	if (layer_rgb)
	{
		view_render_rgb(layer_rgb, 0, 0, w, h, 1);	// Render layer
		if (tf)
		{
			unsigned char *alpha = layer_rgb + w * h * 3;
			collect_alpha(alpha, w, h);
			mem_demultiply(layer_rgb, alpha, w * h, 3);
			settings->img[CHN_ALPHA] = alpha;
		}
		settings->img[CHN_IMAGE] = layer_rgb;
		settings->width = w;
		settings->height = h;
		settings->bpp = 3;
		if (layers_total) /* Remember global offset */
		{
			settings->x = layer_table[0].x;
			settings->y = layer_table[0].y;
		}
		/* Set up palette to go with transparency */
		if (settings->xpm_trans >= 0)
		{
			settings->pal = image->pal;
			settings->colors = image->cols;
		}
		res = save_image(fname, settings);
		free( layer_rgb );
	}
	else memory_errors(1);

	return res;
}

void layer_add_composite()
{
	layer_image *lim;
	image_info *image = layer_selected ? &layer_table[0].image->image_ :
		&mem_image;
	unsigned char **img;
	int w = image->width, h = image->height;

	if (layers_total >= MAX_LAYERS) return;
	if (layer_add(w, h, 3, image->cols, image->pal,
		comp_need_alpha(FT_NONE) ? CMASK_RGBA : CMASK_IMAGE))
	{
		/* Render to an invisible layer */
		layer_table[layers_total].visible = FALSE;
		lim = layer_table[layers_total].image;
		img = lim->image_.img;
		view_render_rgb(img[CHN_IMAGE], 0, 0, w, h, 1);
		/* Add alpha if wanted */
		if (img[CHN_ALPHA])
		{
			collect_alpha(img[CHN_ALPHA], w, h);
			mem_demultiply(img[CHN_IMAGE], img[CHN_ALPHA], w * h, 3);
		}
		/* Copy background's transparency and position */
		lim->image_.trans = image->trans;
		layer_table[layers_total].x = layer_table[0].x;
		layer_table[layers_total].y = layer_table[0].y;
		/* Activate the result */
		layer_show_new();
	}
	else memory_errors(1);
}

int save_layers( char *file_name )
{
	layer_node *t;
	char comp_name[PATHBUF], *c, *msg;
	int i, l = 0, xpm;
	FILE *fp;


	layer_copy_from_main(layer_selected);

	c = strrchr(file_name, DIR_SEP);
	if (c) l = c - file_name + 1;

		// Try to save text file, return -1 if failure
	if ((fp = fopen(file_name, "w")) == NULL) goto fail;

	fprintf( fp, "%s\n%i\n%i\n", LAYERS_HEADER, LAYERS_VERSION, layers_total );
	for ( i=0; i<=layers_total; i++ )
	{
		t = layer_table + i;
		parse_filename(comp_name, file_name, t->image->image_.filename, l);
		fprintf( fp, "%s\n", comp_name );

		xpm = t->image->image_.trans;
		fprintf(fp, "%s\n%i\n%i\n%i\n%i\n%i\n%i\n", t->name,
			t->visible, t->x, t->y, xpm >= 0, xpm, t->opacity);
	}

	ani_write_file(fp);			// Write animation data

	fclose(fp);
	layer_update_filename( file_name );
	register_file( file_name );		// Recently used file list / last directory

	return 1;		// Success
fail:
	c = gtkuncpy(NULL, layers_filename, 0);
	msg = g_strdup_printf(__("Unable to save file: %s"), c);
	alert_box(_("Error"), msg, NULL);
	g_free(msg);
	g_free(c);

	return -1;
}


int check_layers_all_saved()
{
	if (layers_changed_tot() < 2) return (0);
	alert_box(_("Warning"), _("One or more of the image layers has not been saved.  You must save each image individually before saving the layers text file in order to load this composite image in the future."), NULL);
	return (1);
}

void layer_press_save()
{
	if (!layers_filename[0]) file_selector(FS_LAYER_SAVE);
	else if (!check_layers_all_saved()) save_layers(layers_filename);
}

void layer_press_remove_all()
{
	int i = check_layers_for_changes();

	if (i < 0) i = alert_box(_("Warning"),
		_("Do you really want to delete all of the layers?"),
		_("No"), _("Yes"), NULL);
	if (i != 2) return;

	layers_free_all();

	layer_refresh_list(0);
	update_main_with_new_layer();
}

static void layer_tog_visible(layers_dd *dt, void **wdata, int what,
	void **where, void *xdata)
{
	/* !!! Column is self-reading */
	if (dt->lock) return;
	layers_notify_changed();
	repaint_layer((int)xdata); // !!! row passed in there
}

static void layer_inputs_changed(layers_dd *dt, void **wdata, int what,
	void **where)
{
	layer_node *t = layer_table + layer_selected;
	void *cause;
	int dx, dy;

	cause = cmd_read(where, dt);
	if (cause == &show_layers_main)
	{
		update_stuff(UPD_RENDER);
		return;
	}
	if (dt->lock) return;

	layers_notify_changed();

	if (cause == &dt->lname) // Name entry
	{
		strncpy0(t->name, dt->lname, LAYER_NAMELEN);
		cmd_setv(dt->llist, (void *)layer_selected, LISTCC_RESET_ROW);
	}
	else if ((cause == &dt->x) || (cause == &dt->y)) // Position spin
	{
		dx = dt->x - t->x;
		dy = dt->y - t->y;
		if (dx | dy) move_layer_relative(layer_selected, dx, dy);
	}
	else if (cause == &dt->opacity) // Opacity slider
	{
		t->opacity = dt->opacity;
		repaint_layer(layer_selected);
	}
	else /* if (cause == &dt->trans) */ // Transparency spin
	{
		mem_set_trans(dt->trans);
	}
}

void layer_choose(int l)	// Select a new layer from the list
{
	if ((l <= layers_total) && (l >= 0) && (l != layer_selected))
	{
		layers_dd *dt = GET_DDATA(layers_box_);
		cmd_set(dt->llist, l);
	}
}

static void layer_select(layers_dd *dt, void **wdata, int what, void **where)
{
	layer_node *t;
	int j;

	cmd_read(where, dt);
	if (dt->lock) return; // Paranoia

	j = dt->nlayer;
	if (j > layers_total) return; // Paranoia

	dt->lock++;
	if (j != layer_selected) /* Move data before doing anything else */
	{
		layer_copy_from_main(layer_selected);
		layer_copy_to_main(layer_selected = j);
		update_main_with_new_layer();
	}

	t = layer_table + j;
	cmd_setv(dt->nmentry, t->name, ENTRY_VALUE);
	cmd_sensitive(dt->ltb_raise, j < layers_total);
	cmd_sensitive(dt->ltb_lower, j);
	cmd_sensitive(dt->ltb_del, j);
	cmd_sensitive(dt->ltb_center, j);
	// Disable new/duplicate if we have max layers
	cmd_sensitive(dt->ltb_new, layers_total < MAX_LAYERS);
	cmd_sensitive(dt->ltb_dup, layers_total < MAX_LAYERS);

	cmd_set(dt->opslider, t->opacity);
	layer_show_position();
	layer_show_trans();

	dt->lock--;
}

void delete_layers_window()
{
	void **wdata = layers_window_;

	// No deletion if no window
	if (!wdata) return;

	layers_window_ = NULL;
	cmd_set(menu_slots[MENU_LAYER], FALSE); // Ensure it's unchecked
	run_destroy(wdata);
}

void pressed_paste_layer()
{
	layer_image *lim;
	unsigned char *dest;
	int i, j, k, chan = mem_channel, cmask = CMASK_IMAGE;

	if (layers_total >= MAX_LAYERS)
	{
		alert_box(_("Error"), _("You cannot add any more layers."), NULL);
		return;
	}

	/* No way to put RGB clipboard into utility channel */
	if (mem_clip_bpp == 3) chan = CHN_IMAGE;

	if ((mem_clip_alpha || mem_clip_mask) && !channel_dis[CHN_ALPHA])
		cmask = CMASK_RGBA;
	cmask |= CMASK_FOR(chan);

	if (!layer_add(mem_clip_w, mem_clip_h, mem_clip_bpp, mem_cols, mem_pal,
		cmask)) return; // Failed

	layer_table[layers_total].x = layer_table[layer_selected].x + mem_clip_x;
	layer_table[layers_total].y = layer_table[layer_selected].y + mem_clip_y;
	lim = layer_table[layers_total].image;

	lim->state_ = mem_state;
	lim->state_.channel = chan;

	j = mem_clip_w * mem_clip_h;
	memcpy(lim->image_.img[chan], mem_clipboard, j * mem_clip_bpp);

	/* Image channel with alpha */
	dest = lim->image_.img[CHN_ALPHA];
	if (dest && (chan == CHN_IMAGE))
	{
		/* Fill alpha channel */
		if (mem_clip_alpha) memcpy(dest, mem_clip_alpha, j);
		else memset(dest, 255, j);
	}

	/* Image channel with mask */
	if (mem_clip_mask && (chan == CHN_IMAGE))
	{
		/* Mask image - fill unselected part with color A */
		dest = lim->image_.img[CHN_IMAGE];
		k = mem_clip_bpp == 1 ? mem_col_A : mem_col_A24.red;
		for (i = 0; i < j; i++ , dest += mem_clip_bpp)
		{
			if (mem_clip_mask[i]) continue;
			dest[0] = k;
			if (mem_clip_bpp == 1) continue;
			dest[1] = mem_col_A24.green;
			dest[2] = mem_col_A24.blue;
		}
	}

	/* Utility channel with mask */
	dest = lim->image_.img[CHN_ALPHA];
	if (chan != CHN_IMAGE) dest = lim->image_.img[chan];
	if (dest && mem_clip_mask)
	{
		/* Mask the channel */
		for (i = 0; i < j; i++)
		{
			k = dest[i] * mem_clip_mask[i];
			dest[i] = (k + (k >> 8) + 1) >> 8;
		}
	}

	set_new_filename(layers_total, NULL);

	layer_show_new();
	view_show();
}

/* Move a layer & update window labels */
void move_layer_relative(int l, int change_x, int change_y)
{
	image_info *image = l == layer_selected ? &mem_image :
		&layer_table[l].image->image_;
	int upd = 0;

	layer_table[l].x += change_x;
	layer_table[l].y += change_y;

	layers_notify_changed();
	if (l == layer_selected)
	{
		layer_show_position();
		// All layers get moved while the current one stays still
		if (show_layers_main) upd |= UPD_RENDER;
	}
	// All layers get moved while the background stays still
	if (l == 0) upd |= UPD_VIEW;

	lr_update_area(l, change_x < 0 ? 0 : -change_x, change_y < 0 ? 0 : -change_y,
		image->width + abs(change_x), image->height + abs(change_y));
	if (upd) update_stuff(upd);
}

static void layer_bar_click(layers_dd *dt, void **wdata, int what, void **where)
{
	int act_m = TOOL_ID(where);

	action_dispatch(act_m >> 16, (act_m & 0xFFFF) - 0x8000, TRUE, FALSE);
}

#define WBbase layers_dd
static void *layers_code[] = {
	TOPVBOX,
	SCRIPTED, BORDER(SCROLL, 0), BORDER(LISTCC, 0),
	XSCROLL(1, 1), // auto/auto
	WLIST,
	IDXCOLUMN(0, 1, 40, 1), // center
	XTXTCOLUMNv(layer_table[0].name, sizeof(layer_table[0]), 0, 0), // left
	CHKCOLUMNv(layer_table[0].visible, sizeof(layer_table[0]), 0, 0,
		layer_tog_visible),
	REF(llist), LISTCCHr(nlayer, lnum, MAX_LAYERS + 1, layer_select), TRIGGER,
	BORDER(TOOLBAR, 0),
	TOOLBAR(layer_bar_click),
	REF(ltb_new), TBBUTTON(_("New Layer"), XPM_ICON(new),
		ACTMOD(ACT_LR_ADD, LR_NEW)),
	REF(ltb_raise), TBBUTTON(_("Raise"), XPM_ICON(up),
		ACTMOD(ACT_LR_SHIFT, 1)),
	REF(ltb_lower), TBBUTTON(_("Lower"), XPM_ICON(down),
		ACTMOD(ACT_LR_SHIFT, -1)),
	REF(ltb_dup), TBBUTTON(_("Duplicate Layer"), XPM_ICON(copy),
		ACTMOD(ACT_LR_ADD, LR_DUP)),
	REF(ltb_center), TBBUTTON(_("Centralise Layer"), XPM_ICON(centre),
		ACTMOD(ACT_LR_CENTER, 0)),
	REF(ltb_del), TBBUTTON(_("Delete Layer"), XPM_ICON(layer_delete),
		ACTMOD(ACT_LR_DEL, 0)),
	REF(ltb_close), TBBUTTON(_("Close Layers Window"), XPM_ICON(layers_close),
		ACTMOD(DLG_LAYERS, 1)), UNNAME,
	WDONE,
	TABLEs(3, 4, 5),
	BORDER(LABEL, 0), BORDER(ENTRY, 0),
	BORDER(SPIN, 0), BORDER(SPINSLIDE, 0),
	TLABEL(_("Layer Name")),
	REF(nmentry), MINWIDTH(100), TLENTRY(lname, LAYER_NAMELEN - 1, 1, 0, 2),
	EVENT(CHANGE, layer_inputs_changed),
	TLABEL(_("Position")),
	REF(xspin), TLSPIN(x, -MAX_WIDTH, MAX_WIDTH, 1, 1),
	EVENT(CHANGE, layer_inputs_changed), OPNAME("X"),
	REF(yspin), TLSPIN(y, -MAX_HEIGHT, MAX_HEIGHT, 2, 1),
	EVENT(CHANGE, layer_inputs_changed), OPNAME("Y"),
	TLABEL(_("Opacity")),
	REF(opslider), TLSPINSLIDExl(opacity, 0, 100, 1, 2, 2),
	EVENT(CHANGE, layer_inputs_changed),
	TLLABELl(_("Transparent Colour"), 0, 3, 2),
	REF(trspin), TLSPIN(trans, -1, 255, 2, 3),
	EVENT(CHANGE, layer_inputs_changed),
	WDONE,
	CHECKv(_("Show all layers in main window"), show_layers_main),
	EVENT(CHANGE, layer_inputs_changed), UNNAME,
	WEND
};
#undef WBbase

void **create_layers_box()
{
	static char *noscript;
	layers_dd tdata;
	void **res;

	memset(&tdata, 0, sizeof(tdata));
	tdata.nlayer = layer_selected;
	tdata.lnum = layers_total + 1;
	tdata.lname = "";
	layers_box_ = res = run_create_(layers_code, &tdata, sizeof(tdata),
		cmd_mode ? &noscript : NULL);

	return (res);
}

static void *layersw_code[] = {
	WPWHEREVER, WINDOW(""), EVENT(CANCEL, delete_layers_window),
	WXYWH("layers", 400, 400),
	REMOUNTv(layers_dock),
	WSHOW
};

void pressed_layers()
{
	void **res;

	if (cmd_mode) return;
	if (layers_window_) return; // Already have it open
	layers_window_ = res = run_create(layersw_code, layersw_code, 0);

	layers_update_titlebar();

	cmd_setv(GET_WINDOW(res),
		((layers_dd *)GET_DDATA(layers_box_))->ltb_close, WINDOW_ESC_BTN);
}
