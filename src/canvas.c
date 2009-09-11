/*	canvas.c
	Copyright (C) 2004-2006 Mark Tyler

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
#include "mainwindow.h"
#include "otherwindow.h"
#include "mygtk.h"
#include "inifile.h"
#include "canvas.h"
#include "png.h"
#include "quantizer.h"
#include "viewer.h"
#include "layer.h"
#include "polygon.h"
#include "wu.h"
#include "prefs.h"
#include "ani.h"
#include "channels.h"
#include "toolbar.h"

GdkWindow *the_canvas = NULL;			// Pointer to the canvas we will be drawing on

GtkWidget *label_bar[STATUS_ITEMS];


float can_zoom = 1;				// Zoom factor 1..MAX_ZOOM
int margin_main_x=0, margin_main_y=0,		// Top left of image from top left of canvas
	margin_view_x=0, margin_view_y=0;
int zoom_flag = 0;
int fs_type = 0;
int perim_status = 0, perim_x = 0, perim_y = 0, perim_s = 2;		// Tool perimeter
int marq_status = MARQUEE_NONE,
	marq_x1 = 0, marq_y1 = 0, marq_x2 = 0, marq_y2 = 0;		// Selection marquee
int marq_drag_x = 0, marq_drag_y = 0;					// Marquee dragging offset
int line_status = LINE_NONE,
	line_x1 = 0, line_y1 = 0, line_x2 = 0, line_y2 = 0;		// Line tool
int poly_status = POLY_NONE;						// Polygon selection tool
int clone_x, clone_y;							// Clone offsets

int recent_files;					// Current recent files setting

gboolean show_paste,					// Show contents of clipboard while pasting
	col_reverse = FALSE,				// Painting with right button
	text_paste = FALSE,				// Are we pasting text?
	canvas_image_centre = TRUE,			// Are we centering the image?
	fs_do_gif_explode = FALSE,
	chequers_optimize = TRUE			// Are we optimizing the chequers for speed?
	;

void commit_paste( gboolean undo )
{
	int fx, fy, fw, fh, fx2, fy2;		// Screen coords
	int mx = 0, my = 0;			// Mem coords
	int i, ofs;
	unsigned char *image, *mask, *alpha = NULL;

	if ( marq_x1 < 0 ) mx = -marq_x1;
	if ( marq_y1 < 0 ) my = -marq_y1;

	mtMAX( fx, marq_x1, 0 )
	mtMAX( fy, marq_y1, 0 )
	mtMIN( fx2, marq_x2, mem_width-1 )
	mtMIN( fy2, marq_y2, mem_height-1 )

	fw = fx2 - fx + 1;
	fh = fy2 - fy + 1;

	mask = malloc(fw);
	if (!mask) return;	/* !!! Not enough memory */
	if ((mem_channel == CHN_IMAGE) && RGBA_mode && !mem_clip_alpha && mem_img[CHN_ALPHA])
	{
		alpha = malloc(fw);
		if (!alpha) return;
		memset(alpha, channel_col_A[CHN_ALPHA], fw);
	}

	if ( undo ) mem_undo_next(UNDO_DRAW);	// Do memory stuff for undo
	update_menus();				// Update menu undo issues

	ofs = my * mem_clip_w + mx;
	image = mem_clipboard + ofs * mem_clip_bpp;

	for (i = 0; i < fh; i++)
	{
		row_protected(fx, fy + i, fw, mask);
		paste_pixels(fx, fy + i, fw, mask, image, mem_clip_alpha ?
			mem_clip_alpha + ofs : alpha, mem_clip_mask ?
			mem_clip_mask + ofs : NULL, tool_opacity);
		image += mem_clip_w * mem_clip_bpp;
		ofs += mem_clip_w;
	}

	free(mask);
	free(alpha);

	vw_update_area( fx, fy, fw, fh );
	gtk_widget_queue_draw_area( drawing_canvas,
			fx*can_zoom + margin_main_x, fy*can_zoom + margin_main_y,
			fw*can_zoom + 1, fh*can_zoom + 1);
}

void paste_prepare()
{
	poly_status = POLY_NONE;
	poly_points = 0;
	if ( tool_type != TOOL_SELECT && tool_type != TOOL_POLYGON )
	{
		perim_status = 0;
		clear_perim();
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
	}
	else
	{
		if ( marq_status != MARQUEE_NONE ) paint_marquee(0, marq_x1, marq_y1);
	}
}

void iso_trans( GtkMenuItem *menu_item, gpointer user_data )
{
	int i, j = 0;

	for ( i=0; i<4; i++ ) if ( menu_iso[i] == GTK_WIDGET(menu_item) ) j=i;

	i = mem_isometrics(j);

	if ( i==0 ) canvas_undo_chores();
	else
	{
		if ( i==-666 ) alert_box( _("Error"), _("The image is too large to transform."),
					_("OK"), NULL, NULL );
		else memory_errors(i);
	}
}

void create_pal_quantized(int dl)
{
	int i = 0;
	unsigned char newpal[3][256];

	pen_down = 0;
	mem_undo_next(UNDO_PAL);
	pen_down = 0;

	if ( dl==1 )
		i = dl1quant(mem_img[CHN_IMAGE], mem_width, mem_height, mem_cols, newpal);
	if ( dl==3 )
		i = dl3quant(mem_img[CHN_IMAGE], mem_width, mem_height, mem_cols, newpal);
	if ( dl==5 )
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

void pressed_create_dl1( GtkMenuItem *menu_item, gpointer user_data )
{	create_pal_quantized(1);	}

void pressed_create_dl3( GtkMenuItem *menu_item, gpointer user_data )
{	create_pal_quantized(3);	}

void pressed_create_wu( GtkMenuItem *menu_item, gpointer user_data )
{	create_pal_quantized(5);	}

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

int do_blur(GtkWidget *spin, gpointer fdata)
{
	int i, j;

	gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
	i = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
	spot_undo(UNDO_FILT);
	progress_init(_("Image Blur Effect"), 1);
	for (j = 0; j < i; j++)
	{
		if (progress_update(((float)j) / i)) break;
		do_effect(1, 25 + i / 2);
	}
	progress_end();

	return TRUE;
}

void pressed_blur( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *spin = add_a_spin(10, 1, 100);
	filter_window(_("Blur Effect"), spin, do_blur, NULL);
}

int do_fx(GtkWidget *spin, gpointer fdata)
{
	gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
	int i = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
	spot_undo(UNDO_FILT);
	do_effect((int)fdata, i);

	return TRUE;
}

void pressed_sharpen( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *spin = add_a_spin(50, 1, 100);
	filter_window(_("Edge Sharpen"), spin, do_fx, (gpointer)(3));
}

void pressed_soften( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *spin = add_a_spin(50, 1, 100);
	filter_window(_("Edge Soften"), spin, do_fx, (gpointer)(4));
}

void pressed_emboss( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_FILT);
	do_effect(2, 0);
	update_all_views();
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

void pressed_greyscale( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_COL);

	mem_greyscale();

	init_pal();
	update_all_views();
	gtk_widget_queue_draw( drawing_col_prev );
}

void rot_im(int dir)
{
	if ( mem_image_rot(dir) == 0 )
	{
		check_marquee();
		canvas_undo_chores();
	}
	else alert_box( _("Error"), _("Not enough memory to rotate image"), _("OK"), NULL, NULL );
}

void pressed_rotate_image_clock( GtkMenuItem *menu_item, gpointer user_data )
{	rot_im(0);	}

void pressed_rotate_image_anti( GtkMenuItem *menu_item, gpointer user_data )
{	rot_im(1);	}

void rot_sel(int dir)
{
	if ( mem_sel_rot(dir) == 0 )
	{
		check_marquee();
		gtk_widget_queue_draw( drawing_canvas );
	}
	else	alert_box( _("Error"), _("Not enough memory to rotate clipboard"), _("OK"), NULL, NULL );
}

void pressed_rotate_sel_clock( GtkMenuItem *menu_item, gpointer user_data )
{	rot_sel(0);	}

void pressed_rotate_sel_anti( GtkMenuItem *menu_item, gpointer user_data )
{	rot_sel(1);	}

int do_rotate_free(GtkWidget *box, gpointer fdata)
{
	GtkWidget *spin = ((GtkBoxChild*)GTK_BOX(box)->children->data)->widget;
	int j, smooth = 0;
	double angle;

	gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
	angle = gtk_spin_button_get_value_as_float(GTK_SPIN_BUTTON(spin));

	if (mem_img_bpp == 3)
	{
		GtkWidget *check = ((GtkBoxChild*)GTK_BOX(box)->children->next->data)->widget;
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)))
			smooth = 1;
	}
	j = mem_rotate_free(angle, smooth);
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
	if (mem_img_bpp == 3) add_a_toggle(_("Smooth"), box, TRUE);
	filter_window(_("Free Rotate"), box, do_rotate_free, NULL);
}


void mask_ab(int v)
{
	int i;

	if ( mem_clip_mask == NULL )
	{
		i = mem_clip_mask_init(v);
		if ( i != 0 )
		{
			memory_errors(1);	// Not enough memory
			return;
		}
	}
	mem_clip_mask_set(255-v);
	gtk_widget_queue_draw( drawing_canvas );
}

void pressed_clip_unmask()
{	mask_ab(255);	}

void pressed_clip_mask()
{	mask_ab(0);	}

void pressed_clip_alphamask()
{
	int i, j = mem_clip_w * mem_clip_h;

	if (!mem_clipboard || !mem_clip_alpha) return;

	if ( !mem_clip_mask )		// Create clipboard mask if not already there
	{
		mem_clip_mask = malloc( j*mem_clip_bpp );
		if ( !mem_clip_mask )
		{
			memory_errors(1);
			return;		// Bail out - not enough memory
		}
	}

	memcpy( mem_clip_mask, mem_clip_alpha, j * mem_clip_bpp );	// Copy alpha to mask

	for (i=0; i<j; i++ ) mem_clip_mask[i] = 255-mem_clip_mask[i];	// Flip values

	gtk_widget_queue_draw( drawing_canvas );
}

void pressed_clip_alpha_scale()
{
	if (!mem_clipboard || (mem_clip_bpp != 3)) return;
	if (!mem_clip_mask) mem_clip_mask_init(0);
	if (!mem_clip_mask) return;

	if (mem_scale_alpha(mem_clipboard, mem_clip_mask,
		mem_clip_w, mem_clip_h, TRUE, 255)) return;

	gtk_widget_queue_draw( drawing_canvas );
}

