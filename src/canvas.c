/*	canvas.c
	Copyright (C) 2004-2008 Mark Tyler and Dmitry Groshev

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

#include <unistd.h>

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "inifile.h"
#include "canvas.h"
#include "viewer.h"
#include "layer.h"
#include "polygon.h"
#include "wu.h"
#include "prefs.h"
#include "ani.h"
#include "channels.h"
#include "toolbar.h"
#include "font.h"
#include "fpick.h"

GtkWidget *label_bar[STATUS_ITEMS];


float can_zoom = 1;				// Zoom factor 1..MAX_ZOOM
int margin_main_x, margin_main_y,		// Top left of image from top left of canvas
	margin_view_x, margin_view_y;
int zoom_flag;
int marq_status = MARQUEE_NONE,
	marq_x1 = -1, marq_y1 = -1, marq_x2 = -1, marq_y2 = -1;		// Selection marquee
int marq_drag_x, marq_drag_y;						// Marquee dragging offset
int line_status = LINE_NONE,
	line_x1, line_y1, line_x2, line_y2;				// Line tool
int poly_status = POLY_NONE;						// Polygon selection tool
int clone_x, clone_y;							// Clone offsets

int recent_files;					// Current recent files setting

int	show_paste,					// Show contents of clipboard while pasting
	col_reverse,					// Painting with right button
	text_paste,					// Are we pasting text?
	canvas_image_centre = TRUE,			// Are we centering the image?
	chequers_optimize = TRUE			// Are we optimizing the chequers for speed?
	;

int brush_spacing;	// Step in non-continuous mode; 0 means use event coords

///	STATUS BAR

static void update_image_bar()
{
	char txt[128], txt2[16], *tmp = "RGB";


	if (!status_on[STATUS_GEOMETRY]) return;

	if (mem_img_bpp == 1) sprintf(tmp = txt2, "%i", mem_cols);

	tmp = txt + snprintf(txt, 80, "%s %i x %i x %s",
		channames[mem_channel], mem_width, mem_height, tmp);

	if ( mem_img[CHN_ALPHA] || mem_img[CHN_SEL] || mem_img[CHN_MASK] )
	{
		strcpy(tmp, " + "); tmp += 3;
		if (mem_img[CHN_ALPHA]) *tmp++ = 'A';
		if (mem_img[CHN_SEL])   *tmp++ = 'S';
		if (mem_img[CHN_MASK])  *tmp++ = 'M';
	// !!! String not NUL-terminated at this point
	}

	if ( layers_total>0 )
		tmp += sprintf(tmp, "  (%i/%i)", layer_selected, layers_total);
	if ( mem_xpm_trans>=0 )
		tmp += sprintf(tmp, "  (T=%i)", mem_xpm_trans);
	strcpy(tmp, "  ");
	gtk_label_set_text( GTK_LABEL(label_bar[STATUS_GEOMETRY]), txt );
}

void update_sel_bar()			// Update selection stats on status bar
{
	char txt[64] = "";
	int x1, y1, w, h;
	float lang, llen;
	grad_info *grad = gradient + mem_channel;


	if (!status_on[STATUS_SELEGEOM]) return;

	if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON))
	{
		if ( marq_status > MARQUEE_NONE )
		{
			x1 = marq_x1 < marq_x2 ? marq_x1 : marq_x2;
			y1 = marq_y1 < marq_y2 ? marq_y1 : marq_y2;
			w = abs(marq_x2 - marq_x1) + 1;
			h = abs(marq_y2 - marq_y1) + 1;
			lang = (180.0 / M_PI) * atan2(marq_x2 - marq_x1,
				marq_y1 - marq_y2);
			llen = sqrt(w * w + h * h);
			snprintf(txt, 60, "  %i,%i : %i x %i   %.1f' %.1f\"",
				x1, y1, w, h, lang, llen);
		}
		else if (tool_type == TOOL_POLYGON)
		{
			snprintf(txt, 60, "  (%i)%c", poly_points,
				poly_status != POLY_DONE ? '+' : '\0');
		}
	}

	else if ((tool_type == TOOL_GRADIENT) && (grad->status != GRAD_NONE))
	{
		w = grad->x2 - grad->x1;
		h = grad->y2 - grad->y1;
		lang = (180.0 / M_PI) * atan2(w, -h);
		llen = sqrt(w * w + h * h);
		snprintf(txt, 60, "  %i,%i : %i x %i   %.1f' %.1f\"",
			grad->x1, grad->y1, w, h, lang, llen);
	}

	gtk_label_set_text(GTK_LABEL(label_bar[STATUS_SELEGEOM]), txt);
}

static char *chan_txt_cat(char *txt, int chan, int x, int y)
{
	if (!mem_img[chan]) return (txt);
	return (txt + sprintf(txt, "%i", mem_img[chan][x + mem_width*y]));
}

void update_xy_bar(int x, int y)
{
	char txt[96], *tmp = txt;
	int pixel;

	if (status_on[STATUS_CURSORXY])
	{
		snprintf(txt, 60, "%i,%i", x, y);
		gtk_label_set_text(GTK_LABEL(label_bar[STATUS_CURSORXY]), txt);
	}

	if (!status_on[STATUS_PIXELRGB]) return;
	if ((x >= 0) && (x < mem_width) && (y >= 0) && (y < mem_height))
	{
		pixel = get_pixel_img(x, y);
		if (mem_img_bpp == 1)
			tmp += sprintf(tmp, "[%u] = {%i,%i,%i}", pixel,
				mem_pal[pixel].red, mem_pal[pixel].green,
				mem_pal[pixel].blue);
		else
			tmp += sprintf(txt, "{%i,%i,%i}", INT_2_R(pixel),
				INT_2_G(pixel), INT_2_B(pixel));
		if (mem_img[CHN_ALPHA] || mem_img[CHN_SEL] || mem_img[CHN_MASK])
		{
			strcpy(tmp, " + {"); tmp += 4;
			tmp = chan_txt_cat(tmp, CHN_ALPHA, x, y);
			*tmp++ = ',';
			tmp = chan_txt_cat(tmp, CHN_SEL, x, y);
			*tmp++ = ',';
			tmp = chan_txt_cat(tmp, CHN_MASK, x, y);
			strcpy(tmp, "}");
		}
		gtk_label_set_text(GTK_LABEL(label_bar[STATUS_PIXELRGB]), txt);
	}
	else gtk_label_set_text(GTK_LABEL(label_bar[STATUS_PIXELRGB]), "");
}

static void update_undo_bar()
{
	char txt[32];

	if (status_on[STATUS_UNDOREDO])
	{
		sprintf(txt, "%i+%i", mem_undo_done, mem_undo_redo);
		gtk_label_set_text(GTK_LABEL(label_bar[STATUS_UNDOREDO]), txt);
	}
}

void init_status_bar()
{
	if ( !status_on[STATUS_GEOMETRY] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_GEOMETRY]), "" );
	else update_image_bar();

	if ( status_on[STATUS_CURSORXY] )
		gtk_widget_set_usize(label_bar[STATUS_CURSORXY], 90, -2);
	else
	{
		gtk_widget_set_usize(label_bar[STATUS_CURSORXY], 0, -2);
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_CURSORXY]), "" );
	}

	if ( !status_on[STATUS_PIXELRGB] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_PIXELRGB]), "" );

	if ( !status_on[STATUS_SELEGEOM] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_SELEGEOM]), "" );

	if (status_on[STATUS_UNDOREDO])
	{	
		gtk_widget_set_usize(label_bar[STATUS_UNDOREDO], 70, -2);
		update_undo_bar();
	}
	else
	{
		gtk_widget_set_usize(label_bar[STATUS_UNDOREDO], 0, -2);
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_UNDOREDO]), "" );
	}
}


void commit_paste(int *update)
{
	int fx, fy, fw, fh, fx2, fy2;		// Screen coords
	int i, ofs = 0, ua;
	unsigned char *image, *mask, *alpha = NULL;

	fx = marq_x1 > 0 ? marq_x1 : 0;
	fy = marq_y1 > 0 ? marq_y1 : 0;
	fx2 = marq_x2 < mem_width ? marq_x2 : mem_width - 1;
	fy2 = marq_y2 < mem_height ? marq_y2 : mem_height - 1;

	fw = fx2 - fx + 1;
	fh = fy2 - fy + 1;

	ua = channel_dis[CHN_ALPHA];	// Ignore clipboard alpha if disabled

	mask = malloc(fw);
	if (!mask) return;	/* !!! Not enough memory */
	if ((mem_channel == CHN_IMAGE) && RGBA_mode && !mem_clip_alpha &&
		!ua && mem_img[CHN_ALPHA])
	{
		alpha = malloc(fw);
		if (!alpha) return;
		memset(alpha, channel_col_A[CHN_ALPHA], fw);
	}
	ua |= !mem_clip_alpha;

	mem_undo_next(UNDO_PASTE);	// Do memory stuff for undo

	/* Offset in memory */
	if (marq_x1 < 0) ofs -= marq_x1;
	if (marq_y1 < 0) ofs -= marq_y1 * mem_clip_w;
	image = mem_clipboard + ofs * mem_clip_bpp;

	for (i = 0; i < fh; i++)
	{
		row_protected(fx, fy + i, fw, mask);
		paste_pixels(fx, fy + i, fw, mask, image, ua ?
			alpha : mem_clip_alpha + ofs, mem_clip_mask ?
			mem_clip_mask + ofs : NULL, tool_opacity);
		image += mem_clip_w * mem_clip_bpp;
		ofs += mem_clip_w;
	}

	free(mask);
	free(alpha);

	if (!update) /* Update right now */
	{
		update_stuff(UPD_IMGP);
		vw_update_area(fx, fy, fw, fh);
		main_update_area(fx, fy, fw, fh);
	}
	else /* Accumulate update area for later */
	{
		fw += fx; fh += fy;
		if (fx < update[0]) update[0] = fx;
		if (fy < update[1]) update[1] = fy;
		if (fw > update[2]) update[2] = fw;
		if (fh > update[3]) update[3] = fh;
	}
}

void iso_trans(int mode)
{
	int i = mem_isometrics(mode);

	if (!i) update_stuff(UPD_GEOM);
	else if (i == -5) alert_box( _("Error"),
		_("The image is too large to transform."), _("OK"), NULL, NULL );
	else memory_errors(i);
}

void pressed_invert()
{
	spot_undo(UNDO_INV);

	mem_invert();
	mem_undo_prepare();

	update_stuff(UPD_COL);
}

static int edge_mode;

static int do_edge(GtkWidget *box, gpointer fdata)
{
	static const unsigned char fxmap[] = { FX_EDGE, FX_SOBEL, FX_PREWITT,
		FX_KIRSCH, FX_GRADIENT, FX_ROBERTS, FX_LAPLACE, FX_MORPHEDGE };

	spot_undo(UNDO_FILT);
	do_effect(fxmap[edge_mode], 0);
	mem_undo_prepare();

	return TRUE;
}

void pressed_edge_detect()
{
	char *fnames[] = { _("MT"), _("Sobel"), _("Prewitt"), _("Kirsch"),
		_("Gradient"), _("Roberts"), _("Laplace"), _("Morphological"),
		NULL };
	GtkWidget *box;

	box = wj_radio_pack(fnames, -1, 4, edge_mode, &edge_mode, NULL);
	filter_window(_("Edge Detect"), box, do_edge, NULL, FALSE);
}

static int do_fx(GtkWidget *spin, gpointer fdata)
{
	int i;

	i = read_spin(spin);
	spot_undo(UNDO_FILT);
	do_effect((int)fdata, i);
	mem_undo_prepare();

	return TRUE;
}

void pressed_sharpen()
{
	GtkWidget *spin = add_a_spin(50, 1, 100);
	filter_window(_("Edge Sharpen"), spin, do_fx, (gpointer)FX_SHARPEN, FALSE);
}

void pressed_soften()
{
	GtkWidget *spin = add_a_spin(50, 1, 100);
	filter_window(_("Edge Soften"), spin, do_fx, (gpointer)FX_SOFTEN, FALSE);
}

void pressed_fx(int what)
{
	spot_undo(UNDO_FILT);
	do_effect(what, 0);
	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

static int do_gauss(GtkWidget *box, gpointer fdata)
{
	GtkWidget *spinX, *spinY, *toggleXY;
	double radiusX, radiusY;
	int gcor = FALSE;

	spinX = BOX_CHILD(box, 0);
	spinY = BOX_CHILD(box, 1);
	toggleXY = BOX_CHILD(box, 2);
	if (mem_channel == CHN_IMAGE) gcor = gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(BOX_CHILD(box, 3)));

	radiusX = radiusY = read_float_spin(spinX);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggleXY)))
		radiusY = read_float_spin(spinY);

	spot_undo(UNDO_DRAW);
	mem_gauss(radiusX, radiusY, gcor);
	mem_undo_prepare();

	return TRUE;
}

