/*	canvas.c
	Copyright (C) 2004-2006 Mark Tyler and Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/

#include <math.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"

#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "mygtk.h"
#include "inifile.h"
#include "canvas.h"
#include "quantizer.h"
#include "viewer.h"
#include "layer.h"
#include "polygon.h"
#include "wu.h"
#include "prefs.h"
#include "ani.h"
#include "channels.h"
#include "toolbar.h"

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

gboolean show_paste,					// Show contents of clipboard while pasting
	col_reverse = FALSE,				// Painting with right button
	text_paste = FALSE,				// Are we pasting text?
	canvas_image_centre = TRUE,			// Are we centering the image?
	chequers_optimize = TRUE			// Are we optimizing the chequers for speed?
	;


///	STATUS BAR

void update_image_bar()
{
	char txt[64], txt2[16];

	toolbar_update_settings();		// Update A/B labels in settings toolbar

	if ( mem_img_bpp == 1 )
		sprintf(txt2, "%i", mem_cols);
	else
		sprintf(txt2, "RGB");

	snprintf(txt, 50, "%s %i x %i x %s", channames[mem_channel],
		mem_width, mem_height, txt2);

	if ( mem_img[CHN_ALPHA] || mem_img[CHN_SEL] || mem_img[CHN_MASK] )
	{
		strcat(txt, " + ");
		if ( mem_img[CHN_ALPHA] ) strcat(txt, "A");
		if ( mem_img[CHN_SEL] ) strcat(txt, "S");
		if ( mem_img[CHN_MASK] ) strcat(txt, "M");
	}

	if ( layers_total>0 )
	{
		sprintf(txt2, "  (%i/%i)", layer_selected, layers_total);
		strcat(txt, txt2);
	}
	if ( mem_xpm_trans>=0 )
	{
		sprintf(txt2, "  (T=%i)", mem_xpm_trans);
		strcat(txt, txt2);
	}
	strcat(txt, "  ");
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

static void chan_txt_cat(char *txt, int chan, int x, int y)
{
	char txt2[8];

	if ( mem_img[chan] )
	{
		snprintf( txt2, 8, "%i", mem_img[chan][x + mem_width*y] );
		strcat(txt, txt2);
	}
}

void update_xy_bar(int x, int y)
{
	char txt[96];
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
			snprintf(txt, 60, "[%u] = {%i,%i,%i}", pixel,
				mem_pal[pixel].red, mem_pal[pixel].green,
				mem_pal[pixel].blue);
		else
			snprintf(txt, 60, "{%i,%i,%i}", INT_2_R(pixel),
				INT_2_G(pixel), INT_2_B(pixel));
		if (mem_img[CHN_ALPHA] || mem_img[CHN_SEL] || mem_img[CHN_MASK])
		{
			strcat(txt, " + {");
			chan_txt_cat(txt, CHN_ALPHA, x, y);
			strcat(txt, ",");
			chan_txt_cat(txt, CHN_SEL, x, y);
			strcat(txt, ",");
			chan_txt_cat(txt, CHN_MASK, x, y);
			strcat(txt, "}");
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
	update_image_bar();
	if ( !status_on[STATUS_GEOMETRY] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_GEOMETRY]), "" );

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
		gtk_widget_set_usize(label_bar[STATUS_UNDOREDO], 50, -2);
		update_undo_bar();
	}
	else
	{
		gtk_widget_set_usize(label_bar[STATUS_UNDOREDO], 0, -2);
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_UNDOREDO]), "" );
	}
}


void commit_paste( gboolean undo )
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

	if ( undo ) mem_undo_next(UNDO_PASTE);	// Do memory stuff for undo

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

	update_menus();				// Update menu undo issues
	vw_update_area(fx, fy, fw, fh);
	main_update_area(fx, fy, fw, fh);
}

void paste_prepare()
{
	poly_status = POLY_NONE;
	poly_points = 0;
	if ( tool_type != TOOL_SELECT && tool_type != TOOL_POLYGON )
	{
		clear_perim();
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
	}
	else
	{
		if ( marq_status != MARQUEE_NONE ) paint_marquee(0, marq_x1, marq_y1);
	}
}

void iso_trans( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	int i;

	i = mem_isometrics(item);

	if ( i==0 ) canvas_undo_chores();
	else
	{
		if ( i==-666 ) alert_box( _("Error"), _("The image is too large to transform."),
					_("OK"), NULL, NULL );
		else memory_errors(i);
	}
}

void create_pal_quantized( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	int i = 0;
	unsigned char newpal[3][256];

	mem_undo_next(UNDO_PAL);

	if ( item==1 )
		i = dl1quant(mem_img[CHN_IMAGE], mem_width, mem_height, mem_cols, newpal);
	if ( item==3 )
		i = dl3quant(mem_img[CHN_IMAGE], mem_width, mem_height, mem_cols, newpal);
	if ( item==5 )
		i = wu_quant(mem_img[CHN_IMAGE], mem_width, mem_height, mem_cols, newpal);

	if ( i!=0 ) memory_errors(i);
	else
	{
		for ( i=0; i<mem_cols; i++ )
		{
			mem_pal[i].red = newpal[0][i];
			mem_pal[i].green = newpal[1][i];
			mem_pal[i].blue = newpal[2][i];
		}

		update_menus();
		init_pal();
	}
}

void pressed_invert( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_INV);

	mem_invert();

	init_pal();
	update_all_views();
	gtk_widget_queue_draw( drawing_col_prev );
}

void pressed_edge_detect( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_FILT);
	do_effect(0, 0);
	update_all_views();
}

int do_fx(GtkWidget *spin, gpointer fdata)
{
	int i;

	i = read_spin(spin);
	spot_undo(UNDO_FILT);
	do_effect((int)fdata, i);

	return TRUE;
}

void pressed_sharpen( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *spin = add_a_spin(50, 1, 100);
	filter_window(_("Edge Sharpen"), spin, do_fx, (gpointer)(3), FALSE);
}

void pressed_soften( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *spin = add_a_spin(50, 1, 100);
	filter_window(_("Edge Soften"), spin, do_fx, (gpointer)(4), FALSE);
}

void pressed_emboss( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_FILT);
	do_effect(2, 0);
	update_all_views();
}

int do_gauss(GtkWidget *box, gpointer fdata)
{
	GtkWidget *spinX, *spinY, *toggleXY;
	double radiusX, radiusY;
	int gcor = FALSE;

	spinX = BOX_CHILD(box, 0);
	spinY = BOX_CHILD(box, 1);
	toggleXY = BOX_CHILD(box, 2);
	if (mem_channel == CHN_IMAGE) gcor = gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(BOX_CHILD(box, 3)));

	gtk_spin_button_update(GTK_SPIN_BUTTON(spinX));
	radiusX = radiusY = gtk_spin_button_get_value_as_float(GTK_SPIN_BUTTON(spinX));
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggleXY)))
	{
		gtk_spin_button_update(GTK_SPIN_BUTTON(spinY));
		radiusY = gtk_spin_button_get_value_as_float(GTK_SPIN_BUTTON(spinY));
	}

	spot_undo(UNDO_DRAW);
	mem_gauss(radiusX, radiusY, gcor);

	return TRUE;
}

static void gauss_xy_click(GtkButton *button, GtkWidget *spin)
{
	gtk_widget_set_sensitive(spin,
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}

void pressed_gauss(GtkMenuItem *menu_item, gpointer user_data)
{
	int i;
	GtkWidget *box, *spin, *check;

	box = gtk_vbox_new(FALSE, 5);
	gtk_widget_show(box);
	for (i = 0; i < 2; i++)
	{
		spin = add_float_spin(1, 0, 200);
		gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
	}
	gtk_widget_set_sensitive(spin, FALSE);
	check = add_a_toggle(_("Different X/Y"), box, FALSE);
	gtk_signal_connect(GTK_OBJECT(check), "clicked",
		GTK_SIGNAL_FUNC(gauss_xy_click), (gpointer)spin);
	if (mem_channel == CHN_IMAGE) add_a_toggle(_("Gamma corrected"), box,
		inifile_get_gboolean("defaultGamma", FALSE));
	filter_window(_("Gaussian Blur"), box, do_gauss, NULL, FALSE);
}

int do_unsharp(GtkWidget *box, gpointer fdata)
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

	gtk_spin_button_update(GTK_SPIN_BUTTON(spinR));
	gtk_spin_button_update(GTK_SPIN_BUTTON(spinA));
	gtk_spin_button_update(GTK_SPIN_BUTTON(spinT));
	radius = gtk_spin_button_get_value_as_float(GTK_SPIN_BUTTON(spinR));
	amount = gtk_spin_button_get_value_as_float(GTK_SPIN_BUTTON(spinA));
	threshold = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinT));

	// !!! No RGBA mode for now, so UNDO_DRAW isn't needed
	spot_undo(UNDO_FILT);
	mem_unsharp(radius, amount, threshold, gcor);

	return TRUE;
}

void pressed_unsharp(GtkMenuItem *menu_item, gpointer user_data)
{
	GtkWidget *box, *table, *spin;

	box = gtk_vbox_new(FALSE, 5);
	table = add_a_table(3, 2, 0, box);
	gtk_widget_show_all(box);
	spin = add_float_spin(5, 0, 200);
	gtk_table_attach(GTK_TABLE(table), spin, 1, 2,
		0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 5);
	spin = add_float_spin(0.5, 0, 10);
	gtk_table_attach(GTK_TABLE(table), spin, 1, 2,
		1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 5);
	spin = add_a_spin(0, 0, 255);
	gtk_table_attach(GTK_TABLE(table), spin, 1, 2,
		2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 5);
	add_to_table(_("Radius"), table, 0, 0, 5);
	add_to_table(_("Amount"), table, 1, 0, 5);
	add_to_table(_("Threshold "), table, 2, 0, 5);
	if (mem_channel == CHN_IMAGE) add_a_toggle(_("Gamma corrected"), box,
		inifile_get_gboolean("defaultGamma", FALSE));
	filter_window(_("Unsharp Mask"), box, do_unsharp, NULL, FALSE);
}

void pressed_convert_rgb( GtkMenuItem *menu_item, gpointer user_data )
{
	int i;

	i = mem_convert_rgb();

	if ( i!=0 ) memory_errors(i);
	else
	{
		if ( tool_type == TOOL_SELECT && marq_status >= MARQUEE_PASTE )
			pressed_select_none( NULL, NULL );
				// If the user is pasting, lose it!

		update_menus();
		init_pal();
		gtk_widget_queue_draw( drawing_canvas );
		gtk_widget_queue_draw( drawing_col_prev );
	}
}

void pressed_greyscale( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	spot_undo(UNDO_COL);

	mem_greyscale(item);

	init_pal();
	update_all_views();
	gtk_widget_queue_draw( drawing_col_prev );
}

void pressed_rotate_image( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	if ( mem_image_rot(item) == 0 )
	{
		check_marquee();
		canvas_undo_chores();
	}
	else alert_box( _("Error"), _("Not enough memory to rotate image"), _("OK"), NULL, NULL );
}

void pressed_rotate_sel( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	if ( mem_sel_rot(item) == 0 )
	{
		check_marquee();
		gtk_widget_queue_draw( drawing_canvas );
	}
	else	alert_box( _("Error"), _("Not enough memory to rotate clipboard"), _("OK"), NULL, NULL );
}

int do_rotate_free(GtkWidget *box, gpointer fdata)
{
	GtkWidget *spin = ((GtkBoxChild*)GTK_BOX(box)->children->data)->widget;
	int j, smooth = 0, gcor = 0;
	double angle;

	gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
	angle = gtk_spin_button_get_value_as_float(GTK_SPIN_BUTTON(spin));

	if (mem_img_bpp == 3)
	{
		GtkWidget *gch = ((GtkBoxChild*)GTK_BOX(box)->children->next->data)->widget;
		GtkWidget *check = ((GtkBoxChild*)GTK_BOX(box)->children->next->next->data)->widget;
		gcor = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gch));
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)))
			smooth = 1;
	}
	j = mem_rotate_free(angle, smooth, gcor);
	if (!j) canvas_undo_chores();
	else
	{
		if (j == -5) alert_box(_("Error"),
			_("The image is too large for this rotation."),
			_("OK"), NULL, NULL);
		else memory_errors(j);
	}

	return TRUE;
}

void pressed_rotate_free( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *box, *spin = add_a_spin(45, -360, 360);
	box = gtk_vbox_new(FALSE, 5);
	gtk_widget_show(box);
	gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
	if (mem_img_bpp == 3)
	{
		add_a_toggle(_("Gamma corrected"), box,
			inifile_get_gboolean("defaultGamma", FALSE));
		add_a_toggle(_("Smooth"), box, TRUE);
	}
	filter_window(_("Free Rotate"), box, do_rotate_free, NULL, FALSE);
}


void pressed_clip_mask( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	int i;

	if ( mem_clip_mask == NULL )
	{
		i = mem_clip_mask_init(item ^ 255);
		if ( i != 0 )
		{
			memory_errors(1);	// Not enough memory
			return;
		}
	}
	mem_clip_mask_set(item);
	gtk_widget_queue_draw( drawing_canvas );
}

void pressed_clip_alphamask()
{
	unsigned char *old_mask = mem_clip_mask;
	int i, j = mem_clip_w * mem_clip_h, k;

	if (!mem_clipboard || !mem_clip_alpha) return;

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

	gtk_widget_queue_draw( drawing_canvas );
}

void pressed_clip_alpha_scale()
{
	if (!mem_clipboard || (mem_clip_bpp != 3)) return;
	if (!mem_clip_mask) mem_clip_mask_init(255);
	if (!mem_clip_mask) return;

	if (mem_scale_alpha(mem_clipboard, mem_clip_mask,
		mem_clip_w, mem_clip_h, TRUE)) return;

	gtk_widget_queue_draw( drawing_canvas );
}

void pressed_clip_mask_all()
{
	int i;

	i = mem_clip_mask_init(0);
	if ( i != 0 )
	{
		memory_errors(1);	// Not enough memory
		return;
	}
	gtk_widget_queue_draw( drawing_canvas );
}

void pressed_clip_mask_clear()
{
	if ( mem_clip_mask != NULL )
	{
		mem_clip_mask_clear();
		gtk_widget_queue_draw( drawing_canvas );
	}
}

void pressed_flip_image_v( GtkMenuItem *menu_item, gpointer user_data )
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
	update_all_views();
}

void pressed_flip_image_h( GtkMenuItem *menu_item, gpointer user_data )
{
	int i;

	spot_undo(UNDO_XFORM);
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!mem_img[i]) continue;
		mem_flip_h(mem_img[i], mem_width, mem_height, BPP(i));
	}
	update_all_views();
}

void pressed_flip_sel_v( GtkMenuItem *menu_item, gpointer user_data )
{
	unsigned char *temp;

	temp = malloc(mem_clip_w * mem_clip_bpp);
	if (!temp) return; /* Not enough memory for temp buffer */
	mem_flip_v(mem_clipboard, temp, mem_clip_w, mem_clip_h, mem_clip_bpp);
	if (mem_clip_mask) mem_flip_v(mem_clip_mask, temp, mem_clip_w, mem_clip_h, 1);
	if (mem_clip_alpha) mem_flip_v(mem_clip_alpha, temp, mem_clip_w, mem_clip_h, 1);
	gtk_widget_queue_draw( drawing_canvas );
}

