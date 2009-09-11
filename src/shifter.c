/*	shifter.c
	Copyright (C) 2006-2008 Mark Tyler

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

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "canvas.h"

#include "shifter.h"


static GtkWidget *shifter_window, *shifter_spin[8][3], *shifter_slider, *shifter_label;
static png_color sh_old_pal[256];
static int shifter_in[8][3], shifter_pos, shifter_max, shift_play_state, shift_timer_state;



static void pal_shift( int a, int b, int shift )	// Shift palette between a & b shift times
{
	int dir = (a>b) ? -1 : 1, minab, maxab, i, j;

	if ( a == b ) return;				// a=b => so nothing to do
	mtMIN(minab, a, b )
	mtMAX(maxab, a, b )

	shift = shift % (maxab-minab+1);
	j = minab + dir*shift;
	while ( j>maxab ) j = j - (maxab-minab+1);
	while ( j<minab ) j = j + (maxab-minab+1);

	for ( i=minab; i<=maxab; i++ )
	{
		mem_pal[i] = sh_old_pal[j];
		j++;
		if ( j>maxab ) j=minab;
	}
}


static void shifter_set_palette(int pos)	// Set current palette to a given position in cycle
{
	int i, pos2;

	if ( pos<0 || pos>=shifter_max ) return;	// Ensure sanity
	mem_pal_copy( mem_pal, sh_old_pal );
	if ( pos==0 ) return;				// pos=0 => original state

	for ( i=0; i<8; i++ )				// Implement each of the shifts
	{
		pos2 = pos / (shifter_in[i][2]+1);	// Normalize the position shift for delay
		pal_shift( shifter_in[i][0], shifter_in[i][1], pos2 );
	}
}

static gboolean shift_play_timer_call()
{
	int i;

	if ( shift_play_state == 0 )
	{
		shift_timer_state = 0;
		return FALSE;			// Stop animating
	}
	else
	{
		i = mt_spinslide_get_value(shifter_slider);
		if (++i >= shifter_max) i = 0;

		mt_spinslide_set_value(shifter_slider, i);

		return TRUE;
	}
}

static void shift_play_start()
{
	if ( shift_play_state == 0 )
	{
		shift_play_state = 1;
		if ( shift_timer_state == 0 )
			shift_timer_state = g_timeout_add( 100, shift_play_timer_call, NULL );
	}
}

static void shift_play_stop()
{
	shift_play_state = 0;
}


static void shift_but_playstop(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (gtk_toggle_button_get_active(togglebutton)) shift_play_start();
	else shift_play_stop();
}


static void shifter_slider_moved()		// Slider position changed
{
	int pos = mt_spinslide_read_value(shifter_slider);

	if ( pos != shifter_pos )
	{
		shifter_pos = pos;
		shifter_set_palette(pos);
		update_stuff(UPD_PAL);
	}
}


static int gcd( int a, int b )
{
	int c;

	if ( b>a ) { c=a; a=b; b=c; }		// Ensure a>=b

	while ( b>0 )
	{
		c = b;
		b = a % b;
		a = c;
	}

	return a;
}

static void shifter_moved()			// An input widget has changed in the dialog
{
	char txt[130];
	int i, j, fr[8], fs[8], tot, s1, s2, p, lcm, lcm2;

	for ( i=0; i<8; i++ )
	{
		for ( j=0; j<3; j++ )
		{
			shifter_in[i][j] = gtk_spin_button_get_value_as_int(
					GTK_SPIN_BUTTON(shifter_spin[i][j]) );
		}
		if ( shifter_in[i][0] != shifter_in[i][1] )
			fr[i] = (1 + abs(shifter_in[i][0] - shifter_in[i][1])) * (shifter_in[i][2] + 1);
		else	fr[i] = 0;
				// Total frames needed for shifts, including delays
	}

	tot = 0;
	for ( i=0; i<8; i++ )
	{
		if ( fr[i]>1 )
		{
			fs[tot] = fr[i];
			tot++;
		}
	}

	if ( tot<2 )
	{
		if ( tot==1 ) lcm=fs[0]; else lcm=1;
	}
	else
	{	// Calculate the lowest common multiple for all of the numbers
		i   = 2;
		j   = tot;
		s1  = fs[0];
		s2  = fs[1];

		p   = s1 * s2;
		s1  = gcd( s1, s2 );
		lcm = p / s1;

		while ( i < j )
		{
			s2   = fs[i];

			p    = lcm * s2;
			s1   = gcd(s1, s2);
			lcm2 = gcd(lcm, s2);
			lcm  = p / lcm2;

			i++;
		}
	}

	mt_spinslide_set_range(shifter_slider, 0, lcm-1);  // Set min/max value of slider
	shifter_max = lcm;

	snprintf(txt, 128, "%s = %i", _("Frames"), lcm);
	gtk_label_set_text( GTK_LABEL(shifter_label), txt );

	if ( shifter_pos >= lcm )
		mt_spinslide_set_value(shifter_slider, 0);
			// Re-centre the slider if its out of range on the new scale
}

static void click_shift_fix()			// Button to fix palette pressed
{
	int i = mt_spinslide_get_value(shifter_slider);

	if ( i==0 || i>=shifter_max ) return;	// Nothing to do

	mem_pal_copy( mem_pal, sh_old_pal );
	spot_undo(UNDO_PAL);
	shifter_set_palette(i);
	mem_pal_copy( sh_old_pal, mem_pal );

	mt_spinslide_set_value(shifter_slider, 0);

	update_stuff(UPD_PAL);
}

static gboolean click_shift_close()	// Palette Shifter window closed by user or WM
{
	shift_play_stop();
	mem_pal_copy( mem_pal, sh_old_pal );
	update_stuff(UPD_PAL);

	gtk_widget_destroy( shifter_window );
	return (FALSE);
}

static void click_shift_clear()		// Button to clear all of the values
{
	int i, j;

	for ( i=0; i<8; i++ ) for ( j=0; j<3; j++ )
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(shifter_spin[i][j]),0);
}

static void click_shift_create()		// Button to create a sequence of undo images
{
	int i;

	if ( shifter_max<2 ) return;		// Nothing to do

	for ( i=0; i<shifter_max; i++ )
	{
		shifter_set_palette(i);
		spot_undo(UNDO_PAL);
	}

	shifter_set_palette( mt_spinslide_get_value(shifter_slider) );
	update_stuff(UPD_PAL);
}


void pressed_shifter()
{
	GtkWidget *vbox, *hbox, *table, *button, *label;
	GtkAccelGroup* ag = gtk_accel_group_new();
	int i, j, max;
	char txt[32];


	shifter_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Palette Shifter"),
			GTK_WIN_POS_CENTER, TRUE );

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (shifter_window), vbox);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);

	table = add_a_table(9, 4, 5, vbox);

	label = add_to_table( _("Start"),  table, 0, 1, 1 );
	gtk_misc_set_alignment( GTK_MISC(label), 0.5, 0.5 );
	label = add_to_table( _("Finish"), table, 0, 2, 1 );
	gtk_misc_set_alignment( GTK_MISC(label), 0.5, 0.5 );
	label = add_to_table( _("Delay"),  table, 0, 3, 1 );
	gtk_misc_set_alignment( GTK_MISC(label), 0.5, 0.5 );

	for ( i=0; i<8; i++ )
	{
		sprintf(txt, "%i", i);
		add_to_table( txt, table, i+1, 0, 5 );

		for ( j=0; j<3; j++ )
		{
			if ( j==2 ) max=255; else max=mem_cols-1;
			shifter_spin[i][j] = spin_to_table( table, i+1, j+1, 2,
						shifter_in[i][j], 0, max );
			spin_connect(shifter_spin[i][j],
				GTK_SIGNAL_FUNC(shifter_moved), NULL);
		}
	}


	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show (hbox);

	button = pack(hbox, sig_toggle_button(_("Play"), FALSE, NULL,
		GTK_SIGNAL_FUNC(shift_but_playstop)));
	shift_play_state = FALSE;			// Stopped

	shifter_label = xpack(hbox, gtk_label_new(""));
	gtk_widget_show( shifter_label );
	gtk_misc_set_alignment( GTK_MISC(shifter_label), 0.5, 0.5 );


	shifter_slider = xpack5(vbox, mt_spinslide_new(-1, -1));
	mt_spinslide_set_range(shifter_slider, 0, 0);
	mt_spinslide_set_value(shifter_slider, 0);
	mt_spinslide_connect(shifter_slider, GTK_SIGNAL_FUNC(shifter_slider_moved), NULL);


	add_hseparator( vbox, -2, 10 );

	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show (hbox);

	button = xpack5(hbox, gtk_button_new_with_label(_("Clear")));
	gtk_widget_show(button);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_shift_clear), NULL);

	button = xpack5(hbox, gtk_button_new_with_label(_("Fix Palette")));
	gtk_widget_show(button);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_shift_fix), NULL);

	button = xpack5(hbox, gtk_button_new_with_label(_("Create Frames")));
	gtk_widget_show(button);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_shift_create), NULL);

	button = xpack5(hbox, gtk_button_new_with_label(_("Close")));
	gtk_widget_show(button);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_shift_close), NULL );
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_signal_connect_object(GTK_OBJECT(shifter_window), "delete_event",
		GTK_SIGNAL_FUNC(click_shift_close), NULL );

	gtk_widget_show(shifter_window);
	gtk_window_add_accel_group(GTK_WINDOW (shifter_window), ag);

#if GTK_MAJOR_VERSION == 1
	gtk_widget_queue_resize(shifter_window); /* Re-render sliders */
#endif

	mem_pal_copy( sh_old_pal, mem_pal );			// Backup the current palette
	shifter_pos = 0;
	shifter_max = 0;
	shifter_moved();					// Initialize the input array
}
