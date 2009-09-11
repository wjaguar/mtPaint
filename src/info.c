/*	info.c
	Copyright (C) 2005-2007 Mark Tyler and Dmitry Groshev

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

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "canvas.h"
#include "layer.h"


// Maximum median cuts to make

#define MAX_CUTS 128

int	hs_rgb[256][3],			// Raw frequencies
	hs_rgb_sorted[256][3],		// Sorted frequencies
	hs_rgb_norm[256][3]		// Normalized frequencies
	;

#define HS_GRAPH_W 256
#define HS_GRAPH_H 64

unsigned char *hs_rgb_mem = NULL;	// RGB chunk holding graphs

GtkWidget *hs_drawingarea;
gboolean hs_norm;


static void hs_plot_graph()				// Plot RGB graphs
{
	unsigned char *im, col1[3] = { mem_pal_def[0].red, mem_pal_def[0].green, mem_pal_def[0].blue},
			col2[3];
	int i, j, k, t, min[3], max[3], med[3], bars[256][3];
	float f;

	for ( i=0; i<3; i++ ) col2[i] = 255 - col1[i];

	im = hs_rgb_mem;
	if ( im != NULL )
	{
			// Flush background to palette 0 colour
		j = HS_GRAPH_W * HS_GRAPH_H * mem_img_bpp;
		for ( i=0; i<j; i++ )
		{
			im[0] = col1[0];
			im[1] = col1[1];
			im[2] = col1[2];
			im += 3;
		}

			// Draw axis in negative of palette 0 colour for 3 channels
		for ( j=HS_GRAPH_H*mem_img_bpp-1; j>0; j=j-HS_GRAPH_H/2 )
		{
			im = hs_rgb_mem + j*HS_GRAPH_W*3;
			for ( i=0; i<HS_GRAPH_W; i++ )			// Horizontal lines
			{
				im[0] = col2[0];
				im[1] = col2[1];
				im[2] = col2[2];
				im += 3;
			}
		}
		for ( j=HS_GRAPH_W*0.75; j>0; j=j-HS_GRAPH_W/4 )
		{
			im = hs_rgb_mem + j*3;
			for ( i=0; i<HS_GRAPH_H*mem_img_bpp; i++ )		// Vertical lines
			{
				im[0] = col2[0];
				im[1] = col2[1];
				im[2] = col2[2];
				im += HS_GRAPH_W*3;
			}
		}

		for ( k=0; k<3; k++ )
		{
			t = 255;		
			for ( j=0; j<256; j++ )		// Find first non zero frequency for this channel
			{
				if ( hs_rgb_sorted[j][k] > 0 )
				{
					t = j;
					break;
				}
			}

			min[k] = hs_rgb_sorted[t][k];
			med[k] = hs_rgb_sorted[(t+255)/2][k];
			max[k] = hs_rgb_sorted[255][k];
		}

			// Calculate bar values - either linear or normalized
		if ( hs_norm )
		{
			for ( k=0; k<mem_img_bpp; k++ )
			{
				for ( i=0; i<256; i++ )			// Normalize
				{
					t = hs_rgb[i][k];
					if ( t == 0 ) bars[i][k] = 0;
					else
					{
						if ( t < med[k] )
						{
							f = ((float) t) / med[k];
							bars[i][k] = f * HS_GRAPH_H / 2;
						}
						else
						{
							f = ((float) t-med[k]) / (max[k] - med[k]);
							bars[i][k] = 32 + f * HS_GRAPH_H / 2;
						}
					}
				}
			}
		}
		else
		{
			for ( k=0; k<mem_img_bpp; k++ )
			{
				for ( i=0; i<256; i++ )			// Linear
				{
					f = ((float) hs_rgb[i][k]) / max[k];
					bars[i][k] = f * HS_GRAPH_H;
				}
			}
		}

			// Draw 3 graphs in red, green and blue
		for ( k=0; k<mem_img_bpp; k++ )
		{
			col1[0] = 0;
			col1[1] = 0;
			col1[2] = 0;
			col1[k] = 255;			// Pure red/green/blue coloured bars
			if ( mem_img_bpp == 1 )
			{
				col1[0] = 128;
				col1[1] = 128;
				col1[2] = 128;
			}
			for ( i=0; i<256; i++ )
			{
				t = bars[i][k];
				if ( t<0 ) t=0;
				if ( t>63 ) t=63;
				im = hs_rgb_mem + i*3 + (k+1)*HS_GRAPH_H*HS_GRAPH_W*3;
				im = im - HS_GRAPH_W*3;
				for ( j=0; j<t; j++ )
				{
					im[0] = col1[0];
					im[1] = col1[1];
					im[2] = col1[2];
					im -= HS_GRAPH_W*3;
				}
			}
		}
	}
}

static void hs_click_normalize(GtkToggleButton *togglebutton, gpointer user_data)
{
	hs_norm = gtk_toggle_button_get_active(togglebutton);

	hs_plot_graph();

	gtk_widget_queue_draw(hs_drawingarea);
}

static void hs_populate_rgb()				// Populate RGB tables
{
	int i, j, k, t;
	unsigned char *im = mem_img[CHN_IMAGE];

	memset(&hs_rgb[0][0], 0, sizeof(hs_rgb));

	j = mem_width * mem_height;

	if ( mem_img_bpp == 3 )
	{
		for ( i=0; i<j; i++ )			// Populate table with RGB frequencies
		{
			hs_rgb[im[0]][0]++;
			hs_rgb[im[1]][1]++;
			hs_rgb[im[2]][2]++;
			im += 3;
		}
	}
	else
	{
		for ( i=0; i<j; i++ )			// Populate table with pixel indexes
		{
			hs_rgb[im[i]][0]++;
		}
	}

	for ( i=0; i<256; i++ )
	{
		hs_rgb_sorted[i][0] = hs_rgb[i][0];
		hs_rgb_sorted[i][1] = hs_rgb[i][1];
		hs_rgb_sorted[i][2] = hs_rgb[i][2];
	}

	for ( k=0; k<3; k++ )			// Sort RGB table
	{
		for ( j=255; j>0; j-- )		// The venerable bubble sort
		{
			for ( i=0; i<j; i++ )
			{
				if ( hs_rgb_sorted[i][k] > hs_rgb_sorted[i+1][k] )
				{
					t = hs_rgb_sorted[i][k];
					hs_rgb_sorted[i][k] = hs_rgb_sorted[i+1][k];
					hs_rgb_sorted[i+1][k] = t;
				}
			}
		}
	}
}

static gint hs_expose_graph( GtkWidget *widget, GdkEventExpose *event )
{
	int x = event->area.x, y = event->area.y;
	int w = event->area.width, h = event->area.height;

	if ( hs_rgb_mem == NULL ) return FALSE;
	if ( x >= HS_GRAPH_W || y >= HS_GRAPH_H*mem_img_bpp ) return FALSE;

	mtMIN( w, w, HS_GRAPH_W-x )
	mtMIN( h, h, HS_GRAPH_H*3-y )

	gdk_draw_rgb_image (hs_drawingarea->window,
			hs_drawingarea->style->black_gc,
			x, y, w, h,
			GDK_RGB_DITHER_NONE, hs_rgb_mem + 3*(x + y*HS_GRAPH_W), HS_GRAPH_W*3
			);

	return FALSE;
}










////	INFORMATION WINDOW

GtkWidget *info_window;

gint delete_info( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy(info_window);

	if ( hs_rgb_mem != NULL )
	{
		free( hs_rgb_mem );
		hs_rgb_mem = NULL;
	}

	return FALSE;
}

void pressed_information( GtkMenuItem *menu_item, gpointer user_data )
{
	char txt[256];
	int i, j, orphans = 0, maxi;

	GtkWidget *vbox4, *vbox5, *table4, *hs_normalize_check;
	GtkWidget *scrolledwindow1, *viewport1, *table5, *button3;
	GtkAccelGroup* ag = gtk_accel_group_new();

	info_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Information"), GTK_WIN_POS_CENTER, TRUE );

	if ( mem_img_bpp == 1 )
		gtk_widget_set_usize (GTK_WIDGET (info_window), -2, 400);

	vbox4 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox4);
	gtk_container_add (GTK_CONTAINER (info_window), vbox4);

	table4 = gtk_table_new (3, 2, FALSE);
	gtk_widget_show (table4);
	add_with_frame(vbox4, _("Memory"), table4, 5);
	gtk_container_set_border_width (GTK_CONTAINER (table4), 5);

	add_to_table( _("Total memory for main + undo images"), table4, 0, 0, 5 );

	snprintf(txt, 60, "%1.1f MB", ( (float) mem_used() )/1024/1024 );
	add_to_table( txt, table4, 0, 1, 5 );

	maxi = rint(((double)mem_undo_limit * 1024 * 1024) /
		(mem_width * mem_height * mem_img_bpp * (layers_total + 1)) - 1.25);
	maxi = maxi < 0 ? 0 : maxi >= MAX_UNDO ? MAX_UNDO - 1 : maxi;

	snprintf(txt, 60, "%i / %i / %i", mem_undo_done, mem_undo_redo, maxi );
	add_to_table( txt, table4, 1, 1, 5 );

	add_to_table( _("Undo / Redo / Max levels used"), table4, 1, 0, 5 );

	if ( mem_clipboard == NULL )
	{
		add_to_table( _("Clipboard"), table4, 2, 0, 5 );
		add_to_table( _("Unused"), table4, 2, 1, 5 );
	}
	else
	{
		if ( mem_clip_bpp == 1 )
			snprintf(txt, 250, _("Clipboard = %i x %i"), mem_clip_w, mem_clip_h );
		if ( mem_clip_bpp == 3 )
			snprintf(txt, 250, _("Clipboard = %i x %i x RGB"), mem_clip_w, mem_clip_h );
		add_to_table( txt, table4, 2, 0, 5 );
		snprintf(txt, 250, "%1.1f MB", ( (float) mem_clip_w * mem_clip_h * mem_clip_bpp )/1024/1024 );
		add_to_table( txt, table4, 2, 1, 5 );
	}

	if ( mem_img_bpp == 3)		// RGB image so count different colours
	{
		add_to_table( _("Unique RGB pixels"), table4, 3, 0, 5 );
		i = mem_count_all_cols();
		if ( i<0 )
		{
			maxi = mem_cols_used(1024);
			if ( maxi < 1024 ) snprintf(txt, 250, "%i", maxi);
			else sprintf( txt, ">1023" );
		}	else snprintf(txt, 250, "%i", i);
		add_to_table( txt, table4, 3, 1, 5 );
	}

	if ( layers_total>0 )
	{
		add_to_table( _("Layers"), table4, 4, 0, 5 );
		snprintf(txt, 60, "%i", layers_total );
		add_to_table( txt, table4, 4, 1, 5 );

		add_to_table( _("Total layer memory usage"), table4, 5, 0, 5 );
		snprintf(txt, 60, "%1.1f MB", ( (float) mem_used_layers() )/1024/1024 );
		add_to_table( txt, table4, 5, 1, 5 );
	}

	vbox5 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox5);
	add_with_frame(vbox4, _("Colour Histogram"), vbox5, 4);

	hs_norm = FALSE;
	hs_rgb_mem = malloc( HS_GRAPH_W * HS_GRAPH_H * 3 * mem_img_bpp );
	hs_populate_rgb();
	hs_plot_graph();

	hs_drawingarea = pack(vbox5, gtk_drawing_area_new());
	gtk_widget_show (hs_drawingarea);
	gtk_widget_set_usize (hs_drawingarea, HS_GRAPH_W, HS_GRAPH_H*mem_img_bpp);
	gtk_signal_connect_object( GTK_OBJECT(hs_drawingarea), "expose_event",
		GTK_SIGNAL_FUNC (hs_expose_graph), NULL );

	hs_normalize_check = pack(vbox5, sig_toggle(_("Normalize"), FALSE, NULL,
		GTK_SIGNAL_FUNC(hs_click_normalize)));

	if ( mem_img_bpp == 1 )
	{
		mem_get_histogram(CHN_IMAGE);

		j = 0;
		for ( i=0; i<mem_cols; i++ ) if ( mem_histogram[i] > 0 ) j++;
		snprintf( txt, 250, _("Colour index totals - %i of %i used"), j, mem_cols );

		scrolledwindow1 = gtk_scrolled_window_new (NULL, NULL);
		gtk_widget_show (scrolledwindow1);
		add_with_frame_x(vbox4, txt, scrolledwindow1, 4, TRUE);
		gtk_container_set_border_width (GTK_CONTAINER (scrolledwindow1), 4);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow1),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

		viewport1 = gtk_viewport_new (NULL, NULL);
		gtk_widget_show (viewport1);
		gtk_container_add (GTK_CONTAINER (scrolledwindow1), viewport1);

///	Big index table

		table5 = gtk_table_new (mem_cols+2+1, 3, FALSE);
		gtk_widget_show (table5);
		gtk_container_add (GTK_CONTAINER (viewport1), table5);

		add_to_table( _("Index"), table5, 0, 0, 5 );
		add_to_table( _("Canvas pixels"), table5, 0, 1, 5 );
		add_to_table( "%", table5, 0, 2, 5 );

		for ( i=0; i<mem_cols; i++ )
		{
			snprintf(txt, 60, "%i", i);
			add_to_table( txt, table5, i+1, 0, 0 );

			snprintf(txt, 60, "%i", mem_histogram[i]);
			add_to_table( txt, table5, i+1, 1, 0 );

			snprintf(txt, 60, "%1.1f", 100*((float) mem_histogram[i]) /
				(mem_width*mem_height));
			add_to_table( txt, table5, i+1, 2, 0 );
		}
		add_to_table( _("Orphans"), table5, mem_cols+1, 0, 0 );
		if ( mem_cols < 256 ) for ( i=mem_cols; i<256; i++ ) orphans = orphans +
			mem_histogram[i];

		snprintf(txt, 60, "%i", orphans);
		add_to_table( txt, table5, mem_cols+1, 1, 0 );

		snprintf(txt, 60, "%1.1f", 100*((float) orphans) / (mem_width*mem_height));
		add_to_table( txt, table5, mem_cols+1, 2, 0 );
	}

	add_hseparator( vbox4, -2, 10 );

	button3 = add_a_button(_("OK"), 2, vbox4, FALSE);
	gtk_widget_add_accelerator (button3, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button3, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button3, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
	gtk_signal_connect(GTK_OBJECT(button3), "clicked",
		GTK_SIGNAL_FUNC (delete_info), GTK_OBJECT (info_window) );
	gtk_signal_connect_object (GTK_OBJECT (info_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_info), GTK_OBJECT (info_window));

	gtk_widget_show (info_window);
	gtk_window_set_transient_for( GTK_WINDOW(info_window), GTK_WINDOW(main_window) );
	gtk_window_add_accel_group(GTK_WINDOW (info_window), ag);
}