static void gauss_xy_click(GtkButton *button, GtkWidget *spin)
{
	gtk_widget_set_sensitive(spin,
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}

void pressed_gauss()
{
	int i;
	GtkWidget *box, *spin, *check;

	box = gtk_vbox_new(FALSE, 5);
	gtk_widget_show(box);
	for (i = 0; i < 2; i++)
	{
		spin = pack(box, add_float_spin(1, 0, 200));
	}
	gtk_widget_set_sensitive(spin, FALSE);
	check = add_a_toggle(_("Different X/Y"), box, FALSE);
	gtk_signal_connect(GTK_OBJECT(check), "clicked",
		GTK_SIGNAL_FUNC(gauss_xy_click), (gpointer)spin);
	if (mem_channel == CHN_IMAGE) pack(box, gamma_toggle());
	filter_window(_("Gaussian Blur"), box, do_gauss, NULL, FALSE);
}

static int do_unsharp(GtkWidget *box, gpointer fdata)
{
	GtkWidget *table, *spinR, *spinA, *spinT;
	double radius, amount;
	int threshold, gcor = FALSE;

	table = BOX_CHILD(box, 0);
	spinR = table_slot(table, 0, 1);
	spinA = table_slot(table, 1, 1);
	spinT = table_slot(table, 2, 1);
	if (mem_channel == CHN_IMAGE) gcor = gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(BOX_CHILD(box, 1)));

	radius = read_float_spin(spinR);
	amount = read_float_spin(spinA);
	threshold = read_float_spin(spinT);

	// !!! No RGBA mode for now, so UNDO_DRAW isn't needed
	spot_undo(UNDO_FILT);
	mem_unsharp(radius, amount, threshold, gcor);
	mem_undo_prepare();

	return TRUE;
}

void pressed_unsharp()
{
	GtkWidget *box, *table;

	box = gtk_vbox_new(FALSE, 5);
	table = add_a_table(3, 2, 0, box);
	gtk_widget_show_all(box);
	float_spin_to_table(table, 0, 1, 5, 5, 0, 200);
	float_spin_to_table(table, 1, 1, 5, 0.5, 0, 10);
	spin_to_table(table, 2, 1, 5, 0, 0, 255);
	add_to_table(_("Radius"), table, 0, 0, 5);
	add_to_table(_("Amount"), table, 1, 0, 5);
	add_to_table(_("Threshold "), table, 2, 0, 5);
	if (mem_channel == CHN_IMAGE) pack(box, gamma_toggle());
	filter_window(_("Unsharp Mask"), box, do_unsharp, NULL, FALSE);
}

static int do_dog(GtkWidget *box, gpointer fdata)
{
	GtkWidget *table, *spinW, *spinN;
	double radW, radN;
	int norm, gcor = FALSE;

	table = BOX_CHILD(box, 0);
	spinW = table_slot(table, 0, 1);
	spinN = table_slot(table, 1, 1);
	norm = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(BOX_CHILD(box, 1)));
	if (mem_channel == CHN_IMAGE) gcor = gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(BOX_CHILD(box, 2)));

	radW = read_float_spin(spinW);
	radN = read_float_spin(spinN);
	if (radW <= radN) return (FALSE); /* Invalid parameters */

	spot_undo(UNDO_FILT);
	mem_dog(radW, radN, norm, gcor);
	mem_undo_prepare();

	return TRUE;
}

void pressed_dog()
{
	GtkWidget *box, *table, *spin;

	box = gtk_vbox_new(FALSE, 5);
	table = add_a_table(3, 2, 0, box);
	gtk_widget_show_all(box);
	spin = add_float_spin(3, 0, 200);
	gtk_table_attach(GTK_TABLE(table), spin, 1, 2,
		0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 5);
	spin = add_float_spin(1, 0, 200);
	gtk_table_attach(GTK_TABLE(table), spin, 1, 2,
		1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 5);
	add_to_table(_("Outer radius"), table, 0, 0, 5);
	add_to_table(_("Inner radius"), table, 1, 0, 5);
	add_a_toggle(_("Normalize"), box, TRUE);
	if (mem_channel == CHN_IMAGE) pack(box, gamma_toggle());
	filter_window(_("Difference of Gaussians"), box, do_dog, NULL, FALSE);
}

static int do_kuwahara(GtkWidget *box, gpointer fdata)
{
	GtkWidget *spin = BOX_CHILD_0(box), *gamma = BOX_CHILD_1(box);
	int r, gcor;

	r = read_spin(spin);
	gcor = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gamma));

	spot_undo(UNDO_COL); // Always processes RGB image channel
	mem_kuwahara(r, gcor);
	mem_undo_prepare();

	return (TRUE);
}

void pressed_kuwahara()
{
	GtkWidget *box;

	box = gtk_vbox_new(FALSE, 5);
	gtk_widget_show(box);
	pack(box, add_a_spin(1, 1, 127));
	pack(box, gamma_toggle());
	filter_window(_("Kuwahara-Nagao Blur"), box, do_kuwahara, NULL, FALSE);
}

void pressed_convert_rgb()
{
	int i = mem_convert_rgb();

	if (i) memory_errors(i);
	else update_stuff(UPD_2RGB);
}

void pressed_greyscale(int mode)
{
	spot_undo(UNDO_COL);

	mem_greyscale(mode);
	mem_undo_prepare();

	update_stuff(UPD_COL);
}

void pressed_rotate_image(int dir)
{
	int i = mem_image_rot(dir);
	if (i) memory_errors(i);
	else update_stuff(UPD_GEOM);
}

void pressed_rotate_sel(int dir)
{
	if (mem_sel_rot(dir)) memory_errors(1);
	else update_stuff(UPD_CGEOM);
}

static int do_rotate_free(GtkWidget *box, gpointer fdata)
{
	GtkWidget *spin = BOX_CHILD_0(box);
	int j, smooth = 0, gcor = 0;
	double angle;

	angle = read_float_spin(spin);

	if (mem_img_bpp == 3)
	{
		GtkWidget *gch = BOX_CHILD_1(box);
		GtkWidget *check = BOX_CHILD_2(box);
		gcor = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gch));
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)))
			smooth = 1;
	}
	j = mem_rotate_free(angle, smooth, gcor, 0);
	if (!j) update_stuff(UPD_GEOM);
	else
	{
		if (j == -5) alert_box(_("Error"),
			_("The image is too large for this rotation."),
			_("OK"), NULL, NULL);
		else memory_errors(j);
	}

	return TRUE;
}

void pressed_rotate_free()
{
	GtkWidget *box;

	box = gtk_vbox_new(FALSE, 5);
	gtk_widget_show(box);
	pack(box, add_float_spin(45, -360, 360));
	if (mem_img_bpp == 3)
	{
		pack(box, gamma_toggle());
		add_a_toggle(_("Smooth"), box, TRUE);
	}
	filter_window(_("Free Rotate"), box, do_rotate_free, NULL, FALSE);
}


void pressed_clip_mask(int val)
{
	int i;

	if ( mem_clip_mask == NULL )
	{
		i = mem_clip_mask_init(val ^ 255);
		if (i)
		{
			memory_errors(1);	// Not enough memory
			return;
		}
	}
	mem_clip_mask_set(val);
	update_stuff(UPD_CLIP);
}

int api_clip_alphamask()
{
	unsigned char *old_mask = mem_clip_mask;
	int i, j = mem_clip_w * mem_clip_h, k;

	if (!mem_clipboard || !mem_clip_alpha) return FALSE;

	mem_clip_mask = mem_clip_alpha;
	mem_clip_alpha = NULL;

	if (old_mask)
	{
		for (i = 0; i < j; i++)
		{
			k = old_mask[i] * mem_clip_mask[i];
			mem_clip_mask[i] = (k + (k >> 8) + 1) >> 8;
		}
		free(old_mask);
	}

	return TRUE;
}

void pressed_clip_alphamask()
{
	if (api_clip_alphamask()) update_stuff(UPD_CLIP);
}

void pressed_clip_alpha_scale()
{
	if (!mem_clipboard || (mem_clip_bpp != 3)) return;
	if (!mem_clip_mask) mem_clip_mask_init(255);
	if (!mem_clip_mask) return;

	if (mem_scale_alpha(mem_clipboard, mem_clip_mask,
		mem_clip_w, mem_clip_h, TRUE)) return;

	update_stuff(UPD_CLIP);
}

void pressed_clip_mask_all()
{
	if (mem_clip_mask_init(0))
		memory_errors(1);	// Not enough memory
	else update_stuff(UPD_CLIP);
}

void pressed_clip_mask_clear()
{
	if (!mem_clip_mask) return;
	mem_clip_mask_clear();
	update_stuff(UPD_CLIP);
}

void pressed_flip_image_v()
{
	int i;
	unsigned char *temp;

	temp = malloc(mem_width * mem_img_bpp);
	if (!temp) return; /* Not enough memory for temp buffer */
	spot_undo(UNDO_XFORM);
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!mem_img[i]) continue;
		mem_flip_v(mem_img[i], temp, mem_width, mem_height, BPP(i));
	}
	free(temp);
	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

void pressed_flip_image_h()
{
	int i;

	spot_undo(UNDO_XFORM);
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!mem_img[i]) continue;
		mem_flip_h(mem_img[i], mem_width, mem_height, BPP(i));
	}
	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

void pressed_flip_sel_v()
{
	unsigned char *temp;
	int i, bpp = mem_clip_bpp;

	temp = malloc(mem_clip_w * mem_clip_bpp);
	if (!temp) return; /* Not enough memory for temp buffer */
	for (i = 0; i < NUM_CHANNELS; i++ , bpp = 1)
	{
		if (!mem_clip.img[i]) continue;
		mem_flip_v(mem_clip.img[i], temp, mem_clip_w, mem_clip_h, bpp);
	}
	update_stuff(UPD_CLIP);
}

void pressed_flip_sel_h()
{
	int i, bpp = mem_clip_bpp;
	for (i = 0; i < NUM_CHANNELS; i++ , bpp = 1)
	{
		if (!mem_clip.img[i]) continue;
		mem_flip_h(mem_clip.img[i], mem_clip_w, mem_clip_h, bpp);
	}
	update_stuff(UPD_CLIP);
}

void pressed_paste(int centre)
{
	if (!mem_clipboard) return;

	poly_status = POLY_NONE;
	poly_points = 0;
	if ((tool_type != TOOL_SELECT) && (tool_type != TOOL_POLYGON))
		change_to_tool(TTB_SELECT);
	else if (marq_status != MARQUEE_NONE) paint_marquee(0, 0, 0);

	if (centre)
	{
		int w, h;
		GtkAdjustment *hori, *vert;

		hori = gtk_scrolled_window_get_hadjustment(
			GTK_SCROLLED_WINDOW(scrolledwindow_canvas));
		vert = gtk_scrolled_window_get_vadjustment(
			GTK_SCROLLED_WINDOW(scrolledwindow_canvas));

		canvas_size(&w, &h);
		if (hori->page_size > w) mem_icx = 0.5;
		else mem_icx = (hori->value + hori->page_size * 0.5) / w;

		if (vert->page_size > h) mem_icy = 0.5;
		else mem_icy = (vert->value + vert->page_size * 0.5) / h;

		marq_x1 = mem_width * mem_icx - mem_clip_w * 0.5;
		marq_y1 = mem_height * mem_icy - mem_clip_h * 0.5;
	}
	else
	{	
		marq_x1 = mem_clip_x;
		marq_y1 = mem_clip_y;
	}
	marq_x2 = marq_x1 + mem_clip_w - 1;
	marq_y2 = marq_y1 + mem_clip_h - 1;
	marq_status = MARQUEE_PASTE;
	cursor_corner = -1;
	update_stuff(UPD_PASTE);
}