void pressed_clip_mask_all()
{
	int i;

	i = mem_clip_mask_init(255);
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
	int canz = can_zoom;
	GtkAdjustment *hori, *vert;

	if ( canz<1 ) canz = 1;

	hori = gtk_scrolled_window_get_hadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );
	vert = gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );

	if ( hori->page_size > mem_width*can_zoom ) mem_icx = 0.5;
	else mem_icx = ( hori->value + hori->page_size/2 ) / (mem_width*can_zoom);

	if ( vert->page_size > mem_height*can_zoom ) mem_icy = 0.5;
	else mem_icy = ( vert->value + vert->page_size/2 ) / (mem_height*can_zoom);

	paste_prepare();
	align_size( can_zoom );
	marq_x1 = mem_width * mem_icx - mem_clip_w/2;
	marq_y1 = mem_height * mem_icy - mem_clip_h/2;
	marq_x2 = marq_x1 + mem_clip_w - 1;
	marq_y2 = marq_y1 + mem_clip_h - 1;
	paste_init();
}

void do_the_copy(int op)
{
	int x1 = marq_x1, y1 = marq_y1;
	int x2 = marq_x2, y2 = marq_y2;
	int x, y, w, h, bpp, ofs, delta, len;
	int i, j;
	unsigned char *src, *dest;

	mtMIN( x, x1, x2 )
	mtMIN( y, y1, y2 )

	w = x1 - x2;
	h = y1 - y2;

	if ( w < 0 ) w = -w;
	if ( h < 0 ) h = -h;

	w++; h++;

	if ( op == 1 )		// COPY
	{
		bpp = MEM_BPP;
		if (mem_clipboard) free(mem_clipboard);		// Lose old clipboard
		if (mem_clip_alpha) free(mem_clip_alpha);	// Lose old clipboard alpha
		mem_clip_mask_clear();				// Lose old clipboard mask
		mem_clip_alpha = NULL;
		if (mem_channel == CHN_IMAGE)
		{
			if (mem_img[CHN_ALPHA]) mem_clip_alpha = malloc(w * h);
			if (mem_img[CHN_SEL]) mem_clip_mask = malloc(w * h);
		}
		mem_clipboard = malloc(w * h * bpp);
		text_paste = FALSE;
	
		if (!mem_clipboard)
		{
			if (mem_clip_alpha) free(mem_clip_alpha);
			mem_clip_mask_clear();
			alert_box( _("Error"), _("Not enough memory to create clipboard"),
					_("OK"), NULL, NULL );
		}
		else
		{
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
				memcpy(mem_clipboard + delta,
					mem_img[mem_channel] + ofs, len);
				ofs += mem_width * bpp;
				delta += len;
			}

			/* Utility channels */
			if (mem_clip_alpha)
			{
				ofs = y * mem_width + x;
				delta = 0;
				for (i = 0; i < h; i++)
				{
					memcpy(mem_clip_alpha + delta,
						mem_img[CHN_ALPHA] + ofs, w);
					ofs += mem_width;
					delta += w;
				}
			}

			/* Selection channel */
			if (mem_clip_mask)
			{
				src = mem_img[CHN_SEL] + y * mem_width + x;
				dest = mem_clip_mask;
				for (i = 0; i < h; i++)
				{
					for (j = 0; j < w; j++)
						*dest++ = 255 - *src++;
					src += mem_width - w;
				}
			}
		}
	}
	if ( op == 2 )		// CLEAR area
	{
		f_rectangle( x, y, w, h );
	}
	if ( op == 3 )		// Remember new coords for copy while pasting
	{
		mem_clip_x = x;
		mem_clip_y = y;
	}

	update_menus();
}

void pressed_outline_rectangle( GtkMenuItem *menu_item, gpointer user_data )
{
	int x, y, w, h, x2, y2;

	spot_undo(UNDO_DRAW);

	if ( tool_type == TOOL_POLYGON )
	{
		poly_outline();
	}
	else
	{
		mtMIN( x, marq_x1, marq_x2 )
		mtMIN( y, marq_y1, marq_y2 )
		mtMAX( x2, marq_x1, marq_x2 )
		mtMAX( y2, marq_y1, marq_y2 )
		w = abs(marq_x1 - marq_x2) + 1;
		h = abs(marq_y1 - marq_y2) + 1;

		if ( 2*tool_size >= w || 2*tool_size >= h )
			f_rectangle( x, y, w, h );
		else
		{
			f_rectangle( x, y, w, tool_size );				// TOP
			f_rectangle( x, y + tool_size, tool_size, h - 2*tool_size );	// LEFT
			f_rectangle( x, y2 - tool_size + 1, w, tool_size );		// BOTTOM
			f_rectangle( x2 - tool_size + 1,
				y + tool_size, tool_size, h - 2*tool_size );		// RIGHT
		}
	}

	update_all_views();
}

void pressed_fill_ellipse( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_DRAW);
	f_ellipse( marq_x1, marq_y1, marq_x2, marq_y2 );
	update_all_views();
}

void pressed_outline_ellipse( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_DRAW);
	o_ellipse( marq_x1, marq_y1, marq_x2, marq_y2, tool_size );
	update_all_views();
}

void pressed_fill_rectangle( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_DRAW);
	if ( tool_type == TOOL_SELECT ) do_the_copy(2);
	if ( tool_type == TOOL_POLYGON ) poly_paint();
	update_all_views();
}

void pressed_cut( GtkMenuItem *menu_item, gpointer user_data )
{			// Copy current selection to clipboard and then fill area with current pattern
	do_the_copy(1);
	spot_undo(UNDO_DRAW);
	if ( tool_type == TOOL_SELECT ) do_the_copy(2);
	if ( tool_type == TOOL_POLYGON )
	{
		poly_mask();
		poly_paint();
	}

	update_all_views();
}

void pressed_lasso( GtkMenuItem *menu_item, gpointer user_data )
{
	do_the_copy(1);
	if ( mem_clipboard == NULL ) return;		// No memory so bail out
	poly_mask();
	poly_lasso();
	pressed_paste_centre( NULL, NULL );
}

void pressed_lasso_cut( GtkMenuItem *menu_item, gpointer user_data )
{
	pressed_lasso( menu_item, user_data );
	if ( mem_clipboard == NULL ) return;		// No memory so bail out
	spot_undo(UNDO_DRAW);
	poly_lasso_cut();
}

void pressed_copy( GtkMenuItem *menu_item, gpointer user_data )
{			// Copy current selection to clipboard
	if ( tool_type == TOOL_POLYGON )
	{
		do_the_copy(1);
		poly_mask();
	}
	if ( tool_type == TOOL_SELECT )
	{
		if ( marq_status >= MARQUEE_PASTE ) do_the_copy(3);
		else do_the_copy(1);
	}
}

/* !!! Add support for channel-specific option sets !!! */
void update_menus()			// Update edit/undo menu
{
	char txt[32];

	sprintf(txt, "%i+%i", mem_undo_done, mem_undo_redo);

	if ( status_on[STATUS_UNDOREDO] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_UNDOREDO]), txt );

	if ( mem_img_bpp == 1 )
	{
		men_item_state( menu_only_indexed, TRUE );
		men_item_state( menu_only_24, FALSE );
	}
	if ( mem_img_bpp == 3 )
	{
		men_item_state( menu_only_indexed, FALSE );
		men_item_state( menu_only_24, TRUE );
	}

	if ( mem_img_bpp == 3 && mem_clipboard != NULL && mem_clip_bpp )
		men_item_state( menu_alphablend, TRUE );
	else	men_item_state( menu_alphablend, FALSE );

	if ( marq_status == MARQUEE_NONE )
	{
		men_item_state( menu_need_selection, FALSE );
		men_item_state( menu_crop, FALSE );
		if ( poly_status == POLY_DONE )
		{
			men_item_state( menu_lasso, TRUE );
			men_item_state( menu_need_marquee, TRUE );
		}
		else
		{
			men_item_state( menu_lasso, FALSE );
			men_item_state( menu_need_marquee, FALSE );
		}
	}
	else
	{
		if ( poly_status != POLY_DONE ) men_item_state( menu_lasso, FALSE );

		men_item_state( menu_need_marquee, TRUE );

		if ( marq_status >= MARQUEE_PASTE )	// If we are pasting disallow copy/cut/crop
		{
			men_item_state( menu_need_selection, FALSE );
			men_item_state( menu_crop, FALSE );
		}
		else	men_item_state( menu_need_selection, TRUE );

		if ( marq_status <= MARQUEE_DONE )
		{
			if ( (marq_x1 - marq_x2)*(marq_x1 - marq_x2) < (mem_width-1)*(mem_width-1) ||
				(marq_y1 - marq_y2)*(marq_y1 - marq_y2) < (mem_height-1)*(mem_height-1) )
					men_item_state( menu_crop, TRUE );
				// Only offer the crop option if the user hasn't selected everything
			else men_item_state( menu_crop, FALSE );
		}
		else men_item_state( menu_crop, FALSE );
	}

	if ( mem_clipboard == NULL ) men_item_state( menu_need_clipboard, FALSE );
	else
	{
		if (mem_clip_bpp == MEM_BPP) men_item_state( menu_need_clipboard, TRUE );
		else men_item_state( menu_need_clipboard, FALSE );
			// Only allow pasting if the image is the same type as the clipboard
	}

	if ( mem_undo_done == 0 ) men_item_state( menu_undo, FALSE );
	else men_item_state( menu_undo, TRUE );

	if ( mem_undo_redo == 0 ) men_item_state( menu_redo, FALSE );
	else  men_item_state( menu_redo, TRUE );

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_chann_x[mem_channel]), TRUE);
}