void pressed_flip_sel_h( GtkMenuItem *menu_item, gpointer user_data )
{
	mem_flip_h(mem_clipboard, mem_clip_w, mem_clip_h, mem_clip_bpp);
	if (mem_clip_mask) mem_flip_h(mem_clip_mask, mem_clip_w, mem_clip_h, 1);
	if (mem_clip_alpha) mem_flip_h(mem_clip_alpha, mem_clip_w, mem_clip_h, 1);
	gtk_widget_queue_draw( drawing_canvas );
}

void paste_init()
{
	marq_status = MARQUEE_PASTE;
	cursor_corner = -1;
	update_sel_bar();
	update_menus();
	gtk_widget_queue_draw( drawing_canvas );
}

void pressed_paste( GtkMenuItem *menu_item, gpointer user_data )
{
	paste_prepare();
	marq_x1 = mem_clip_x;
	marq_y1 = mem_clip_y;
	marq_x2 = mem_clip_x + mem_clip_w - 1;
	marq_y2 = mem_clip_y + mem_clip_h - 1;
	paste_init();
}

void pressed_paste_centre( GtkMenuItem *menu_item, gpointer user_data )
{
	int w, h;
	GtkAdjustment *hori, *vert;

	hori = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas));
	vert = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas));

	canvas_size(&w, &h);
	if (hori->page_size > w) mem_icx = 0.5;
	else mem_icx = (hori->value + hori->page_size * 0.5) / w;

	if (vert->page_size > h) mem_icy = 0.5;
	else mem_icy = (vert->value + vert->page_size * 0.5) / h;

	paste_prepare();
	align_size(can_zoom);
	marq_x1 = mem_width * mem_icx - mem_clip_w * 0.5;
	marq_y1 = mem_height * mem_icy - mem_clip_h * 0.5;
	marq_x2 = marq_x1 + mem_clip_w - 1;
	marq_y2 = marq_y1 + mem_clip_h - 1;
	paste_init();
}