void pressed_rectangle(int filled)
{
	int x, y, w, h, sb, l2;

	spot_undo(UNDO_DRAW);

	/* Shapeburst mode */
	sb = mem_gradient && (gradient[mem_channel].status == GRAD_NONE);

	if ( tool_type == TOOL_POLYGON )
	{
		if (sb)
		{
			poly_init();
			l2 = tool_size >> 1;
			sb_xywh[0] = poly_min_x > l2 ? poly_min_x - l2 : 0;
			sb_xywh[1] = poly_min_y > l2 ? poly_min_y - l2 : 0;
			sb_xywh[2] = (poly_max_x + l2 > mem_width ?
				poly_max_x + l2 : mem_width) - sb_xywh[0];
			sb_xywh[3] = (poly_max_y + l2 > mem_height ?
				poly_max_y + l2 : mem_height) - sb_xywh[1];
			sb = init_sb();
		}
		if (!filled) poly_outline();
		else poly_paint();
	}
	else
	{
		x = marq_x1 < marq_x2 ? marq_x1 : marq_x2;
		y = marq_y1 < marq_y2 ? marq_y1 : marq_y2;
		w = abs(marq_x1 - marq_x2) + 1;
		h = abs(marq_y1 - marq_y2) + 1;

		if (sb)
		{
			sb_xywh[0] = x; sb_xywh[1] = y;
			sb_xywh[2] = w; sb_xywh[3] = h;
			sb = init_sb();
		}

		if (filled || (2 * tool_size >= w) || (2 * tool_size >= h))
			f_rectangle(x, y, w, h);
		else
		{
			f_rectangle(x, y, w, tool_size);
			f_rectangle(x, y + tool_size, tool_size, h - 2 * tool_size);
			f_rectangle(x, y + h - tool_size, w, tool_size);
			f_rectangle(x + w - tool_size, y + tool_size,
				tool_size, h - 2 * tool_size);
		}
	}

	if (sb) render_sb();

	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

void pressed_ellipse(int filled)
{
	spot_undo(UNDO_DRAW);
	mem_ellipse(marq_x1, marq_y1, marq_x2, marq_y2, filled ? 0 : tool_size);
	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

static int copy_clip(gboolean api)
{
	int i, x, y, w, h, bpp, ofs, delta, len, cmask = CMASK_IMAGE;


	x = marq_x1 < marq_x2 ? marq_x1 : marq_x2;
	y = marq_y1 < marq_y2 ? marq_y1 : marq_y2;
	w = abs(marq_x1 - marq_x2) + 1;
	h = abs(marq_y1 - marq_y2) + 1;

	bpp = MEM_BPP;
	if ((mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA] &&
		 !channel_dis[CHN_ALPHA]) cmask = CMASK_RGBA;
	mem_clip_new(w, h, bpp, cmask, FALSE);

	if (!mem_clipboard)
	{
		if (!api) alert_box( _("Error"), _("Not enough memory to create clipboard"),
				_("OK"), NULL, NULL );
		return (FALSE);
	}

	mem_clip_x = x;
	mem_clip_y = y;

	/* Current channel */
	ofs = (y * mem_width + x) * bpp;
	delta = 0;
	len = w * bpp;
	for (i = 0; i < h; i++)
	{
		memcpy(mem_clipboard + delta, mem_img[mem_channel] + ofs, len);
		ofs += mem_width * bpp;
		delta += len;
	}

	/* Alpha channel */
	if (mem_clip_alpha)
	{
		ofs = y * mem_width + x;
		delta = 0;
		for (i = 0; i < h; i++)
		{
			memcpy(mem_clip_alpha + delta, mem_img[CHN_ALPHA] + ofs, w);
			ofs += mem_width;
			delta += w;
		}
	}

	return (TRUE);
}

static void channel_mask()
{
	int i, j, ofs, delta;

	if (!mem_img[CHN_SEL] || channel_dis[CHN_SEL]) return;
	if (mem_channel > CHN_ALPHA) return;

	if (!mem_clip_mask) mem_clip_mask_init(255);
	if (!mem_clip_mask) return;

	ofs = mem_clip_y * mem_width + mem_clip_x;
	delta = 0;
	for (i = 0; i < mem_clip_h; i++)
	{
		for (j = 0; j < mem_clip_w; j++)
			mem_clip_mask[delta + j] &= mem_img[CHN_SEL][ofs + j];
		ofs += mem_width;
		delta += mem_clip_w;
	}
}

static void cut_clip()
{
	int i, j, k, kk, step, to;
	unsigned char *sel, fix = 255;

	spot_undo(UNDO_DRAW);
	step = mem_clip_mask ? 1 : 0;
	to = tool_opacity;
	kk = IS_INDEXED ? 255 : tool_opacity;

	for (i = 0; i < mem_clip_h; i++)
	{
		sel = mem_clip_mask ? mem_clip_mask + i * mem_clip_w : &fix;
		for (j = 0; j < mem_clip_w; j++ , sel += step)
		{
			k = *sel * kk;
			tool_opacity = (k + (k >> 8) + 1) >> 8;
			if (!tool_opacity) continue;
			put_pixel(mem_clip_x + j, mem_clip_y + i);
		}
	}
	tool_opacity = to;
	mem_undo_prepare();
}

static void trim_clip()
{
	int i, j, k, offs, offd, maxx, maxy, minx, miny, nw, nh;
	unsigned char *tmp;

	minx = MAX_WIDTH; miny = MAX_HEIGHT; maxx = maxy = 0;

	/* Find max & min values for shrink wrapping */
	for (j = 0; j < mem_clip_h; j++)
	{
		offs = mem_clip_w * j;
		for (i = 0; i < mem_clip_w; i++)
		{
			if (!mem_clip_mask[offs + i]) continue;
			if (i < minx) minx = i;
			if (i > maxx) maxx = i;
			if (j < miny) miny = j;
			if (j > maxy) maxy = j;
		}
	}

	/* No live pixels found */
	if (minx > maxx) return;

	nw = maxx - minx + 1;
	nh = maxy - miny + 1;

	/* No decrease so no resize either */
	if ((nw == mem_clip_w) && (nh == mem_clip_h)) return;

	/* Pack data to front */
	for (j = miny; j <= maxy; j++)
	{
		offs = j * mem_clip_w + minx;
		offd = (j - miny) * nw;
		memmove(mem_clipboard + offd * mem_clip_bpp,
			mem_clipboard + offs * mem_clip_bpp, nw * mem_clip_bpp);
		for (k = 1; k < NUM_CHANNELS; k++)
		{
			if (!(tmp = mem_clip.img[k])) continue;
			memmove(tmp + offd, tmp + offs, nw);
		}
	}

	/* Try to realloc memory for smaller clipboard */
	tmp = realloc(mem_clipboard, nw * nh * mem_clip_bpp);
	if (tmp) mem_clipboard = tmp;
	for (k = 1; k < NUM_CHANNELS; k++)
	{
		if (!(tmp = mem_clip.img[k])) continue;
		tmp = realloc(tmp, nw * nh);
		if (tmp) mem_clip.img[k] = tmp;
	}

	mem_clip_w = nw;
	mem_clip_h = nh;
	mem_clip_x += minx;
	mem_clip_y += miny;

	if (marq_status >= MARQUEE_PASTE) // We're trimming live paste area
	{
		marq_x2 = (marq_x1 += minx) + nw - 1;
		marq_y2 = (marq_y1 += miny) + nh - 1;
	}
}

void pressed_copy(int cut)
{
	if (!copy_clip(FALSE)) return;
	if (tool_type == TOOL_POLYGON) poly_mask();
	channel_mask();
	if (cut) cut_clip();
	update_stuff(cut ? UPD_CUT : UPD_COPY);
}

#ifdef U_API
int api_copy_rectangle()
{
	return copy_clip(TRUE);
}

int api_copy_polygon()
{
	poly_init();
	marq_x1 = poly_min_x;
	marq_x2 = poly_max_x;
	marq_y1 = poly_min_y;
	marq_y2 = poly_max_y;
	if ( copy_clip(TRUE) )
	{
		poly_mask();
		channel_mask();
	}
	else return -1;			// Problem

	return 1;
}
#endif

void pressed_lasso(int cut)
{
	/* Lasso a new selection */
	if (((marq_status > MARQUEE_NONE) && (marq_status < MARQUEE_PASTE)) ||
		(poly_status == POLY_DONE))
	{
		if (!copy_clip(FALSE)) return;
		if (tool_type == TOOL_POLYGON) poly_mask();
		else mem_clip_mask_init(255);
		poly_lasso();
		channel_mask();
		trim_clip();
		if (cut) cut_clip();
		pressed_paste(TRUE);
	}
	/* Trim an existing clipboard */
	else
	{
		unsigned char *oldmask = mem_clip_mask;

		mem_clip_mask = NULL;
		mem_clip_mask_init(255);
		poly_lasso();
		if (mem_clip_mask && oldmask)
		{
			int i, j = mem_clip_w * mem_clip_h;
			for (i = 0; i < j; i++) oldmask[i] &= mem_clip_mask[i];
			mem_clip_mask_clear();
		}
		if (!mem_clip_mask) mem_clip_mask = oldmask;
		trim_clip();
		update_stuff(UPD_CGEOM);
	}
}

void update_menus()			// Update edit/undo menu
{
	int i, j, statemap;

	update_undo_bar();

	statemap = mem_img_bpp == 3 ? NEED_24 | NEED_NOIDX : NEED_IDX;
	if (mem_channel != CHN_IMAGE) statemap |= NEED_NOIDX;
	if ((mem_img_bpp == 3) && mem_img[CHN_ALPHA]) statemap |= NEED_RGBA;

	if (mem_clipboard) statemap |= NEED_PCLIP;
	if (mem_clipboard && (mem_clip_bpp == 3)) statemap |= NEED_ACLIP;

	if ( marq_status == MARQUEE_NONE )
	{
//		statemap &= ~(NEED_SEL | NEED_CROP);
		if (poly_status == POLY_DONE) statemap |= NEED_MARQ | NEED_LASSO;
	}
	else
	{
		statemap |= NEED_MARQ;

		/* If we are pasting disallow copy/cut/crop */
		if (marq_status < MARQUEE_PASTE)
			statemap |= NEED_SEL | NEED_CROP | NEED_LASSO;

		/* Only offer the crop option if the user hasn't selected everything */
		if (!((abs(marq_x1 - marq_x2) < mem_width - 1) ||
			(abs(marq_y1 - marq_y2) < mem_height - 1)))
			statemap &= ~NEED_CROP;
	}

	/* Forbid RGB-to-indexed paste, but allow indexed-to-RGB */
	if (mem_clipboard && (mem_clip_bpp <= MEM_BPP)) statemap |= NEED_CLIP;

	if (mem_undo_done) statemap |= NEED_UNDO;
	if (mem_undo_redo) statemap |= NEED_REDO;

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
		menu_widgets[MENU_CHAN0 + mem_channel]), TRUE);

	for (i = j = 0; i < NUM_CHANNELS; i++)	// Enable/disable channel enable/disable
	{
		if (mem_img[i]) j++;
		gtk_widget_set_sensitive(menu_widgets[MENU_DCHAN0 + i], !!mem_img[i]);
	}
	if (j > 1) statemap |= NEED_CHAN;

	mapped_item_state(statemap);

	/* Switch to default tool if active smudge tool got disabled */
	if ((tool_type == TOOL_SMUDGE) &&
		!GTK_WIDGET_IS_SENSITIVE(icon_buttons[SMUDGE_TOOL_ICON]))
		change_to_tool(DEFAULT_TOOL_ICON);
}

void update_stuff(int flags)
{
	/* Always check current channel first */
	if (!mem_img[mem_channel])
	{
		mem_channel = CHN_IMAGE;
		flags |= UPD_CHAN;
	}

	/* And check paste validity too */
	if ((marq_status >= MARQUEE_PASTE) &&
		(!mem_clipboard || (mem_clip_bpp > MEM_BPP)))
		pressed_select(FALSE);

	if (flags & CF_CAB)
		flags |= mem_channel == CHN_IMAGE ? UPD_AB : UPD_GRAD;
	if (flags & CF_NAME)
		update_titlebar();
	if (flags & CF_GEOM)
	{
		int w, h;
		canvas_size(&w, &h);
		gtk_widget_set_usize(drawing_canvas, w, h);
	}
	if (flags & (CF_GEOM | CF_CGEOM))
		check_marquee();
	if (flags & CF_PAL)
	{
		if (mem_col_A >= mem_cols) mem_col_A = 0;
		if (mem_col_B >= mem_cols) mem_col_B = 0;
		mem_mask_init();	// Reinit RGB masks
		gtk_widget_set_usize(drawing_palette, PALETTE_WIDTH,
			mem_cols * PALETTE_SWATCH_H + PALETTE_SWATCH_Y * 2);
	}
	if (flags & CF_AB)
	{
		mem_pat_update(); // Also updates gradient (CF_GRAD)
		if (text_paste && (marq_status >= MARQUEE_PASTE))
		{
			if (text_paste == TEXT_PASTE_FT) ft_render_text();
			else /* if ( text_paste == TEXT_PASTE_GTK ) */
				render_text( drawing_col_prev );
			check_marquee();
			flags |= CF_PMODE;
		}
	}
	if (flags & CF_GRAD)
		grad_def_update();
	if (flags & CF_PREFS)
	{
		update_undo_depth();	// If undo depth was changed
		update_recent_files();
		init_status_bar();	// Takes care of all statusbar parts
	}
	if (flags & CF_MENU)
		update_menus();
	if (flags & CF_SET)
		toolbar_update_settings();
	if (flags & CF_IMGBAR)
		update_image_bar();
	if (flags & CF_SELBAR)
		update_sel_bar();
#if 0
// !!! Too risky for now - need a safe path which only calls update_xy_bar()
	if (flags & CF_PIXEL)
		move_mouse(0, 0, 0);	// To cause update of XY bar
#endif
	if (flags & CF_CURSOR)
		set_cursor();
	if (flags & CF_PMODE)
		if ((marq_status >= MARQUEE_PASTE) && show_paste) flags |= CF_DRAW;
	if (flags & CF_GMODE)
		if ((tool_type == TOOL_GRADIENT) && grad_opacity) flags |= CF_DRAW;
	if (flags & CF_DRAW)
		if (drawing_canvas) gtk_widget_queue_draw(drawing_canvas);
	if (flags & CF_VDRAW)
		if (view_showing && vw_drawing) gtk_widget_queue_draw(vw_drawing);
	if (flags & CF_PDRAW)
	{
		mem_pal_init();		// Update palette RGB on screen
		gtk_widget_queue_draw(drawing_palette);
	}
	if (flags & CF_TDRAW)
	{
		update_top_swatch();
		gtk_widget_queue_draw(drawing_col_prev);
	}
	if (flags & CF_ALIGN)
	{
		float old_zoom = can_zoom;
		can_zoom = -1;
		align_size(old_zoom);
	}
	if (flags & CF_VALIGN)
		vw_realign();
}

void main_undo()
{
	mem_undo_backward();
	update_stuff(UPD_ALL);
}

void main_redo()
{
	mem_undo_forward();
	update_stuff(UPD_ALL);
}

/* Save palette to a file */
static int save_pal(char *file_name, ls_settings *settings)		
{
	FILE *fp;
	int i;

	if ((fp = fopen(file_name, "w")) == NULL) return -1;

	if (settings->ftype == FT_GPL)		// .gpl file
	{
		fprintf(fp, "GIMP Palette\nName: mtPaint\nColumns: 16\n#\n");
		for (i = 0; i < settings->colors; i++)
			fprintf(fp, "%3i %3i %3i\tUntitled\n",
				settings->pal[i].red, settings->pal[i].green,
				settings->pal[i].blue);
	}

	if (settings->ftype == FT_TXT)		// .txt file
	{
		fprintf(fp, "%i\n", settings->colors);
		for (i = 0; i < settings->colors; i++)
			fprintf(fp, "%i,%i,%i\n",
				settings->pal[i].red, settings->pal[i].green,
				settings->pal[i].blue);
	}

	fclose( fp );

	return 0;
}

