/*	ani.c
	Copyright (C) 2005-2008 Mark Tyler and Dmitry Groshev

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

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <stdio.h>

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "ani.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "canvas.h"
#include "viewer.h"
#include "inifile.h"
#include "layer.h"
#include "mtlib.h"
#include "wu.h"

#ifdef WIN32
#ifndef WEXITSTATUS
#define WEXITSTATUS(A) ((A) & 0xFF)
#endif
#else
#include <sys/wait.h>
#endif

///	GLOBALS

int	ani_frame1 = 1, ani_frame2 = 1, ani_gif_delay = 10, ani_play_state = FALSE,
	ani_timer_state = 0;



///	FORM VARIABLES

static GtkWidget *animate_window = NULL, *ani_prev_win,
	*ani_entry_path, *ani_entry_prefix,
	*ani_spin[5],			// start, end, delay
	*ani_text_pos, *ani_text_cyc,	// Text input widgets
	*ani_prev_slider		// Slider widget on preview area
	;

#define MAX_CYC_ITEMS 50
typedef struct {
	int frame0, frame1, len, layers[MAX_CYC_ITEMS];
} ani_cycle;

static ani_cycle ani_cycle_table[MAX_CYC_SLOTS];

static int
	ani_layer_data[MAX_LAYERS + 1][4],	// x, y, opacity, visible
	ani_currently_selected_layer;
static char ani_output_path[PATHBUF], ani_file_prefix[ANI_PREFIX_LEN+2];
static gboolean ani_use_gif, ani_show_main_state;



static void ani_win_read_widgets();




static void ani_widget_changed()	// Widget changed so flag the layers as changed
{
	layers_changed = 1;
}



static void ani_cyc_len_init()		// Initialize the cycle length array before doing any animating
{
	int i, k;

	for (i = 0; i < MAX_CYC_SLOTS; i++)
	{
		if (!ani_cycle_table[i].frame0) break;	// Last slot reached
		for (k = 0; (k < MAX_CYC_ITEMS) && ani_cycle_table[i].layers[k]; k++);
		// Must be a minimum of 1 for modulo use
		ani_cycle_table[i].len = k ? k : 1;
	}
}

static void set_layer_from_slot( int layer, int slot )		// Set layer x, y, opacity from slot
{
	ani_slot *ani = layer_table[layer].image->ani_pos + slot;
	layer_table[layer].x = ani->x;
	layer_table[layer].y = ani->y;
	layer_table[layer].opacity = ani->opacity;
}

static void set_layer_inbetween( int layer, int i, int frame, int effect )		// Calculate in between value for layer from slot i & (i+1) at given frame
{
	MT_Coor c[4], co_res, lenz;
	float p1, p2;
	int f0, f1, f2, f3, ii[4] = {i-1, i, i+1, i+2}, j;
	ani_slot *ani = layer_table[layer].image->ani_pos;


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

static void ani_set_frame_state( int frame )
{
	int i, k, e, a, b, done, l;
	ani_slot *ani;

	for ( k=1; k<=layers_total; k++ )	// Set x, y, opacity for each layer
	{
		ani = layer_table[k].image->ani_pos;
		if (ani[0].frame > 0)
		{
			for ( i=0; i<MAX_POS_SLOTS; i++ )		// Find first frame in position list that excedes or equals 'frame'
			{
				if (ani[i].frame <= 0) break;		// End of list
				if (ani[i].frame >= frame) break;	// Exact match or one exceding it found
			}

			if ( i>=MAX_POS_SLOTS )		// All position slots < 'frame'
			{
				set_layer_from_slot( k, MAX_POS_SLOTS - 1 );
					// Set layer pos/opac to last slot values
			}
			else
			{
				if (ani[i].frame == 0)		// All position slots < 'frame'
				{
					set_layer_from_slot( k, i - 1 );
						// Set layer pos/opac to last slot values
				}
				else
				{
					if (ani[i].frame == frame || i == 0)
					{
						set_layer_from_slot( k, i );
							// If closest frame = requested frame, set all values to this
							// ditto if i=0, i.e. no better matches exist
					}
					else
					{
						// i is currently pointing to slot that excedes 'frame', so in between this and the previous slot
						set_layer_inbetween( k, i-1, frame, ani[i - 1].effect );
					}
				}
			}
		}	// If no slots have been defined leave the layer x, y, opacity as now
	}



	// Set visibility for each layer by processing cycle table

	for ( i=0; i<MAX_CYC_SLOTS; i++ )
	{
		a = ani_cycle_table[i].frame0;
		b = ani_cycle_table[i].frame1;
		if (!a) break;		// End of list reached

		if ( a==b && a<=frame )		// Special case for enabling/disabling en-masse
		{
			for (k = 0; k < MAX_CYC_ITEMS; k++)
			{
				e = ani_cycle_table[i].layers[k];
				if ( e==0 ) break;		// End delimeter encountered so stop
				if ( e<0 )
				{
					if ( (-e) <= layers_total )		// If valid, hide layer
						layer_table[-e].visible = FALSE;
				}
				if ( e>0 )
				{
					if ( e <= layers_total )		// If valid, show layer
						layer_table[e].visible = TRUE;
				}
			}
		}
		if ( a<b && a<=frame && frame<=b )	// Frame is between these points so act
		{
			done = -1;
			l = ani_cycle_table[i].len;
			for (k = 0; k < MAX_CYC_ITEMS; k++)
			{
				e = ani_cycle_table[i].layers[k];
				if ( e==0 ) break;		// End delimeter encountered so stop
				if ( e>0 && e<=layers_total && e!=done )
				{
					if ((frame - a) % l == k)
					{
						layer_table[e].visible = TRUE;
						done = e;
						// Don't switch this off later in loop
					}
					else
						layer_table[e].visible = FALSE;
					// Switch layer on or off according to frame position in cycle
				}
			}
		}
	}
}





static void ani_read_layer_data()		// Read current layer x/y/opacity data to table
{
	int i;

	for ( i=0; i<=MAX_LAYERS; i++ )
	{
		ani_layer_data[i][0] = layer_table[i].x;
		ani_layer_data[i][1] = layer_table[i].y;
		ani_layer_data[i][2] = layer_table[i].opacity;
		ani_layer_data[i][3] = layer_table[i].visible;
	}
}

static void ani_write_layer_data()		// Write current layer x/y/opacity data from table
{
	int i;

	for ( i=0; i<=MAX_LAYERS; i++ )
	{
		layer_table[i].x       = ani_layer_data[i][0];
		layer_table[i].y       = ani_layer_data[i][1];
		layer_table[i].opacity = ani_layer_data[i][2];
		layer_table[i].visible = ani_layer_data[i][3];
	}
}


static char *text_edit_widget_get(GtkWidget *w)		// Get text string from input widget
		// WARNING memory allocated for this so lose it with g_free(txt)
{
#if GTK_MAJOR_VERSION == 1
	return gtk_editable_get_chars( GTK_EDITABLE(w), 0, -1 );
#endif
#if GTK_MAJOR_VERSION == 2
	GtkTextIter begin, end;
	GtkTextBuffer *buffer = GTK_TEXT_VIEW(w)->buffer;

	gtk_text_buffer_get_start_iter( buffer, &begin );
	gtk_text_buffer_get_end_iter( buffer, &end );
	return gtk_text_buffer_get_text( buffer, &begin, &end, -1 );
#endif
}

static void empty_text_widget(GtkWidget *w)	// Empty the text widget
{
#if GTK_MAJOR_VERSION == 1
	gtk_text_set_point( GTK_TEXT(w), 0 );
	gtk_text_forward_delete( GTK_TEXT(w), gtk_text_get_length(GTK_TEXT(w)) );
#endif
#if GTK_MAJOR_VERSION == 2
	gtk_text_buffer_set_text( GTK_TEXT_VIEW(w)->buffer, "", 0 );
#endif
}

static void ani_cyc_refresh_txt()		// Refresh the text in the cycle text widget
{
	int i, j, k;
	char txt[128 + MAX_CYC_ITEMS * 6], *tmp;
#if GTK_MAJOR_VERSION == 2
	GtkTextIter iter;

	g_signal_handlers_disconnect_by_func( GTK_TEXT_VIEW(ani_text_cyc)->buffer,
			GTK_SIGNAL_FUNC(ani_widget_changed), NULL );
#endif
	empty_text_widget(ani_text_cyc);	// Clear the text in the widget

	for (i = 0; i < MAX_CYC_SLOTS; i++)
	{
		if (!ani_cycle_table[i].frame0) break;
		tmp = txt + sprintf(txt, "%i\t%i\t%i", ani_cycle_table[i].frame0,
			ani_cycle_table[i].frame1, ani_cycle_table[i].layers[0]);
		for (j = 1; j < MAX_CYC_ITEMS; j++)
		{
			k = ani_cycle_table[i].layers[j];
			if (!k) break;
			tmp += sprintf(tmp, ",%i", k);
		}
		strcpy(tmp, "\n");
#if GTK_MAJOR_VERSION == 1
		gtk_text_insert (GTK_TEXT (ani_text_cyc), NULL, NULL, NULL, txt, -1);
#endif
#if GTK_MAJOR_VERSION == 2
		gtk_text_buffer_get_end_iter( GTK_TEXT_VIEW(ani_text_cyc)->buffer, &iter );
		gtk_text_buffer_insert( GTK_TEXT_VIEW(ani_text_cyc)->buffer, &iter, txt, -1 );
#endif
	}

#if GTK_MAJOR_VERSION == 2
	g_signal_connect( GTK_TEXT_VIEW(ani_text_cyc)->buffer, "changed",
			GTK_SIGNAL_FUNC(ani_widget_changed), NULL );
	// We have to switch off then back on or it looks like the user changed it
#endif
}

static void ani_pos_refresh_txt()		// Refresh the text in the position text widget
{
	char txt[256];
	int i = ani_currently_selected_layer, j;
	ani_slot *ani;
#if GTK_MAJOR_VERSION == 2
	GtkTextIter iter;

	g_signal_handlers_disconnect_by_func( GTK_TEXT_VIEW(ani_text_pos)->buffer,
		GTK_SIGNAL_FUNC(ani_widget_changed), NULL );
#endif

	empty_text_widget(ani_text_pos);	// Clear the text in the widget

	if ( i > 0 )		// Must no be for background layer or negative => PANIC!
	{
		for (j = 0; j < MAX_POS_SLOTS; j++)
		{
			ani = layer_table[i].image->ani_pos + j;
			if (ani->frame <= 0) break;
			// Add a line if one exists
			snprintf(txt, 250, "%i\t%i\t%i\t%i\t%i\n",
				ani->frame, ani->x, ani->y, ani->opacity, ani->effect);
#if GTK_MAJOR_VERSION == 1
			gtk_text_insert (GTK_TEXT (ani_text_pos), NULL, NULL, NULL, txt, -1);
#endif
#if GTK_MAJOR_VERSION == 2
			gtk_text_buffer_get_end_iter( GTK_TEXT_VIEW(ani_text_pos)->buffer, &iter );
			gtk_text_buffer_insert( GTK_TEXT_VIEW(ani_text_pos)->buffer, &iter, txt,
				strlen(txt) );

#endif
		}
	}
#if GTK_MAJOR_VERSION == 2
	g_signal_connect( GTK_TEXT_VIEW(ani_text_pos)->buffer, "changed",
		GTK_SIGNAL_FUNC(ani_widget_changed), NULL );
	// We have to switch off then back on or it looks like the user changed it
#endif
}

void ani_init()			// Initialize variables/arrays etc. before loading or on startup
{
	int j;

	ani_frame1 = 1;
	ani_frame2 = 100;
	ani_gif_delay = 10;

	ani_cycle_table[0].frame0 = 0;

	if ( layers_total>0 )		// No position array malloc'd until layers>0
	{
		for (j = 0; j <= layers_total; j++)
			layer_table[j].image->ani_pos[0].frame = 0;
	}

	strcpy(ani_output_path, "frames");
	strcpy(ani_file_prefix, "f");

	ani_use_gif = TRUE;
}



///	EXPORT ANIMATION FRAMES WINDOW


static void ani_win_set_pos()
{
	win_restore_pos(animate_window, "ani", 0, 0, 200, 200);
}

static void ani_fix_pos()
{
	ani_read_layer_data();
	layers_notify_changed();
}

static void ani_but_save()
{
	ani_win_read_widgets();
	ani_write_layer_data();
	layer_press_save();
}

static void delete_ani()
{
	win_store_pos(animate_window, "ani");
	ani_win_read_widgets();
	gtk_widget_destroy(animate_window);
	animate_window = NULL;
	ani_write_layer_data();
	layers_pastry_cut = FALSE;

	show_layers_main = ani_show_main_state;
	update_all_views();
}


static int parse_line_pos( char *txt, int layer, int row )	// Read in position row from some text
{
	ani_slot data = { -1, -1, -1, -1, -1 };
	char *tx, *eol;
	int tot;

	tx = strchr( txt, '\n' );			// Find out length of this input line
	if ( tx == NULL ) tot = strlen(txt);
	else tot = tx - txt + 1;
	eol = txt + tot - 1;

	while ( txt[0] < 32 )				// Skip non ascii chars
	{
		if ( txt[0] == 0 ) return -1;		// If we reach the end, tell the caller
		txt = txt + 1;
	}
	sscanf(txt, "%i\t%i\t%i\t%i\t%i", &data.frame, &data.x, &data.y,
		&data.opacity, &data.effect);

	layer_table[layer].image->ani_pos[row] = data;

	return tot;
}

static void ani_parse_store_positions()		// Read current positions in text input and store
{
	char *txt = text_edit_widget_get( ani_text_pos ), *tx;
	int i, j, layer = ani_currently_selected_layer;

	tx = txt;

	for ( i=0; i<MAX_POS_SLOTS; i++ )
	{
		j = parse_line_pos( txt, layer, i );
		if ( j<0 ) break;
		txt += j;
	}
	if ( i<MAX_POS_SLOTS ) layer_table[layer].image->ani_pos[i].frame = 0;	// End delimeter

	g_free(tx);
}

static int parse_line_cyc( char *txt, int row )		// Read in cycle row from some text
{
	char *tx, *eol;
	int a=-1, b=-1, c=-1, tot, i;

	tx = strchr( txt, '\n' );			// Find out length of this input line
	if ( tx == NULL ) tot = strlen(txt);
	else tot = tx - txt + 1;

	eol = txt + tot - 1;

	while ( txt[0] < 32 )				// Skip non ascii chars
	{
		if ( txt[0] == 0 ) return -1;		// If we reach the end, tell the caller
		txt = txt + 1;
	}
	sscanf( txt, "%i\t%i\t%i", &a, &b, &c );
	ani_cycle_table[row].frame0 = a;
	ani_cycle_table[row].frame1 = b;
	ani_cycle_table[row].layers[0] = c;

	// Read in a number after each comma
	for (i = 1; i < MAX_CYC_ITEMS; i++)
	{
		tx = strchr( txt, ',' );		// Get next comma
		if (!tx || (eol - tx < 2)) break;	// Bail out if no comma on this line
		a = -1;
		sscanf(tx + 1, "%i", &a);
		ani_cycle_table[row].layers[i] = a;
		txt = tx + 1;
	}
	// Terminate with zero if needed
	if (i < MAX_CYC_ITEMS) ani_cycle_table[row].layers[i] = 0;

	return tot;
}

static void ani_parse_store_cycles()		// Read current cycles in text input and store
{
	char *txt = text_edit_widget_get( ani_text_cyc ), *tx;
	int i, j;

	tx = txt;

	for ( i=0; i<MAX_CYC_SLOTS; i++ )
	{
		j = parse_line_cyc( txt, i );
		if ( j<0 ) break;
		txt += j;
	}
	if ( i<MAX_CYC_SLOTS ) ani_cycle_table[i].frame0 = 0;	// End delimeter

	g_free(tx);
}

static void ani_win_read_widgets()		// Read all widgets and set up relevant variables
{
	int	a = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(ani_spin[0]) ),
		b = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(ani_spin[1]) );


	ani_gif_delay = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(ani_spin[2]) );

	ani_parse_store_positions();
	ani_parse_store_cycles();
	ani_pos_refresh_txt();		// Update 2 text widgets
	ani_cyc_refresh_txt();

	ani_frame1 = a < b ? a : b;
	ani_frame2 = a < b ? b : a;
	gtkncpy(ani_output_path, gtk_entry_get_text(GTK_ENTRY(ani_entry_path)), PATHBUF);
	gtkncpy(ani_file_prefix, gtk_entry_get_text(GTK_ENTRY(ani_entry_prefix)), ANI_PREFIX_LEN + 1);
	// GIF toggle is automatically set by callback
}


static gboolean ani_play_timer_call()
{
	int i;

	if ( ani_play_state == 0 )
	{
		ani_timer_state = 0;
		return FALSE;			// Stop animating
	}
	else
	{
		i = ADJ2INT(SPINSLIDE_ADJUSTMENT(ani_prev_slider)) + 1;
		if (i > ani_frame2) i = ani_frame1;
		mt_spinslide_set_value(ani_prev_slider, i);
		return TRUE;
	}
}

static void ani_play_start()
{
	if ( ani_play_state == 0 )
	{
		ani_play_state = 1;
		if ( ani_timer_state == 0 )
			ani_timer_state = g_timeout_add( ani_gif_delay*10, ani_play_timer_call, NULL );
	}
}

static void ani_play_stop()
{
	ani_play_state = 0;
}


///	PREVIEW WINDOW CALLBACKS

static void ani_but_playstop(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (gtk_toggle_button_get_active(togglebutton)) ani_play_start();
	else ani_play_stop();
}

static void ani_frame_slider_moved(GtkAdjustment *adjustment, gpointer user_data)
{
	int x = 0, y = 0, w = mem_width, h = mem_height;

	ani_set_frame_state(ADJ2INT(adjustment));

	if (layer_selected)
	{
		x = -layer_table[layer_selected].x;
		y = -layer_table[layer_selected].y;
		w = layer_table[0].image->image_.width;
		h = layer_table[0].image->image_.height;
	}

	vw_update_area(x, y, w, h);	// Update only the area we need
}

static void ani_but_preview_close()
{
	ani_play_stop();				// Stop animation playing if necessary

	win_store_pos(ani_prev_win, "ani_prev");
	gtk_widget_destroy( ani_prev_win );

	if ( animate_window != NULL )
	{
		ani_win_set_pos();
		gtk_widget_show (animate_window);
	}
	else
	{
		ani_write_layer_data();
		layers_pastry_cut = FALSE;
		update_all_views();
	}
}

void ani_but_preview()
{
	GtkWidget *hbox3, *button;
	GtkAccelGroup* ag = gtk_accel_group_new();


	if ( animate_window != NULL )
	{
		/* We need to remember this as we are hiding it */
		win_store_pos(animate_window, "ani");
		ani_win_read_widgets();		// Get latest values for the preview
	}
	else	ani_read_layer_data();

	ani_cyc_len_init();			// Prepare the cycle index for the animation

	if ( !view_showing ) view_show();	// If not showing, show the view window

	ani_prev_win = add_a_window( GTK_WINDOW_TOPLEVEL,
			_("Animation Preview"), GTK_WIN_POS_NONE, TRUE );
	gtk_container_set_border_width(GTK_CONTAINER(ani_prev_win), 5);

	win_restore_pos(ani_prev_win, "ani_prev", 0, 0, 200, -1);

	hbox3 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox3);
	gtk_container_add (GTK_CONTAINER (ani_prev_win), hbox3);

	pack(hbox3, sig_toggle_button(_("Play"), FALSE, NULL,
		GTK_SIGNAL_FUNC(ani_but_playstop)));
	ani_play_state = FALSE;			// Stopped

	ani_prev_slider = mt_spinslide_new(-2, -2);
	xpack(hbox3, widget_align_minsize(ani_prev_slider, 200, -2));
	mt_spinslide_set_range(ani_prev_slider, ani_frame1, ani_frame2);
	mt_spinslide_set_value(ani_prev_slider, ani_frame1);
	mt_spinslide_connect(ani_prev_slider,
		GTK_SIGNAL_FUNC(ani_frame_slider_moved), NULL);

	if ( animate_window == NULL )	// If called via the menu have a fix button
	{
		button = add_a_button( _("Fix"), 5, hbox3, FALSE );
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(ani_fix_pos), NULL);
	}

	button = add_a_button( _("Close"), 5, hbox3, FALSE );
	gtk_signal_connect(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(ani_but_preview_close), NULL);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_signal_connect (GTK_OBJECT (ani_prev_win), "delete_event",
			GTK_SIGNAL_FUNC(ani_but_preview_close), NULL);

	gtk_window_set_transient_for( GTK_WINDOW(ani_prev_win), GTK_WINDOW(main_window) );
	gtk_widget_show (ani_prev_win);
	gtk_window_add_accel_group(GTK_WINDOW (ani_prev_win), ag);

	if ( animate_window != NULL ) gtk_widget_hide (animate_window);
	else
	{
		layers_pastry_cut = TRUE;
		update_all_views();
	}

	gtk_adjustment_value_changed(SPINSLIDE_ADJUSTMENT(ani_prev_slider));
}