void pressed_rectangle( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	int x, y, w, h;

	spot_undo(UNDO_DRAW);

	if ( tool_type == TOOL_POLYGON )
	{
		if (!item) poly_outline();
		else poly_paint();
	}
	else
	{
		x = marq_x1 < marq_x2 ? marq_x1 : marq_x2;
		y = marq_y1 < marq_y2 ? marq_y1 : marq_y2;
		w = abs(marq_x1 - marq_x2) + 1;
		h = abs(marq_y1 - marq_y2) + 1;

		if (item || (2 * tool_size >= w) || (2 * tool_size >= h))
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

	update_all_views();
}

void pressed_ellipse( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	spot_undo(UNDO_DRAW);
	if (!item) o_ellipse( marq_x1, marq_y1, marq_x2, marq_y2, tool_size );
	else f_ellipse( marq_x1, marq_y1, marq_x2, marq_y2 );
	update_all_views();
}

static int copy_clip()
{
	int i, x, y, w, h, bpp, ofs, delta, len;


	x = marq_x1 < marq_x2 ? marq_x1 : marq_x2;
	y = marq_y1 < marq_y2 ? marq_y1 : marq_y2;
	w = abs(marq_x1 - marq_x2) + 1;
	h = abs(marq_y1 - marq_y2) + 1;

	bpp = MEM_BPP;
	free(mem_clipboard);		// Lose old clipboard
	free(mem_clip_alpha);		// Lose old clipboard alpha
	mem_clip_mask_clear();		// Lose old clipboard mask
	mem_clip_alpha = NULL;
	if ((mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA] &&
		 !channel_dis[CHN_ALPHA]) mem_clip_alpha = malloc(w * h);
	mem_clipboard = malloc(w * h * bpp);
	text_paste = FALSE;

	if (!mem_clipboard)
	{
		free(mem_clip_alpha);
		alert_box( _("Error"), _("Not enough memory to create clipboard"),
			_("OK"), NULL, NULL );
		return (FALSE);
	}

	mem_clip_bpp = bpp;
	mem_clip_x = x;
	mem_clip_y = y;
	mem_clip_w = w;
	mem_clip_h = h;

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
	kk = mem_channel <= CHN_ALPHA ? tool_opacity : 255;

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
}

static void trim_clip()
{
	int i, j, offs, offd, maxx, maxy, minx, miny, nw, nh;
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
		memmove(mem_clip_mask + offd, mem_clip_mask + offs, nw);
		if (mem_clip_alpha)
			memmove(mem_clip_alpha + offd, mem_clip_alpha + offs, nw);
		memmove(mem_clipboard + offd * mem_clip_bpp,
			mem_clipboard + offs * mem_clip_bpp, nw * mem_clip_bpp);
	}

	/* Try to realloc memory for smaller clipboard */
	tmp = realloc(mem_clipboard, nw * nh * mem_clip_bpp);
	if (tmp) mem_clipboard = tmp;
	tmp = realloc(mem_clip_mask, nw * nh);
	if (tmp) mem_clip_mask = tmp;
	if (mem_clip_alpha)
	{
		tmp = realloc(mem_clip_alpha, nw * nh);
		if (tmp) mem_clip_alpha = tmp;
	}

	mem_clip_w = nw;
	mem_clip_h = nh;
	mem_clip_x += minx;
	mem_clip_y += miny;
}

void pressed_copy( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	if (!item && (tool_type == TOOL_SELECT) && (marq_status >= MARQUEE_PASTE))
	{
		mem_clip_x = marq_x1 < marq_x2 ? marq_x1 : marq_x2;
		mem_clip_y = marq_y1 < marq_y2 ? marq_y1 : marq_y2;
		return;
	}

	if (!copy_clip()) return;
	if (tool_type == TOOL_POLYGON) poly_mask();
	channel_mask();
	if (item) cut_clip();
	update_all_views();
	update_menus();
}

void pressed_lasso( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	if (!copy_clip()) return;
	if (tool_type == TOOL_POLYGON) poly_mask();
	else mem_clip_mask_init(255);
	poly_lasso();
	channel_mask();
	trim_clip();
	if (item) cut_clip();
	pressed_paste_centre( NULL, NULL );
}

void update_menus()			// Update edit/undo menu
{
	int i, j;

	update_undo_bar();

	men_item_state(menu_only_24, mem_img_bpp == 3);
	men_item_state(menu_not_indexed, (mem_img_bpp == 3) ||
		(mem_channel != CHN_IMAGE));
	men_item_state(menu_only_indexed, mem_img_bpp == 1);

	men_item_state(menu_alphablend, mem_clipboard && (mem_clip_bpp == 3));

	if ( marq_status == MARQUEE_NONE )
	{
		men_item_state(menu_need_selection, FALSE);
		men_item_state(menu_crop, FALSE);
		men_item_state(menu_lasso, poly_status == POLY_DONE);
		men_item_state(menu_need_marquee, poly_status == POLY_DONE);
	}
	else
	{
		men_item_state(menu_need_marquee, TRUE);

		men_item_state(menu_lasso, marq_status <= MARQUEE_DONE);

		/* If we are pasting disallow copy/cut/crop */
		men_item_state(menu_need_selection, marq_status < MARQUEE_PASTE);

		/* Only offer the crop option if the user hasn't selected everything */
		men_item_state(menu_crop, (marq_status <= MARQUEE_DONE) &&
			((abs(marq_x1 - marq_x2) < mem_width - 1) ||
			(abs(marq_y1 - marq_y2) < mem_height - 1)));
	}

	/* Forbid RGB-to-indexed paste, but allow indexed-to-RGB */
	men_item_state(menu_need_clipboard, mem_clipboard && (mem_clip_bpp <= MEM_BPP));

	men_item_state(menu_undo, !!mem_undo_done);
	men_item_state(menu_redo, !!mem_undo_redo);

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_chann_x[mem_channel]), TRUE);

	for (i = j = 0; i < NUM_CHANNELS; i++)	// Enable/disable channel enable/disable
	{
		if (mem_img[i]) j++;
		gtk_widget_set_sensitive(menu_chan_dis[i], !!mem_img[i]);
	}
	men_item_state(menu_chan_del, j > 1);
}