int load_pal(char *file_name)			// Load palette file
{
	int i;
	png_color new_mem_pal[256];

	i = mem_load_pal( file_name, new_mem_pal );

	if ( i < 0 ) return i;		// Failure

	spot_undo(UNDO_PAL);

	mem_pal_copy( mem_pal, new_mem_pal );
	mem_cols = i;

	update_stuff(UPD_PAL);

	return 0;
}


void set_new_filename(int layer, char *fname)
{
	if (layer == layer_selected)
	{
		strncpy(mem_filename, fname, PATHBUF);
		update_stuff(UPD_NAME);
	}
	else strncpy(layer_table[layer].image->state_.filename, fname, PATHBUF);
}

static int populate_channel(char *filename)
{
	int ftype, res = -1;

	ftype = detect_image_format(filename);
	if (ftype < 0) return (-1); /* Silently fail if no file */

	/* Don't bother with mismatched formats */
	if (file_formats[ftype].flags & (MEM_BPP == 1 ? FF_IDX : FF_RGB))
		res = load_image(filename, FS_CHANNEL_LOAD, ftype);

	/* Successful */
	if (res == 1) update_stuff(UPD_UIMG);

	/* Not enough memory available */
	else if (res == FILE_MEM_ERROR) memory_errors(1);

	/* Unspecified error */
	else alert_box(_("Error"), _("Invalid channel file."), _("OK"), NULL, NULL);

	return (res == 1 ? 0 : -1);
}

int do_a_load( char *fname )
{
	char mess[256], real_fname[PATHBUF];
	int res, i = 0, ftype;


	if ((fname[0] != DIR_SEP)
#ifdef WIN32
		&& (fname[1] != ':')
#endif
	)
	{
		getcwd(real_fname, PATHBUF - 1);
		i = strlen(real_fname);
		real_fname[i++] = DIR_SEP;
	}
	real_fname[i] = 0;
	strnncat(real_fname, fname, PATHBUF);

	ftype = detect_image_format(real_fname);
	if ((ftype < 0) || (ftype == FT_NONE))
	{
		alert_box(_("Error"), ftype < 0 ? _("Cannot open file") :
			_("Unsupported file format"), _("OK"), NULL, NULL);
		return (1);
	}

	set_image(FALSE);

	if (ftype == FT_LAYERS1) res = load_layers(real_fname);
	else res = load_image(real_fname, FS_PNG_LOAD, ftype);

	if ( res<=0 )				// Error loading file
	{
		if (res == TOO_BIG)
		{
			snprintf(mess, 250, _("File is too big, must be <= to width=%i height=%i"), MAX_WIDTH, MAX_HEIGHT);
			alert_box( _("Error"), mess, _("OK"), NULL, NULL );
		}
		else
		{
			alert_box( _("Error"), _("Unable to load file"),
				_("OK"), NULL, NULL );
		}
		goto fail;
	}

	if ( res == FILE_LIB_ERROR )
		alert_box( _("Error"), _("The file import library had to terminate due to a problem with the file (possibly corrupt image data or a truncated file). I have managed to load some data as the header seemed fine, but I would suggest you save this image to a new file to ensure this does not happen again."), _("OK"), NULL, NULL );

	if ( res == FILE_MEM_ERROR ) memory_errors(1);		// Image was too large for OS

	/* Whether we loaded something or failed to, old image is gone anyway */
	register_file(real_fname);
	if (ftype != FT_LAYERS1) /* A single image */
	{
		set_new_filename(layer_selected, real_fname);

		if ( layers_total>0 )
			layers_notify_changed(); // We loaded an image into the layers, so notify change
	}
	else /* A whole bunch of layers */
	{
//		pressed_layers();
		// We have just loaded a layers file so ensure view window is open
		view_show();
	}

	/* Show new image */
	if (!undo_load) reset_tools();
	else // No reason to reset tools in undoable mode
	{
		notify_unchanged();
		update_stuff(UPD_ALL);
	}

fail:	set_image(TRUE);
	return (res <= 0);
}



///	FILE SELECTION WINDOW

int check_file( char *fname )		// Does file already exist?  Ask if OK to overwrite
{
	char *msg, *f8;
	int res = 0;

	if ( valid_file(fname) == 0 )
	{
		f8 = gtkuncpy(NULL, fname, 0);
		msg = g_strdup_printf(_("File: %s already exists. Do you want to overwrite it?"), f8);
		res = alert_box(_("File Found"), msg, _("NO"), _("YES"), NULL) != 2;
		g_free(msg);
		g_free(f8);
	}

	return (res);
}

#define FORMAT_SPINS 7
static void change_image_format(GtkMenuItem *menuitem, GtkWidget *box)
{
	static int flags[FORMAT_SPINS] = { FF_TRANS, FF_COMPJ, FF_COMPZ,
		FF_COMPR, FF_COMPJ2, FF_SPOT, FF_SPOT };
	GList *chain = GTK_BOX(box)->children->next->next;
	int i, j, ftype;

	ftype = (int)gtk_object_get_user_data(GTK_OBJECT(menuitem));
	/* Hide/show name/value widget pairs */
	for (i = 0; i < FORMAT_SPINS; i++)
	{
		j = flags[i] & FF_COMP ? FF_COMP : flags[i];
		if ((file_formats[ftype].flags & j) == flags[i])
		{
			gtk_widget_show(((GtkBoxChild*)chain->data)->widget);
			gtk_widget_show(((GtkBoxChild*)chain->next->data)->widget);
		}
		else
		{
			gtk_widget_hide(((GtkBoxChild*)chain->data)->widget);
			gtk_widget_hide(((GtkBoxChild*)chain->next->data)->widget);
		}
		chain = chain->next->next;
	}
}

static void image_widgets(GtkWidget *box, char *name, int mode)
{
	char *spinnames[FORMAT_SPINS] = {
		_("Transparency index"), _("JPEG Save Quality (100=High)"),
		_("PNG Compression (0=None)"), _("TGA RLE Compression"),
		_("JPEG2000 Compression (0=Lossless)"),
		_("Hotspot at X ="), _("Y =") };
	int spindata[FORMAT_SPINS][3] = {
		{mem_xpm_trans, -1, mem_cols - 1}, {jpeg_quality, 0, 100},
		{png_compression, 0, 9}, {tga_RLE, 0, 1},
		{jp2_rate, 0, 100},
		{mem_xbm_hot_x, -1, mem_width - 1}, {mem_xbm_hot_y, -1, mem_height - 1} };
	GtkWidget *opt, *menu, *item, *label, *spin;
	fformat *ff;
	int i, j, k, l, ft_sort[NUM_FTYPES][2], mask = FF_256;
	char *ext = strrchr(name, '.');

	ext = ext ? ext + 1 : "";
	switch (mode)
	{
	default: return;
	case FS_CHANNEL_SAVE: if (mem_channel != CHN_IMAGE) break;
	case FS_PNG_SAVE: mask = FF_SAVE_MASK;
		break;
	case FS_COMPOSITE_SAVE: mask = FF_RGB;
	}

	/* Create controls (!!! two widgets per value - used in traversal) */
	label = pack5(box, gtk_label_new(_("File Format")));
	opt = pack5(box, gtk_option_menu_new());
	for (i = 0; i < FORMAT_SPINS; i++)
	{
		label = pack5(box, gtk_label_new(spinnames[i]));
		spin = pack5(box, add_a_spin(spindata[i][0],
			spindata[i][1], spindata[i][2]));
	}
	gtk_widget_show_all(box);

	menu = gtk_menu_new();
	for (i = k = 0; i < NUM_FTYPES; i++)		// Populate sorted filetype list
	{
		ff = file_formats + i;
		if (ff->flags & FF_NOSAVE) continue;
		if (!(ff->flags & mask)) continue;
		l = (ff->name[0] << 16) + (ff->name[1] << 8) + ff->name[2];
		for (j = k; j > 0; j--)
		{
			if (ft_sort[j - 1][0] < l) break;
			ft_sort[j][0] = ft_sort[j - 1][0];
			ft_sort[j][1] = ft_sort[j - 1][1];
		}
		ft_sort[j][0] = l;
		ft_sort[j][1] = i;
		k++;
	}
	j=-1;
	for ( l=0; l<k; l++ )				// Populate the option menu list
	{
		i = ft_sort[l][1];
		if ((j < 0) && (i == FT_PNG)) j = l;	// Default to PNG type if not saved yet
		ff = file_formats + i;
		if (!strncasecmp(ext, ff->ext, LONGEST_EXT) || (ff->ext2[0] &&
			!strncasecmp(ext, ff->ext2, LONGEST_EXT))) j = l;
		item = gtk_menu_item_new_with_label(ff->name);
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)i);
		gtk_signal_connect(GTK_OBJECT(item), "activate",
			GTK_SIGNAL_FUNC(change_image_format), (gpointer)box);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	gtk_widget_show_all(menu);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(opt), menu);

	gtk_option_menu_set_history(GTK_OPTION_MENU(opt), j);

	FIX_OPTION_MENU_SIZE(opt);

	gtk_signal_emit_by_name(GTK_OBJECT(g_list_nth_data(
		GTK_MENU_SHELL(menu)->children, j)), "activate", (gpointer)box);
}

static void ftype_widgets(GtkWidget *box, char *name, int mode)
{
	GtkWidget *opt, *menu, *item, *label;
	fformat *ff;
	int i, j, k, mask;
	char *ext = strrchr(name, '.');

	mask = mode == FS_PALETTE_SAVE ? FF_PALETTE : FF_IMAGE;
	ext = ext ? ext + 1 : "";

	label = gtk_label_new(_("File Format"));
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 10);
	opt = gtk_option_menu_new();
	gtk_box_pack_start(GTK_BOX(box), opt, FALSE, FALSE, 10);

	menu = gtk_menu_new();
	for (i = j = k = 0; i < NUM_FTYPES; i++)
	{
		ff = file_formats + i;
		if (ff->flags & FF_NOSAVE) continue;
		if (!(ff->flags & mask)) continue;
		if (!strncasecmp(ext, ff->ext, LONGEST_EXT) || (ff->ext2[0] &&
			!strncasecmp(ext, ff->ext2, LONGEST_EXT))) j = k;
		item = gtk_menu_item_new_with_label(ff->name);
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)i);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		k++;
  	}
	gtk_widget_show_all(box);
	gtk_widget_show_all(menu);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(opt), menu);
	gtk_option_menu_set_history(GTK_OPTION_MENU(opt), j);

	FIX_OPTION_MENU_SIZE(opt);
}

static void loader_widgets(GtkWidget *box, char *name, int mode)
{
	add_a_toggle(_("Undoable"), box, undo_load);
	gtk_widget_show_all(box);
}

static GtkWidget *ls_settings_box(char *name, int mode)
{
	GtkWidget *box, *label;

	box = gtk_hbox_new(FALSE, 0);
	gtk_object_set_user_data(GTK_OBJECT(box), (gpointer)mode);

	switch (mode) /* Only save operations need settings */
	{
	case FS_PNG_SAVE:
	case FS_CHANNEL_SAVE:
	case FS_COMPOSITE_SAVE:
		image_widgets(box, name, mode);
		break;
	case FS_LAYER_SAVE: /* !!! No selectable layer file format yet */
		break;
	case FS_EXPORT_GIF: /* !!! No selectable formats yet */
		label = pack(box, gtk_label_new(_("Animation delay")));
		gtk_widget_show(label);
		label = add_a_spin(preserved_gif_delay, 1, MAX_DELAY);
		gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 10);
		break;
	case FS_EXPORT_UNDO:
	case FS_EXPORT_UNDO2:
	case FS_PALETTE_SAVE:
		ftype_widgets(box, name, mode);
		break;
	case FS_PNG_LOAD:
		loader_widgets(box, name, mode);
		break;
	default: /* Give a hidden empty box */
		return (box);
	}

	gtk_widget_show(box);
	return (box);
}

static int selected_file_type(GtkWidget *box)
{
	GtkWidget *opt;

	opt = BOX_CHILD(box, 1);
	opt = gtk_option_menu_get_menu(GTK_OPTION_MENU(opt));
	if (!opt) return (FT_NONE);
	opt = gtk_menu_get_active(GTK_MENU(opt));
	return ((int)gtk_object_get_user_data(GTK_OBJECT(opt)));
}

void init_ls_settings(ls_settings *settings, GtkWidget *box)
{
	int xmode;

	/* Set defaults */
	memset(settings, 0, sizeof(ls_settings));
	settings->ftype = FT_NONE;
	settings->xpm_trans = mem_xpm_trans;
	settings->hot_x = mem_xbm_hot_x;
	settings->hot_y = mem_xbm_hot_y;
	settings->jpeg_quality = jpeg_quality;
	settings->png_compression = png_compression;
	settings->tga_RLE = tga_RLE;
	settings->jp2_rate = jp2_rate;
	settings->gif_delay = preserved_gif_delay;

	/* Read in settings */
	if (box)
	{
		xmode = (int)gtk_object_get_user_data(GTK_OBJECT(box));
		settings->mode = xmode;
		switch (xmode)
		{
		case FS_PNG_SAVE:
		case FS_CHANNEL_SAVE:
		case FS_COMPOSITE_SAVE:
			settings->ftype = selected_file_type(box);
			settings->xpm_trans = read_spin(BOX_CHILD(box, 3));
			settings->jpeg_quality = read_spin(BOX_CHILD(box, 5));
			settings->png_compression = read_spin(BOX_CHILD(box, 7));
			settings->tga_RLE = read_spin(BOX_CHILD(box, 9));
			settings->jp2_rate = read_spin(BOX_CHILD(box, 11));
			settings->hot_x = read_spin(BOX_CHILD(box, 13));
			settings->hot_y = read_spin(BOX_CHILD(box, 15));
			break;
		case FS_LAYER_SAVE: /* Nothing to do yet */
			break;
		case FS_EXPORT_GIF: /* No formats yet */
			settings->gif_delay = read_spin(BOX_CHILD(box, 1));
			break;
		case FS_EXPORT_UNDO:
		case FS_EXPORT_UNDO2:
		case FS_PALETTE_SAVE:
			settings->ftype = selected_file_type(box);
			break;
		case FS_PNG_LOAD:
			undo_load = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(BOX_CHILD_0(box)));
			break;
		default: /* Use defaults */
			break;
		}
	}

	/* Default expansion of xpm_trans */
	settings->rgb_trans = settings->xpm_trans < 0 ? -1 :
		PNG_2_INT(mem_pal[settings->xpm_trans]);
}