void canvas_undo_chores()
{
	gtk_widget_set_usize( drawing_canvas, mem_width*can_zoom, mem_height*can_zoom );
	update_all_views();				// redraw canvas widget
	update_menus();
	init_pal();
	gtk_widget_queue_draw( drawing_col_prev );
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

static int save_pal(char *file_name, int type)		// Save the current palette to a file
{
	FILE *fp;
	int i;

	if ((fp = fopen(file_name, "w")) == NULL) return -1;

	if ( type == 0 )		// .gpl file
	{
		fprintf(fp, "GIMP Palette\nName: mtPaint\nColumns: 16\n#\n");
		for ( i=0; i<mem_cols; i++ )
			fprintf(fp, "%3i %3i %3i\tUntitled\n",
				mem_pal[i].red, mem_pal[i].green, mem_pal[i].blue );
	}

	if ( type == 1 )		// .txt file
	{
		fprintf(fp, "%i\n", mem_cols);
		for ( i=0; i<mem_cols; i++ )
			fprintf(fp, "%i,%i,%i\n", mem_pal[i].red, mem_pal[i].green, mem_pal[i].blue );
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

void update_status_bar1()
{
	char txt[64], txt2[16];

	toolbar_update_settings();		// Update A/B labels in settings toolbar

	if ( mem_img_bpp == 1 )
		sprintf(txt2, "%i", mem_cols);
	else
		sprintf(txt2, "RGB");

	snprintf(txt, 50, "%s %i x %i x %s", channames[mem_channel], mem_width, mem_height, txt2);

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

void update_cols()
{
	if (!mem_img[CHN_IMAGE]) return;	// Only do this if we have an image

	update_status_bar1();
	mem_pat_update();

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
	mem_pal_init();				// Update palette RGB on screen
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

static void populate_channel( char *filename, int c )
{
	int res;
	unsigned char *temp;
	undo_item *undo = &mem_undo_im_[mem_undo_pointer];

	if ( valid_file(filename) == 0 )
	{
		temp = malloc( mem_width * mem_height );
		if (temp)
		{
			res = load_channel( filename, temp, mem_width, mem_height);
			if ( res != 0 ) free(temp);	// Problem with file I/O
			else mem_img[c] = undo->img[c] = temp;
		}
		else memory_errors(1);		// Not enough memory available
	}
}


int do_a_load( char *fname )
{
	gboolean loading_single = FALSE, loading_png = FALSE;
	int res, i, gif_delay;
	char mess[512], real_fname[300], chan_fname[260];

#if DIR_SEP == '/'
	if ( fname[0] != DIR_SEP )		// GNU/Linux
#endif
#if DIR_SEP == '\\'
	if ( fname[1] != ':' )			// Windows
#endif
	{
		getcwd( real_fname, 256 );
		i = strlen(real_fname);
		real_fname[i] = DIR_SEP;
		real_fname[i+1] = 0;
		strncat( real_fname, fname, 256 );
	}
	else strncpy( real_fname, fname, 256 );

gtk_widget_hide( drawing_canvas );

	if ( (res = load_gif( real_fname, &gif_delay )) == -1 )
	if ( (res = load_tiff( real_fname )) == -1 )
	if ( (res = load_bmp( real_fname )) == -1 )
	if ( (res = load_jpeg( real_fname )) == -1 )
	if ( (res = load_xpm( real_fname )) == -1 )
	if ( (res = load_xbm( real_fname )) == -1 )
	{
		loading_png = TRUE;
		res = load_png( real_fname, 0 );
	}

	if ( res>0 ) loading_single = TRUE;
	else
	{		// Not a single image file, but is it an mtPaint layers file?
		if ( layers_check_header(real_fname) )
		{
			res = load_layers(real_fname);
		}
	}

	if ( res<=0 )				// Error loading file
	{
		if (res == NOT_INDEXED)
		{
			snprintf(mess, 500, _("Image is not indexed: %s"), fname);
			alert_box( _("Error"), mess, ("OK"), NULL, NULL );
		} else
			if (res == TOO_BIG)
			{
				snprintf(mess, 500, _("File is too big, must be <= to width=%i height=%i : %s"), MAX_WIDTH, MAX_HEIGHT, fname);
				alert_box( _("Error"), mess, _("OK"), NULL, NULL );
			} else
			{
				alert_box( _("Error"), _("Unable to load file"),
					_("OK"), NULL, NULL );
			}
		goto fail;
	}

	if ( res == FILE_LIB_ERROR )
		alert_box( _("Error"), _("The file import library had to terminate due to a problem with the file (possibly corrupt image data or a truncated file). I have managed to load some data as the header seemed fine, but I would suggest you save this image to a new file to ensure this does not happen again."), _("OK"), NULL, NULL );

	if ( res == FILE_MEM_ERROR ) memory_errors(1);		// Image was too large for OS

	if ( loading_single )
	{
		reset_tools();
		register_file(real_fname);
		set_new_filename(real_fname);
		if ( marq_status > MARQUEE_NONE ) marq_status = MARQUEE_NONE;
			// Stops unwanted automatic paste following a file load when enabling
			// "Changing tool commits paste" via preferences

		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[PAINT_TOOL_ICON]), TRUE );
			// Set tool to square for new image - easy way to lose a selection marquee
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
		if ( layers_total>0 )
			layers_notify_changed(); // We loaded an image into the layers, so notify change
	}
	else
	{
		register_file(real_fname);		// Update recently used file list
//		if ( layers_window == NULL ) pressed_layers( NULL, NULL );
		if ( !view_showing ) view_show();
			// We have just loaded a layers file so display view & layers window if not up
	}

	if ( res>0 )
	{
		if ( res == FILE_GIF_ANIM )		// Animated GIF was loaded so tell user
		{
			res = alert_box( _("Warning"), _("This is an animated GIF file.  What do you want to do?"), _("Cancel"), _("Edit Frames"),
#ifdef WIN32
			NULL
#else
			_("View Animation")
#endif
			);

			if ( res == 2 )			// Ask for directory to explode frames to
			{
				preserved_gif_delay = gif_delay;
					// Needed when starting new mtpaint process later
				strncpy( preserved_gif_filename, mem_filename, 250 );

				if ( fs_type > 0 )
					fs_do_gif_explode = TRUE;
				else
					file_selector( FS_GIF_EXPLODE );
				/* This horrendous fiddle is needed:
					1) Either we were called via a file selector for load, if so we must terminate this FS dialog before calling the next one
					2) OR we could have been called on startup, command line window, or recently used file list in which case we must call the file selector now
				*/
			}
			if ( res == 3 )
			{
				snprintf(mess, 500, "gifview -a \"%s\" &", fname );
				gifsicle(mess);
			}

			create_default_image();		// Have empty image again to avoid destroying old animation
		}
		else	// Load channels files if they exist
		{
			// Check for existence of <filename>_c? (PNG) <filename>.png_c? (non-PNG)
			// if not RGB PNG loaded, check for ?=0.  Always look for ?=1, ?=2

			if ( loading_png ) snprintf( chan_fname, 256, "%s_c0", fname );
			else snprintf( chan_fname, 256, "%s.png_c0", fname );

			if ( !( loading_png && mem_img_bpp==3) )
				populate_channel( chan_fname, CHN_ALPHA );

			chan_fname[strlen(chan_fname)-1] = '1';
			populate_channel( chan_fname, CHN_SEL );

			chan_fname[strlen(chan_fname)-1] = '2';
			populate_channel( chan_fname, CHN_MASK );
		}

		update_all_views();					// Show new image
		init_status_bar();

		gtk_adjustment_value_changed( gtk_scrolled_window_get_hadjustment(
			GTK_SCROLLED_WINDOW(scrolledwindow_canvas) ) );
		gtk_adjustment_value_changed( gtk_scrolled_window_get_vadjustment(
			GTK_SCROLLED_WINDOW(scrolledwindow_canvas) ) );
				// These 2 are needed to synchronize the scrollbars & image view
	}
	gtk_widget_show( drawing_canvas );
	return 0;

fail:
	gtk_widget_show( drawing_canvas );
	return 1;
}



///	FILE SELECTION WINDOW


GtkWidget *filew, *fs_png[2], *fs_jpg[2], *fs_xbm[5], *fs_combo_entry;

static int fs_get_extension()			// Get the extension type from the combo box
{
	char txt[8], *txt2[] = { "", "PNG", "JPEG", "TIFF", "BMP", "GIF", "XPM", "XBM", "GPL", "TXT" };
	int i, j=0;

	strncpy( txt, gtk_entry_get_text( GTK_ENTRY(fs_combo_entry) ), 6 );

	for ( i=0; i<10; i++ )
	{
		if ( strcmp( txt, txt2[i] ) == 0 ) j=i;
	}

	return j;		// The order of txt2 matches EXT_NONE, EXT_PNG ...
}

static void fs_set_extension()			// Change the filename extension to match the combo
{
	char *ext[] = { "NONE", ".png", ".jpg", ".tif", ".bmp", ".gif", ".xpm", ".xbm", ".gpl", ".txt" },
		old_name[260], new_name[260];
	int i, j, k, l;

	strncpy( old_name,
		gtk_entry_get_text( GTK_ENTRY(GTK_FILE_SELECTION(filew)->selection_entry) ),
		256 );
	strncpy( new_name, old_name, 256 );
	i = fs_get_extension();			// New selected extension from combo
	j = file_extension_get( old_name );	// Find out current extension

	if ( i<1 || i>9 ) return;		// Ignore any dodgy numbers

	if ( i != j )				// Only do this if the format is different
	{
		k = strlen( old_name );
		if ( j == 0 )			// No current extension so just append it
		{
			if ( k<250 )
			{
				for ( l=0; l<4; l++ )
				{
					new_name[k++] = ext[i][l];
				}
				new_name[k] = 0;
			}
		}
		else				// Remove old extension, put in new one
		{
			if ( k>3 )
			{
				for ( l=0; l<4; l++ )
				{
					new_name[k-1-l] = ext[i][3-l];
				}
			}
		}
		gtk_entry_set_text( GTK_ENTRY(GTK_FILE_SELECTION(filew)->selection_entry), new_name );
	}
}

static void fs_check_format()			// Check format selected and hide widgets not needed
{
	char txt[8], *txt2[] = { "PNG", "JPEG", "TIFF", "BMP", "GIF", "XPM", "XBM" };
	int i, j = -1;
	gboolean w_show[3];

	if ( fs_type == FS_PALETTE_SAVE ) return;	// Don't bother for palette saving

	strncpy( txt, gtk_entry_get_text( GTK_ENTRY(fs_combo_entry) ), 6 );

	for ( i=0; i<7; i++ )
	{
		if ( strcmp( txt, txt2[i] ) == 0 ) j=i;
	}

	w_show[0] = FALSE;
	w_show[1] = FALSE;
	w_show[2] = FALSE;
	switch ( j )
	{
		case 0:
		case 4:
		case 5:		w_show[0] = TRUE;	// PNG, GIF, XPM
				break;
		case 1:		w_show[1] = TRUE;	// JPG
				break;
		case 6:		w_show[2] = TRUE;	// XBM
				break;
	}

	for ( i=0; i<2; i++ )
	{
		if ( w_show[0] ) gtk_widget_show( fs_png[i] );
		else gtk_widget_hide( fs_png[i] );
		if ( w_show[1] ) gtk_widget_show( fs_jpg[i] );
		else gtk_widget_hide( fs_jpg[i] );
	}
	for ( i=0; i<5; i++ )
	{
		if ( w_show[2] ) gtk_widget_show( fs_xbm[i] );
		else gtk_widget_hide( fs_xbm[i] );
	}
}

static void fs_set_format()			// Check format selected and set memory accordingly
{
	char txt[8], *txt2[] = { "PNG", "JPEG", "TIFF", "BMP", "GIF", "XPM", "XBM" };
	int i, j = -1;

	strncpy( txt, gtk_entry_get_text( GTK_ENTRY(fs_combo_entry) ), 6 );

	for ( i=0; i<7; i++ )
	{
		if ( strcmp( txt, txt2[i] ) == 0 ) j=i;
	}

	switch ( j )
	{
		case 0:
		case 4:			// PNG, GIF, XPM
		case 5:		if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fs_png[0])) )
				{
					mem_xpm_trans = gtk_spin_button_get_value_as_int(
							GTK_SPIN_BUTTON(fs_png[1]) );
				}
				else
				{
					mem_xpm_trans = -1;
				}
				break;
		case 1:		gtk_spin_button_update( GTK_SPIN_BUTTON(fs_jpg[1]) );		// JPG
				mem_jpeg_quality = gtk_spin_button_get_value_as_int(
							GTK_SPIN_BUTTON(fs_jpg[1]) );
				inifile_set_gint32( "jpegQuality", mem_jpeg_quality );
				break;
		case 6:		if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fs_xbm[0])) ) // XBM
				{
					mem_xbm_hot_x = gtk_spin_button_get_value_as_int(
							GTK_SPIN_BUTTON(fs_xbm[2]) );
					mem_xbm_hot_y = gtk_spin_button_get_value_as_int(
							GTK_SPIN_BUTTON(fs_xbm[4]) );
				}
				else
				{
					mem_xbm_hot_x = -1;
					mem_xbm_hot_y = -1;
				}
				break;
	}
}