void canvas_undo_chores()
{
	int w, h;

	canvas_size(&w, &h);
	gtk_widget_set_usize(drawing_canvas, w, h);
	update_all_views();			// redraw canvas widget
	update_menus();
	init_pal();
	gtk_widget_queue_draw(drawing_col_prev);
}

void check_undo_paste_bpp()
{
	if (marq_status >= MARQUEE_PASTE && (mem_clip_bpp != MEM_BPP))
		pressed_select_none( NULL, NULL );

	if ( tool_type == TOOL_SMUDGE && mem_img_bpp == 1 )
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
		// User is smudging and undo/redo to an indexed image - reset tool
}

void main_undo( GtkMenuItem *menu_item, gpointer user_data )
{
	mem_undo_backward();
	check_undo_paste_bpp();
	canvas_undo_chores();
}

void main_redo( GtkMenuItem *menu_item, gpointer user_data )
{
	mem_undo_forward();
	check_undo_paste_bpp();
	canvas_undo_chores();
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

	update_all_views();
	init_pal();
	gtk_widget_queue_draw(drawing_col_prev);

	return 0;
}


void update_cols()
{
	if (!mem_img[CHN_IMAGE]) return;	// Only do this if we have an image

	mem_pat_update(); /* !!! The order is significant for gradient sample */
	update_image_bar();

	if ( marq_status >= MARQUEE_PASTE && text_paste )
	{
		render_text( drawing_col_prev );
		check_marquee();
		gtk_widget_queue_draw( drawing_canvas );
	}

	gtk_widget_queue_draw( drawing_col_prev );
}

void init_pal()					// Initialise palette after loading
{
	mem_mask_init();		// Reinit RGB masks
	mem_pal_init();			// Update palette RGB on screen
	gtk_widget_set_usize( drawing_palette, PALETTE_WIDTH, 4+mem_cols*16 );

	update_cols();
	gtk_widget_queue_draw( drawing_palette );
}

#if GTK_MAJOR_VERSION == 2
void cleanse_txt( char *out, char *in )		// Cleans up non ASCII chars for GTK+2
{
	char *c;

	c = g_locale_to_utf8( (gchar *) in, -1, NULL, NULL, NULL );
	if ( c == NULL )
	{
		sprintf(out, "Error in cleanse_txt using g_*_to_utf8");
	}
	else
	{
		strcpy( out, c );
		g_free(c);
	}
}
#endif

void set_new_filename( char *fname )
{
	strncpy( mem_filename, fname, 250 );
	update_titlebar();
}

static int populate_channel(char *filename)
{
	int ftype, res = -1;

	ftype = detect_image_format(filename);
	if (ftype < 0) return (-1); /* Silently fail if no file */

	/* Don't bother with mismatched formats */
	if (file_formats[ftype].flags & (MEM_BPP == 1 ? FF_IDX | FF_BW : FF_RGB))
		res = load_image(filename, FS_CHANNEL_LOAD, ftype);

	/* Successful */
	if (res == 1) canvas_undo_chores();

	/* Not enough memory available */
	else if (res == FILE_MEM_ERROR) memory_errors(1);

	/* Unspecified error */
	else alert_box(_("Error"), _("Invalid channel file."), _("OK"), NULL, NULL);

	return (res == 1 ? 0 : -1);
}

int do_a_load( char *fname )
{
	char mess[512], real_fname[300];
	int res, i, ftype;


	if ((fname[0] != DIR_SEP)
#ifdef WIN32
		&& (fname[1] != ':')
#endif
	)
	{
		getcwd(real_fname, 256);
		i = strlen(real_fname);
		real_fname[i] = DIR_SEP;
		real_fname[i + 1] = 0;
		strncat(real_fname, fname, 256);
	}
	else strncpy(real_fname, fname, 256);

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
			snprintf(mess, 500, _("File is too big, must be <= to width=%i height=%i : %s"), MAX_WIDTH, MAX_HEIGHT, fname);
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
		set_new_filename(real_fname);

		if ( layers_total>0 )
			layers_notify_changed(); // We loaded an image into the layers, so notify change
	}
	else /* A whole bunch of layers */
	{
//		if ( layers_window == NULL ) pressed_layers( NULL, NULL );
		if ( !view_showing ) view_show();
			// We have just loaded a layers file so display view & layers window if not up
	}
	/* To prevent automatic paste following a file load when enabling
	 * "Changing tool commits paste" via preferences */
	pressed_select_none(NULL, NULL);
	reset_tools();
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );

	/* Show new image */
	update_all_views();
	update_image_bar();

	/* These 2 are needed to synchronize the scrollbars & image view */
	gtk_adjustment_value_changed( gtk_scrolled_window_get_hadjustment(
		GTK_SCROLLED_WINDOW(scrolledwindow_canvas) ) );
	gtk_adjustment_value_changed( gtk_scrolled_window_get_vadjustment(
		GTK_SCROLLED_WINDOW(scrolledwindow_canvas) ) );

fail:	set_image(TRUE);
	return (res <= 0);
}



///	FILE SELECTION WINDOW

int check_file( char *fname )		// Does file already exist?  Ask if OK to overwrite
{
	char mess[512];

	if ( valid_file(fname) == 0 )
	{
		snprintf(mess, 500, _("File: %s already exists. Do you want to overwrite it?"), fname);
		if ( alert_box( _("File Found"), mess, _("NO"), _("YES"), NULL ) != 2 ) return 1;
	}

	return 0;
}


static void change_image_format(GtkMenuItem *menuitem, GtkWidget *box)
{
	static int flags[] = {FF_TRANS, FF_COMPR, FF_SPOT, FF_SPOT, 0};
	GList *chain = GTK_BOX(box)->children->next->next;
	int i, ftype;

	ftype = (int)gtk_object_get_user_data(GTK_OBJECT(menuitem));
	/* Hide/show name/value widget pairs */
	for (i = 0; flags[i]; i++)
	{
		if (file_formats[ftype].flags & flags[i])
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
	GtkWidget *opt, *menu, *item, *label, *spin;
	int i, j, k, mask = FF_IDX;
	char *ext = strrchr(name, '.');

	ext = ext ? ext + 1 : "";
	switch (mode)
	{
	default: return;
	case FS_CHANNEL_SAVE: if (mem_channel != CHN_IMAGE) break;
	case FS_PNG_SAVE: mask = mem_img_bpp == 3 ? FF_RGB : mem_cols <= 2 ?
		FF_BW | FF_IDX : FF_IDX;
		break;
	case FS_COMPOSITE_SAVE: mask = FF_RGB;
	}

	/* Create controls (!!! two widgets per value - used in traversal) */
	label = gtk_label_new(_("File Format"));
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);
	opt = gtk_option_menu_new();
	gtk_box_pack_start(GTK_BOX(box), opt, FALSE, FALSE, 5);

	label = gtk_label_new(_("Transparency index"));
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);
	spin = add_a_spin(mem_xpm_trans, -1, mem_cols - 1);
	gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 5);

	label = gtk_label_new(_("JPEG Save Quality (100=High)"));
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);
	spin = add_a_spin(mem_jpeg_quality, 0, 100);
	gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 5);

	label = gtk_label_new(_("Hotspot at X ="));
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);
	spin = add_a_spin(mem_xbm_hot_x, -1, mem_width - 1);
	gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 5);

	label = gtk_label_new(_("Y ="));
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);
	spin = add_a_spin(mem_xbm_hot_y, -1, mem_height - 1);
	gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 5);

	gtk_widget_show_all(box);

	menu = gtk_menu_new();
	for (i = j = k = 0; i < NUM_FTYPES; i++)
	{
		if (!(file_formats[i].flags & mask)) continue;
		if (!strncasecmp(ext, file_formats[i].ext, LONGEST_EXT) ||
			(file_formats[i].ext2[0] &&
			!strncasecmp(ext, file_formats[i].ext2, LONGEST_EXT)))
			j = k;
		item = gtk_menu_item_new_with_label(file_formats[i].name);
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)i);
		gtk_signal_connect(GTK_OBJECT(item), "activate",
			GTK_SIGNAL_FUNC(change_image_format), (gpointer)box);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		k++;
  	}
	gtk_widget_show_all(menu);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(opt), menu);

	gtk_option_menu_set_history(GTK_OPTION_MENU(opt), j);

	gtk_signal_emit_by_name(GTK_OBJECT(g_list_nth_data(
		GTK_MENU_SHELL(menu)->children, j)), "activate", (gpointer)box);
}

