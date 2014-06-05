/*	shifter.c
	Copyright (C) 2006-2014 Mark Tyler and Dmitry Groshev

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
#include "vcode.h"

#define NSHIFT 8

typedef struct {
	int frame[3];
	png_color old_pal[256];
	void **spinpack, **label, **slider;
	void **clear, **fix, **create;
} shifter_dd;


static int shift_play_state, shift_timer_state, spins[NSHIFT][3][3];


/* Shift palette between a & b shift times */
static void pal_shift(png_color *old_pal, int a, int b, int shift)
{
	int i, j, d, dir = 1;

	if (a == b) return;			// a=b => so nothing to do
	if (a > b) dir = a , a = b , b = dir , dir = -1;
	d = b - a + 1;

	shift %= d;
	j = a + dir * shift;
	if (j < a) j += d;

	for (i = a; i <= b; i++)
	{
		mem_pal[i] = old_pal[j];
		if (++j > b) j = a;
	}
}


/* Set current palette to a given position in cycle */
static void shifter_set_palette(shifter_dd *dt, int pos)
{
	int i, pos2;

	mem_pal_copy(mem_pal, dt->old_pal);
	if (!pos) return;			// pos=0 => original state

	for (i = 0; i < NSHIFT; i++)		// Implement each of the shifts
	{
		// Normalize the position shift for delay
		pos2 = pos / (spins[i][2][0] + 1);
		pal_shift(dt->old_pal, spins[i][0][0], spins[i][1][0], pos2);
	}
}

static gboolean shift_play_timer_call(gpointer data)
{
	if (!shift_play_state)
	{
		shift_timer_state = 0;
		return FALSE;			// Stop animating
	}
	else
	{
		shifter_dd *dt = data;
		int i;

		cmd_read(dt->slider, dt);
		i = dt->frame[0] + 1;
		if (i > dt->frame[2]) i = dt->frame[1];
		cmd_set(dt->slider, i);
		return TRUE;
	}
}


static void shifter_slider_moved(shifter_dd *dt, void **wdata, int what,
	void **where)
{
	cmd_read(where, dt);

	shifter_set_palette(dt, dt->frame[0]);
	update_stuff(UPD_PAL);
}


static int gcd(int a, int b)
{
	int c;

	while (b > 0)
	{
		c = b;
		b = a % b;
		a = c;
	}

	return (a);
}

/* An input widget has changed in the dialog */
static void shifter_moved(shifter_dd *dt, void **wdata)
{
	char txt[130];
	int i, j, l, lcm;

	/* !!! Spins are self-reading */
	for (lcm = 1 , i = 0; i < NSHIFT; i++)
	{
		l = spins[i][0][0] - spins[i][1][0];
		if (!l) continue;
		// Total frames needed for shifts, including delays
		l = (1 + abs(l)) * (spins[i][2][0] + 1);
		// Calculate the lowest common multiple for all of the numbers
		j = gcd(lcm, l);
		lcm = (lcm / j) * l;
	}

	dt->frame[2] = lcm - 1;
	// Re-centre the slider if its out of range on the new scale
	if (dt->frame[0] >= lcm) dt->frame[0] = 0;
	cmd_setv(dt->slider, dt->frame, SPIN_ALL); // Set min/max value of slider

	snprintf(txt, 128, "%s = %i", _("Frames"), lcm);
	cmd_setv(dt->label, txt, LABEL_VALUE);
}

static void shift_btn(shifter_dd *dt, void **wdata, int what, void **where)
{
	int i;

	if (what == op_EVT_CANCEL)
	{
		shift_play_state = FALSE; // Stop

		mem_pal_copy(mem_pal, dt->old_pal);
		update_stuff(UPD_PAL);

		run_destroy(wdata);
		return;
	}

	if (what == op_EVT_CHANGE) // Play toggle
	{
		cmd_read(where, dt);
		if (shift_play_state && !shift_timer_state) // Start timer
			shift_timer_state = threads_timeout_add(100,
				shift_play_timer_call, dt);
		return;
	}

	where = origin_slot(where);

	if (where == dt->fix)	// Button to fix palette pressed
	{
		i = dt->frame[0];
		if (!i || (i > dt->frame[2])) return; // Nothing to do

		mem_pal_copy(mem_pal, dt->old_pal);
		spot_undo(UNDO_PAL);
		shifter_set_palette(dt, i);
		mem_pal_copy(dt->old_pal, mem_pal);
		cmd_set(dt->slider, 0);
		update_stuff(UPD_PAL);
	}

	else if (where == dt->clear)	// Button to clear all of the values
	{
		for (i = 0; i < NSHIFT; i++)
			spins[i][0][0] = spins[i][1][0] = spins[i][2][0] = 0;
		cmd_reset(dt->spinpack, dt);
		shifter_moved(dt, wdata);
	}

	else if (where == dt->create)	// Button to create a sequence of undo images
	{
		if (!dt->frame[2]) return;	// Nothing to do

		for (i = 0; i <= dt->frame[2]; i++)
		{
			shifter_set_palette(dt, i);
			spot_undo(UNDO_PAL);
		}
		shifter_set_palette(dt, dt->frame[0]);
		update_stuff(UPD_PAL);
	}
}


#undef _
#define _(X) X

#define WBbase shifter_dd
static void *shifter_code[] = {
	WINDOWm(_("Palette Shifter")),
	XVBOXb(0, 5), // !!! Originally the main vbox was that way
	TABLE(4, 9),
	BORDER(TLABEL, 0),
	TLLABELx(_("Start"), 1, 0, 0, 0, 5),
	TLLABELx(_("Finish"), 2, 0, 0, 0, 5),
	TLLABELx(_("Delay"), 3, 0, 0, 0, 5),
	DEFBORDER(TLABEL),
	TLTEXT("0\n1\n2\n3\n4\n5\n6\n7", 0, 1),
	REF(spinpack), TLSPINPACKv(spins, 3 * NSHIFT, shifter_moved, 3, 1, 1),
	TRIGGER,
	WDONE,
	HBOX,
	UTOGGLEv(_("Play"), shift_play_state, shift_btn),
	REF(label), XLABELr(""),
	WDONE,
	REF(slider), SPINSLIDEa(frame),
	EVENT(CHANGE, shifter_slider_moved),
	HSEP,
	BORDER(OKBOX, 0),
	UOKBOX0,
	REF(clear), BUTTON(_("Clear"), shift_btn),
	REF(fix), BUTTON(_("Fix Palette"), shift_btn),
	REF(create), BUTTON(_("Create Frames"), shift_btn),
	CANCELBTN(_("Close"), shift_btn),
	WDONE,
// !!! Transient windows cannot be minimized; don't know if that's desirable here
	ONTOP0,
	WSHOW
};
#undef WBbase

#undef _
#define _(X) __(X)

void pressed_shifter()
{
	shifter_dd tdata;
	int i;

	memset(&tdata, 0, sizeof(tdata));
	mem_pal_copy(tdata.old_pal, mem_pal);
	for (i = 0; i < NSHIFT; i++)
	{
		spins[i][0][2] = spins[i][1][2] = mem_cols - 1;
		spins[i][2][2] = 255;
	}

	shift_play_state = FALSE; // Stopped

	run_create(shifter_code, &tdata, sizeof(tdata));
}