static void store_ls_settings(ls_settings *settings)
{
	guint32 fflags = file_formats[settings->ftype].flags;

	switch (settings->mode)
	{
	case FS_PNG_SAVE:
	case FS_CHANNEL_SAVE:
	case FS_COMPOSITE_SAVE:
		if (fflags & FF_TRANS)
			mem_xpm_trans = settings->xpm_trans;
		if (fflags & FF_SPOT)
		{
			mem_xbm_hot_x = settings->hot_x;
			mem_xbm_hot_y = settings->hot_y;
		}
		fflags &= FF_COMP;
		if (fflags == FF_COMPJ)
			jpeg_quality = settings->jpeg_quality;
		else if (fflags == FF_COMPZ)
			png_compression = settings->png_compression;
		else if (fflags == FF_COMPR)
			tga_RLE = settings->tga_RLE;
		else if (fflags == FF_COMPJ2)
			jp2_rate = settings->jp2_rate;
		break;
	case FS_EXPORT_GIF:
		preserved_gif_delay = settings->gif_delay;
		break;
	}
}

static gboolean fs_destroy(GtkWidget *fs)
{
	win_store_pos(fs, "fs_window");
	fpick_destroy(fs);

	return FALSE;
}

static void fs_ok(GtkWidget *fs)
{
	ls_settings settings;
	GtkWidget *xtra, *entry;
	char fname[PATHTXT], *msg, *f8;
	char *c, *ext, *ext2, *tmp, *gif, *gif2;
	int i, j;

	/* Pick up extra info */
	xtra = GTK_WIDGET(gtk_object_get_user_data(GTK_OBJECT(fs)));
	init_ls_settings(&settings, xtra);

	/* Needed to show progress in Windows GTK+2 */
	gtk_window_set_modal(GTK_WINDOW(fs), FALSE);

	/* Looks better if no dialog under progressbar */
	win_store_pos(fs, "fs_window"); /* Save the location */
	gtk_widget_hide(fs);

	/* File extension */
	strncpy0(fname, fpick_get_filename(fs, TRUE), PATHTXT);
	c = strrchr(fname, '.');
	while (TRUE)
	{
		/* Cut the extension off */
		if ((settings.mode == FS_CLIP_FILE) ||
			(settings.mode == FS_EXPORT_UNDO) ||
			(settings.mode == FS_EXPORT_UNDO2))
		{
			if (!c) break;
			*c = '\0';
		}
		/* Modify the file extension if needed */
		else
		{
			ext = file_formats[settings.ftype].ext;
			if (!ext[0]) break;
		
			if (c) /* There is an extension */
			{
				/* Same extension? */
				if (!strncasecmp(c + 1, ext, 256)) break;
				/* Alternate extension? */
				ext2 = file_formats[settings.ftype].ext2;
				if (ext2[0] && !strncasecmp(c + 1, ext2, 256))
					break;
				/* Another file type? */
				for (i = 0; i < NUM_FTYPES; i++)
				{
					if (strncasecmp(c + 1, file_formats[i].ext, 256) &&
						strncasecmp(c + 1, file_formats[i].ext2, 256))
						continue;
					/* Truncate */
					*c = '\0';
					break;
				}
			}
			i = strlen(fname);
			j = strlen(ext);
			if (i + j >= PATHTXT - 1) break; /* Too long */
			fname[i] = '.';
			strncpy(fname + i + 1, ext, j + 1);
		}
		fpick_set_filename(fs, fname, TRUE);
		break;
	}

	/* Get filename the proper way (convert it from UTF8 in GTK2/Windows,
	 * leave it in system filename encoding on Unix) */
#ifdef WIN32
	gtkncpy(fname, fpick_get_filename(fs, FALSE), PATHBUF);
#else
	strncpy0(fname, fpick_get_filename(fs, FALSE), PATHBUF);
#endif

	switch (settings.mode)
	{
	case FS_PNG_LOAD:
		if (do_a_load(fname) == 1) goto redo;
		break;
	case FS_PNG_SAVE:
		if (check_file(fname)) goto redo;
		store_ls_settings(&settings);	// Update data in memory
		if (gui_save(fname, &settings) < 0) goto redo;
		if (layers_total > 0)
		{
			/* Filename has changed so layers file needs re-saving to be correct */
			if (strcmp(fname, mem_filename)) layers_notify_changed();
		}
		set_new_filename(layer_selected, fname);
		update_stuff(UPD_TRANS);
		break;
	case FS_PALETTE_LOAD:
		if (load_pal(fname))
		{
			f8 = gtkuncpy(NULL, fname, 0);
			msg = g_strdup_printf(_("File: %s invalid - palette not updated"), f8);
			alert_box(_("Error"), msg, _("OK"), NULL, NULL);
			g_free(msg);
			g_free(f8);
			goto redo;
		}
		else notify_changed();
		break;
	case FS_PALETTE_SAVE:
		if (check_file(fname)) goto redo;
		settings.pal = mem_pal;
		settings.colors = mem_cols;
		if (save_pal(fname, &settings) < 0) goto redo_name;
		break;
	case FS_CLIP_FILE:
	case FS_SELECT_FILE:
	case FS_SELECT_DIR:
		entry = gtk_object_get_data(GTK_OBJECT(fs), FS_ENTRY_KEY);
		if (entry)
		{
			f8 = gtkuncpy(NULL, fname, 0);
			gtk_entry_set_text(GTK_ENTRY(entry), f8);
			g_free(f8);
		}
		break;
	case FS_EXPORT_UNDO:
	case FS_EXPORT_UNDO2:
		if (export_undo(fname, &settings))
			alert_box( _("Error"), _("Unable to export undo images"),
				_("OK"), NULL, NULL );
		break;
	case FS_EXPORT_ASCII:
		if (check_file(fname)) goto redo;
		if (export_ascii(fname))
			alert_box( _("Error"), _("Unable to export ASCII file"),
				_("OK"), NULL, NULL );
		break;
	case FS_LAYER_SAVE:
		if (check_file(fname)) goto redo;
		if (save_layers(fname) != 1) goto redo;
		break;
	case FS_GIF_EXPLODE:
		c = strrchr( preserved_gif_filename, DIR_SEP );
		if (!c) c = preserved_gif_filename;
		else c++;
		tmp = g_strdup_printf("gifsicle -U --explode \"%s\" -o \"%s%c%s\"",
			preserved_gif_filename, fname, DIR_SEP, c);
		gifsicle(tmp);
		g_free(tmp);
		gif = g_strdup_printf("%s%c%s.???", fname, DIR_SEP, c);
		gif2 = quote_spaces(gif);
		tmp = g_strdup_printf("mtpaint -g %i %s &",
			preserved_gif_delay, gif2);
		gifsicle(tmp);
		g_free(tmp);
		free(gif2);
		g_free(gif);
		break;
	case FS_EXPORT_GIF:
		if (check_file(fname)) goto redo;
		store_ls_settings(&settings);	// Update data in memory
		gif2 = quote_spaces(mem_filename);
		for (i = strlen(gif2) - 1; i >= 0; i--)
		{
			if (gif2[i] == DIR_SEP) break;
			if ((unsigned char)(gif2[i] - '0') <= 9) gif2[i] = '?';
		}
						
		tmp = g_strdup_printf("%s -d %i %s -o \"%s\"",
			GIFSICLE_CREATE, settings.gif_delay, gif2, fname);
		gifsicle(tmp);
		g_free(tmp);
		free(gif2);

#ifndef WIN32
		tmp = g_strdup_printf("gifview -a \"%s\" &", fname);
		gifsicle(tmp);
		g_free(tmp);
#endif

		break;
	case FS_CHANNEL_LOAD:
		if (populate_channel(fname)) goto redo;
		break;
	case FS_CHANNEL_SAVE:
		if (check_file(fname)) goto redo;
		settings.img[CHN_IMAGE] = mem_img[mem_channel];
		settings.width = mem_width;
		settings.height = mem_height;
		if (mem_channel == CHN_IMAGE)
		{
			settings.pal = mem_pal;
			settings.bpp = mem_img_bpp;
			settings.colors = mem_cols;
		}
		else
		{
			settings.pal = NULL; /* Greyscale one 'll be created */
			settings.bpp = 1;
			settings.colors = 256;
			settings.xpm_trans = -1;
		}
		if (save_image(fname, &settings)) goto redo_name;
		break;
	case FS_COMPOSITE_SAVE:
		if (check_file(fname)) goto redo;
		if (layer_save_composite(fname, &settings)) goto redo_name;
		break;
	}

	update_menus();
	fpick_destroy(fs);
	return;
redo_name:
	f8 = gtkuncpy(NULL, fname, 0);
	msg = g_strdup_printf(_("Unable to save file: %s"), f8);
	alert_box(_("Error"), msg, _("OK"), NULL, NULL);
	g_free(msg);
	g_free(f8);
redo:
	gtk_widget_show(fs);
	gtk_window_set_modal(GTK_WINDOW(fs), TRUE);
}

void fs_setup(GtkWidget *fs, int action_type)
{
	char txt[PATHTXT];
	GtkWidget *xtra;


	if ((action_type == FS_PNG_SAVE) && mem_filename[0])
		strncpy(txt, mem_filename, PATHBUF);	// If we have a filename and saving
	else if ((action_type == FS_LAYER_SAVE) && layers_filename[0])
		strncpy(txt, layers_filename, PATHBUF);
	else if (action_type == FS_LAYER_SAVE)
	{
		snprintf(txt, PATHBUF, "%s%clayers.txt",
			inifile_get("last_dir", get_home_directory()),
			DIR_SEP );
	}
	else
	{
		snprintf(txt, PATHBUF, "%s%c",
			inifile_get("last_dir", get_home_directory()),
			DIR_SEP );		// Default
	}

#ifdef WIN32 /* Convert from codepage to UTF8 in GTK2/Windows */
	gtkuncpy(txt, txt, PATHTXT);
#endif

	gtk_window_set_modal(GTK_WINDOW(fs), TRUE);
	win_restore_pos(fs, "fs_window", 0, 0, 550, 500);

	xtra = ls_settings_box(txt, action_type);
	gtk_object_set_user_data(GTK_OBJECT(fs), xtra);
	fpick_setup(fs, xtra, GTK_SIGNAL_FUNC(fs_ok), GTK_SIGNAL_FUNC(fs_destroy));
	fpick_set_filename(fs, txt, FALSE);

	gtk_widget_show(fs);
	gtk_window_set_transient_for(GTK_WINDOW(fs), GTK_WINDOW(main_window));
	gdk_window_raise(fs->window);	// Needed to ensure window is at the top
}

void file_selector(int action_type)
{
	char *title = NULL;
	int fpick_flags = FPICK_ENTRY;

	switch (action_type)
	{
	case FS_PNG_LOAD:
		if ((layers_total ? check_layers_for_changes() :
			check_for_changes()) == 1) return;
		title = _("Load Image File");
		fpick_flags = FPICK_LOAD;
		break;
	case FS_PNG_SAVE:
		title = _("Save Image File");
		break;
	case FS_PALETTE_LOAD:
		title = _("Load Palette File");
		fpick_flags = FPICK_LOAD;
		break;
	case FS_PALETTE_SAVE:
		title = _("Save Palette File");
		break;
	case FS_EXPORT_UNDO:
		if (!mem_undo_done) return;
		title = _("Export Undo Images");
		break;
	case FS_EXPORT_UNDO2:
		if (!mem_undo_done) return;
		title = _("Export Undo Images (reversed)");
		break;
	case FS_EXPORT_ASCII:
		if (mem_cols > 16)
		{
			alert_box( _("Error"), _("You must have 16 or fewer palette colours to export ASCII art."),
				_("OK"), NULL, NULL );
			return;
		}
		title = _("Export ASCII Art");
		break;
	case FS_LAYER_SAVE:
		check_layers_all_saved();
		title = _("Save Layer Files");
		break;
	case FS_GIF_EXPLODE:
		title = _("Import GIF animation - Choose frames directory");
		fpick_flags = FPICK_DIRS_ONLY;
		break;
	case FS_EXPORT_GIF:
		if (!mem_filename[0])
		{
			alert_box( _("Error"), _("You must save at least one frame to create an animated GIF."),
				_("OK"), NULL, NULL );
			return;
		}
		title = _("Export GIF animation");
		break;
	case FS_CHANNEL_LOAD:
		title = _("Load Channel");
		fpick_flags = FPICK_LOAD;
		break;
	case FS_CHANNEL_SAVE:
		title = _("Save Channel");
		break;
	case FS_COMPOSITE_SAVE:
		title = _("Save Composite Image");
		break;
	}

	fs_setup(fpick_create(title, fpick_flags), action_type);
}