static void ftype_widgets(GtkWidget *box, char *name, int mode)
{
	GtkWidget *opt, *menu, *item, *label;
	int i, j, k, mask;
	char *ext = strrchr(name, '.');

	mask = mode == FS_PALETTE_SAVE ? FF_PALETTE : FF_BW | FF_IDX | FF_RGB;
	ext = ext ? ext + 1 : "";

	label = gtk_label_new(_("File Format"));
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 10);
	opt = gtk_option_menu_new();
	gtk_box_pack_start(GTK_BOX(box), opt, FALSE, FALSE, 10);

	menu = gtk_menu_new();
	for (i = j = k = 0; i < NUM_FTYPES; i++)
	{
		if (!(file_formats[i].flags & mask)) continue;
		if (!strncasecmp(ext, file_formats[i].ext, LONGEST_EXT) ||
			(file_formats[i].ext2[0] &&
			!strncasecmp(ext, file_formats[i].ext2, LONGEST_EXT)))
			j = k;
		item = gtk_menu_item_new_with_label(file_formats[i].name);
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)i);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		k++;
  	}
	gtk_option_menu_set_menu(GTK_OPTION_MENU(opt), menu);
	gtk_option_menu_set_history(GTK_OPTION_MENU(opt), j);

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
		label = gtk_label_new(_("Animation delay"));
		gtk_widget_show(label);
		gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
		label = add_a_spin(preserved_gif_delay, 1, MAX_DELAY);
		gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 10);
		break;
	case FS_EXPORT_UNDO:
	case FS_EXPORT_UNDO2:
	case FS_PALETTE_SAVE:
		ftype_widgets(box, name, mode);
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
	settings->jpeg_quality = mem_jpeg_quality;
	settings->hot_x = mem_xbm_hot_x;
	settings->hot_y = mem_xbm_hot_y;
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
			settings->hot_x = read_spin(BOX_CHILD(box, 7));
			settings->hot_y = read_spin(BOX_CHILD(box, 9));
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
		if (fflags & FF_COMPR)
		{
			mem_jpeg_quality = settings->jpeg_quality;
			inifile_set_gint32("jpegQuality", mem_jpeg_quality);
		}
		if (fflags & FF_SPOT)
		{
			mem_xbm_hot_x = settings->hot_x;
			mem_xbm_hot_y = settings->hot_y;
		}
		break;
	case FS_EXPORT_GIF:
		preserved_gif_delay = settings->gif_delay;
		break;
	}
}

static gboolean fs_destroy(GtkWidget *fs)
{
	int x, y, width, height;

	gdk_window_get_size(fs->window, &width, &height);
	gdk_window_get_root_origin(fs->window, &x, &y);

	inifile_set_gint32("fs_window_x", x);
	inifile_set_gint32("fs_window_y", y);
	inifile_set_gint32("fs_window_w", width);
	inifile_set_gint32("fs_window_h", height);

	gtk_window_set_transient_for(GTK_WINDOW(fs), NULL);
	gtk_widget_destroy(fs);

	return FALSE;
}

static gint fs_ok(GtkWidget *fs)
{
	ls_settings settings;
	GtkWidget *xtra;
	char fname[256], mess[512], gif_nam[256], gif_nam2[320], *c, *ext, *ext2;
	int i, j;

	/* Pick up extra info */
	xtra = GTK_WIDGET(gtk_object_get_user_data(GTK_OBJECT(fs)));
	init_ls_settings(&settings, xtra);

	/* Needed to show progress in Windows GTK+2 */
	gtk_window_set_modal(GTK_WINDOW(fs), FALSE);

	/* Better aesthetics? */
	gtk_widget_hide(fs);

	/* File extension */
	strncpy(fname, gtk_entry_get_text(GTK_ENTRY(
		GTK_FILE_SELECTION(fs)->selection_entry)), 256);
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
				/* Truncate */
				*c = '\0';
			}
			i = strlen(fname);
			j = strlen(ext);
			if (i + j + 1 > 250) break; /* Too long */
			fname[i] = '.';
			strncpy(fname + i + 1, ext, j + 1);
		}
		gtk_entry_set_text(GTK_ENTRY(
			GTK_FILE_SELECTION(fs)->selection_entry), fname);
		break;
	}

	/* Get filename the proper way */
	gtkncpy(fname, gtk_file_selection_get_filename(GTK_FILE_SELECTION(fs)), 250);

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
		set_new_filename(fname);
		update_image_bar();	// Update transparency info
		update_all_views();	// Redraw in case transparency changed
		break;
	case FS_PALETTE_LOAD:
		if (load_pal(fname))
		{
			snprintf(mess, 500, _("File: %s invalid - palette not updated"), fname);
			alert_box( _("Error"), mess, _("OK"), NULL, NULL );
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
		if (clipboard_entry)
			gtk_entry_set_text(GTK_ENTRY(clipboard_entry), fname);
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
		if ( c == NULL ) c = preserved_gif_filename;
		else c++;
		snprintf(gif_nam, 250, "%s%c%s", fname, DIR_SEP, c);
		snprintf(mess, 500,
			"gifsicle -U --explode \"%s\" -o \"%s\"",
			preserved_gif_filename, gif_nam );
//printf("%s\n", mess);
		gifsicle(mess);
		strncat( gif_nam, ".???", 250 );
		wild_space_change( gif_nam, gif_nam2, 315 );
		snprintf(mess, 500,
			"mtpaint -g %i %s &",
			preserved_gif_delay, gif_nam2 );
//printf("%s\n", mess);
		gifsicle(mess);
		break;
	case FS_EXPORT_GIF:
		if (check_file(fname)) goto redo;
		store_ls_settings(&settings);	// Update data in memory
		snprintf(gif_nam, 250, "%s", mem_filename);
		wild_space_change( gif_nam, gif_nam2, 315 );
		for (i = strlen(gif_nam2) - 1; (i >= 0) && (gif_nam2[i] != DIR_SEP); i--)
		{
			if ((unsigned char)(gif_nam2[i] - '0') <= 9) gif_nam2[i] = '?';
		}
						
		snprintf(mess, 500, "%s -d %i %s -o \"%s\"",
			GIFSICLE_CREATE, settings.gif_delay, gif_nam2, fname);
//printf("%s\n", mess);
		gifsicle(mess);

#ifndef WIN32
		snprintf(mess, 500, "gifview -a \"%s\" &", fname );
		gifsicle(mess);
//printf("%s\n", mess);
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

	fs_destroy(fs);

	return FALSE;
redo_name:
	snprintf(mess, 500, _("Unable to save file: %s"), fname);
	alert_box( _("Error"), mess, _("OK"), NULL, NULL );
redo:
	gtk_widget_show(fs);
	gtk_window_set_modal(GTK_WINDOW(fs), TRUE);
	return FALSE;
}