static void fs_combo_change( GtkWidget *widget, gpointer *user_data )
{
	fs_check_format();
	fs_set_extension();
}

gint fs_destroy( GtkWidget *widget, GtkFileSelection *fs )
{
	int x, y, width, height;

	gdk_window_get_size( filew->window, &width, &height );
	gdk_window_get_root_origin( filew->window, &x, &y );

	inifile_set_gint32("fs_window_x", x );
	inifile_set_gint32("fs_window_y", y );
	inifile_set_gint32("fs_window_w", width );
	inifile_set_gint32("fs_window_h", height );

	fs_type = 0;
	gtk_widget_destroy( filew );

	return FALSE;
}

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

gint fs_ok( GtkWidget *widget, GtkFileSelection *fs )
{
	char fname[256], mess[512], gif_nam[256], gif_nam2[320], *c;
	int res, i;
	gboolean found;

	gtk_window_set_modal(GTK_WINDOW(filew),FALSE);
		// Needed to show progress in Windows GTK+2

	if (fs_type == FS_PNG_SAVE || fs_type == FS_PALETTE_SAVE )
		fs_set_extension();		// Ensure extension is correct

#if GTK_MAJOR_VERSION == 2
	c = g_locale_from_utf8( (gchar *) gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs)),
			-1, NULL, NULL, NULL );
	if ( c == NULL )
#endif
		strncpy( fname, gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs)), 250 );
#if GTK_MAJOR_VERSION == 2
	else
	{
		strncpy( fname, c, 250 );
		g_free(c);
	}
#endif

	switch ( fs_type )
	{
		case FS_PNG_LOAD:
					if ( do_a_load( fname ) == 1) goto redo;
					break;
		case FS_PNG_SAVE:
					if ( check_file(fname) == 1 ) goto redo;
					fs_set_format();	// Update data in memory from widgets
					if ( gui_save(fname) < 0 ) goto redo;
					if ( layers_total>0 )
					{
						if ( strcmp(fname, mem_filename ) != 0 )
							layers_notify_changed();
			// Filename has changed so layers file needs re-saving to be correct
					}
					set_new_filename( fname );
					update_status_bar1();	// Update transparency info
					update_all_views();	// Redraw in case transparency changed
					break;
		case FS_PALETTE_LOAD:
					if ( load_pal(fname) !=0 )
					{
						snprintf(mess, 500, _("File: %s invalid - palette not updated"), fname);
						res = alert_box( _("Error"), mess, _("OK"), NULL, NULL );
						goto redo;
					}
					else	notify_changed();
					break;
		case FS_PALETTE_SAVE:
					if ( check_file(fname) != 0 ) goto redo;
					if ( file_extension_get( fname ) == EXT_TXT )
						res = save_pal( fname, 1 );	// .txt
					else	res = save_pal( fname, 0 );	// .gpl
					if ( res < 0 )
					{
						snprintf(mess, 500, _("Unable to save file: %s"), fname);
						alert_box( _("Error"), mess, _("OK"), NULL, NULL );
						goto redo;
					}
					break;
		case FS_CLIP_FILE:
					strncpy( mem_clip_file[1], fname, 250 );
					gtk_entry_set_text( GTK_ENTRY(clipboard_entry),
						mem_clip_file[1] );
					break;
		case FS_EXPORT_UNDO:
		case FS_EXPORT_UNDO2:
					if ( export_undo( fname, fs_type - FS_EXPORT_UNDO ) != 0 )
						alert_box( _("Error"), _("Unable to export undo images"),
							_("OK"), NULL, NULL );
					break;
		case FS_EXPORT_ASCII:
					if ( check_file(fname) != 0 ) goto redo;
					if ( export_ascii( fname ) != 0 )
						alert_box( _("Error"), _("Unable to export ASCII file"),
							_("OK"), NULL, NULL );
					break;
		case FS_LAYER_SAVE:
					if ( check_file(fname) == 1 ) goto redo;
					res = save_layers( fname );
					if ( res != 1 ) goto redo;
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
					if ( check_file(fname) == 1 ) goto redo;

					snprintf(gif_nam, 250, "%s", mem_filename);
					wild_space_change( gif_nam, gif_nam2, 315 );

					i = strlen(gif_nam2) - 1;
					found = FALSE;

					while ( i>=0 && !found ) // Find numbers on end of filename
					{
						if ( gif_nam2[i] == DIR_SEP ) break;
							// None in name as we have reached separator
						if ( gif_nam2[i] >= '0' && gif_nam2[i] <= '9' )
							found = TRUE;
						else
							i--;
					}
					while ( found )		// replace numbers on end with ?'s
					{
						if ( gif_nam2[i] >= '0' && gif_nam2[i] <= '9' )
							gif_nam2[i] = '?';
						i--;
						if ( i<0 || gif_nam2[i] == DIR_SEP ) break;
					}

					preserved_gif_delay = gtk_spin_button_get_value_as_int(
								GTK_SPIN_BUTTON(fs_png[1]) );
						
					snprintf(mess, 500, "%s -d %i %s -o \"%s\"",
						GIFSICLE_CREATE, preserved_gif_delay, gif_nam2, fname
					);
//printf("%s\n", mess);
					gifsicle(mess);

#ifndef WIN32
					snprintf(mess, 500, "gifview -a \"%s\" &", fname );
					gifsicle(mess);
//printf("%s\n", mess);
#endif

					break;
	}

	gtk_window_set_transient_for( GTK_WINDOW(filew), NULL );	// Needed in Windows to stop GTK+ lowering the main window below window underneath
	update_menus();

	fs_destroy( NULL, NULL );

	if ( fs_do_gif_explode )
	{
		file_selector( FS_GIF_EXPLODE );
		fs_do_gif_explode = FALSE;
	}

	return FALSE;
redo:
	gtk_window_set_modal(GTK_WINDOW(filew), TRUE);
	return FALSE;
}

void file_selector( int action_type )
{
	GtkWidget *hbox, *label, *combo;
	GList *combo_list = NULL;
	gboolean do_it;
	int i, j, k;
	char *combo_txt[] = { "PNG", "JPEG", "TIFF", "BMP", "PNG", "GIF", "BMP", "XPM", "XBM" },
		*combo_txt2[] = { "GPL", "TXT" };

	char *title = NULL, txt[300], txt2[300], *temp_txt = NULL;
	
	fs_type = action_type;

	switch ( fs_type )
	{
		case FS_PNG_LOAD:	title = _("Load Image File");
					if ( layers_total==0 )
					{
						if ( check_for_changes() == 1 ) return;
					}
					else	if ( check_layers_for_changes() == 1 ) return;
					break;
		case FS_PNG_SAVE:	title = _("Save Image File");
					break;
		case FS_PALETTE_LOAD:	title = _("Load Palette File");
					break;
		case FS_PALETTE_SAVE:	title = _("Save Palette File");
					break;
		case FS_CLIP_FILE:	title = _("Select Clipboard File");
					break;
		case FS_EXPORT_UNDO:	title = _("Export Undo Images");
					break;
		case FS_EXPORT_UNDO2:	title = _("Export Undo Images (reversed)");
					break;
		case FS_EXPORT_ASCII:	title = _("Export ASCII Art");
					break;
		case FS_LAYER_SAVE:	title = _("Save Layer Files");
					break;
		case FS_GIF_EXPLODE:	title = _("Import GIF animation - Choose frames directory");
					break;
		case FS_EXPORT_GIF:	title = _("Export GIF animation");
					break;
	}

	filew = gtk_file_selection_new ( title );

	gtk_window_set_modal(GTK_WINDOW(filew),TRUE);
	gtk_window_set_default_size( GTK_WINDOW(filew),
		inifile_get_gint32("fs_window_w", 550 ), inifile_get_gint32("fs_window_h", 500 ) );
	gtk_widget_set_uposition( filew,
		inifile_get_gint32("fs_window_x", 0 ), inifile_get_gint32("fs_window_y", 0 ) );

	if ( fs_type == FS_EXPORT_UNDO || fs_type == FS_EXPORT_UNDO2 || fs_type == FS_GIF_EXPLODE )
	{
//		gtk_widget_set_sensitive( GTK_WIDGET(GTK_FILE_SELECTION(filew)->file_list), FALSE );
		gtk_widget_hide( GTK_WIDGET(GTK_FILE_SELECTION(filew)->file_list) );
		if ( fs_type == FS_GIF_EXPLODE )
			gtk_widget_hide( GTK_WIDGET(GTK_FILE_SELECTION(filew)->selection_entry) );
	}

	gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (filew)->ok_button),
		"clicked", GTK_SIGNAL_FUNC(fs_ok), (gpointer) filew);

	gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION (filew)->cancel_button),
		"clicked", GTK_SIGNAL_FUNC( fs_destroy ), GTK_OBJECT (filew) );

	gtk_signal_connect_object (GTK_OBJECT(filew),
		"delete_event", GTK_SIGNAL_FUNC( fs_destroy ), GTK_OBJECT (filew) );

	if ( fs_type == FS_PNG_SAVE && strcmp( mem_filename, _("Untitled") ) != 0 )
		strncpy( txt, mem_filename, 256 );	// If we have a filename and saving
	else
	{
		if ( fs_type == FS_LAYER_SAVE && strcmp( layers_filename, _("Untitled") ) != 0 )
			strncpy( txt, layers_filename, 256 );
		else
		{
			if ( fs_type == FS_LAYER_SAVE )
			{
				strncpy( txt, inifile_get("last_dir", "/"), 256 );
				strncat( txt, "layers.txt", 256 );
			}
			else	strncpy( txt, inifile_get("last_dir", "/"), 256 );	// Default
		}
	}