static void create_frames_ani()
{
	image_info *image;
	image_state *state;
	ls_settings settings;
	png_color pngpal[256], *trans;
	unsigned char *layer_rgb, *irgb = NULL;
	char output_path[PATHBUF], *command, *wild_path;
	int a, b, k, i, cols, layer_w, layer_h, npt, l = 0;


	ani_win_read_widgets();
	ani_cyc_len_init();		// Prepare the cycle index for the animation

	gtk_widget_hide(animate_window);

	ani_write_layer_data();
	layer_press_save();		// Save layers data file

	layers_filename[0] = 0;
	command = strrchr(layers_filename, DIR_SEP);
	if (command) l = command - layers_filename + 1;
//	strncpy0(output_path, layers_filename, l + 1);

	if (ani_output_path[0])	// Output path used?
	{
		snprintf(output_path, PATHBUF, "%.*s%s",
			l, layers_filename, ani_output_path);

#ifdef WIN32
		if (mkdir(output_path))
#else
		if (mkdir(output_path, 0777))
#endif
		{
			if ( errno != EEXIST )
			{
				alert_box(_("Error"), _("Unable to create output directory"),
					_("OK"), NULL, NULL );
				goto failure;			// Failure to create directory
			}
		}
	}

		// Create output path and pointer for first char of filename

	a = ani_frame1 < ani_frame2 ? ani_frame1 : ani_frame2;
	b = ani_frame1 < ani_frame2 ? ani_frame2 : ani_frame1;

	if (layer_selected == 0) image = &mem_image , state = &mem_state;
	else image = &layer_table[0].image->image_ , state = &layer_table[0].image->state_;

	layer_w = image->width;
	layer_h = image->height;
	layer_rgb = malloc( layer_w * layer_h * 3);	// Primary layer image for RGB version

	if (!layer_rgb)
	{
		memory_errors(1);
		goto failure;
	}

	/* Prepare settings */
	init_ls_settings(&settings, NULL);
	settings.mode = FS_COMPOSITE_SAVE;
	settings.width = layer_w;
	settings.height = layer_h;
	settings.colors = 256;
	settings.silent = TRUE;
	if (ani_use_gif)
	{
		irgb = malloc(layer_w * layer_h);	// Resulting indexed image
		if (!irgb)
		{
			free(layer_rgb);
			memory_errors(1);
			goto failure;
		}
		settings.ftype = FT_GIF;
		settings.img[CHN_IMAGE] = irgb;
		settings.bpp = 1;
		settings.pal = pngpal;
	}
	else
	{
		settings.ftype = FT_PNG;
		settings.img[CHN_IMAGE] = layer_rgb;
		settings.bpp = 3;
		/* Background transparency */
		settings.xpm_trans = state->xpm_trans;
		settings.rgb_trans = settings.xpm_trans < 0 ? -1 :
			PNG_2_INT(image->pal[settings.xpm_trans]);
	}

	progress_init(_("Creating Animation Frames"), 1);
	for ( k=a; k<=b; k++ )			// Create each frame and save it as a PNG or GIF image
	{
		if (progress_update(b == a ? 0.0 : (k - a) / (float)(b - a)))
			break;

		ani_set_frame_state(k);		// Change layer positions
		view_render_rgb( layer_rgb, 0, 0, layer_w, layer_h, 1 );	// Render layer

		snprintf(output_path, PATHBUF, "%.*s%s%c%s%05d.%s", 
			l, layers_filename, ani_output_path, DIR_SEP,
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
			if (state->xpm_trans >= 0)	// Background has transparency
			{
				trans = image->pal + state->xpm_trans;
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

		if (save_image(output_path, &settings) < 0)
		{
			alert_box( _("Error"), _("Unable to save image"), _("OK"), NULL, NULL );
			goto failure2;
		}
	}

	if ( ani_use_gif )	// all GIF files created OK so lets give them to gifsicle
	{
		snprintf(output_path, PATHBUF, "%.*s%s%c%s?????.gif",
			l, layers_filename, ani_output_path, DIR_SEP,
			ani_file_prefix);
		wild_path = quote_spaces(output_path);

		command = g_strdup_printf("%s -d %i %s -o \"%.*s%s%c%s.gif\"",
			GIFSICLE_CREATE,
			ani_gif_delay, wild_path,
			l, layers_filename, ani_output_path, DIR_SEP,
			ani_file_prefix);
		gifsicle(command);
		g_free(command);
		free(wild_path);

#ifndef WIN32
		command = g_strdup_printf("gifview -a \"%.*s%s%c%s.gif\" &",
			l, layers_filename, ani_output_path, DIR_SEP,
			ani_file_prefix);
		gifsicle(command);
		g_free(command);
#endif
	}

failure2:
	progress_end();
	free( layer_rgb );

failure:
	free( irgb );

	gtk_widget_show(animate_window);
}

void pressed_remove_key_frames()
{
	int i, j;

	i = alert_box( _("Warning"), _("Do you really want to clear all of the position and cycle data for all of the layers?"), _("No"), _("Yes"), NULL );
	if ( i==2 )
	{
		for (j = 0; j <= layers_total; j++)
			layer_table[j].image->ani_pos[0].frame = 0;
							// Flush array in each layer
		ani_cycle_table[0].frame0 = 0;
	}
}

static void ani_set_key_frame(int key)		// Set key frame postions & cycles as per current layers
{
	ani_slot *ani;
	int i, j, k, l;


	for ( k=1; k<=layers_total; k++ )	// Add current position for each layer
	{
		ani = layer_table[k].image->ani_pos;
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

	//  Shift remaining data down a slot
	for ( j=MAX_CYC_SLOTS-1; j>i; j-- )
		ani_cycle_table[j] = ani_cycle_table[j - 1];

	//  Enter data for the current state
	ani_cycle_table[i].frame0 = ani_cycle_table[i].frame1 = key;
	for ( j=1; j<=layers_total; j++ )
	{
		if (j > MAX_CYC_ITEMS) break;	// More layers than free items so bail out
		if ( layer_table[j].visible ) l=j; else l=-j;
		ani_cycle_table[i].layers[j - 1] = l;
	}
	// Add terminator if needed
	if (j <= MAX_CYC_ITEMS) ani_cycle_table[i].layers[j - 1] = 0;
}

static void ani_tog_gif(GtkToggleButton *togglebutton, gpointer user_data)
{
	ani_use_gif = gtk_toggle_button_get_active(togglebutton);
	ani_widget_changed();
}

static void ani_layer_select( GtkList *list, GtkWidget *widget )
{
	int j = layers_total - gtk_list_child_position(list, widget);

	if ( j<1 || j>layers_total ) return;		// Item not found

	if ( ani_currently_selected_layer != -1 )	// Only if not first click
	{
		ani_parse_store_positions();		// Parse & store text inputs
	}

	ani_currently_selected_layer = j;
	ani_pos_refresh_txt();				// Refresh the text in the widget
}

static int do_set_key_frame(GtkWidget *spin, gpointer fdata)
{
	int i;

	i = read_spin(spin);
	ani_set_key_frame(i);
	layers_notify_changed();

	return TRUE;
}

void pressed_set_key_frame()
{
	GtkWidget *spin = add_a_spin(ani_frame1, ani_frame1, ani_frame2);
	filter_window(_("Set Key Frame"), spin, do_set_key_frame, NULL, FALSE);
}

static GtkWidget *ani_text(GtkWidget **textptr)
{
	GtkWidget *scroll, *text;

#if GTK_MAJOR_VERSION == 1
	text = gtk_text_new(NULL, NULL);
	gtk_text_set_editable(GTK_TEXT(text), TRUE);

	gtk_signal_connect(GTK_OBJECT(text), "changed",
			GTK_SIGNAL_FUNC(ani_widget_changed), NULL);

	scroll = gtk_scrolled_window_new(NULL, GTK_TEXT(text)->vadj);
#else /* #if GTK_MAJOR_VERSION == 2 */
	GtkTextBuffer *texbuf = gtk_text_buffer_new(NULL);

	text = gtk_text_view_new_with_buffer(texbuf);

	g_signal_connect(texbuf, "changed", GTK_SIGNAL_FUNC(ani_widget_changed), NULL);

	scroll = gtk_scrolled_window_new(GTK_TEXT_VIEW(text)->hadjustment,
		GTK_TEXT_VIEW(text)->vadjustment);
#endif
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll), text);

	*textptr = text;
	return (scroll);
}

void pressed_animate_window()
{
	GtkWidget *table, *label, *button, *notebook1, *scrolledwindow;
	GtkWidget *ani_toggle_gif, *ani_list_layers, *list_data;
	GtkWidget *hbox4, *hbox2, *vbox1, *vbox3, *vbox4;
	GtkAccelGroup* ag = gtk_accel_group_new();
	char txt[PATHTXT];
	int i;


	if ( layers_total < 1 )					// Only background layer available
	{
		alert_box(_("Error"), _("You must have at least 2 layers to create an animation"),
			_("OK"), NULL, NULL );
		return;
	}

	if (!layers_filename[0])
	{
		alert_box(_("Error"), _("You must save your layers file before creating an animation"),
			_("OK"), NULL, NULL );
		return;
	}

	delete_layers_window();	// Lose the layers window if its up

	ani_read_layer_data();

	ani_currently_selected_layer = -1;

	animate_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Configure Animation"),
					GTK_WIN_POS_NONE, TRUE );

	ani_win_set_pos();

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (animate_window), vbox1);

	notebook1 = xpack(vbox1, gtk_notebook_new());
	gtk_container_set_border_width(GTK_CONTAINER(notebook1), 5);

	vbox4 = add_new_page(notebook1, _("Output Files"));
	table = xpack(vbox4, gtk_table_new(5, 3, FALSE));

	label = add_to_table( _("Start frame"), table, 0, 0, 5 );
	add_to_table( _("End frame"), table, 1, 0, 5 );

	add_to_table( _("Delay"), table, 2, 0, 5 );
	add_to_table( _("Output path"), table, 3, 0, 5 );
	add_to_table( _("File prefix"), table, 4, 0, 5 );

	ani_spin[0] = spin_to_table(table, 0, 1, 5, ani_frame1, 1, MAX_FRAME);	// Start
	ani_spin[1] = spin_to_table(table, 1, 1, 5, ani_frame2, 1, MAX_FRAME);	// End
	ani_spin[2] = spin_to_table(table, 2, 1, 5, ani_gif_delay, 1, MAX_DELAY);	// Delay

	spin_connect(ani_spin[0], GTK_SIGNAL_FUNC(ani_widget_changed), NULL);
	spin_connect(ani_spin[1], GTK_SIGNAL_FUNC(ani_widget_changed), NULL);
	spin_connect(ani_spin[2], GTK_SIGNAL_FUNC(ani_widget_changed), NULL);

	ani_entry_path = gtk_entry_new_with_max_length(PATHBUF);
	gtk_table_attach (GTK_TABLE (table), ani_entry_path, 1, 2, 3, 4,
		(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		(GtkAttachOptions) (0), 0, 0);
	gtkuncpy(txt, ani_output_path, PATHTXT);
	gtk_entry_set_text(GTK_ENTRY(ani_entry_path), txt);
	gtk_signal_connect( GTK_OBJECT(ani_entry_path), "changed",
			GTK_SIGNAL_FUNC(ani_widget_changed), NULL);

	ani_entry_prefix = gtk_entry_new_with_max_length(ANI_PREFIX_LEN);
	gtk_table_attach (GTK_TABLE (table), ani_entry_prefix, 1, 2, 4, 5,
		(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		(GtkAttachOptions) (0), 0, 0);
	gtkuncpy(txt, ani_file_prefix, PATHTXT);
	gtk_entry_set_text(GTK_ENTRY(ani_entry_prefix), txt);
	gtk_signal_connect( GTK_OBJECT(ani_entry_prefix), "changed",
			GTK_SIGNAL_FUNC(ani_widget_changed), NULL);

	ani_toggle_gif = pack(vbox4, sig_toggle(_("Create GIF frames"),
		ani_use_gif, NULL, GTK_SIGNAL_FUNC(ani_tog_gif)));

///	LAYERS TABLES

	hbox4 = gtk_hbox_new(FALSE, 0);
	label = gtk_label_new(_("Positions"));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook1), hbox4, label);

	scrolledwindow = pack(hbox4, gtk_scrolled_window_new(NULL, NULL));
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	ani_list_layers = gtk_list_new ();
	gtk_signal_connect( GTK_OBJECT(ani_list_layers), "select_child",
			GTK_SIGNAL_FUNC(ani_layer_select), NULL );
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolledwindow), ani_list_layers);

	gtk_widget_set_usize (ani_list_layers, 150, -2);
	gtk_container_set_border_width (GTK_CONTAINER (ani_list_layers), 5);

	for ( i=layers_total; i>0; i-- )
	{
		hbox2 = gtk_hbox_new( FALSE, 3 );

		list_data = gtk_list_item_new();
		gtk_container_add( GTK_CONTAINER(ani_list_layers), list_data );
		gtk_container_add( GTK_CONTAINER(list_data), hbox2 );

		sprintf(txt, "%i", i);					// Layer number
		label = pack(hbox2, gtk_label_new(txt));
		gtk_widget_set_usize (label, 40, -2);
		gtk_misc_set_alignment( GTK_MISC(label), 0.5, 0.5 );

		label = xpack(hbox2, gtk_label_new(layer_table[i].name)); // Layer name
		gtk_misc_set_alignment( GTK_MISC(label), 0, 0.5 );
	}

	vbox3 = xpack(hbox4, gtk_vbox_new(FALSE, 0));
	xpack(vbox3, ani_text(&ani_text_pos));