void align_size( float new_zoom )		// Set new zoom level
{
	GtkAdjustment *hori, *vert;
	int w, h, nv_h = 0, nv_v = 0;	// New positions of scrollbar

	if (zoom_flag) return;		// Needed as we could be called twice per iteration

	if (new_zoom < MIN_ZOOM) new_zoom = MIN_ZOOM;
	if (new_zoom > MAX_ZOOM) new_zoom = MAX_ZOOM;

	if (new_zoom == can_zoom) return;

	zoom_flag = 1;
	hori = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas));
	vert = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas));

	if (mem_ics == 0)
	{
		canvas_size(&w, &h);
		if (hori->page_size > w) mem_icx = 0.5;
		else mem_icx = (hori->value + hori->page_size * 0.5) / w;
		if (vert->page_size > h) mem_icy = 0.5;
		else mem_icy = (vert->value + vert->page_size * 0.5) / h;
	}
	mem_ics = 0;

	can_zoom = new_zoom;
	canvas_size(&w, &h);

	if (hori->page_size < w)
		nv_h = rint(w * mem_icx - hori->page_size * 0.5);

	if (vert->page_size < h)
		nv_v = rint(h * mem_icy - vert->page_size * 0.5);

	hori->value = nv_h;
	hori->upper = w;
	vert->value = nv_v;
	vert->upper = h;

#if GTK_MAJOR_VERSION == 1
	gtk_adjustment_value_changed(hori);
	gtk_adjustment_value_changed(vert);
#endif
	gtk_widget_set_usize(drawing_canvas, w, h);

	zoom_flag = 0;
	vw_focus_view();		// View window position may need updating
	toolbar_zoom_update();
}

/* This tool is seamless: doesn't draw pixels twice if not requested to - WJ */
static void rec_continuous(int nx, int ny, int w, int h)
{
	linedata line1, line2, line3, line4;
	int ws2 = w >> 1, hs2 = h >> 1;
	int i, j, i2, j2, *xv;
	int dx[3] = {-ws2, w - ws2 - 1, -ws2};
	int dy[3] = {-hs2, h - hs2 - 1, -hs2};

	i = nx < tool_ox;
	j = ny < tool_oy;

	/* Redraw starting square only if need to fill in possible gap when
	 * size changes, or to draw stroke gradient in the proper direction */
	if (!tablet_working && !(mem_gradient &&
		(gradient[mem_channel].status == GRAD_NONE)))
	{
		i2 = tool_ox + dx[i + 1] + 1 - i * 2;
		j2 = tool_oy + dy[j + 1] + 1 - j * 2;
		xv = &line3[0];
	}
	else
	{
		i2 = tool_ox + dx[i];
		j2 = tool_oy + dy[j];
		xv = &i2;
	}

	if (tool_ox == nx)
	{
		line_init(line1, tool_ox + dx[i], j2,
			tool_ox + dx[i], ny + dy[j + 1]);
		line_init(line3, tool_ox + dx[i + 1], j2,
			tool_ox + dx[i + 1], ny + dy[j + 1]);
		line2[2] = line4[2] = -1;
	}
	else
	{
		line_init(line2, tool_ox + dx[i], tool_oy + dy[j + 1],
			nx + dx[i], ny + dy[j + 1]);
		line_nudge(line2, i2, j2);
		line_init(line3, tool_ox + dx[i + 1], tool_oy + dy[j],
			nx + dx[i + 1], ny + dy[j]);
		line_nudge(line3, i2, j2);
		line_init(line1, *xv, line3[1], *xv, line2[1]);
		line_init(line4, nx + dx[i + 1], ny + dy[j],
			nx + dx[i + 1], ny + dy[j + 1]);
	}

	draw_quad(line1, line2, line3, line4);
}

void update_all_views()				// Update whole canvas on all views
{
	if ( view_showing && vw_drawing ) gtk_widget_queue_draw( vw_drawing );
	if ( drawing_canvas ) gtk_widget_queue_draw( drawing_canvas );
}


static struct {
	float c_zoom;
	int points;
	int xy[2 * MAX_POLY + 2], step[MAX_POLY + 1];
} poly_cache;

static void poly_update_cache()
{
	int i, i0, last, dx, dy, *pxy, *ps, ds, zoom = 1, scale = 1;

	i0 = poly_cache.c_zoom == can_zoom ? poly_cache.points : 0;
	last = poly_points + (poly_status == POLY_DONE);
	if (i0 >= last) return; // Up to date

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);
	ds = scale >> 1;

	poly_cache.c_zoom = can_zoom;
	poly_cache.points = last;
	/* Get locations */
	pxy = poly_cache.xy + i0 * 2;
	for (i = i0; i < poly_points; i++)
	{
		*pxy++ = margin_main_x + (poly_mem[i][0] * scale) / zoom + ds;
		*pxy++ = margin_main_y + (poly_mem[i][1] * scale) / zoom + ds;
	}
	/* Join 1st & last point if finished */
	if (poly_status == POLY_DONE)
	{
		*pxy++ = poly_cache.xy[0];
		*pxy++ = poly_cache.xy[1];
	}
	/* Get distances */
	poly_cache.step[0] = 0;
	if (!i0) i0 = 1;
	ps = poly_cache.step + i0 - 1;
	pxy = poly_cache.xy + i0 * 2 - 2;
	for (i = i0; i < last; i++ , pxy += 2 , ps++)
	{
		dx = abs(pxy[2] - pxy[0]);
		dy = abs(pxy[3] - pxy[1]);
		ps[1] = ps[0] + (dx > dy ? dx : dy);
	}
}

void stretch_poly_line(int x, int y)			// Clear old temp line, draw next temp line
{
	if ((poly_points <= 0) || (poly_points >= MAX_POLY)) return;
	if ((line_x1 == x) && (line_y1 == y)) return;	// This check reduces flicker

	repaint_line(0);
	line_x1 = x;
	line_y1 = y;
	line_x2 = poly_mem[poly_points-1][0];
	line_y2 = poly_mem[poly_points-1][1];
	repaint_line(2);
}

static void poly_conclude()
{
	repaint_line(0);
	if (poly_points < 2)
	{
		poly_status = POLY_NONE;
		poly_points = 0;
	}
	else
	{
		poly_status = POLY_DONE;
		poly_init();			// Set up polygon stats
		marq_x1 = poly_min_x;
		marq_y1 = poly_min_y;
		marq_x2 = poly_max_x;
		marq_y2 = poly_max_y;
		paint_poly_marquee(NULL, FALSE);
	}
	update_stuff(UPD_PSEL);
}

static void poly_add_po( int x, int y )
{
	if (poly_points <= 0) poly_cache.c_zoom = 0; // Invalidate
	if ((poly_points > 1) && (x == poly_mem[poly_points - 1][0]) &&
		(y == poly_mem[poly_points - 1][1])) return; // Never stack
	repaint_line(0);
	poly_add(x, y);
	if ( poly_points >= MAX_POLY ) poly_conclude();
	else
	{
		paint_poly_marquee(NULL, FALSE);
		update_sel_bar();
	}
}

static int first_point;

static int tool_draw(int x, int y, int *update)
{
	static int ncx, ncy;
	int minx, miny, xw, yh, ts2, tr2;
	int i, j, k, ox, oy, px, py, rx, ry, sx, sy, off1, off2;

	ts2 = tool_size >> 1;
	tr2 = tool_size - ts2 - 1;
	minx = x - ts2;
	miny = y - ts2;
	xw = yh = tool_size;

	/* Save the brush coordinates for next step */
	ox = ncx; oy = ncy;
	ncx = x; ncy = y;

	switch (tool_type)
	{
	case TOOL_SQUARE:
		f_rectangle(x - ts2, y - ts2, tool_size, tool_size);
		break;
	case TOOL_CIRCLE:
		f_circle(x, y, tool_size);
		break;
	case TOOL_HORIZONTAL:
		miny = y; yh = 1;
		sline(x - ts2, y, x + tr2, y);
		break;
	case TOOL_VERTICAL:
		minx = x; xw = 1;
		sline(x, y - ts2, x, y + tr2);
		break;
	case TOOL_SLASH:
		sline(x + tr2, y - ts2, x - ts2, y + tr2);
		break;
	case TOOL_BACKSLASH:
		sline(x - ts2, y - ts2, x + tr2, y + tr2);
		break;
	case TOOL_SPRAY:
		for (j = 0; j < tool_flow; j++)
		{
			rx = x - ts2 + rand() % tool_size;
			ry = y - ts2 + rand() % tool_size;
			IF_IN_RANGE(rx, ry) put_pixel(rx, ry);
		}
		break;
	case TOOL_SHUFFLE:
		for (j = 0; j < tool_flow; j++)
		{
			rx = x - ts2 + rand() % tool_size;
			ry = y - ts2 + rand() % tool_size;
			sx = x - ts2 + rand() % tool_size;
			sy = y - ts2 + rand() % tool_size;
			IF_IN_RANGE(rx, ry) IF_IN_RANGE(sx, sy)
			{
		/* !!! Or do something for partial mask too? !!! */
				if (pixel_protected(rx, ry) ||
					pixel_protected(sx, sy))
					continue;
				off1 = rx + ry * mem_width;
				off2 = sx + sy * mem_width;
				if ((mem_channel == CHN_IMAGE) &&
					RGBA_mode && mem_img[CHN_ALPHA])
				{
					px = mem_img[CHN_ALPHA][off1];
					py = mem_img[CHN_ALPHA][off2];
					mem_img[CHN_ALPHA][off1] = py;
					mem_img[CHN_ALPHA][off2] = px;
				}
				k = MEM_BPP;
				off1 *= k; off2 *= k;
				for (i = 0; i < k; i++)
				{
					px = mem_img[mem_channel][off1];
					py = mem_img[mem_channel][off2];
					mem_img[mem_channel][off1++] = py;
					mem_img[mem_channel][off2++] = px;
				}
			}
		}
		break;
	case TOOL_SMUDGE:
		if (first_point) return (FALSE);
		mem_smudge(ox, oy, x, y);
		break;
	case TOOL_CLONE:
		if (first_point || (ox != x) || (oy != y))
			mem_clone(x + clone_x, y + clone_y, x, y);
		else return (TRUE); /* May try again with other x & y */
		break;
	case TOOL_SELECT:
	case TOOL_POLYGON:
		/* Adjust paste location */
		marq_x2 += x - marq_x1;
		marq_y2 += y - marq_y1;
		marq_x1 = x;
		marq_y1 = y;

		commit_paste(update);
		return (TRUE); /* Area updated already */
		break;
	default: return (FALSE); /* Stop this nonsense now! */
	}

	/* Accumulate update info */
	if (minx < update[0]) update[0] = minx;
	if (miny < update[1]) update[1] = miny;
	xw += minx; yh += miny;
	if (xw > update[2]) update[2] = xw;
	if (yh > update[3]) update[3] = yh;

	return (TRUE);
}