#if GTK_MAJOR_VERSION == 2
	cleanse_txt( txt2, txt );		// Clean up non ASCII chars
#else
	strcpy( txt2, txt );
#endif
	gtk_file_selection_set_filename (GTK_FILE_SELECTION(filew), txt2 );


	if ( fs_type == FS_EXPORT_GIF )
	{
		hbox = gtk_hbox_new (FALSE, 0);
		gtk_widget_show(hbox);
		gtk_box_pack_start(GTK_BOX(GTK_FILE_SELECTION(filew)->main_vbox), hbox, FALSE, TRUE, 0);

		label = gtk_label_new(_("Animation delay"));
		gtk_widget_show (label);
		gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

		fs_png[1] = add_a_spin( preserved_gif_delay, 1, MAX_DELAY );

		gtk_box_pack_start (GTK_BOX (hbox), fs_png[1], FALSE, FALSE, 10);
	}


	if ( fs_type == FS_PNG_SAVE || fs_type == FS_PALETTE_SAVE )
	{
		hbox = gtk_hbox_new (FALSE, 0);
		gtk_widget_show(hbox);
		gtk_box_pack_start(GTK_BOX(GTK_FILE_SELECTION(filew)->main_vbox), hbox, FALSE, TRUE, 0);

		label = gtk_label_new( _("File Format") );
		gtk_widget_show( label );
		gtk_box_pack_start( GTK_BOX(hbox), label, FALSE, FALSE, 10 );

		combo = gtk_combo_new ();
		gtk_widget_set_usize(combo, 100, -2);
		gtk_widget_show (combo);
		gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 10);
		fs_combo_entry = GTK_COMBO (combo)->entry;
		gtk_widget_show (fs_combo_entry);
		gtk_entry_set_editable( GTK_ENTRY(fs_combo_entry), FALSE );

		if ( fs_type == FS_PNG_SAVE )
		{
			temp_txt = "PNG";
			i = file_extension_get( mem_filename );
			k = 4;
			if ( i == EXT_BMP ) temp_txt = "BMP";
			if ( mem_img_bpp == 3 )
			{
				j = 0;
				if ( i == EXT_JPEG ) temp_txt = "JPEG";
				if ( i == EXT_TIFF ) temp_txt = "TIFF";
			}
			else
			{
				if ( i == EXT_GIF ) temp_txt = "GIF";
				if ( i == EXT_XPM ) temp_txt = "XPM";
				j = 4;
				if ( mem_cols == 2 )
				{
					k++;			// Allow XBM if indexed with 2 colours
					if ( i == EXT_XBM ) temp_txt = "XBM";
				}
			}

			for ( i=j; i<(j+k); i++ )
			{
				combo_list = g_list_append( combo_list, combo_txt[i] );
			}
		}
		if ( fs_type == FS_PALETTE_SAVE )
		{
			temp_txt = "GPL";
			combo_list = g_list_append( combo_list, combo_txt2[0] );
			combo_list = g_list_append( combo_list, combo_txt2[1] );
		}

		gtk_combo_set_popdown_strings( GTK_COMBO(combo), combo_list );
		g_list_free( combo_list );
		gtk_entry_set_text( GTK_ENTRY(fs_combo_entry), temp_txt );

		gtk_signal_connect_object (GTK_OBJECT (fs_combo_entry), "changed",
			GTK_SIGNAL_FUNC (fs_combo_change), GTK_OBJECT (fs_combo_entry));

		if ( fs_type == FS_PNG_SAVE )
		{
			if ( mem_xpm_trans >=0 ) do_it = TRUE; else do_it = FALSE;
			fs_png[0] = add_a_toggle( _("Set transparency index"), hbox, do_it );

			i = mem_xpm_trans;
			if ( i<0 ) i=0;
			fs_png[1] = add_a_spin( i, 0, mem_cols-1 );
			gtk_box_pack_start (GTK_BOX (hbox), fs_png[1], FALSE, FALSE, 10);

			fs_jpg[0] = gtk_label_new( _("JPEG Save Quality (100=High)") );
			gtk_widget_show( fs_jpg[0] );
			gtk_box_pack_start( GTK_BOX(hbox), fs_jpg[0], FALSE, FALSE, 10 );
			fs_jpg[1] = add_a_spin( mem_jpeg_quality, 0, 100 );
			gtk_box_pack_start (GTK_BOX (hbox), fs_jpg[1], FALSE, FALSE, 10);

			if ( mem_xbm_hot_x >=0 ) do_it = TRUE; else do_it = FALSE;
			fs_xbm[0] = add_a_toggle( _("Set hotspot"), hbox, do_it );
			fs_xbm[1] = gtk_label_new( _("X =") );
			gtk_widget_show( fs_xbm[1] );
			gtk_box_pack_start( GTK_BOX(hbox), fs_xbm[1], FALSE, FALSE, 10 );
			i = mem_xbm_hot_x;
			if ( i<0 ) i = 0;
			fs_xbm[2] = add_a_spin( i, 0, mem_width-1 );
			gtk_box_pack_start (GTK_BOX (hbox), fs_xbm[2], FALSE, FALSE, 10);
			fs_xbm[3] = gtk_label_new( _("Y =") );
			gtk_widget_show( fs_xbm[3] );
			gtk_box_pack_start( GTK_BOX(hbox), fs_xbm[3], FALSE, FALSE, 10 );
			i = mem_xbm_hot_y;
			if ( i<0 ) i = 0;
			fs_xbm[4] = add_a_spin( i, 0, mem_height-1 );
			gtk_box_pack_start (GTK_BOX (hbox), fs_xbm[4], FALSE, FALSE, 10);
		}

		fs_check_format();
	}

	gtk_widget_show (filew);
	gtk_window_set_transient_for( GTK_WINDOW(filew), GTK_WINDOW(main_window) );
	gdk_window_raise( filew->window );		// Needed to ensure window is at the top
}

void align_size( float new_zoom )		// Set new zoom level
{
	GtkAdjustment *hori, *vert;
	int nv_h = 0, nv_v = 0;			// New positions of scrollbar

	if ( zoom_flag != 0 ) return;		// Needed as we could be called twice per iteration

	if ( new_zoom<MIN_ZOOM ) new_zoom = MIN_ZOOM;
	if ( new_zoom>MAX_ZOOM ) new_zoom = MAX_ZOOM;

	if ( new_zoom != can_zoom )
	{
		zoom_flag = 1;
		hori = gtk_scrolled_window_get_hadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );
		vert = gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );

		if ( mem_ics == 0 )
		{
			if ( hori->page_size > mem_width*can_zoom ) mem_icx = 0.5;
			else mem_icx = ( hori->value + ((float) hori->page_size )/2 )
				/ (mem_width*can_zoom);

			if ( vert->page_size > mem_height*can_zoom ) mem_icy = 0.5;
			else mem_icy = ( vert->value + ((float) vert->page_size )/2 )
				/ (mem_height*can_zoom);
		}
		mem_ics = 0;

		can_zoom = new_zoom;

		if ( hori->page_size < mem_width*can_zoom )
			nv_h = mt_round( mem_width*can_zoom*mem_icx - ((float)hori->page_size)/2 );
		else	nv_h = 0;

		if ( vert->page_size < mem_height*can_zoom )
			nv_v = mt_round( mem_height*can_zoom*mem_icy - ((float)vert->page_size)/2 );
		else	nv_v = 0;

		hori->value = nv_h;
		hori->upper = mt_round(mem_width*can_zoom);
		vert->value = nv_v;
		vert->upper = mt_round(mem_height*can_zoom);

#if GTK_MAJOR_VERSION == 1
		gtk_adjustment_value_changed( hori );
		gtk_adjustment_value_changed( vert );
#endif
		gtk_widget_set_usize( drawing_canvas, mem_width*can_zoom, mem_height*can_zoom );

		init_status_bar();
		zoom_flag = 0;
		vw_focus_view();		// View window position may need updating
		toolbar_zoom_update();
	}
}

void square_continuous( int nx, int ny, int *minx, int *miny, int *xw, int *yh )
{
	if ( tool_size == 1 )
	{
		put_pixel( nx, ny );
	}
	else
	{
		if ( tablet_working )	// Needed to fill in possible gap when size changes
		{
			f_rectangle( tool_ox - tool_size/2, tool_oy - tool_size/2,
					tool_size, tool_size );
		}
		if ( ny > tool_oy )		// Down
		{
			h_para( tool_ox - tool_size/2,
				tool_oy - tool_size/2 + tool_size - 1,
				nx - tool_size/2,
				ny - tool_size/2 + tool_size - 1,
				tool_size );
		}
		if ( nx > tool_ox )		// Right
		{
			v_para( tool_ox - tool_size/2 + tool_size - 1,
				tool_oy - tool_size/2,
				nx - tool_size/2 + tool_size -1,
				ny - tool_size/2,
				tool_size );
		}
		if ( ny < tool_oy )		// Up
		{
			h_para( tool_ox - tool_size/2,
				tool_oy - tool_size/2,
				nx - tool_size/2,
				ny - tool_size/2,
				tool_size );
		}
		if ( nx < tool_ox )		// Left
		{
			v_para( tool_ox - tool_size/2,
				tool_oy - tool_size/2,
				nx - tool_size/2,
				ny - tool_size/2,
				tool_size );
		}
	}
}