///	CYCLES TAB

	vbox3 = add_new_page(notebook1, _("Cycling"));
	xpack(vbox3, ani_text(&ani_text_cyc));

	ani_cyc_refresh_txt();

///	MAIN BUTTONS

	hbox2 = pack(vbox1, gtk_hbox_new(FALSE, 0));

	button = add_a_button(_("Close"), 5, hbox2, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(delete_ani), NULL);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button = add_a_button(_("Save"), 5, hbox2, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(ani_but_save), NULL);

	button = add_a_button(_("Preview"), 5, hbox2, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(ani_but_preview), NULL);

	button = add_a_button(_("Create Frames"), 5, hbox2, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(create_frames_ani), NULL);

	gtk_signal_connect_object (GTK_OBJECT (animate_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_ani), NULL);

	ani_show_main_state = show_layers_main;	// Remember old state
	show_layers_main = FALSE;		// Don't show all layers in main window - too messy

	gtk_window_set_transient_for( GTK_WINDOW(animate_window), GTK_WINDOW(main_window) );

	gtk_list_select_item( GTK_LIST(ani_list_layers), 0 );

	gtk_widget_show_all(animate_window);
	gtk_window_add_accel_group(GTK_WINDOW (animate_window), ag);

	layers_pastry_cut = TRUE;
	update_all_views();
}



