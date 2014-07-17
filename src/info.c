/*	info.c
	Copyright (C) 2005-2014 Mark Tyler and Dmitry Groshev

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
#include "vcode.h"
#include "ani.h"
#include "png.h"
#include "mainwindow.h"
#include "canvas.h"
#include "layer.h"


// Maximum median cuts to make

#define MAX_CUTS 128

#define HS_GRAPH_W 256
#define HS_GRAPH_H 64

typedef struct {
	int indexed, clip, layers;
	int norm;
	int wh[3];
	char *col_h, *col_d;
	unsigned char *rgb_mem;
	void **drawingarea;
	char mem_d[128], clip_d[256], rgb_d[64], lr_d[128];
	int rgb[256][3];	// Raw frequencies
	int rgb_sorted[256][3];	// Sorted frequencies
} info_dd;


/* Plot RGB graphs */
static void hs_plot_graph(unsigned char *hs_rgb_mem, int hs_norm,
	int hs_rgb[256][3], int hs_rgb_sorted[256][3])
{
	unsigned char *im, col1[3] = { mem_pal_def[0].red, mem_pal_def[0].green, mem_pal_def[0].blue},
			col2[3];
	int i, j, k, t, /*min[3],*/ max[3], med[3], bars[256][3];
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

//			min[k] = hs_rgb_sorted[t][k];
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

static void hs_click_normalize(info_dd *dt, void **wdata, int what, void **where)
{
	cmd_read(where, dt);

	hs_plot_graph(dt->rgb_mem, dt->norm, dt->rgb, dt->rgb_sorted);

	cmd_repaint(dt->drawingarea);
}

/* Populate RGB tables */
static void hs_populate_rgb(int hs_rgb[256][3], int hs_rgb_sorted[256][3])
{
	int i, j, k, t;
	unsigned char *im = mem_img[CHN_IMAGE];

	memset(&hs_rgb[0][0], 0, 256 * 3 * sizeof(hs_rgb[0][0]));

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

	memcpy(hs_rgb_sorted, hs_rgb, 256 * 3 * sizeof(hs_rgb_sorted[0][0]));

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










////	INFORMATION WINDOW

#undef _
#define _(X) X

#define WBbase info_dd
static void *info_code[] = {
	WINDOWm(_("Information")),
	IF(indexed), DEFH(400),
	FTABLE(_("Memory"), 2, 3),
	TLLABEL(_("Total memory for main + undo images"), 0, 0),
		TLTEXTf(mem_d, 1, 0),
	TLLABEL(_("Undo / Redo / Max levels used"), 0, 1),
	UNLESSx(clip, 1),
		TLLABEL(_("Clipboard"), 0, 2), TLLABEL(_("Unused"), 1, 2),
	ENDIF(1),
	IF(clip), TLTEXTf(clip_d, 0, 2),
	UNLESSx(indexed, 1),
		TLLABEL(_("Unique RGB pixels"), 0, 3), TLTEXTf(rgb_d, 1, 3),
	ENDIF(1),
	IFx(layers, 1),
		TLLABEL(_("Layers"), 0, 4), TLTEXTf(lr_d, 1, 4),
		TLLABEL(_("Total layer memory usage"), 0, 5),
	ENDIF(1),
	WDONE,
	BORDER(TABLE, 0),
	FTABLE(_("Colour Histogram"), 2, 2),
	TALLOC(rgb_mem, wh[2]),
	REF(drawingarea), TLRGBIMAGE(rgb_mem, wh, 0, 0),
	TLCHECK(_("Normalize"), norm, 0, 1), EVENT(CHANGE, hs_click_normalize),
		TRIGGER,
	WDONE,
	IFx(indexed, 1),
///	Big index table
		BORDER(SCROLL, 0),
		XFRAMEp(col_h), VBOXbp(0, 4, 0), XSCROLL(1, 1), // auto/auto
		BORDER(TABLE, 0),
		TABLE(3, 256 + 3),
		TLLABEL(_("Index"), 0, 0),
		TLLABEL(_("Canvas pixels"), 1, 0),
		TLLABEL("%", 2, 0),
		BORDER(LABEL, 0),
		TLTEXTp(col_d, 0, 1), CLEANUP(col_d),
		WDONE, WDONE, // VBOXbp, TABLE
	ENDIF(1),
	HSEP,
	UDONEBTN(_("OK"), NULL),
	WSHOW
};
#undef WBbase

#undef _
#define _(X) __(X)

void pressed_information()
{
	info_dd tdata;
	char txt[256];
	int i, j, maxi, orphans;


	memset(&tdata, 0, sizeof(tdata));
	tdata.indexed = mem_img_bpp == 1;

	maxi = rint(((double)mem_undo_limit * 1024 * 1024) *
		(mem_undo_common * layers_total * 0.01 + 1) /
		(mem_width * mem_height * mem_img_bpp * (layers_total + 1)) - 1.25);
	maxi = maxi < 0 ? 0 : maxi >= mem_undo_max ? mem_undo_max - 1 : maxi;

	snprintf(tdata.mem_d, sizeof(tdata.mem_d), "%1.1f MB\n%d / %d / %d",
		mem_used() / (double)(1024 * 1024),
		mem_undo_done, mem_undo_redo, maxi);

	if (mem_clipboard)
	{
		tdata.clip = TRUE;
		snprintf(tdata.clip_d, sizeof(tdata.clip_d), mem_clip_bpp == 1 ?
			_("Clipboard = %i x %i") : _("Clipboard = %i x %i x RGB"),
			mem_clip_w, mem_clip_h);
		snprintf(txt, sizeof(txt), "\t%1.1f MB",
			((size_t)mem_clip_w * mem_clip_h * mem_clip_bpp) /
			(double)(1024 * 1024));
		strnncat(tdata.clip_d, txt, sizeof(tdata.clip_d));
	}

	if (mem_img_bpp == 3)	// RGB image so count different colours
	{
		i = mem_count_all_cols();
		if (i < 0) // not enough memory
		{
			i = mem_cols_used(1024);
			if (i >= 1024) i = -1;
			strcpy(tdata.rgb_d, ">1023");
		}
		if (i >= 0) snprintf(tdata.rgb_d, sizeof(tdata.rgb_d), "%d", i);
	}

	if ((tdata.layers = layers_total))
	{
		snprintf(tdata.lr_d, sizeof(tdata.lr_d), "%d\n%1.1f MB",
			layers_total, mem_used_layers() / (double)(1024 * 1024));
	}

	hs_populate_rgb(tdata.rgb, tdata.rgb_sorted);
	tdata.wh[0] = HS_GRAPH_W;
	tdata.wh[1] = HS_GRAPH_H * mem_img_bpp;
	tdata.wh[2] = tdata.wh[0] * tdata.wh[1] * 3;

	if (mem_img_bpp == 1)
	{
		memx2 mem;

		mem_get_histogram(CHN_IMAGE);

		memset(&mem, 0, sizeof(mem));
		j = mem_width * mem_height;
		for (i = 0; i < mem_cols; i++)
		{
			snprintf(txt, sizeof(txt), "%d\t%d\t%1.1f\n",
				i, mem_histogram[i],
				(100.0 * mem_histogram[i]) / j);
			addstr(&mem, txt, 1);
		}
		for (orphans = 0 , i = mem_cols; i < 256; i++)
			orphans += mem_histogram[i];
		snprintf(txt, sizeof(txt), "%s\t%d\t%1.1f",
				_("Orphans"), orphans, (100.0 * orphans) / j);
		addstr(&mem, txt, 0);
		tdata.col_d = mem.buf;

		for (j = i = 0; i < mem_cols; i++) if (mem_histogram[i]) j++;
		snprintf(tdata.col_h = txt, sizeof(txt),
			_("Colour index totals - %i of %i used"), j, mem_cols);
	}

	run_create(info_code, &tdata, sizeof(tdata));
}