void vertical_continuous( int nx, int ny, int *minx, int *miny, int *xw, int *yh )
{
	int	ax = tool_ox, ay = tool_oy - tool_size/2,
		bx = nx, by = ny - tool_size/2, vlen = tool_size;

	int mny, may;

	if ( ax == bx )		// Simple vertical line required
	{
		mtMIN( ay, tool_oy - tool_size/2, ny - tool_size/2 )
		mtMAX( by, tool_oy - tool_size/2 + tool_size - 1, ny - tool_size/2 + tool_size - 1 )
		vlen = by - ay + 1;
		if ( ay < 0 )
		{
			vlen = vlen + ay;
			ay = 0;
		}
		if ( by >= mem_height )
		{
			vlen = vlen - ( mem_height - by + 1 );
			by = mem_height - 1;
		}

		if ( vlen <= 1 )
		{
			ax = bx; ay = by;
			put_pixel( bx, by );
		}
		else
		{
			sline( ax, ay, bx, by );

			mtMIN( *minx, ax, bx )
			mtMIN( *miny, ay, by )
			*xw = abs( ax - bx ) + 1;
			*yh = abs( ay - by ) + 1;
		}
	}
	else			// Parallelogram with vertical left and right sides required
	{
		v_para( ax, ay, bx, by, tool_size );

		mtMIN( *minx, ax, bx )
		*xw = abs( ax - bx ) + 1;

		mtMIN( mny, ay, by )
		mtMAX( may, ay + tool_size - 1, by + tool_size - 1 )

		mtMAX( mny, mny, 0 )
		mtMIN( may, may, mem_height )

		*miny = mny;
		*yh = may - mny + 1;
	}
}

void horizontal_continuous( int nx, int ny, int *minx, int *miny, int *xw, int *yh )
{
	int ax = tool_ox - tool_size/2, ay = tool_oy,
		bx = nx - tool_size/2, by = ny, hlen = tool_size;

	int mnx, max;

	if ( ay == by )		// Simple horizontal line required
	{
		mtMIN( ax, tool_ox - tool_size/2, nx - tool_size/2 )
		mtMAX( bx, tool_ox - tool_size/2 + tool_size - 1, nx - tool_size/2 + tool_size - 1 )
		hlen = bx - ax + 1;
		if ( ax < 0 )
		{
			hlen = hlen + ax;
			ax = 0;
		}
		if ( bx >= mem_width )
		{
			hlen = hlen - ( mem_width - bx + 1 );
			bx = mem_width - 1;
		}

		if ( hlen <= 1 )
		{
			ax = bx; ay = by;
			put_pixel( bx, by );
		}
		else
		{
			sline( ax, ay, bx, by );

			mtMIN( *minx, ax, bx )
			mtMIN( *miny, ay, by )
			*xw = abs( ax - bx ) + 1;
			*yh = abs( ay - by ) + 1;
		}
	}
	else			// Parallelogram with horizontal top and bottom sides required
	{
		h_para( ax, ay, bx, by, tool_size );

		mtMIN( *miny, ay, by )
		*yh = abs( ay - by ) + 1;

		mtMIN( mnx, ax, bx )
		mtMAX( max, ax + tool_size - 1, bx + tool_size - 1 )

		mtMAX( mnx, mnx, 0 )
		mtMIN( max, max, mem_width )

		*minx = mnx;
		*xw = max - mnx + 1;
	}
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

void tool_action( int x, int y, int button, gdouble pressure )
{
	int minx = -1, miny = -1, xw = -1, yh = -1;
	int i, j, k, rx, ry, sx, sy;
	int ox, oy, off1, off2, o_size = tool_size, o_flow = tool_flow, o_opac = tool_opacity, n_vs[3];
	int xdo, ydo, px, py, todo, oox, ooy;	// Continuous smudge stuff
	float rat;
	gboolean first_point = FALSE, paint_action = FALSE;

	if ( tool_fixx > -1 ) x = tool_fixx;
	if ( tool_fixy > -1 ) y = tool_fixy;

	if ( (button == 1 || button == 3) && (tool_type <= TOOL_SPRAY) )
		paint_action = TRUE;

	if ( tool_type <= TOOL_SHUFFLE ) tint_mode[2] = button;

	if ( pen_down == 0 )
	{
		first_point = TRUE;
		if ( button == 3 && paint_action && !tint_mode[0] )
		{
			col_reverse = TRUE;
			mem_swap_cols();
		}
	}
	else if ( tool_ox == x && tool_oy == y ) return;	// Only do something with a new point

	if ( (button == 1 || paint_action) && tool_type != TOOL_FLOOD &&
		tool_type != TOOL_SELECT && tool_type != TOOL_POLYGON )
	{
		if ( !(tool_type == TOOL_LINE && line_status == LINE_NONE) )
		{
			mem_undo_next(UNDO_DRAW);	// Do memory stuff for undo
		}
	}

	if ( tablet_working )
	{
		pressure = (pressure - 0.2)/0.8;
		mtMIN( pressure, pressure, 1)
		mtMAX( pressure, pressure, 0)

		n_vs[0] = tool_size;
		n_vs[1] = tool_flow;
		n_vs[2] = tool_opacity;
		for ( i=0; i<3; i++ )
		{
			if ( tablet_tool_use[i] )
			{
				if ( tablet_tool_factor[i] > 0 )
					n_vs[i] *= (1 + tablet_tool_factor[i] * (pressure - 1));
				else
					n_vs[i] *= (0 - tablet_tool_factor[i] * (1 - pressure));
				mtMAX( n_vs[i], n_vs[i], 1 )
			}
		}
		tool_size = n_vs[0];
		tool_flow = n_vs[1];
		tool_opacity = n_vs[2];
	}

	minx = x - tool_size/2;
	miny = y - tool_size/2;
	xw = tool_size;
	yh = tool_size;

	if ( paint_action && !first_point && mem_continuous && tool_size == 1 &&
		tool_type < TOOL_SPRAY && ( abs(x - tool_ox) > 1 || abs(y - tool_oy ) > 1 ) )
	{		// Single point continuity
		sline( tool_ox, tool_oy, x, y );

		mtMIN( minx, tool_ox, x )
		mtMIN( miny, tool_oy, y )
		xw = abs( tool_ox - x ) + 1;
		yh = abs( tool_oy - y ) + 1;
	}
	else
	{
		if ( mem_continuous && !first_point && (button == 1 || button == 3) )
		{
			mtMIN( minx, tool_ox, x )
			mtMAX( xw, tool_ox, x )
			xw = xw - minx + tool_size;
			minx = minx - tool_size/2;

			mtMIN( miny, tool_oy, y )
			mtMAX( yh, tool_oy, y )
			yh = yh - miny + tool_size;
			miny = miny - tool_size/2;

			mem_boundary( &minx, &miny, &xw, &yh );
		}
		if ( tool_type == TOOL_SQUARE && paint_action )
		{
			if ( !mem_continuous || first_point )
				f_rectangle( minx, miny, xw, yh );
			else
			{
				square_continuous(x, y, &minx, &miny, &xw, &yh);
			}
		}
		if ( tool_type == TOOL_CIRCLE  && paint_action )
		{
			if ( mem_continuous && !first_point )
			{
				tline( tool_ox, tool_oy, x, y, tool_size );
			}
			f_circle( x, y, tool_size );
		}
		if ( tool_type == TOOL_HORIZONTAL && paint_action )
		{
			if ( !mem_continuous || first_point || tool_size == 1 )
			{
				for ( j=0; j<tool_size; j++ )
				{
					rx = x - tool_size/2 + j;
					ry = y;
					IF_IN_RANGE( rx, ry ) put_pixel( rx, ry );
				}
			}
			else	horizontal_continuous(x, y, &minx, &miny, &xw, &yh);
		}
		if ( tool_type == TOOL_VERTICAL && paint_action )
		{
			if ( !mem_continuous || first_point || tool_size == 1 )
			{
				for ( j=0; j<tool_size; j++ )
				{
					rx = x;
					ry = y - tool_size/2 + j;
					IF_IN_RANGE( rx, ry ) put_pixel( rx, ry );
				}
			}
			else	vertical_continuous(x, y, &minx, &miny, &xw, &yh);
		}
		if ( tool_type == TOOL_SLASH && paint_action )
		{
			if ( mem_continuous && !first_point && tool_size > 1 )
				g_para( x + (tool_size-1)/2, y - tool_size/2,
					x + (tool_size-1)/2 - (tool_size - 1),
					y - tool_size/2 + tool_size - 1,
					tool_ox - x, tool_oy - y );
			else for ( j=0; j<tool_size; j++ )
			{
				rx = x + (tool_size-1)/2 - j;
				ry = y - tool_size/2 + j;
				IF_IN_RANGE( rx, ry ) put_pixel( rx, ry );
			}
		}
		if ( tool_type == TOOL_BACKSLASH && paint_action )
		{
			if ( mem_continuous && !first_point && tool_size > 1 )
				g_para( x - tool_size/2, y - tool_size/2,
					x - tool_size/2 + tool_size - 1,
					y - tool_size/2 + tool_size - 1,
					tool_ox - x, tool_oy - y );
			else for ( j=0; j<tool_size; j++ )
			{
				rx = x - tool_size/2 + j;
				ry = y - tool_size/2 + j;
				IF_IN_RANGE( rx, ry ) put_pixel( rx, ry );
			}
		}
		if ( tool_type == TOOL_SPRAY && paint_action )
		{
			for ( j=0; j<tool_flow; j++ )
			{
				rx = x - tool_size/2 + rand() % tool_size;
				ry = y - tool_size/2 + rand() % tool_size;
				IF_IN_RANGE( rx, ry ) put_pixel( rx, ry );
			}
		}
		if ( tool_type == TOOL_SHUFFLE && button == 1 )
		{
			for ( j=0; j<tool_flow; j++ )
			{
				rx = x - tool_size/2 + rand() % tool_size;
				ry = y - tool_size/2 + rand() % tool_size;
				sx = x - tool_size/2 + rand() % tool_size;
				sy = y - tool_size/2 + rand() % tool_size;
				IF_IN_RANGE( rx, ry ) IF_IN_RANGE( sx, sy )
				{
					if (!pixel_protected(rx, ry) &&
						!pixel_protected(sx, sy))
					{
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
			}
		}
		if ( tool_type == TOOL_FLOOD && button == 1 )
		{
			/* Flood fill shouldn't start on masked points */
			if (!pixel_protected(x, y))
			{
				j = get_pixel(x, y);
				k = mem_channel != CHN_IMAGE ? channel_col_A[mem_channel] :
					mem_img_bpp == 1 ? mem_col_A : PNG_2_INT(mem_col_A24);
				if (j != k) /* And never start on colour A */
				{
					spot_undo(UNDO_DRAW);
					/* Regular fill */
					if (!tool_pat && (tool_opacity == 255))
						flood_fill(x, y, j);
					/* Fill using temp buffer */
					else flood_fill_pat(x, y, j);
					update_all_views();
				}
			}
		}
		if ( tool_type == TOOL_SMUDGE && button == 1 )
		{
			if ( !first_point && (tool_ox!=x || tool_oy!=y) )
			{
				if ( mem_continuous )
				{
					xdo = tool_ox - x;
					ydo = tool_oy - y;
					mtMAX( todo, abs(xdo), abs(ydo) )
					oox = tool_ox;
					ooy = tool_oy;

					for ( i=1; i<=todo; i++ )
					{
						rat = ((float) i ) / todo;
						px = mt_round(tool_ox + (x - tool_ox) * rat);
						py = mt_round(tool_oy + (y - tool_oy) * rat);
						mem_smudge(oox, ooy, px, py);
						oox = px;
						ooy = py;
					}
				}
				else mem_smudge(tool_ox, tool_oy, x, y);
			}
		}
		if ( tool_type == TOOL_CLONE && button == 1 )
		{
			if ( first_point || (!first_point && (tool_ox!=x || tool_oy!=y)) )
			{
				mem_clone( x+clone_x, y+clone_y, x, y );
			}
		}
	}

	if ( tool_type == TOOL_LINE )
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
				if ( tool_size > 1 )
				{
					f_circle( line_x1, line_y1, tool_size );
					f_circle( line_x2, line_y2, tool_size );
					// Draw tool_size thickness line from 1-2
					tline( line_x1, line_y1, line_x2, line_y2, tool_size );
				}
				else sline( line_x1, line_y1, line_x2, line_y2 );

				mtMIN( minx, line_x1, line_x2 )
				mtMIN( miny, line_y1, line_y2 )
				minx = minx - tool_size/2;
				miny = miny - tool_size/2;
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

	if ( tool_type == TOOL_SELECT || tool_type == TOOL_POLYGON )
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
			(button == 13 || button == 3) )
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
				if ( (i%2) == 0 )
				{	mtMAX(marq_x1, marq_x1, marq_x2)	}
				else
				{	mtMIN(marq_x1, marq_x1, marq_x2)	}
				if ( (i/2) == 0 )
				{	mtMAX(marq_y1, marq_y1, marq_y2)	}
				else
				{	mtMIN(marq_y1, marq_y1, marq_y2)	}
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
	}

	if ( tool_type == TOOL_POLYGON )
	{
		if ( poly_status == POLY_NONE && marq_status == MARQUEE_NONE )
		{
			if ( button != 0 )		// Start doing something
			{
				if ( button == 1 )
					poly_status = POLY_SELECTING;
				else
					poly_status = POLY_DRAGGING;
			}
		}
		if ( poly_status == POLY_SELECTING )
		{
			if ( button == 1 ) poly_add_po(x, y);		// Add another point to polygon
			else
			{
				if ( button == 3 ) poly_conclude();	// Stop adding points
			}
		}
		if ( poly_status == POLY_DRAGGING )
		{
			if ( button == 0 ) poly_conclude();		// Stop forming polygon
			else poly_add_po(x, y);				// Add another point to polygon
		}
	}

	if ( tool_type != TOOL_SELECT && tool_type != TOOL_POLYGON )
	{
		if ( minx<0 )
		{
			xw = xw + minx;
			minx = 0;
		}

		if ( miny<0 )
		{
			yh = yh + miny;
			miny = 0;
		}
		if ( can_zoom<1 )
		{
			xw = xw + mt_round(1/can_zoom) + 1;
			yh = yh + mt_round(1/can_zoom) + 1;
		}
		if ( (minx+xw) > mem_width ) xw = mem_width - minx;
		if ( (miny+yh) > mem_height ) yh = mem_height - miny;
		if ( tool_type != TOOL_FLOOD && (button == 1 || paint_action) &&
			minx>-1 && miny>-1 && xw>-1 && yh>-1)
		{
			gtk_widget_queue_draw_area( drawing_canvas,
				margin_main_x + minx*can_zoom, margin_main_y + miny*can_zoom,
				xw*can_zoom + 1, yh*can_zoom + 1);
			vw_update_area( minx, miny, xw+1, yh+1 );
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
	}
	else			// Selection mode in operation
	{
		mtMAX( marq_x1, marq_x1, 0 )
		mtMAX( marq_y1, marq_y1, 0 )
		mtMAX( marq_x2, marq_x2, 0 )
		mtMAX( marq_y2, marq_y2, 0 )
		mtMIN( marq_x1, marq_x1, mem_width-1 )
		mtMIN( marq_y1, marq_y1, mem_height-1 )
		mtMIN( marq_x2, marq_x2, mem_width-1 )
		mtMIN( marq_y2, marq_y2, mem_height-1 )
		if ( tool_type == TOOL_POLYGON && poly_points > 0 )
		{
			for ( i=0; i<poly_points; i++ )
			{
				mtMIN( poly_mem[i][0], poly_mem[i][0], mem_width-1 )
				mtMIN( poly_mem[i][1], poly_mem[i][1], mem_height-1 )
			}
		}
	}
}

int vc_x1, vc_y1, vc_x2, vc_y2;			// Visible canvas
GtkAdjustment *hori, *vert;

void get_visible()
{
	GtkAdjustment *hori, *vert;

	hori = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );
	vert = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );

	vc_x1 = hori->value;
	vc_y1 = vert->value;
	vc_x2 = hori->value + hori->page_size - 1;
	vc_y2 = vert->value + vert->page_size - 1;
}