///	FILE HANDLING

void ani_read_file( FILE *fp )			// Read data from layers file already opened
{
	int i, j, k, tot;
	char tin[2048];

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
	for ( j=0; j<tot; j++ )					// Read each cycle line
	{
		if (!fgets(tin, 2000, fp)) break;		// BAILOUT - invalid line

		parse_line_cyc( tin, j );
	}
	if ( j<MAX_CYC_SLOTS ) ani_cycle_table[j].frame0 = 0;	// Mark end

///	POSITION DATA

	for ( k=0; k<=layers_total; k++ )
	{
		i = read_file_num(fp, tin);
		if ( i<0 || i>MAX_POS_SLOTS ) return;			// BAILOUT - invalid #

		tot = i;
		for ( j=0; j<tot; j++ )					// Read each position line
		{
			if (!fgets(tin, 2000, fp)) break;		// BAILOUT - invalid line

			parse_line_pos( tin, k, j );
		}
		if ( j<MAX_POS_SLOTS )
			layer_table[k].image->ani_pos[j].frame = 0;	// Mark end
	}
}

void ani_write_file( FILE *fp )			// Write data to layers file already opened
{
	int gifcode = ani_gif_delay, i, j, k, l;

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

	for ( k=0; k<i; k++ )
	{
		fprintf(fp, "%i\t%i\t%i", ani_cycle_table[k].frame0,
			ani_cycle_table[k].frame1, ani_cycle_table[k].layers[0]);
		for (j = 1; j < MAX_CYC_ITEMS; j++)
		{
			l = ani_cycle_table[k].layers[j];
			if (!l) break;
			fprintf( fp, ",%i", l);
		}
		fprintf( fp, "\n" );
	}

	// POSITION INFO

	// NOTE - we are saving data for layer 0 even though its never used during animation.
	// This is because the user may shift this layer up/down and bring it into play

	for ( k=0; k<=layers_total; k++ )		// Write position table for each layer
	{
		ani_slot *ani = layer_table[k].image->ani_pos;
		for ( i=0; i<MAX_POS_SLOTS; i++ )	// Count how many lines are in the table
		{
			if (ani[i].frame == 0) break;
		}
		fprintf( fp, "%i\n", i );		// Number of position lines for this layer
		if ( i>0 )
		{
			for ( j=0; j<i; j++ )
			{
				fprintf( fp, "%i\t%i\t%i\t%i\t%i\n",
					ani[j].frame, ani[j].x, ani[j].y,
					ani[j].opacity, ani[j].effect);
			}
		}
	}
}


int gifsicle( char *command )	// Execute Gifsicle/Gifview
{
	int res = system(command), code;
	char *msg, *c8;

	if ( res != 0 )
	{
		if ( res>0 ) code = WEXITSTATUS(res);
		else code = res;
		c8 = gtkuncpy(NULL, command, 0);
		msg = g_strdup_printf(_("Error %i reported when trying to run %s"),
			code, c8);
		alert_box( _("Error"), msg, _("OK"), NULL, NULL );
		g_free(msg);
		g_free(c8);
	}

	return res;
}