void file_selector(int action_type)
{
	char *title = NULL, txt[300], txt2[300];
	GtkWidget *fs, *xtra;


	switch (action_type)
	{
	case FS_PNG_LOAD:
		title = _("Load Image File");
		if (layers_total == 0)
		{
			if (check_for_changes() == 1) return;
		}
		else if (check_layers_for_changes() == 1) return;
		break;
	case FS_PNG_SAVE:
		title = _("Save Image File");
		break;
	case FS_PALETTE_LOAD:
		title = _("Load Palette File");
		break;
	case FS_PALETTE_SAVE:
		title = _("Save Palette File");
		break;
	case FS_CLIP_FILE:
		title = _("Select Clipboard File");
		break;
	case FS_EXPORT_UNDO:
		title = _("Export Undo Images");
		break;
	case FS_EXPORT_UNDO2:
		title = _("Export Undo Images (reversed)");
		break;
	case FS_EXPORT_ASCII:
		title = _("Export ASCII Art");
		break;
	case FS_LAYER_SAVE:
		title = _("Save Layer Files");
		break;
	case FS_GIF_EXPLODE:
		title = _("Import GIF animation - Choose frames directory");
		break;
	case FS_EXPORT_GIF:
		title = _("Export GIF animation");
		break;
	case FS_CHANNEL_LOAD:
		title = _("Load Channel");
		break;
	case FS_CHANNEL_SAVE:
		title = _("Save Channel");
		break;
	case FS_COMPOSITE_SAVE:
		title = _("Save Composite Image");
		break;
	}

	fs = gtk_file_selection_new(title);

	gtk_window_set_modal(GTK_WINDOW(fs), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(fs),
		inifile_get_gint32("fs_window_w", 550),
		inifile_get_gint32("fs_window_h", 500));
	gtk_widget_set_uposition(fs,
		inifile_get_gint32("fs_window_x", 0),
		inifile_get_gint32("fs_window_y", 0));

	if (action_type ==  FS_GIF_EXPLODE)
		gtk_widget_hide(GTK_WIDGET(GTK_FILE_SELECTION(fs)->selection_entry));

	gtk_signal_connect_object(GTK_OBJECT(GTK_FILE_SELECTION(fs)->ok_button),
		"clicked", GTK_SIGNAL_FUNC(fs_ok), GTK_OBJECT(fs));

	gtk_signal_connect_object(GTK_OBJECT(GTK_FILE_SELECTION(fs)->cancel_button),
		"clicked", GTK_SIGNAL_FUNC(fs_destroy), GTK_OBJECT(fs));

	gtk_signal_connect_object(GTK_OBJECT(fs),
		"delete_event", GTK_SIGNAL_FUNC(fs_destroy), GTK_OBJECT(fs));

	if ((action_type == FS_PNG_SAVE) && strcmp(mem_filename, _("Untitled")))
		strncpy( txt, mem_filename, 256 );	// If we have a filename and saving
	else if ((action_type == FS_LAYER_SAVE) &&
		strcmp(layers_filename, _("Untitled")))
		strncpy(txt, layers_filename, 256);
	else if (action_type == FS_LAYER_SAVE)
	{
		snprintf(txt, 256, "%s%clayers.txt",
			inifile_get("last_dir", get_home_directory()),
			DIR_SEP );
	}
	else
	{
		snprintf(txt, 256, "%s%c",
			inifile_get("last_dir", get_home_directory()),
			DIR_SEP );		// Default
	}

#if GTK_MAJOR_VERSION == 2
	cleanse_txt( txt2, txt );		// Clean up non ASCII chars
#else
	strcpy( txt2, txt );
#endif
	gtk_file_selection_set_filename(GTK_FILE_SELECTION(fs), txt2);

	xtra = ls_settings_box(txt2, action_type);
	gtk_box_pack_start(GTK_BOX(GTK_FILE_SELECTION(fs)->main_vbox), xtra,
		FALSE, TRUE, 0);
	gtk_object_set_user_data(GTK_OBJECT(fs), xtra);

	gtk_widget_show(fs);
	gtk_window_set_transient_for(GTK_WINDOW(fs), GTK_WINDOW(main_window));
	gdk_window_raise(fs->window);	// Needed to ensure window is at the top
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

	update_image_bar();
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


void stretch_poly_line(int x, int y)			// Clear old temp line, draw next temp line
{
	if ( poly_points>0 && poly_points<MAX_POLY )
	{
		if ( line_x1 != x || line_y1 != y )	// This check reduces flicker
		{
			repaint_line(0);
			paint_poly_marquee();

			line_x1 = x;
			line_y1 = y;
			line_x2 = poly_mem[poly_points-1][0];
			line_y2 = poly_mem[poly_points-1][1];

			repaint_line(2);
		}
	}
}

static void poly_conclude()
{
	repaint_line(0);
	if ( poly_points < 3 )
	{
		poly_status = POLY_NONE;
		poly_points = 0;
		update_all_views();
		update_sel_bar();
	}
	else
	{
		poly_status = POLY_DONE;
		poly_init();			// Set up polgon stats
		marq_x1 = poly_min_x;
		marq_y1 = poly_min_y;
		marq_x2 = poly_max_x;
		marq_y2 = poly_max_y;
		update_menus();			// Update menu/icons

		paint_poly_marquee();
		update_sel_bar();
	}
}

static void poly_add_po( int x, int y )
{
	repaint_line(0);
	poly_add(x, y);
	if ( poly_points >= MAX_POLY ) poly_conclude();
	paint_poly_marquee();
	update_sel_bar();
}

void tool_action(int event, int x, int y, int button, gdouble pressure)
{
	int minx = -1, miny = -1, xw = -1, yh = -1;
	int i, j, k, rx, ry, sx, sy, ts2, tr2, res;
	int ox, oy, off1, off2, o_size = tool_size, o_flow = tool_flow, o_opac = tool_opacity, n_vs[3];
	int px, py, oox, ooy;	// Continuous smudge stuff
	gboolean first_point = FALSE;

	if ( tool_type <= TOOL_SHUFFLE ) tint_mode[2] = button;

	if ( pen_down == 0 )
	{
		first_point = TRUE;
		if ((button == 3) && (tool_type <= TOOL_SPRAY) && !tint_mode[0])
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
	if (tool_type == TOOL_FLOOD)
	{
		/* Left click and non-masked start point */
		if ((button == 1) && (pixel_protected(x, y) < 255))
		{
			j = get_pixel(x, y);
			k = mem_channel != CHN_IMAGE ? channel_col_A[mem_channel] :
				mem_img_bpp == 1 ? mem_col_A : PNG_2_INT(mem_col_A24);
			if (j != k) /* And never start on colour A */
			{
				spot_undo(UNDO_DRAW);
				flood_fill(x, y, j);
				update_all_views();
			}
		}
	}
	else if (tool_type == TOOL_LINE)
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
			ox = marq_x1;
			oy = marq_y1;
			paint_marquee(0, x - marq_drag_x, y - marq_drag_y);
			marq_x1 = x - marq_drag_x;
			marq_y1 = y - marq_drag_y;
			marq_x2 = marq_x1 + mem_clip_w - 1;
			marq_y2 = marq_y1 + mem_clip_h - 1;
			paint_marquee(1, ox, oy);
		}
		if ( (marq_status == MARQUEE_PASTE_DRAG || marq_status == MARQUEE_PASTE ) &&
			(((button == 3) && (event == GDK_BUTTON_PRESS)) ||
			((button == 13) && (event == GDK_MOTION_NOTIFY))))
		{	// User wants to commit the paste
			commit_paste(TRUE);
		}
		if ( tool_type == TOOL_SELECT && button == 3 && (marq_status == MARQUEE_DONE ) )
		{
			pressed_select_none(NULL, NULL);
			set_cursor();
		}
		if ( tool_type == TOOL_SELECT && button == 1 && (marq_status == MARQUEE_NONE ||
			marq_status == MARQUEE_DONE) )		// Starting a selection
		{
			if ( marq_status == MARQUEE_DONE )
			{
				paint_marquee(0, marq_x1-mem_width, marq_y1-mem_height);
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
			paint_marquee(1, marq_x1-mem_width, marq_y1-mem_height);
		}
		else
		{
			if ( marq_status == MARQUEE_SELECTING )		// Continuing to make a selection
			{
				paint_marquee(0, marq_x1-mem_width, marq_y1-mem_height);
				marq_x2 = x;
				marq_y2 = y;
				paint_marquee(1, marq_x1-mem_width, marq_y1-mem_height);
			}
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
		if ((button == 1) || ((button == 3) && (tool_type <= TOOL_SPRAY)))
		{
			mem_undo_next(UNDO_TOOL);	// Do memory stuff for undo
			res = 0; 
		}
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
		minx = x - ts2;
		miny = y - ts2;
		xw = tool_size;
		yh = tool_size;

		switch (tool_type)
		{
		case TOOL_SQUARE:
			f_rectangle(minx, miny, xw, yh);
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
			if (!first_point) mem_smudge(tool_ox, tool_oy, x, y);
			break;
		case TOOL_CLONE:
			if (first_point || (tool_ox != x) || (tool_oy != y))
				mem_clone(x + clone_x, y + clone_y, x, y);
			break;
		default: xw = yh = -1; /* Nothing was done */
			break;
		}
	}

	if ((xw >= 0) && (yh >= 0)) /* Some drawing action */
	{
		if (xw + minx > mem_width) xw = mem_width - minx;
		if (yh + miny > mem_height) yh = mem_height - miny;
		if (minx < 0) xw += minx , minx = 0;
		if (miny < 0) yh += miny , miny = 0;

		if ((xw >= 0) && (yh >= 0))
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

static int vc_x1, vc_y1, vc_x2, vc_y2;	// Visible canvas

static void get_visible()
{
	GtkAdjustment *hori, *vert;

	hori = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );
	vert = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );

	vc_x1 = hori->value;
	vc_y1 = vert->value;
	vc_x2 = hori->value + hori->page_size - 1;
	vc_y2 = vert->value + vert->page_size - 1;
}

/* Clip area to visible canvas */
static void clip_area( int *rx, int *ry, int *rw, int *rh )
{
// !!! This assumes that if there's clipping, then there aren't margins
	if ( *rx<vc_x1 )
	{
		*rw = *rw + (*rx - vc_x1);
		*rx = vc_x1;
	}
	if ( *ry<vc_y1 )
	{
		*rh = *rh + (*ry - vc_y1);
		*ry = vc_y1;
	}
	if ( *rx + *rw > vc_x2 ) *rw = vc_x2 - *rx + 1;
	if ( *ry + *rh > vc_y2 ) *rh = vc_y2 - *ry + 1;
}

void update_paste_chunk( int x1, int y1, int x2, int y2 )
{
	int ux1, uy1, ux2, uy2, w, h;

	get_visible();
	canvas_size(&w, &h);

	ux1 = x1 > vc_x1 ? x1 : vc_x1;
	uy1 = y1 > vc_y1 ? y1 : vc_y1;
	ux2 = x2 < vc_x2 ? x2 : vc_x2;
	uy2 = y2 < vc_y2 ? y2 : vc_y2;
	if (ux2 >= w) ux2 = w - 1;
	if (uy2 >= h) uy2 = h - 1;

	/* Only repaint if on visible canvas */
	if ((ux1 <= ux2) && (uy1 <= uy2))
		repaint_paste(ux1, uy1, ux2, uy2);
}

void paint_poly_marquee()			// Paint polygon marquee
{
	int i, last = poly_points;
	GdkPoint xy[MAX_POLY + 1];


	check_marquee();
	if ((tool_type != TOOL_POLYGON) || (poly_points < 2)) return;

	for (i = 0; i < last; i++)
	{
		xy[i].x = margin_main_x + rint((poly_mem[i][0] + 0.5) * can_zoom);
		xy[i].y = margin_main_y + rint((poly_mem[i][1] + 0.5) * can_zoom);
	}
	/* Join 1st & last point if finished */
	if (poly_status == POLY_DONE) xy[last++] = xy[0];

	gdk_draw_lines( drawing_canvas->window, dash_gc, xy, last );
}


void paint_marquee(int action, int new_x, int new_y)
{
	unsigned char *rgb;
	int x1, y1, x2, y2, w, h, new_x2, new_y2, mst, zoom = 1, scale = 1;
	int i, j, r, g, b, rx, ry, rw, rh, offx, offy;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	new_x2 = new_x + marq_x2 - marq_x1;
	new_y2 = new_y + marq_y2 - marq_y1;

	/* Get onscreen coords */
	check_marquee();
	x1 = (marq_x1 * scale) / zoom;
	y1 = (marq_y1 * scale) / zoom;
	x2 = (marq_x2 * scale) / zoom;
	y2 = (marq_y2 * scale) / zoom;
	w = abs(x2 - x1) + scale;
	h = abs(y2 - y1) + scale;
	if (x2 < x1) x1 = x2;
	if (y2 < y1) y1 = y2;

	get_visible();

	if (action == 0) /* Clear */
	{
		mst = marq_status;
		marq_status = 0;
		/* Redraw inner area if displaying the clipboard */
		if (show_paste && (mst >= MARQUEE_PASTE))
		{
			/* Do nothing if not moved anywhere */
			if ((new_x == marq_x1) && (new_y == marq_y1));
			/* Full redraw if no intersection */
			else if ((new_x2 < marq_x1) || (new_x > marq_x2) ||
				(new_y2 < marq_y1) || (new_y > marq_y2))
				repaint_canvas(margin_main_x + x1,
					margin_main_y + y1, w, h);
			/* Partial redraw */
			else
			{
				if (new_x != marq_x1) /* Horizontal shift */
				{
					ry = y1; rh = h;
					if (new_x < marq_x1) /* Move left */
					{
						rx = (new_x2 * scale) / zoom + scale;
						rw = x1 + w - rx;
					}
					else /* Move right */
					{
						rx = x1;
						rw = (new_x * scale) / zoom - x1;
					}
					clip_area(&rx, &ry, &rw, &rh);
					repaint_canvas(margin_main_x + rx,
						margin_main_y + ry, rw, rh);
				}
				if (new_y != marq_y1) /* Vertical shift */
				{
					rx = x1; rw = w;
					if (new_y < marq_y1) /* Move up */
					{
						ry = (new_y2 * scale) / zoom + scale;
						rh = y1 + h - ry;
					}
					else /* Move down */
					{
						ry = y1;
						rh = (new_y * scale) / zoom - y1;
					}
					clip_area(&rx, &ry, &rw, &rh);
					repaint_canvas(margin_main_x + rx,
						margin_main_y + ry, rw, rh);
				}
			}
		}
		/* Redraw only borders themselves */
		else
		{
			repaint_canvas(margin_main_x + x1,
				margin_main_y + y1 + 1, 1, h - 2);
			repaint_canvas(margin_main_x + x1 + w - 1,
				margin_main_y + y1 + 1, 1, h - 2);
			repaint_canvas(margin_main_x + x1,
				margin_main_y + y1, w, 1);
			repaint_canvas(margin_main_x + x1,
				margin_main_y + y1 + h - 1, w, 1);
		}
		marq_status = mst;
	}

	/* Draw */
	else if ((action == 1) || (action == 11))
	{
		r = 255; g = b = 0; /* Draw in red */
		if (marq_status >= MARQUEE_PASTE)
		{
			/* Display paste RGB, only if not being called from repaint_canvas */
			/* Only do something if there is a change in position */
			if (show_paste && (action == 1) &&
				((new_x != marq_x1) || (new_y != marq_y1)))
				update_paste_chunk(marq_x1 < 0 ? 0 : x1 + 1,
					marq_y1 < 0 ? 0 : y1 + 1,
					x1 + w - 2, y1 + h - 2);
			r = g = 0; b = 255; /* Draw in blue */
		}

		/* Determine visible area */
		rx = x1; ry = y1; rw = w; rh = h;
		clip_area(&rx, &ry, &rw, &rh);
		if ((rw < 1) || (rh < 1)) return;

		offx = (abs(rx - x1) % 6) * 3;
		offy = (abs(ry - y1) % 6) * 3;

		/* Create pattern */
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

		i = ((mem_width + zoom - 1) * scale) / zoom;
		j = ((mem_height + zoom - 1) * scale) / zoom;
		if (rx + rw > i) rw = i - rx;
		if (ry + rh > j) rh = j - ry;

		if ((x1 >= vc_x1) && (marq_x1 >= 0) && (marq_x2 >= 0))
			gdk_draw_rgb_image(drawing_canvas->window,
				drawing_canvas->style->black_gc,
				margin_main_x + rx, margin_main_y + ry,
				1, rh, GDK_RGB_DITHER_NONE, rgb + offy, 3);

		if ((x1 + w - 1 <= vc_x2) && (marq_x1 < mem_width) && (marq_x2 < mem_width))
			gdk_draw_rgb_image(drawing_canvas->window,
				drawing_canvas->style->black_gc,
				margin_main_x + rx + rw - 1, margin_main_y + ry,
				1, rh, GDK_RGB_DITHER_NONE, rgb + offy, 3);

		if ((y1 >= vc_y1) && (marq_y1 >= 0) && (marq_y2 >= 0))
			gdk_draw_rgb_image(drawing_canvas->window,
				drawing_canvas->style->black_gc,
				margin_main_x + rx, margin_main_y + ry,
				rw, 1, GDK_RGB_DITHER_NONE, rgb + offx, 0);

		if ((y1 + h - 1 <= vc_y2) && (marq_y1 < mem_height) && (marq_y2 < mem_height))
			gdk_draw_rgb_image(drawing_canvas->window,
				drawing_canvas->style->black_gc,
				margin_main_x + rx, margin_main_y + ry + rh - 1,
				rw, 1, GDK_RGB_DITHER_NONE, rgb + offx, 0);

		free(rgb);
	}
}


int close_to( int x1, int y1 )		// Which corner of selection is coordinate closest to?
{
	return ((x1 + x1 <= marq_x1 + marq_x2 ? 0 : 1) +
		(y1 + y1 <= marq_y1 + marq_y2 ? 0 : 2));
}

static int floor_div(int dd, int dr)
{
	return (dd < 0 ? -((dr - 1 - dd) / dr) : dd / dr);
}

#define MIN_REDRAW 16 /* Minimum dimension for redraw rectangle */
void trace_line(int mode, int lx1, int ly1, int lx2, int ly2,
	int vx1, int vy1, int vx2, int vy2)
{
	int j, x, y, tx, ty, aw, ah, ax = 0, ay = 0, cf = 0, zoom = 1, scale = 1;
	unsigned char rgb[MAX_ZOOM * 3], col[3];
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
	else scale = rint(can_zoom);

	line_init(line, lx1, ly1, lx2, ly2);
	for (; line[2] >= 0; line_step(line))
	{
		x = (tx = line[0]) * scale;
		y = (ty = line[1]) * scale;

		if ((x + scale > vx1) && (y + scale > vy1) &&
			(x <= vx2) && (y <= vy2))
		{
			if (mode != 0) /* Show a line */
			{
				if (mode == 1) /* Drawing */
				{
					j = ((ty & 7) * 8 + (tx & 7)) * 3;
					col[0] = mem_col_pat24[j + 0];
					col[1] = mem_col_pat24[j + 1];
					col[2] = mem_col_pat24[j + 2];
				}
				else if (mode == 2) /* Tracking */
				{
					col[0] = col[1] = col[2] =
						((line[2] >> 2) & 1) * 255;
				}
				else if (mode == 3) /* Gradient */
				{
					col[0] = col[1] = col[2] =
						((line[2] >> 2) & 1) * 255;
					col[1] ^= ((line[2] >> 1) & 1) * 255;
				}
				for (j = 0; j < scale * 3; j += 3)
				{
					rgb[j + 0] = col[0];
					rgb[j + 1] = col[1];
					rgb[j + 2] = col[2];
				}
				gdk_draw_rgb_image(drawing_canvas->window,
					drawing_canvas->style->black_gc,
					margin_main_x + x, margin_main_y + y,
					scale, scale, GDK_RGB_DITHER_NONE, rgb, 0);
				continue;
			}
			/* Doing a clear */
			if (!cf) ax = x , ay = y , cf = 1; // Start a new rectangle
		}
		/* Do nothing if no area to clear */
		else if ((mode != 0) || !cf) continue;

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
	get_visible();
	trace_line(mode, line_x1, line_y1, line_x2, line_y2,
// !!! This assumes that if there's clipping, then there aren't margins
		vc_x1, vc_y1, vc_x2, vc_y2);
}

void repaint_grad(int mode)
{
	grad_info *grad = gradient + mem_channel;
	int oldgrad = grad->status;

	if (mode) mode = 3;
	else grad->status = GRAD_NONE; /* To avoid hitting repaint */

	get_visible();
	trace_line(mode, grad->x2, grad->y2, grad->x1, grad->y1,
		vc_x1 - margin_main_x, vc_y1 - margin_main_y,
		vc_x2 - margin_main_x, vc_y2 - margin_main_y);
	grad->status = oldgrad;
}

void refresh_grad(int px, int py, int pw, int ph)
{
	grad_info *grad = gradient + mem_channel;

	pw += px - 1; ph += py - 1;
	get_visible();
	trace_line(3, grad->x2, grad->y2, grad->x1, grad->y1,
		(px > vc_x1 ? px : vc_x1) - margin_main_x,
		(py > vc_y1 ? py : vc_y1) - margin_main_y,
		(pw < vc_x2 ? pw : vc_x2) - margin_main_x,
		(ph < vc_y2 ? ph : vc_y2) - margin_main_y);
}

void men_item_visible( GtkWidget *menu_items[], gboolean state )
{	// Show or hide menu items
	int i = 0;
	while ( menu_items[i] != NULL )
	{
		if ( state )
			gtk_widget_show( menu_items[i] );
		else
			gtk_widget_hide( menu_items[i] );
		i++;
	}
}

void update_recent_files()			// Update the menu items
{
	char txt[64], *t, txt2[600];
	int i, count;

	if ( recent_files == 0 ) men_item_visible( menu_recent, FALSE );
	else
	{
		for ( i=0; i<=MAX_RECENT; i++ )			// Show or hide items
		{
			if ( i <= recent_files )
				gtk_widget_show( menu_recent[i] );
			else
				gtk_widget_hide( menu_recent[i] );
		}
		count = 0;
		for ( i=1; i<=recent_files; i++ )		// Display recent filenames
		{
			sprintf( txt, "file%i", i );

			t = inifile_get( txt, "." );
			if ( strlen(t) < 2 )
				gtk_widget_hide( menu_recent[i] );	// Hide if empty
			else
			{
#if GTK_MAJOR_VERSION == 2
				cleanse_txt( txt2, t );		// Clean up non ASCII chars
#else
				strcpy( txt2, t );
#endif
				gtk_label_set_text( GTK_LABEL( GTK_MENU_ITEM(
					menu_recent[i] )->item.bin.child ) , txt2 );
				count++;
			}
		}
		if ( count == 0 ) gtk_widget_hide( menu_recent[0] );	// Hide separator if not needed
	}
}

void register_file( char *filename )		// Called after successful load/save
{
	char txt[280], *c;
	int i, f;

	c = strrchr( filename, DIR_SEP );
	if (c)
	{
		txt[0] = *c;
		*c = '\0';		// Strip off filename
		inifile_set("last_dir", filename);
		*c = txt[0];
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
			sprintf( txt+100, "file%i", i );
			inifile_set( txt+100,
				inifile_get( txt, "" )
				);

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
		nt = inifile_get_gint32("lastnewType", 2 ),
		bpp = 1;

	if ( nt == 0 || nt>2 ) bpp = 3;
	do_new_one( nw, nh, nc, nt, bpp );
}