void clip_area( int *rx, int *ry, int *rw, int *rh )		// Clip area to visible canvas
{
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
	int ux1, uy1, ux2, uy2;

	get_visible();

	mtMAX( ux1, vc_x1, x1 )
	mtMAX( uy1, vc_y1, y1 )
	mtMIN( ux2, vc_x2, x2 )
	mtMIN( uy2, vc_y2, y2 )

	mtMIN( ux2, ux2, mem_width*can_zoom - 1 )
	mtMIN( uy2, uy2, mem_height*can_zoom - 1 )

	if ( ux1 <= ux2 && uy1 <= uy2 )		// Only repaint if on visible canvas
		repaint_paste( ux1, uy1, ux2, uy2 );
}

void paint_poly_marquee()			// Paint polygon marquee
{
	int i, j, last = poly_points-1, co[2];
	
	GdkPoint xy[MAX_POLY+1];

	check_marquee();

	if ( tool_type == TOOL_POLYGON && poly_points > 1 )
	{
		if ( poly_status == POLY_DONE ) last++;		// Join 1st & last point if finished
		for ( i=0; i<=last; i++ )
		{
			for ( j=0; j<2; j++ )
			{
				co[j] = poly_mem[ i % (poly_points) ][j];
				co[j] = mt_round(co[j] * can_zoom + can_zoom/2);
						// Adjust for zoom
			}
			xy[i].x = margin_main_x + co[0];
			xy[i].y = margin_main_y + co[1];
		}
		gdk_draw_lines( drawing_canvas->window, dash_gc, xy, last+1 );
	}
}

void paint_marquee(int action, int new_x, int new_y)
{
	int x1, y1, x2, y2;
	int x, y, w, h, offx = 0, offy = 0;
	int rx, ry, rw, rh, canz = can_zoom, zerror = 0;
	int i, j, new_x2 = new_x + (marq_x2-marq_x1), new_y2 = new_y + (marq_y2-marq_y1);
	char *rgb;

	if ( canz<1 )
	{
		canz = 1;
		zerror = 2;
	}

	check_marquee();
	x1 = marq_x1*can_zoom; y1 = marq_y1*can_zoom;
	x2 = marq_x2*can_zoom; y2 = marq_y2*can_zoom;

	mtMIN( x, x1, x2 )
	mtMIN( y, y1, y2 )
	w = x1 - x2;
	h = y1 - y2;

	if ( w < 0 ) w = -w;
	if ( h < 0 ) h = -h;

	w = w + canz;
	h = h + canz;

	get_visible();

	if ( action == 0 )		// Clear marquee
	{
		j = marq_status;
		marq_status = 0;
		if ( j >= MARQUEE_PASTE && show_paste )
		{
			if ( new_x != marq_x1 || new_y != marq_y1 )
			{	// Only do something if there is a change
				if (	new_x2 < marq_x1 || new_x > marq_x2 ||
					new_y2 < marq_y1 || new_y > marq_y2	)
						repaint_canvas( margin_main_x + x, margin_main_y + y,
							w, h );	// Remove completely
				else
				{
					if ( new_x != marq_x1 )
					{	// Horizontal shift
						if ( new_x < marq_x1 )	// LEFT
						{
							ry = y; rh = h + zerror;
							rx = (new_x2 + 1) * can_zoom;
							rw = (marq_x2 - new_x2) * can_zoom + zerror;
						}
						else			// RIGHT
						{
							ry = y; rx = x; rh = h + zerror;
							rw = (new_x - marq_x1) * can_zoom + zerror;
						}
						clip_area( &rx, &ry, &rw, &rh );
						repaint_canvas( margin_main_x + rx, margin_main_y + ry,
							rw, rh );
					}
					if ( new_y != marq_y1 )
					{	// Vertical shift
						if ( new_y < marq_y1 )	// UP
						{
							rx = x; rw = w + zerror;
							ry = (new_y2 + 1) * can_zoom;
							rh = (marq_y2 - new_y2) * can_zoom + zerror;
						}
						else			// DOWN
						{
							rx = x; ry = y; rw = w + zerror;
							rh = (new_y - marq_y1) * can_zoom + zerror;
						}
						clip_area( &rx, &ry, &rw, &rh );
						repaint_canvas( margin_main_x + rx, margin_main_y + ry,
							rw, rh );
					}
				}
			}
		}
		else
		{
			repaint_canvas( margin_main_x + x, margin_main_y + y, 1, h );
			repaint_canvas(	margin_main_x + x+w-1-zerror/2, margin_main_y + y, 1+zerror, h );
			repaint_canvas(	margin_main_x + x, margin_main_y + y, w, 1 );
			repaint_canvas(	margin_main_x + x, margin_main_y + y+h-1-zerror/2, w, 1+zerror );
				// zerror required here to stop artifacts being left behind while dragging
				// a selection at the right/bottom edges
		}
		marq_status = j;
	}
	if ( action == 1 || action == 11 )		// Draw marquee
	{
		mtMAX( j, w, h )
		rgb = grab_memory( j*3, 255 );

		if ( marq_status >= MARQUEE_PASTE )
		{
			if ( action == 1 && show_paste )
			{	// Display paste RGB, only if not being called from repaint_canvas
				if ( new_x != marq_x1 || new_y != marq_y1 )
				{	// Only do something if there is a change in position
					update_paste_chunk( x1+1, y1+1,
						x2 + canz-2, y2 + canz-2 );
				}
			}
			for ( i=0; i<j; i++ )
			{
				rgb[ 0 + 3*i ] = 255 * ((i/3) % 2);
				rgb[ 1 + 3*i ] = 255 * ((i/3) % 2);
				rgb[ 2 + 3*i ] = 255;
			}
		}
		else
		{
			for ( i=0; i<j; i++ )
			{
				rgb[ 0 + 3*i ] = 255;
				rgb[ 1 + 3*i ] = 255 * ((i/3) % 2);
				rgb[ 2 + 3*i ] = 255 * ((i/3) % 2);
			}
		}

		rx = x; ry = y; rw = w; rh = h;
		clip_area( &rx, &ry, &rw, &rh );

		if ( rx != x ) offx = 3*( abs(rx - x) );
		if ( ry != y ) offy = 3*( abs(ry - y) );

		if ( (rx + rw) >= mem_width*can_zoom ) rw = mem_width*can_zoom - rx;
		if ( (ry + rh) >= mem_height*can_zoom ) rh = mem_height*can_zoom - ry;

		if ( x >= vc_x1 )
		{
			gdk_draw_rgb_image (the_canvas, drawing_canvas->style->black_gc,
				margin_main_x + rx, margin_main_y + ry,
				1, rh, GDK_RGB_DITHER_NONE, rgb + offy, 3 );
		}

		if ( (x+w-1) <= vc_x2 && (x+w-1) < mem_width*can_zoom )
		{
			gdk_draw_rgb_image (the_canvas, drawing_canvas->style->black_gc,
				margin_main_x + rx+rw-1, margin_main_y + ry,
				1, rh, GDK_RGB_DITHER_NONE, rgb + offy, 3 );
		}

		if ( y >= vc_y1 )
		{
			gdk_draw_rgb_image (the_canvas, drawing_canvas->style->black_gc,
				margin_main_x + rx, margin_main_y + ry,
				rw, 1, GDK_RGB_DITHER_NONE, rgb + offx, 3*j );
		}

		if ( (y+h-1) <= vc_y2 && (y+h-1) < mem_height*can_zoom )
		{
			gdk_draw_rgb_image (the_canvas, drawing_canvas->style->black_gc,
				margin_main_x + rx, margin_main_y + ry+rh-1,
				rw, 1, GDK_RGB_DITHER_NONE, rgb + offx, 3*j );
		}

		free(rgb);
	}
}