void tool_action(int event, int x, int y, int button, gdouble pressure)
{
	static double lstep;
	linedata ncline;
	double len1;
	int update_area[4];
	int minx = -1, miny = -1, xw = -1, yh = -1;
	int i, j, k, ts2, tr2, res, ox, oy;
	int o_size = tool_size, o_flow = tool_flow, o_opac = tool_opacity, n_vs[3];
	int oox, ooy;	// Continuous smudge stuff
	gboolean rmb_tool;

	/* Does tool draw with color B when right button pressed? */
	rmb_tool = (tool_type <= TOOL_SPRAY) || (tool_type == TOOL_FLOOD);

	if (rmb_tool) tint_mode[2] = button; /* Swap tint +/- */

	if ((first_point = !pen_down))
	{
		lstep = 0.0;
		if ((button == 3) && rmb_tool && !tint_mode[0])
		{
			col_reverse = TRUE;
			mem_swap_cols();
		}
	}
	else if ( tool_ox == x && tool_oy == y ) return;	// Only do something with a new point

	if ( tablet_working )
	{
		pressure = pressure <= 0.2 ? -1.0 : pressure >= 1.0 ? 0.0 :
			(pressure - 1.0) * (1.0 / 0.8);

		n_vs[0] = tool_size;
		n_vs[1] = tool_flow;
		n_vs[2] = tool_opacity;
		for (i = 0; i < 3; i++)
		{
			if (!tablet_tool_use[i]) continue;
			n_vs[i] *= (tablet_tool_factor[i] > 0 ? 1.0 : 0.0) +
				tablet_tool_factor[i] * pressure;
			if (n_vs[i] < 1) n_vs[i] = 1;
		}
		tool_size = n_vs[0];
		tool_flow = n_vs[1];
		tool_opacity = n_vs[2];
	}

	ts2 = tool_size >> 1;
	tr2 = tool_size - ts2 - 1;

	/* Handle "exceptional" tools */
	res = 1;
	if (tool_type == TOOL_LINE)
	{
		if ( button == 1 )
		{
			line_x1 = x;
			line_y1 = y;
			if ( line_status == LINE_NONE )
			{
				line_x2 = x;
				line_y2 = y;
			}

			// Draw circle at x, y
			if ( line_status == LINE_LINE )
			{
				mem_undo_next(UNDO_TOOL);
				if ( tool_size > 1 )
				{
					int oldmode = mem_undo_opacity;
					mem_undo_opacity = TRUE;
					f_circle( line_x1, line_y1, tool_size );
					f_circle( line_x2, line_y2, tool_size );
					// Draw tool_size thickness line from 1-2
					tline( line_x1, line_y1, line_x2, line_y2, tool_size );
					mem_undo_opacity = oldmode;
				}
				else sline( line_x1, line_y1, line_x2, line_y2 );

				minx = (line_x1 < line_x2 ? line_x1 : line_x2) - ts2;
				miny = (line_y1 < line_y2 ? line_y1 : line_y2) - ts2;
				xw = abs( line_x2 - line_x1 ) + 1 + tool_size;
				yh = abs( line_y2 - line_y1 ) + 1 + tool_size;

				line_x2 = line_x1;
				line_y2 = line_y1;
				line_status = LINE_START;
			}
			if ( line_status == LINE_NONE ) line_status = LINE_START;
		}
		else stop_line();	// Right button pressed so stop line process
	}
	else if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON))
	{
		if ( marq_status == MARQUEE_PASTE )		// User wants to drag the paste box
		{
			if ( x>=marq_x1 && x<=marq_x2 && y>=marq_y1 && y<=marq_y2 )
			{
				marq_status = MARQUEE_PASTE_DRAG;
				marq_drag_x = x - marq_x1;
				marq_drag_y = y - marq_y1;
			}
		}
		if ( marq_status == MARQUEE_PASTE_DRAG && ( button == 1 || button == 13 || button == 2 ) )
		{	// User wants to drag the paste box
			paint_marquee(2, x - marq_drag_x, y - marq_drag_y);
		}
		if ( (marq_status == MARQUEE_PASTE_DRAG || marq_status == MARQUEE_PASTE ) &&
			(((button == 3) && (event == GDK_BUTTON_PRESS)) ||
			((button == 13) && (event == GDK_MOTION_NOTIFY))))
		{	// User wants to commit the paste
			res = 0; /* Fall through to noncontinuous tools */
		}
		if ( tool_type == TOOL_SELECT && button == 3 && (marq_status == MARQUEE_DONE ) )
		{
			pressed_select(FALSE);
		}
		if ( tool_type == TOOL_SELECT && button == 1 && (marq_status == MARQUEE_NONE ||
			marq_status == MARQUEE_DONE) )		// Starting a selection
		{
			if ( marq_status == MARQUEE_DONE )
			{
				paint_marquee(0, 0, 0);
				i = close_to(x, y);
				if (!(i & 1) ^ (marq_x1 > marq_x2))
					marq_x1 = marq_x2;
				if (!(i & 2) ^ (marq_y1 > marq_y2))
					marq_y1 = marq_y2;
				set_cursor();
			}
			else
			{
				marq_x1 = x;
				marq_y1 = y;
			}
			marq_x2 = x;
			marq_y2 = y;
			marq_status = MARQUEE_SELECTING;
			paint_marquee(1, 0, 0);
		}
		else
		{
			if (marq_status == MARQUEE_SELECTING)	// Continuing to make a selection
				paint_marquee(3, x, y);
		}

		if ( tool_type == TOOL_POLYGON )
		{
			if ( poly_status == POLY_NONE && marq_status == MARQUEE_NONE )
			{
				// Start doing something
				if (button == 1) poly_status = POLY_SELECTING;
				else if (button) poly_status = POLY_DRAGGING;
			}
			if ( poly_status == POLY_SELECTING )
			{
				/* Add another point to polygon */
				if (button == 1) poly_add_po(x, y);
				/* Stop adding points */
				else if (button == 3) poly_conclude();
			}
			if ( poly_status == POLY_DRAGGING )
			{
				/* Stop forming polygon */
				if (event == GDK_BUTTON_RELEASE) poly_conclude();
				/* Add another point to polygon */
				else poly_add_po(x, y);		
			}
		}
	}
	else /* Some other kind of tool */
	{
		/* If proper button for tool */
		if ((button == 1) || ((button == 3) && rmb_tool))
		{
			// Do memory stuff for undo
			if (tool_type != TOOL_FLOOD) mem_undo_next(UNDO_TOOL);	
			res = 0; 
		}
	}

	/* Handle floodfill here, as too irregular a non-continuous tool */
	if (!res && (tool_type == TOOL_FLOOD))
	{
		/* Non-masked start point */
		if (pixel_protected(x, y) < 255)
		{
			j = get_pixel(x, y);
			k = mem_channel != CHN_IMAGE ? channel_col_A[mem_channel] :
				mem_img_bpp == 1 ? mem_col_A : PNG_2_INT(mem_col_A24);
			if (j != k) /* And never start on colour A */
			{
				spot_undo(UNDO_TOOL);
				flood_fill(x, y, j);
				update_all_views();
			}
		}
		/* Undo the color swap if fill failed */
		if (!pen_down && col_reverse)
		{
			col_reverse = FALSE;
			mem_swap_cols();
		}
		res = 1;
	}

	/* Handle continuous mode */
	while (!res && mem_continuous && !first_point)
	{
		minx = tool_ox < x ? tool_ox : x;
		xw = (tool_ox > x ? tool_ox : x) - minx + tool_size;
		minx -= ts2;

		miny = tool_oy < y ? tool_oy : y;
		yh = (tool_oy > y ? tool_oy : y) - miny + tool_size;
		miny -= ts2;

		res = 1;

		if (ts2 ? tool_type == TOOL_SQUARE : tool_type < TOOL_SPRAY)
		{
			rec_continuous(x, y, tool_size, tool_size);
			break;
		}
		if (tool_type == TOOL_CIRCLE)
		{
			/* Redraw stroke gradient in proper direction */
			if (mem_gradient &&
				(gradient[mem_channel].status == GRAD_NONE))
				f_circle(tool_ox, tool_oy, tool_size);
			tline(tool_ox, tool_oy, x, y, tool_size);
			f_circle(x, y, tool_size);
			break;
		}
		if (tool_type == TOOL_HORIZONTAL)
		{
			miny += ts2; yh -= tool_size - 1;
			rec_continuous(x, y, tool_size, 1);
			break;
		}
		if (tool_type == TOOL_VERTICAL)
		{
			minx += ts2; xw -= tool_size - 1;
			rec_continuous(x, y, 1, tool_size);
			break;
		}
		if (tool_type == TOOL_SLASH)
		{
			g_para(x + tr2, y - ts2, x - ts2, y + tr2,
				tool_ox - x, tool_oy - y);
			break;
		}
		if (tool_type == TOOL_BACKSLASH)
		{
			g_para(x - ts2, y - ts2, x + tr2, y + tr2,
				tool_ox - x, tool_oy - y);
			break;
		}
		if (tool_type == TOOL_SMUDGE)
		{
			linedata line;

			if (button != 1) break; /* Do nothing on right button */
			line_init(line, tool_ox, tool_oy, x, y);
			while (TRUE)
			{
				oox = line[0];
				ooy = line[1];
				if (line_step(line) < 0) break;
				mem_smudge(oox, ooy, line[0], line[1]);
			}
			break;
		}
		xw = yh = -1; /* Nothing was done */
		res = 0; /* Non-continuous tool */
		break;
	}

	/* Handle non-continuous mode & tools */
	if (!res)
	{
		update_area[0] = update_area[1] = MAX_WIDTH;
		update_area[2] = update_area[3] = 0;

		/* Use marquee coords for paste */
		i = j = 0;
		ox = marq_x1;
		oy = marq_y1;
		if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON))
		{
			i = marq_x1 - x;
			j = marq_y1 - y;
		}

		if (first_point || !brush_spacing) /* Single point */
			tool_draw(x + i, y + j, update_area);
		else /* Multiple points */
		{
			line_init(ncline, tool_ox + i, tool_oy + j, x + i, y + j);
			i = abs(x - tool_ox);
			j = abs(y - tool_oy);
			len1 = sqrt(i * i + j * j) / (i > j ? i : j);
			
			while (TRUE)
			{
				if (lstep + (1.0 / 65536.0) >= brush_spacing)
				{
					/* Drop error for 1-pixel step */
					lstep = brush_spacing == 1 ? 0.0 :
						lstep - brush_spacing;
					if (!tool_draw(ncline[0], ncline[1],
						update_area)) break;
				}
				if (line_step(ncline) < 0) break;
				lstep += len1;
			}
			marq_x2 += ox - marq_x1;
			marq_y2 += oy - marq_y1;
			marq_x1 = ox;
			marq_y1 = oy;
		}

		/* Convert update limits */
		minx = update_area[0];
		miny = update_area[1];
		xw = update_area[2] - minx;
		yh = update_area[3] - miny;
	}

	if ((xw > 0) && (yh > 0)) /* Some drawing action */
	{
		if (xw + minx > mem_width) xw = mem_width - minx;
		if (yh + miny > mem_height) yh = mem_height - miny;
		if (minx < 0) xw += minx , minx = 0;
		if (miny < 0) yh += miny , miny = 0;

		if ((xw > 0) && (yh > 0))
		{
			main_update_area(minx, miny, xw, yh);
			vw_update_area(minx, miny, xw, yh);
		}
	}
	tool_ox = x;	// Remember the coords just used as they are needed in continuous mode
	tool_oy = y;

	if ( tablet_working )
	{
		tool_size = o_size;
		tool_flow = o_flow;
		tool_opacity = o_opac;
	}
}

void check_marquee()		// Check marquee boundaries are OK - may be outside limits via arrow keys
{
	int i;

	if ( marq_status >= MARQUEE_PASTE )
	{
		mtMAX( marq_x1, marq_x1, 1-mem_clip_w )
		mtMAX( marq_y1, marq_y1, 1-mem_clip_h )
		mtMIN( marq_x1, marq_x1, mem_width-1 )
		mtMIN( marq_y1, marq_y1, mem_height-1 )
		marq_x2 = marq_x1 + mem_clip_w - 1;
		marq_y2 = marq_y1 + mem_clip_h - 1;
		return;
	}
	/* Selection mode in operation */
	if ((marq_status != MARQUEE_NONE) || (poly_status == POLY_DONE))
	{
		mtMAX( marq_x1, marq_x1, 0 )
		mtMAX( marq_y1, marq_y1, 0 )
		mtMAX( marq_x2, marq_x2, 0 )
		mtMAX( marq_y2, marq_y2, 0 )
		mtMIN( marq_x1, marq_x1, mem_width-1 )
		mtMIN( marq_y1, marq_y1, mem_height-1 )
		mtMIN( marq_x2, marq_x2, mem_width-1 )
		mtMIN( marq_y2, marq_y2, mem_height-1 )
	}
	if ( tool_type == TOOL_POLYGON && poly_points > 0 )
	{
		for ( i=0; i<poly_points; i++ )
		{
			mtMIN( poly_mem[i][0], poly_mem[i][0], mem_width-1 )
			mtMIN( poly_mem[i][1], poly_mem[i][1], mem_height-1 )
		}
	}
}

void get_visible(int *vxy)
{
	GtkAdjustment *hori, *vert;

	hori = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );
	vert = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );

	vxy[0] = hori->value;
	vxy[1] = vert->value;
	vxy[2] = hori->value + hori->page_size - 1;
	vxy[3] = vert->value + vert->page_size - 1;
}

void paint_poly_marquee(rgbcontext *ctx, int whole)	// Paint polygon marquee
{
	int i;

	if ((tool_type != TOOL_POLYGON) || (poly_points < 2)) return;
// !!! Maybe check boundary clipping too
	poly_update_cache();
	i = poly_cache.points;
	if (whole) draw_poly(poly_cache.xy, i, 0, ctx);
	else
	{
		i -= 2;
		draw_poly(poly_cache.xy + i * 2, 2, poly_cache.step[i], ctx);
	}
}


static int clip(int *rxy, int x0, int y0, int x1, int y1, const int *vxy)
{
	rxy[0] = x0 < vxy[0] ? vxy[0] : x0;
	rxy[1] = y0 < vxy[1] ? vxy[1] : y0;
	rxy[2] = x1 > vxy[2] ? vxy[2] : x1;
	rxy[3] = y1 > vxy[3] ? vxy[3] : y1;
	return ((rxy[2] > rxy[0]) && (rxy[3] > rxy[1]));
}

static void repaint_clipped(int x0, int y0, int x1, int y1, const int *vxy)
{
	int rxy[4];

	if (clip(rxy, x0, y0, x1, y1, vxy))
		repaint_canvas(margin_main_x + rxy[0], margin_main_y + rxy[1],
			rxy[2] - rxy[0], rxy[3] - rxy[1]);
}

static void locate_marquee(int *xy)
{
	int x1, y1, x2, y2, w, h, zoom = 1, scale = 1;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	/* Get onscreen coords */
	check_marquee();
	x1 = (marq_x1 * scale) / zoom;
	y1 = (marq_y1 * scale) / zoom;
	x2 = (marq_x2 * scale) / zoom;
	y2 = (marq_y2 * scale) / zoom;
	w = abs(x2 - x1) + scale;
	h = abs(y2 - y1) + scale;
	xy[2] = (xy[0] = x1 < x2 ? x1 : x2) + w;
	xy[3] = (xy[1] = y1 < y2 ? y1 : y2) + h;
}

// Actions: 0 - hide, 1 - show, 2 - move, 3 - resize
static void trace_marquee(int action, int new_x, int new_y, const int *vxy,
	rgbcontext *ctx)
{
	unsigned char *rgb;
	int xy[4], nxy[4], rxy[4], clips[4 * 3];
	int i, j, nc, r, g, b, rw, rh, offx, offy, mst = marq_status;

	locate_marquee(xy);
	memcpy(nxy, xy, sizeof(xy));
	memcpy(clips, xy, sizeof(xy));
	nc = action == 1 ? 0 : 4; // No clear if showing anew

	/* Determine which parts moved outside */
	while (action > 1)
	{
		if (action == 2) // Move
		{
			marq_x2 += new_x - marq_x1;
			marq_x1 = new_x;
			marq_y2 += new_y - marq_y1;
			marq_y1 = new_y;
		}
		else marq_x2 = new_x , marq_y2 = new_y; // Resize
		locate_marquee(nxy);

		/* No intersection? */
		if (!clip(rxy, xy[0], xy[1], xy[2], xy[3], nxy)) break;

		/* Horizontal slab */
		if (rxy[1] > xy[1]) clips[3] = rxy[1]; // Top
		else if (rxy[3] < xy[3]) clips[1] = rxy[3]; // Bottom
		else nc = 0; // None

		/* Inside area, if left unfilled */
		if (!(show_paste && (mst >= MARQUEE_PASTE)))
		{
			clips[nc + 0] = nxy[0] + 1;
			clips[nc + 1] = nxy[1] + 1;
			clips[nc + 2] = nxy[2] - 1;
			clips[nc + 3] = nxy[3] - 1;
			nc += 4;
		}

		/* Vertical block */
		if (rxy[0] > xy[0]) // Left
			clips[nc + 0] = xy[0] , clips[nc + 2] = rxy[0];
		else if (rxy[2] < xy[2]) // Right
			clips[nc + 0] = rxy[2] , clips[nc + 2] = xy[2];
		else break; // None
		clips[nc + 1] = rxy[1]; clips[nc + 3] = rxy[3];
		nc += 4;
		break;
	}

	/* Clear - only happens in void context */
	marq_status = 0;
	for (i = 0; i < nc; i += 4)
	{
		/* Clip to visible portion */
		if (!clip(rxy, clips[i + 0], clips[i + 1],
			clips[i + 2], clips[i + 3], vxy)) continue;
		/* Redraw entire area */
		if (show_paste && (mst >= MARQUEE_PASTE))
			repaint_clipped(xy[0], xy[1], xy[2], xy[3], rxy);
		/* Redraw only borders themselves */
		else
		{
			repaint_clipped(xy[0], xy[1] + 1, xy[0] + 1, xy[3] - 1, rxy);
			repaint_clipped(xy[2] - 1, xy[1] + 1, xy[2], xy[3] - 1, rxy);
			repaint_clipped(xy[0], xy[1], xy[2], xy[1] + 1, rxy);
			repaint_clipped(xy[0], xy[3] - 1, xy[2], xy[3], rxy);
		}
	}
	marq_status = mst;
	if (action == 0) return; // All done for clear

	/* Determine visible area */
	if (!clip(rxy, nxy[0], nxy[1], nxy[2], nxy[3], vxy)) return;
	rw = rxy[2] - rxy[0]; rh = rxy[3] - rxy[1];

	/* Draw */
	r = 255; g = b = 0; /* Draw in red */
	if (marq_status >= MARQUEE_PASTE)
	{
		/* Display paste RGB, only if not being called from repaint_canvas */
		if (show_paste && !ctx) repaint_clipped(marq_x1 < 0 ? 0 : nxy[0] + 1,
			marq_y1 < 0 ? 0 : nxy[1] + 1, nxy[2] - 1, nxy[3] - 1, vxy);
		r = g = 0; b = 255; /* Draw in blue */
	}

	/* Create pattern */
	offx = ((rxy[0] - nxy[0]) % 6) * 3;
	offy = ((rxy[1] - nxy[1]) % 6) * 3;
	j = (rw > rh ? rw : rh) * 3 + 6 * 3; /* 6 pixels for offset */
	rgb = malloc(j + 2 * 3); /* 2 extra pixels reserved for loop */
	if (!rgb) return;
	memset(rgb, 255, j);
	for (i = 0; i < j; i += 6 * 3)
	{
		rgb[i + 0] = rgb[i + 3] = rgb[i + 6] = r;
		rgb[i + 1] = rgb[i + 4] = rgb[i + 7] = g;
		rgb[i + 2] = rgb[i + 5] = rgb[i + 8] = b;
	}

	if ((nxy[0] >= vxy[0]) && (marq_x1 >= 0) && (marq_x2 >= 0))
		draw_rgb(margin_main_x + rxy[0], margin_main_y + rxy[1],
			1, rxy[3] - rxy[1], rgb + offy, 3, ctx);

	if ((nxy[2] <= vxy[2]) && (marq_x1 < mem_width) && (marq_x2 < mem_width))
		draw_rgb(margin_main_x + rxy[2] - 1, margin_main_y + rxy[1],
			1, rxy[3] - rxy[1], rgb + offy, 3, ctx);

	if ((nxy[1] >= vxy[1]) && (marq_y1 >= 0) && (marq_y2 >= 0))
		draw_rgb(margin_main_x + rxy[0], margin_main_y + rxy[1],
			rxy[2] - rxy[0], 1, rgb + offx, 0, ctx);

	if ((nxy[3] <= vxy[3]) && (marq_y1 < mem_height) && (marq_y2 < mem_height))
		draw_rgb(margin_main_x + rxy[0], margin_main_y + rxy[3] - 1,
			rxy[2] - rxy[0], 1, rgb + offx, 0, ctx);

	free(rgb);
}

void paint_marquee(int action, int new_x, int new_y)
{
	int vxy[4], cxy[4];

	cxy[0] = cxy[1] = 0;
	canvas_size(cxy + 2, cxy + 3);
	get_visible(vxy);
	clip(vxy, vxy[0] - margin_main_x, vxy[1] - margin_main_y,
		vxy[2] - margin_main_x + 1, vxy[3] - margin_main_y + 1, cxy);
	/* Have to call in any case, to update location */
	trace_marquee(action, new_x, new_y, vxy, NULL);
}

void refresh_marquee(rgbcontext *ctx)
{
	int vxy[4], cxy[4];

	cxy[0] = cxy[1] = 0;
	canvas_size(cxy + 2, cxy + 3);
	get_visible(vxy);
	++vxy[2]; ++vxy[3];
	if (clip(vxy, (ctx->x0 > vxy[0] ? ctx->x0 : vxy[0]) - margin_main_x,
		(ctx->y0 > vxy[1] ? ctx->y0 : vxy[1]) - margin_main_y,
		(ctx->x1 < vxy[2] ? ctx->x1 : vxy[2]) - margin_main_x,
		(ctx->y1 < vxy[3] ? ctx->y1 : vxy[3]) - margin_main_y, cxy))
		trace_marquee(1, 0, 0, vxy, ctx);
}


int close_to( int x1, int y1 )		// Which corner of selection is coordinate closest to?
{
	return ((x1 + x1 <= marq_x1 + marq_x2 ? 0 : 1) +
		(y1 + y1 <= marq_y1 + marq_y2 ? 0 : 2));
}

#define MIN_REDRAW 16 /* Minimum dimension for redraw rectangle */
void trace_line(int mode, int lx1, int ly1, int lx2, int ly2, int *vxy, rgbcontext *ctx)
{
	int vxt[4];
	int i, j, x, y, tx, ty, aw, ah, ax = 0, ay = 0, cf = 0;
	int rgb = 0, zoom = 1, scale = 1;
	linedata line;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0)
	{
		zoom = rint(1.0 / can_zoom);
		lx1 = floor_div(lx1, zoom);
		ly1 = floor_div(ly1, zoom);
		lx2 = floor_div(lx2, zoom);
		ly2 = floor_div(ly2, zoom);
 	}
	else if (can_zoom > 1.0)
	{
		scale = rint(can_zoom);
		for (i = 0; i < 4; i++) vxt[i] = floor_div(vxy[i], scale);
		vxy = vxt;
	}

	line_init(line, lx1, ly1, lx2, ly2); i = line[2];
	if (line_clip(line, vxy, &j) < 0) return;
	for (i -= j; line[2] >= 0; line_step(line) , i--)
	{
		x = (tx = line[0]) * scale;
		y = (ty = line[1]) * scale;

		if (mode != 0) /* Show a line */
		{
			if (mode == 1) /* Drawing */
			{
				j = ((ty & 7) * 8 + (tx & 7)) * 3;
				rgb = MEM_2_INT(mem_col_pat24, j);
			}
			else if (mode == 2) /* Tracking */
			{
				rgb = ((i >> 2) & 1) * 0xFFFFFF;
			}
			else if (mode == 3) /* Gradient */
			{
				rgb = ((i >> 2) & 1) * 0xFFFFFF ^
					((i >> 1) & 1) * 0x00FF00;
			}
// !!! This is slow as molasses on Win32 (or Win98SE at least) when unbuffered
			fill_rgb(margin_main_x + x, margin_main_y + y,
				scale, scale, rgb, ctx);
			continue;
		}

		/* Doing a clear */
		if (!cf) ax = x , ay = y , cf = 1; // Start a new rectangle

		/* Redraw now or wait some more? */
		aw = scale + abs(x - ax);
		ah = scale + abs(y - ay);
		if ((aw < MIN_REDRAW) && (ah < MIN_REDRAW) && line[2]) continue;

		/* Commit canvas clear if >16 pixels or final pixel of this line */
		repaint_canvas(margin_main_x + (ax < x ? ax : x),
			margin_main_y + (ay < y ? ay : y), aw, ah);
		cf = 0;
	}
}

void repaint_line(int mode)			// Repaint or clear line on canvas
{
	int vxy[4];

	get_visible(vxy);
// !!! This assumes that if there's clipping, then there aren't margins
	trace_line(mode, line_x1, line_y1, line_x2, line_y2, vxy, NULL);
}

void repaint_grad(int mode)
{
	grad_info *grad = gradient + mem_channel;
	int vxy[4], oldgrad = grad->status;

	if (mode) mode = 3;
	else grad->status = GRAD_NONE; /* To avoid hitting repaint */

	get_visible(vxy);
	vxy[0] -= margin_main_x; vxy[1] -= margin_main_y;
	vxy[2] -= margin_main_x; vxy[3] -= margin_main_y;
	trace_line(mode, grad->x2, grad->y2, grad->x1, grad->y1, vxy, NULL);
	grad->status = oldgrad;
}

void refresh_grad(rgbcontext *ctx)
{
	grad_info *grad = gradient + mem_channel;
	int vxy[4];

	get_visible(vxy);
	vxy[0] = (ctx->x0 > vxy[0] ? ctx->x0 : vxy[0]) - margin_main_x;
	vxy[1] = (ctx->y0 > vxy[1] ? ctx->y0 : vxy[1]) - margin_main_y;
	vxy[2] = (ctx->x1 <= vxy[2] ? ctx->x1 - 1 : vxy[2]) - margin_main_x;
	vxy[3] = (ctx->y1 <= vxy[3] ? ctx->y1 - 1 : vxy[3]) - margin_main_y;
	trace_line(3, grad->x2, grad->y2, grad->x1, grad->y1, vxy, ctx);
}

void update_recent_files()			// Update the menu items
{
	char txt[64], *t, txt2[PATHTXT];
	int i, count = 0;

	for (i = 0; i < recent_files; i++)	// Display recent filenames
	{
		sprintf(txt, "file%i", i + 1);

		t = inifile_get(txt, ".");
		if (strlen(t) < 2)	// Hide if empty
		{
			gtk_widget_hide(menu_widgets[MENU_RECENT1 + i]);
			continue;
		}
		gtkuncpy(txt2, t, PATHTXT);
		gtk_label_set_text(GTK_LABEL(GTK_MENU_ITEM(
			menu_widgets[MENU_RECENT1 + i])->item.bin.child), txt2);
		gtk_widget_show(menu_widgets[MENU_RECENT1 + i]);
		count++;
	}
	for (; i < MAX_RECENT; i++)		// Hide extra items
		gtk_widget_hide(menu_widgets[MENU_RECENT1 + i]);

	// Hide separator if not needed
	if (count) gtk_widget_show(menu_widgets[MENU_RECENT_S]);
		else gtk_widget_hide(menu_widgets[MENU_RECENT_S]);
}

void register_file( char *filename )		// Called after successful load/save
{
	char txt[64], txt1[64], *c;
	int i, f;

	c = strrchr( filename, DIR_SEP );
	if (c)
	{
		i = *c;
		*c = '\0';		// Strip off filename
		inifile_set("last_dir", filename);
		*c = i;
	}

	// Is it already in used file list?  If so shift relevant filenames down and put at top.
	i = 1;
	f = 0;
	while ( i<MAX_RECENT && f==0 )
	{
		sprintf( txt, "file%i", i );
		c = inifile_get( txt, "." );
		if ( strcmp( filename, c ) == 0 ) f = 1;	// Filename found in list
		else i++;
	}
	if ( i>1 )			// If file is already most recent, do nothing
	{
		while ( i>1 )
		{
			sprintf( txt, "file%i", i-1 );
			sprintf( txt1, "file%i", i );
			inifile_set(txt1, inifile_get(txt, ""));

			i--;
		}
		inifile_set("file1", filename);		// Strip off filename
	}

	update_recent_files();
}

void scroll_wheel( int x, int y, int d )		// Scroll wheel action from mouse
{
	if (d == 1) zoom_in();
	else zoom_out();
}

void create_default_image()			// Create default new image
{
	int	nw = inifile_get_gint32("lastnewWidth", DEFAULT_WIDTH ),
		nh = inifile_get_gint32("lastnewHeight", DEFAULT_HEIGHT ),
		nc = inifile_get_gint32("lastnewCols", 256 ),
		nt = inifile_get_gint32("lastnewType", 2 );

	do_new_one(nw, nh, nc, nt == 1 ? NULL : mem_pal_def,
		(nt == 0) || (nt > 2) ? 3 : 1, FALSE);
}