int close_to( int x1, int y1 )		// Which corner of selection is coordinate closest to?
{
	int distance, xx[2], yy[2], i, closest[2];

	mtMIN( xx[0], marq_x1, marq_x2 )
	mtMAX( xx[1], marq_x1, marq_x2 )
	mtMIN( yy[0], marq_y1, marq_y2 )
	mtMAX( yy[1], marq_y1, marq_y2 )

	closest[0] = 0;
	closest[1] = (x1 - xx[0]) * (x1 - xx[0]) + (y1 - yy[0]) * (y1 - yy[0]);
	for ( i=1; i<4; i++ )
	{
		distance =	( x1 - xx[i % 2] ) * ( x1 - xx[i % 2] ) + 
				( y1 - yy[i / 2] ) * ( y1 - yy[i / 2] );
		if ( distance < closest[1] )
		{
			closest[0] = i;
			closest[1] = distance;
		}
	}

	return closest[0];
}

void update_sel_bar()			// Update selection stats on status bar
{
	char txt[64];
	int x1, y1, x2, y2;
	float lang = 0, llen = 1;

	if ( (tool_type == TOOL_SELECT || tool_type == TOOL_POLYGON) )
	{
		if ( marq_status > MARQUEE_NONE )
		{
			mtMIN( x1, marq_x1, marq_x2 )
			mtMIN( y1, marq_y1, marq_y2 )
			mtMAX( x2, marq_x1, marq_x2 )
			mtMAX( y2, marq_y1, marq_y2 )
			if ( status_on[STATUS_SELEGEOM] )
			{
				if ( x1==x2 )
				{
					if ( marq_y1 < marq_y2 ) lang = 180;
				}
				else
				{
					lang = 90 + 180*atan( ((float) marq_y1 - marq_y2) /
						(marq_x1 - marq_x2) ) / M_PI;
					if ( marq_x1 > marq_x2 )
						lang = lang - 180;
				}

				llen = sqrt( (x2-x1+1)*(x2-x1+1) + (y2-y1+1)*(y2-y1+1) );

				snprintf(txt, 60, "  %i,%i : %i x %i   %.1f' %.1f\"",
					x1, y1, x2-x1+1, y2-y1+1, lang, llen);
				gtk_label_set_text( GTK_LABEL(label_bar[STATUS_SELEGEOM]), txt );
			}
		}
		else
		{
			if ( tool_type == TOOL_POLYGON )
			{
				snprintf(txt, 60, "  (%i)", poly_points);
				if ( poly_status != POLY_DONE ) strcat(txt, "+");
				if ( status_on[STATUS_SELEGEOM] )
					gtk_label_set_text( GTK_LABEL(label_bar[STATUS_SELEGEOM]), txt );
			}
			else if ( status_on[STATUS_SELEGEOM] )
					gtk_label_set_text( GTK_LABEL(label_bar[STATUS_SELEGEOM]), "" );
		}
	}
	else if ( status_on[STATUS_SELEGEOM] )
			gtk_label_set_text( GTK_LABEL(label_bar[STATUS_SELEGEOM]), "" );
}


void repaint_line(int mode)			// Repaint or clear line on canvas
{
	png_color pcol;
	int i, j, pixy = 1, xdo, ydo, px, py, todo, todor;
	int minx, miny, xw, yh, canz = can_zoom, canz2 = 1;
	int lx1, ly1, lx2, ly2,
		ax=-1, ay=-1, bx, by, aw, ah;
	float rat;
	char *rgb;
	gboolean do_redraw = FALSE;

	if ( canz<1 )
	{
		canz = 1;
		canz2 = mt_round(1/can_zoom);
	}
	pixy = canz*canz;
	lx1 = line_x1;
	ly1 = line_y1;
	lx2 = line_x2;
	ly2 = line_y2;

	xdo = abs(lx2 - lx1);
	ydo = abs(ly2 - ly1);
	mtMAX( todo, xdo, ydo )

	mtMIN( minx, lx1, lx2 )
	mtMIN( miny, ly1, ly2 )
	minx = minx * canz;
	miny = miny * canz;
	xw = (xdo + 1)*canz;
	yh = (ydo + 1)*canz;
	get_visible();
	clip_area( &minx, &miny, &xw, &yh );

	mtMIN( lx1, lx1 / canz2, mem_width / canz2 - 1 )
	mtMIN( ly1, ly1 / canz2, mem_height / canz2 - 1 )
	mtMIN( lx2, lx2 / canz2, mem_width / canz2 - 1 )
	mtMIN( ly2, ly2 / canz2, mem_height / canz2 - 1 )
	todo = todo / canz2;

	if ( todo == 0 ) todor = 1; else todor = todo;
	rgb = grab_memory( pixy*3, 255 );

	for ( i=0; i<=todo; i++ )
	{
		rat = ((float) i ) / todor;
		px = mt_round(lx1 + (lx2 - lx1) * rat);
		py = mt_round(ly1 + (ly2 - ly1) * rat);

		if ( (px+1)*canz > vc_x1 && (py+1)*canz > vc_y1 &&
			px*canz <= vc_x2 && py*canz <= vc_y2 )
		{
			if ( mode == 2 )
			{
				pcol.red   = 255*( (todo-i)/4 % 2 );
				pcol.green = pcol.red;
				pcol.blue  = pcol.red;
			}
			if ( mode == 1 )
			{
				pcol.red   = mem_col_pat24[     3*((px % 8) + 8*(py % 8)) ];
				pcol.green = mem_col_pat24[ 1 + 3*((px % 8) + 8*(py % 8)) ];
				pcol.blue  = mem_col_pat24[ 2 + 3*((px % 8) + 8*(py % 8)) ];
			}

			if ( mode == 0 )
			{
				if ( ax<0 )	// 1st corner of repaint rectangle
				{
					ax = px;
					ay = py;
				}
				do_redraw = TRUE;
			}
			else
			{
				for ( j=0; j<pixy; j++ )
				{
					rgb[ 3*j ] = pcol.red;
					rgb[ 1 + 3*j ] = pcol.green;
					rgb[ 2 + 3*j ] = pcol.blue;
				}
				gdk_draw_rgb_image (the_canvas, drawing_canvas->style->black_gc,
					margin_main_x + px*canz, margin_main_y + py*canz,
					canz, canz,
					GDK_RGB_DITHER_NONE, rgb, 3*canz );
			}
		}
		else
		{
			if ( ax>=0 && mode==0 ) do_redraw = TRUE;
		}
		if ( do_redraw )
		{
			do_redraw = FALSE;
			bx = px;	// End corner
			by = py;
			aw = canz * (1 + abs(bx-ax));	// Width of rectangle on canvas
			ah = canz * (1 + abs(by-ay));
			if ( aw>16 || ah>16 || i==todo )
			{ // Commit canvas clear if >16 pixels or final pixel of this line
				mtMIN( ax, ax, bx )
				mtMIN( ay, ay, by )
				repaint_canvas( margin_main_x + ax*canz, margin_main_y + ay*canz,
					aw, ah );
				ax = -1;
			}
		}
	}
	free(rgb);
}

void init_status_bar()
{
	char txt[64];

	update_status_bar1();
	if ( !status_on[STATUS_GEOMETRY] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_GEOMETRY]), "" );

	if ( status_on[STATUS_CURSORXY] ) gtk_widget_set_usize(label_bar[STATUS_CURSORXY], 90, -2);
	else
	{
		gtk_widget_set_usize(label_bar[STATUS_CURSORXY], 0, -2);
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_CURSORXY]), "" );
	}

	if ( !status_on[STATUS_PIXELRGB] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_PIXELRGB]), "" );

	if ( !status_on[STATUS_SELEGEOM] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_SELEGEOM]), "" );

	if ( status_on[STATUS_UNDOREDO] )
	{
		sprintf(txt, "%i+%i", mem_undo_done, mem_undo_redo);
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_UNDOREDO]), txt );
		gtk_widget_set_usize(label_bar[STATUS_UNDOREDO], 50, -2);
	}
	else
	{
		gtk_widget_set_usize(label_bar[STATUS_UNDOREDO], 0, -2);
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_UNDOREDO]), "" );
	}
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
	if (c != NULL)
	{
		txt[0] = c[1];
		c[1] = 0;
	}
	inifile_set("last_dir", filename);		// Strip off filename
	if (c != NULL) c[1] = txt[0];

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
	if ( d == 1 ) zoom_in( NULL, NULL ); else zoom_out( NULL, NULL );
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
