/*	png.c
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

#define PNG_READ_PACK_SUPPORTED

#include <math.h>
#include <stdlib.h>
#include <ctype.h>

#include <png.h>
#include <zlib.h>
#ifdef U_GIF
#include <gif_lib.h>
#endif
#ifdef U_JPEG
#include <jpeglib.h>
#endif
#ifdef U_TIFF
#include <tiffio.h>
#endif

#include <gtk/gtk.h>

#include "global.h"

#include "memory.h"
#include "png.h"
#include "canvas.h"
#include "otherwindow.h"
#include "mygtk.h"
#include "layer.h"
#include "ani.h"


char preserved_gif_filename[256];
int preserved_gif_delay = 10;

fformat file_formats[NUM_FTYPES] = {
	{ "", "", "", 0},
	{ "PNG", "png", "", FF_IDX | FF_RGB | FF_ALPHA | FF_MULTI
		| FF_TRANS },
#ifdef U_JPEG
	{ "JPEG", "jpg", "jpeg", FF_RGB | FF_COMPR },
#else
	{ "", "", "", 0},
#endif
#ifdef U_TIFF
/* !!! Ideal state */
//	{ "TIFF", "tif", "tiff", FF_IDX | FF_RGB | FF_ALPHA | FF_MULTI
//		/* | FF_TRANS | FF_LAYER */ },
/* !!! Current state */
	{ "TIFF", "tif", "tiff", FF_IDX | FF_RGB | FF_ALPHA },
#else
	{ "", "", "", 0},
#endif
#ifdef U_GIF
	{ "GIF", "gif", "", FF_IDX | FF_ANIM | FF_TRANS },
#else
	{ "", "", "", 0},
#endif
	{ "BMP", "bmp", "", FF_IDX | FF_RGB | FF_ALPHAR },
	{ "XPM", "xpm", "", FF_IDX | FF_TRANS | FF_SPOT },
	{ "XBM", "xbm", "", FF_BW | FF_SPOT },
/* !!! Not supported yet */
//	{ "TGA", "tga", "", FF_IDX | FF_RGB | FF_ALPHA },
//	{ "PCX", "pcx", "", FF_IDX | FF_RGB },
/* !!! Placeholders */
	{ "", "", "", 0},
	{ "", "", "", 0},
	{ "GPL", "gpl", "", FF_PALETTE },
	{ "TXT", "txt", "", FF_PALETTE },
/* !!! Not supported yet */
//	{ "PAL", "pal", "", FF_PALETTE },
/* !!! Placeholder */
	{ "", "", "", 0},
	{ "LAYERS", "txt", "", FF_LAYER },
/* !!! No 2nd layers format yet */
	{ "", "", "", 0},
};

int file_type_by_ext(char *name, guint32 mask)
{
	int i;
	char *ext = strrchr(name, '.');

	if (!ext || !ext[0]) return (FT_NONE);
	for (i = 0; i < NUM_FTYPES; i++)
	{
		if (!(file_formats[i].flags & mask)) continue;
		if (!strncasecmp(ext + 1, file_formats[i].ext, LONGEST_EXT))
			return (i);
		if (!file_formats[i].ext2[0]) continue;
		if (!strncasecmp(ext + 1, file_formats[i].ext2, LONGEST_EXT))
			return (i);
	}

	/* Special case for Gifsicle's victims */
	if ((mask & FF_IDX) && (ext - name > 4) &&
		!strncasecmp(ext - 4, ".gif", 4)) return (FT_GIF);

	return (FT_NONE);
}

/* Receives struct with image parameters, and channel flags;
 * returns 0 for success, or an error code;
 * success doesn't mean that anything was allocated, loader must check that;
 * loader may call this multiple times - say, for each channel */
static int allocate_image(ls_settings *settings, int cmask)
{
	int i, j;

	if ((settings->width > MAX_WIDTH) || (settings->height > MAX_HEIGHT))
		return (TOO_BIG);

	/* Don't show progress bar where there's no need */
	if (settings->width * settings->height <= SILENCE_LIMIT)
		settings->silent = TRUE;

/* !!! Currently allocations are created committed, have to rollback on error */

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!(cmask & CMASK_FOR(i))) continue;

		/* Overwriting is allowed */
		if (settings->img[i]) continue;
		/* No utility channels without image */
		if ((i != CHN_IMAGE) && !settings->img[CHN_IMAGE]) return (-1);

		switch (settings->mode)
		{
		case FS_PNG_LOAD: /* Regular image */
			/* Allocate the entire batch at once */
			if (i == CHN_IMAGE)
			{
				j = mem_new(settings->width, settings->height,
					settings->bpp, cmask);
				if (j) return (FILE_MEM_ERROR);
				memcpy(settings->img, mem_img, sizeof(chanlist));
			}
			/* Try to add an utility channel */
			else
			{
				settings->img[i] = malloc(settings->width *
					settings->height);
				if (!settings->img[i]) return (FILE_MEM_ERROR);
				mem_undo_im_[mem_undo_pointer].img[i] =
					mem_img[i] = settings->img[i];
			}
			break;
		case FS_CLIP_FILE: /* Clipboard */
			/* Allocate the clipboard image */
			if (i == CHN_IMAGE)
			{
				free(mem_clipboard);
				free(mem_clip_alpha);
				mem_clipboard = mem_clip_alpha = NULL;
				mem_clip_mask_clear();
				mem_clipboard = malloc(settings->width *
					settings->height * settings->bpp);
				if (!mem_clipboard) return (FILE_MEM_ERROR);
				settings->img[CHN_IMAGE] = mem_clipboard;
			}
			/* There's no such thing */
			else if ((i != CHN_ALPHA) && (i != CHN_SEL)) break;
			/* Try to add clipboard alpha or mask */
			else
			{
				settings->img[i] = malloc(settings->width *
					settings->height);
				if (!settings->img[i]) return (FILE_MEM_ERROR);
				*(i == CHN_ALPHA ? &mem_clip_alpha : &mem_clip_mask) =
					settings->img[i];
			}
			break;
		case FS_CHANNEL_LOAD: /* Current channel */
			/* Allocate temp image */
			if (i == CHN_IMAGE)
			{
				/* Dimensions & depth have to be the same */
				if ((settings->width != mem_width) ||
					(settings->height != mem_height) ||
					(settings->bpp != MEM_BPP)) return (-1);
				settings->img[CHN_IMAGE] = malloc(settings->width *
					settings->height * settings->bpp);
				if (!settings->img[CHN_IMAGE]) return (FILE_MEM_ERROR);
			}
			/* There's nothing else */
			break;
		default: /* Something has gone mad */
			return (-1);
		}
	}
	return (0);
}

#define PNG_BYTES_TO_CHECK 8
#define PNG_HANDLE_CHUNK_ALWAYS 3

static char *chunk_names[NUM_CHANNELS] = { "", "alPh", "seLc", "maSk" };

/* Description of PNG interlacing passes as X0, DX, Y0, DY */
static unsigned char png_interlace[8][4] = {
	{0, 1, 0, 1}, /* One pass for non-interlaced */
	{0, 8, 0, 8}, /* Seven passes for Adam7 interlaced */
	{4, 8, 0, 8},
	{0, 4, 4, 8},
	{2, 4, 0, 4},
	{0, 2, 2, 4},
	{1, 2, 0, 2},
	{0, 1, 1, 2}
};

static int load_png(char *file_name, ls_settings *settings)
{
	png_structp png_ptr;
	png_infop info_ptr;
	png_color_16p trans_rgb;
	png_unknown_chunkp uk_p;
	png_bytep *row_pointers = NULL, trans;
	png_colorp png_palette;
	png_uint_32 pwidth, pheight;
	char buf[PNG_BYTES_TO_CHECK + 1], *msg = NULL;
	unsigned char *src, *dest, *dsta;
	long dest_len;
	FILE *fp;
	int i, j, k, bit_depth, color_type, interlace_type, num_uk, res = -1;
	int maxpass, x0, dx, y0, dy, n, nx, height, width;

	if ((fp = fopen(file_name, "rb")) == NULL) return -1;
	i = fread(buf, 1, PNG_BYTES_TO_CHECK, fp);
	if (i != PNG_BYTES_TO_CHECK) goto fail;
	if (png_sig_cmp(buf, 0, PNG_BYTES_TO_CHECK)) goto fail;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) goto fail;

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) goto fail2;

	if (setjmp(png_jmpbuf(png_ptr)))
	{
		res = FILE_LIB_ERROR;
		goto fail2;
	}

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, PNG_BYTES_TO_CHECK);

	/* Stupid libpng handles private chunks on all-or-nothing basis */
	png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_ALWAYS, NULL, 0);

	png_read_info(png_ptr, info_ptr);
	png_get_IHDR(png_ptr, info_ptr, &pwidth, &pheight, &bit_depth, &color_type,
		&interlace_type, NULL, NULL);

	res = TOO_BIG;
	if ((pwidth > MAX_WIDTH) || (pheight > MAX_HEIGHT)) goto fail2;

	/* Call allocator for image data */
	settings->width = width = (int)pwidth;
	settings->height = height = (int)pheight;
	settings->bpp = 1;
	if ((color_type != PNG_COLOR_TYPE_PALETTE) || (bit_depth > 8))
		settings->bpp = 3;
	i = CMASK_IMAGE;
	if ((color_type == PNG_COLOR_TYPE_RGB_ALPHA) ||
		(color_type == PNG_COLOR_TYPE_GRAY_ALPHA)) i = CMASK_RGBA;
	if ((res = allocate_image(settings, i))) goto fail2;
	res = -1;

	i = sizeof(png_bytep) * height;
	row_pointers = malloc(i + width * 4);
	if (!row_pointers) goto fail2;
	row_pointers[0] = (char *)row_pointers + i;

	if (!settings->silent)
	{
		switch(settings->mode)
		{
		case FS_PNG_LOAD:
			msg = _("Loading PNG image");
			break;
		case FS_CLIP_FILE:
			msg = _("Loading clipboard image");
			break;
		}
	}
	if (msg)
	{
		progress_init(msg, 0);
		progress_update(0.0);
	}

	/* RGB PNG file */
	if (settings->bpp == 3)
	{
		png_set_strip_16(png_ptr);
		png_set_gray_1_2_4_to_8(png_ptr);
		png_set_palette_to_rgb(png_ptr);
		png_set_gray_to_rgb(png_ptr);

		/* Is there a transparent color? */
		if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		{
			png_get_tRNS(png_ptr, info_ptr, 0, 0, &trans_rgb);
			if (color_type == PNG_COLOR_TYPE_GRAY)
			{
				if (bit_depth == 4) i = trans_rgb->gray * 17;
				else if (bit_depth == 8) i = trans_rgb->gray;
				/* Hope libpng compiled w/o accurate transform */
				else if (bit_depth == 16) i = trans_rgb->gray >> 8;
				settings->rgb_trans = RGB_2_INT(i, i, i);
			}
			else settings->rgb_trans = RGB_2_INT(trans_rgb->red,
				trans_rgb->green, trans_rgb->blue);
		}
		else settings->rgb_trans = -1;

		if (settings->img[CHN_ALPHA]) /* RGBA */
		{
			nx = height;
			/* Have to do deinterlacing myself */
			if (interlace_type == PNG_INTERLACE_NONE)
			{
				k = 0; maxpass = 1;
			}
			else if (interlace_type == PNG_INTERLACE_ADAM7)
			{
				k = 1; maxpass = 8;
				nx = (nx + 7) & ~7; nx += 7 * (nx >> 3);
			}
			else goto fail2; /* Unknown type */

			for (n = 0; k < maxpass; k++)
			{
				x0 = png_interlace[k][0];
				dx = png_interlace[k][1];
				y0 = png_interlace[k][2];
				dy = png_interlace[k][3];
				for (i = y0; i < height; i += dy , n++)
				{
					png_read_rows(png_ptr, &row_pointers[0], NULL, 1);
					src = row_pointers[0];
					dest = settings->img[CHN_IMAGE] + (i * width + x0) * 3;
					dsta = settings->img[CHN_ALPHA] + i * width;
					for (j = x0; j < width; j += dx)
					{
						dest[0] = src[0];
						dest[1] = src[1];
						dest[2] = src[2];
						dsta[j] = src[3];
						src += 4; dest += 3 * dx;
					}
					if (msg && ((n * 20) % nx >= nx - 20))
						progress_update((float)n / nx);
				}
			}
		}
		else /* RGB */
		{
			png_set_strip_alpha(png_ptr);
			for (i = 0; i < height; i++)
			{
				row_pointers[i] = settings->img[CHN_IMAGE] + i * width * 3;
			}
			png_read_image(png_ptr, row_pointers);
		}
	}
	/* Paletted PNG file */
	else
	{
		png_get_PLTE(png_ptr, info_ptr, &png_palette, &settings->colors);
		memcpy(settings->pal, png_palette, settings->colors * sizeof(png_color));
		/* Is there a transparent index? */
		if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		{
			/* !!! Currently no support for partial transparency */
			png_get_tRNS(png_ptr, info_ptr, &trans, &i, 0);
			settings->xpm_trans = -1;
			for (j = 0; j < i; j++)
			{
				if (trans[j]) continue;
				settings->xpm_trans = j;
				break;
			}
		}
		png_set_strip_16(png_ptr);
		png_set_strip_alpha(png_ptr);
		png_set_packing(png_ptr);
		if ((color_type == PNG_COLOR_TYPE_GRAY) && (bit_depth < 8))
			png_set_gray_1_2_4_to_8(png_ptr);
		for (i = 0; i < height; i++)
		{
			row_pointers[i] = settings->img[CHN_IMAGE] + i * width;
		}
		png_read_image(png_ptr, row_pointers);
	}
	if (msg) progress_update(1.0);

	png_read_end(png_ptr, info_ptr);
	res = 1;

	num_uk = png_get_unknown_chunks(png_ptr, info_ptr, &uk_p);
	if (num_uk)	/* File contains mtPaint's private chunks */
	{
		for (i = 0; i < num_uk; i++)	/* Examine each chunk */
		{
			for (j = CHN_ALPHA; j < NUM_CHANNELS; j++)
			{
				if (!strcmp(uk_p[i].name, chunk_names[j])) break;
			}
			if (j >= NUM_CHANNELS) continue;

			/* Try to allocate a channel */
			if ((res = allocate_image(settings, CMASK_FOR(j)))) break;
			/* Skip if not allocated */
			if (!settings->img[j]) continue;

			dest_len = width * height;
			uncompress(settings->img[j], &dest_len, uk_p[i].data,
				uk_p[i].size);
		}
		/* !!! Is this call really needed? */
		png_free_data(png_ptr, info_ptr, PNG_FREE_UNKN, -1);
	}
	if (!res) res = 1;

fail2:	if (msg) progress_end();
	free(row_pointers);
	png_destroy_read_struct(&png_ptr, NULL, NULL);
fail:	fclose(fp);
	return (res);
}

#ifndef PNG_AFTER_IDAT
#define PNG_AFTER_IDAT 8
#endif

static int save_png(char *file_name, ls_settings *settings)
{
	png_unknown_chunk unknown0;
	png_structp png_ptr;
	png_infop info_ptr;
	FILE *fp;
	int i, j, h = settings->height, w = settings->width, res = -1;
	int chunks = 0;
	long dest_len;
	char *mess;
	unsigned char trans[256], *rgba_row = NULL, *tmp, *tmi, *tma;
	png_color_16 trans_rgb;

	if ((settings->bpp == 3) && settings->img[CHN_ALPHA])
	{
		rgba_row = malloc(w * 4);
		if (!rgba_row) return -1;
	}

	switch(settings->mode)
	{
	case FS_PNG_SAVE:
		mess = _("Saving PNG image");
		break;
	case FS_CLIP_FILE:
		mess = _("Saving Clipboard image");
		break;
	case FS_COMPOSITE_SAVE:
		mess = _("Saving Layer image");
		break;
	case FS_CHANNEL_SAVE:
		mess = _("Saving Channel image");
		break;
	default:
		mess = NULL;
		break;
	}
	if (settings->silent) mess = NULL;

	if ((fp = fopen(file_name, "wb")) == NULL) goto exit0;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);

	if (!png_ptr) goto exit1;

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) goto exit2;

	res = 0;
	png_init_io(png_ptr, fp);
	png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

	if (settings->bpp == 1)
	{
		png_set_IHDR(png_ptr, info_ptr, w, h,
			8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		png_set_PLTE(png_ptr, info_ptr, settings->pal, settings->colors);
		/* Transparent index in use */
		if ((settings->xpm_trans > -1) && (settings->xpm_trans < 256))
		{
			memset(trans, 255, 256);
			trans[settings->xpm_trans] = 0;
			png_set_tRNS(png_ptr, info_ptr, trans, settings->colors, 0);
		}
	}
	else
	{
		png_set_IHDR(png_ptr, info_ptr, w, h,
			8, settings->img[CHN_ALPHA] ? PNG_COLOR_TYPE_RGB_ALPHA :
			PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		/* Transparent index in use */
		if ((settings->rgb_trans > -1) && !settings->img[CHN_ALPHA])
		{
			trans_rgb.red = INT_2_R(settings->rgb_trans);
			trans_rgb.green = INT_2_G(settings->rgb_trans);
			trans_rgb.blue = INT_2_B(settings->rgb_trans);
			png_set_tRNS(png_ptr, info_ptr, 0, 1, &trans_rgb);
		}
	}

	png_write_info(png_ptr, info_ptr);

	if (mess) progress_init( mess, 0 );

	if ((settings->bpp == 1) || !settings->img[CHN_ALPHA]) /* Flat RGB/Indexed image */
	{
		w *= settings->bpp;
		for (i = 0; i < h; i++)
		{
			png_write_row(png_ptr, (png_bytep)(settings->img[CHN_IMAGE] + i * w));
			if (mess && ((i * 20) % h >= h - 20))
				progress_update((float)i / h);
		}
	}
	else /* RGBA image */
	{
		tmi = settings->img[CHN_IMAGE];
		tma = settings->img[CHN_ALPHA];
		for (i = 0; i < h; i++)
		{
			tmp = rgba_row;
			for (j = 0; j < w; j++) /* Combine RGB and alpha */
			{
				tmp[0] = tmi[0];
				tmp[1] = tmi[1];
				tmp[2] = tmi[2];
				tmp[3] = tma[0];
				tmp += 4; tmi += 3; tma++;
			}
			png_write_row(png_ptr, (png_bytep)rgba_row);
			if (mess && ((i * 20) % h >= h - 20))
				progress_update((float)i / h);
		}
	}

	/* Save private chunks into PNG file if we need to */
	j = settings->bpp == 1 ? CHN_ALPHA : CHN_ALPHA + 1;
	for (i = j; !settings->img[i] && (i < NUM_CHANNELS); i++);
	if (i < NUM_CHANNELS)
	{
		/* Get size required for each zlib compress */
		w = settings->width * settings->height;
#if ZLIB_VERNUM >= 0x1200
		dest_len = compressBound(w);
#else
		dest_len = w + (w >> 8) + 32;
#endif
		tmp = malloc(dest_len);	  // Temporary space for compression
		if (!tmp) res = -1;
		else
		{
			for (; i < NUM_CHANNELS; i++)
			{
				if (!settings->img[i]) continue;
				if (compress(tmp, &dest_len, settings->img[i], w) != Z_OK) continue;
				strncpy(unknown0.name, chunk_names[i], 5);
				unknown0.data = tmp;
				unknown0.size = dest_len;
				png_set_unknown_chunks(png_ptr, info_ptr, &unknown0, 1);
				png_set_unknown_chunk_location(png_ptr, info_ptr,
					chunks++, PNG_AFTER_IDAT);
			}
			free(tmp);
		}
	}
	png_write_end(png_ptr, info_ptr);

	if (mess) progress_end();

	/* Tidy up */
exit2:	png_destroy_write_struct(&png_ptr, NULL);
exit1:	fclose(fp);
exit0:	free(rgba_row);
	return (res);
}

#ifdef U_GIF
static int load_gif(char *file_name, ls_settings *settings)
{
	/* GIF interlace pattern: Y0, DY, ... */
	static unsigned char interlace[10] = {0, 1, 0, 8, 4, 8, 2, 4, 1, 2};
	GifFileType *giffy;
	GifRecordType gif_rec;
	GifByteType *byte_ext;
	ColorMapObject *cmap = NULL;
	int i, j, k, kx, n, w, h, dy, res = -1, frame = 0, val;
	int delay = settings->gif_delay, trans = -1, disposal = 0;


	if (!(giffy = DGifOpenFileName(file_name))) return (-1);

	while (TRUE)
	{
		if (DGifGetRecordType(giffy, &gif_rec) == GIF_ERROR) goto fail;
		if (gif_rec == TERMINATE_RECORD_TYPE) break;
		else if (gif_rec == EXTENSION_RECORD_TYPE)
		{
			if (DGifGetExtension(giffy, &val, &byte_ext) == GIF_ERROR) goto fail;
			while (byte_ext)
			{
				if (val == GRAPHICS_EXT_FUNC_CODE)
				{
					trans = byte_ext[1] & 1 ? byte_ext[4] : -1;
					delay = byte_ext[2] + (byte_ext[3] << 8);
					disposal = (byte_ext[1] >> 2) & 7;
				}
				if (DGifGetExtensionNext(giffy, &byte_ext) == GIF_ERROR) goto fail;
			}
		}
		else if (gif_rec == IMAGE_DESC_RECORD_TYPE)
		{
			if (frame++) /* Multipage GIF - notify user */
			{
				res = FILE_GIF_ANIM;
				goto fail;
			}

			if (DGifGetImageDesc(giffy) == GIF_ERROR) goto fail;

			/* Get palette */
			cmap = giffy->SColorMap ? giffy->SColorMap :
				giffy->Image.ColorMap;
			if (!cmap) goto fail;
			settings->colors = j = cmap->ColorCount;
			if ((j > 256) || (j < 1)) goto fail;
			for (i = 0; i < j; i++)
			{
				settings->pal[i].red = cmap->Colors[i].Red;
				settings->pal[i].green = cmap->Colors[i].Green;
				settings->pal[i].blue = cmap->Colors[i].Blue;
			}

			/* Store actual image parameters */
			settings->gif_delay = delay;
			settings->xpm_trans = trans;
			settings->width = w = giffy->Image.Width;
			settings->height = h = giffy->Image.Height;
			settings->bpp = 1;

			if ((res = allocate_image(settings, CMASK_IMAGE))) goto fail;
			res = -1;

			if (!settings->silent)
				progress_init(_("Loading GIF image"), 0);

			if (giffy->Image.Interlace)
			{
				k = 2; kx = 10;
			}
			else
			{
				k = 0; kx = 2;
			}

			for (n = 0; k < kx; k += 2)
			{
				dy = interlace[k + 1];
				for (i = interlace[k]; i < h; n++ , i += dy)
				{
					if (DGifGetLine(giffy, settings->img[CHN_IMAGE] +
						i * w, w) == GIF_ERROR) goto fail2;
					if (!settings->silent && ((n * 10) % h >= h - 10))
						progress_update((float)n / h);
				}
			}
			res = 1;
fail2:			if (!settings->silent) progress_end();
			if (res < 0) break;
		}
	}

fail:	DGifCloseFile(giffy);
	return (res);
}

static int save_gif(char *file_name, ls_settings *settings)
{
	ColorMapObject *gif_map;
	GifFileType *giffy;
	unsigned char gif_ext_data[8];
	int i, w = settings->width, h = settings->height, msg = -1;


	if (settings->bpp != 1) return NOT_GIF;	// GIF save must be on indexed image

	gif_map = MakeMapObject(256, NULL);
	if (!gif_map) return -1;

	giffy = EGifOpenFileName(file_name, FALSE);
	if (!giffy) goto fail0;

	for (i = 0; i < settings->colors; i++)
	{
		gif_map->Colors[i].Red	 = settings->pal[i].red;
		gif_map->Colors[i].Green = settings->pal[i].green;
		gif_map->Colors[i].Blue	 = settings->pal[i].blue;
	}
	for (; i < 256; i++)
	{
		gif_map->Colors[i].Red = gif_map->Colors[i].Green = 
			gif_map->Colors[i].Blue	= 0;
	}

	if (EGifPutScreenDesc(giffy, w, h, 256, 0, gif_map) == GIF_ERROR)
		goto fail;

	if (settings->xpm_trans >= 0)
	{
		gif_ext_data[0] = 1;
		gif_ext_data[1] = 0;
		gif_ext_data[2] = 0;
		gif_ext_data[3] = settings->xpm_trans;
		EGifPutExtension(giffy, GRAPHICS_EXT_FUNC_CODE, 4, gif_ext_data);
	}

	if (EGifPutImageDesc(giffy, 0, 0, w, h, FALSE, NULL) == GIF_ERROR)
		goto fail;

	if (!settings->silent) progress_init(_("Saving GIF image"), 0);

	for (i = 0; i < h; i++)
	{
		EGifPutLine(giffy, settings->img[CHN_IMAGE] + i * w, w);
		if (!settings->silent && ((i * 20) % h >= h - 20))
			progress_update((float)i / h);
	}
	if (!settings->silent) progress_end();
	msg = 0;

fail:	EGifCloseFile(giffy);
fail0:	FreeMapObject(gif_map);

	return (msg);
}
#endif

#ifdef U_JPEG
struct my_error_mgr
{
	struct jpeg_error_mgr pub;	// "public" fields
	jmp_buf setjmp_buffer;		// for return to caller
};

typedef struct my_error_mgr *my_error_ptr;

METHODDEF(void) my_error_exit (j_common_ptr cinfo)
{
	my_error_ptr myerr = (my_error_ptr) cinfo->err;
	longjmp(myerr->setjmp_buffer, 1);
}
struct my_error_mgr jerr;

static int load_jpeg(char *file_name, ls_settings *settings)
{
	struct jpeg_decompress_struct cinfo;
	unsigned char *memp;
	FILE *fp;
	int i, width, height, bpp = 3, res = -1, pr = 0;


	if ((fp = fopen(file_name, "rb")) == NULL) return (-1);

	jpeg_create_decompress(&cinfo);
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer))
	{
		res = FILE_LIB_ERROR;
		goto fail;
	}
	jpeg_stdio_src(&cinfo, fp);

	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);
	if (cinfo.output_components == 1) /* Greyscale */
	{
		settings->colors = 256;
		for (i = 0; i < 256; i++)
			settings->pal[i].red = settings->pal[i].green =
				settings->pal[i].blue = i;
		bpp = 1;
	}
	settings->width = width = cinfo.output_width;
	settings->height = height = cinfo.output_height;
	settings->bpp = bpp;
	if ((res = allocate_image(settings, CMASK_IMAGE))) goto fail;
	res = -1;
	pr = !settings->silent;

	if (pr) progress_init(_("Loading JPEG image"), 0);

	for (i = 0; i < height; i++)
	{
		memp = settings->img[CHN_IMAGE] + width * i * bpp;
		jpeg_read_scanlines(&cinfo, &memp, 1);
		if (pr && ((i * 20) % height >= height - 20))
			progress_update((float)i / height);
	}
	jpeg_finish_decompress(&cinfo);
	res = 1;

fail:	if (pr) progress_end();
	jpeg_destroy_decompress(&cinfo);
	fclose(fp);
	return (res);
}

static int save_jpeg(char *file_name, ls_settings *settings)
{
	struct jpeg_compress_struct cinfo;
	JSAMPROW row_pointer;
	FILE *fp;
	int i;


	if (settings->bpp == 1) return NOT_JPEG;

	if ((fp = fopen(file_name, "wb")) == NULL) return -1;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer))
	{
		jpeg_destroy_compress(&cinfo);
		fclose(fp);
		return -1;
	}

	jpeg_create_compress(&cinfo);

	jpeg_stdio_dest( &cinfo, fp );
	cinfo.image_width = settings->width;
	cinfo.image_height = settings->height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, settings->jpeg_quality, TRUE );
	jpeg_start_compress( &cinfo, TRUE );

	row_pointer = settings->img[CHN_IMAGE];
	if (!settings->silent) progress_init(_("Saving JPEG image"), 0);
	for (i = 0; i < settings->height; i++ )
	{
		jpeg_write_scanlines(&cinfo, &row_pointer, 1);
		row_pointer += 3 * settings->width;
		if (!settings->silent &&
			((i * 20) % settings->height >= settings->height - 20))
			progress_update((float)i / settings->height);
	}
	jpeg_finish_compress( &cinfo );

	if (!settings->silent) progress_end();

	jpeg_destroy_compress( &cinfo );
	fclose(fp);

	return 0;
}
#endif

#ifdef U_TIFF

/* *** PREFACE ***
 * TIFF is a bitch, and libtiff is a joke. An unstable and buggy joke, at that.
 * It's a fact of life - and when some TIFFs don't load or are mangled, that
 * also is a fact of life. Installing latest libtiff may help - or not; sending
 * a bugreport with the offending file attached may help too - but again, it's
 * not guaranteed. But the common varieties of TIFF format should load OK. */

/* Slow-but-sure universal bitstream parser; may read extra byte at the end */
static void stream_bits(unsigned char *src, unsigned char *dest, int cnt,
	int bits, int bit0, int bitstep, int step)
{
	int i, j, v, mask = (1 << bits) - 1;

	for (i = 0; i < cnt; i++)
	{
		j = bit0 >> 3;
		v = (src[j] << 8) | src[j + 1];
		v >>= 16 - bits - (bit0 & 7);
		*dest = (unsigned char)(v & mask);
		bit0 += bitstep;
		dest += step;
	}
}

static int load_tiff(char *file_name, ls_settings *settings)
{
	char cbuf[1024];
	TIFF *tif;
	uint16 bpsamp, sampp, xsamp, pmetric, planar, orient, sform;
	uint16 *sampinfo, *red16, *green16, *blue16;
	uint32 width, height, tw = 0, th = 0, rps = 0;
	uint32 *tr, *raster = NULL;
	unsigned char *tmp, *src, *buf = NULL;
	double d, d1;
	int i, j, k, x0, y0, bsz, xstep, ystep, plane, nplanes, mirror;
	int x, w, h, dx, bpr, bits1, bit0, db, n, nx;
	int res = -1, bpp = 3, cmask = CMASK_IMAGE, argb = FALSE, pr = FALSE;

	/* We don't want any echoing to the output */
	TIFFSetErrorHandler(NULL);
	TIFFSetWarningHandler(NULL);

	if (!(tif = TIFFOpen(file_name, "r"))) return (-1);

	/* Let's learn what we've got */
	TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &sampp);
	TIFFGetFieldDefaulted(tif, TIFFTAG_EXTRASAMPLES, &xsamp, &sampinfo);
	if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &pmetric))
	{
		/* Defaults like in libtiff */
		if (sampp - xsamp == 1) pmetric = PHOTOMETRIC_MINISBLACK;
		else if (sampp - xsamp == 3) pmetric = PHOTOMETRIC_RGB;
		else goto fail;
	}
	TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sform);
	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
	TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bpsamp);
	TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
	planar = planar != PLANARCONFIG_CONTIG;
	TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &orient);
	switch (orient)
	{
	case ORIENTATION_TOPLEFT:
	case ORIENTATION_LEFTTOP: mirror = 0; break;
	case ORIENTATION_TOPRIGHT:
	case ORIENTATION_RIGHTTOP: mirror = 1; break;
	default:
	case ORIENTATION_BOTLEFT:
	case ORIENTATION_LEFTBOT: mirror = 2; break;
	case ORIENTATION_BOTRIGHT:
	case ORIENTATION_RIGHTBOT: mirror = 3; break;
	}
	if (TIFFIsTiled(tif))
	{
		TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tw);
		TIFFGetField(tif, TIFFTAG_TILELENGTH, &th);
	}
	else
	{
		TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rps);
	}

	/* Let's decide how to store it */
	if ((width > MAX_WIDTH) || (height > MAX_HEIGHT)) goto fail;
	settings->width = width;
	settings->height = height;
	if ((sform != SAMPLEFORMAT_UINT) && (sform != SAMPLEFORMAT_INT) &&
		(sform != SAMPLEFORMAT_VOID)) argb = TRUE;
	else	switch (pmetric)
		{
		case PHOTOMETRIC_PALETTE:
			if (bpsamp > 8)
			{
				argb = TRUE;
				break;
			}
			if (!TIFFGetField(tif, TIFFTAG_COLORMAP,
				&red16, &green16, &blue16)) goto fail;
		case PHOTOMETRIC_MINISWHITE:
		case PHOTOMETRIC_MINISBLACK:
			bpp = 1; break;
		case PHOTOMETRIC_RGB:
			break;
		default:
			argb = TRUE;
		}

	/* libtiff can't handle this and neither can we */
	if (argb && !TIFFRGBAImageOK(tif, cbuf)) goto fail;

	settings->bpp = bpp;
	if (xsamp && ((sampinfo[0] == EXTRASAMPLE_ASSOCALPHA) ||
		(sampinfo[0] == EXTRASAMPLE_UNASSALPHA) || (sampp > 3)))
		cmask = CMASK_RGBA;

	/* !!! No alpha support for RGB mode yet */
	if (argb) cmask = CMASK_IMAGE;

	if ((res = allocate_image(settings, cmask))) goto fail;
	res = -1;

	if ((pr = !settings->silent))
	{
		progress_init(_("Loading TIFF image"), 0);
		progress_update(0.0);
	}

	/* Read it as ARGB if can't understand it ourselves */
	if (argb)
	{
		/* libtiff is too much of a moving target if finer control is
		 * needed, so let's trade memory for stability */
		raster = (uint32 *)_TIFFmalloc(width * height * sizeof(uint32));
		res = FILE_MEM_ERROR;
		if (!raster) goto fail2;
		res = FILE_LIB_ERROR;
		if (!TIFFReadRGBAImage(tif, width, height, raster, 0)) goto fail2;
		res = -1;

		/* Parse the RGB part only - alpha might be eaten by bugs */
		tr = raster;
		for (i = height - 1; i >= 0; i--)
		{
			tmp = settings->img[CHN_IMAGE] + width * i * bpp;
			for (j = 0; j < width; j++)
			{
				tmp[0] = TIFFGetR(tr[j]);
				tmp[1] = TIFFGetG(tr[j]);
				tmp[2] = TIFFGetB(tr[j]);
				tmp += 3;
			}
			tr += width;
			if (pr && ((i * 10) % height >= height - 10))
				progress_update((float)(height - i) / height);
		}

		_TIFFfree(raster);
		raster = NULL;

/* !!! Now it would be good to read in alpha ourselves - but not yet... */

		res = 1;
	}

	/* Read & interpret it ourselves */
	else
	{
		xstep = tw ? tw : width;
		ystep = th ? th : rps;
		nplanes = !planar ? 1 :	(pmetric == PHOTOMETRIC_RGB ? 3 : 1) +
			(settings->img[CHN_ALPHA] ? 1 : 0);
		bits1 = bpsamp > 8 ? 8 : bpsamp;
		*(uint32 *)cbuf = 1; /* Test endianness */

		/* !!! Assume 16-, 32- and 64-bit data follow machine's
		 * endianness, and everything else is packed big-endian way -
		 * like TIFF 6.0 spec says; but TIFF 5.0 and before specs said
		 * differently, so let's wait for examples to see if I'm right
		 * or not; as for 24- and 128-bit, even different libtiff
		 * versions handle them differently, so I leave them alone
		 * for now - WJ */

		bit0 = cbuf[0] && ((bpsamp == 16) || (bpsamp == 32) ||
			(bpsamp == 64)) ? bpsamp - 8 : 0;
		db = (planar ? 1 : sampp) * bpsamp;
		bsz = (tw ? TIFFTileSize(tif) : TIFFStripSize(tif)) + 1;
		bpr = tw ? TIFFTileRowSize(tif) : TIFFScanlineSize(tif);

		buf = _TIFFmalloc(bsz);
		res = FILE_MEM_ERROR;
		if (!buf) goto fail2;
		res = FILE_LIB_ERROR;

		/* Progress steps */
		nx = ((width + xstep - 1) / xstep) * nplanes * height;

		/* Read image tile by tile - considering strip a wide tile */
		for (n = y0 = 0; y0 < height; y0 += ystep)
		for (x0 = 0; x0 < width; x0 += xstep)
		for (plane = 0; plane < nplanes; plane++)
		{
			/* Read one piece */
			if (tw)
			{
				if (TIFFReadTile(tif, buf, x0, y0, 0, plane) < 0)
					goto fail2;
			}
			else
			{
				if (TIFFReadEncodedStrip(tif,
					TIFFComputeStrip(tif, y0, plane),
					buf, bsz) < 0) goto fail2;
			}

			/* Prepare decoding loops */
			if (mirror & 1) /* X mirror */
			{
				x = width - x0 - 1;
				w = x < xstep ? x : xstep;
				x -= w - 1;
			}
			else
			{
				x = x0;
				w = x + xstep > width ? width - x : xstep;
			}
			if (mirror & 2) /* Y mirror */
			{
				h = height - y0 - 1;
				if (h > ystep) h = ystep;
			}
			else h = y0 + ystep > height ? height - y0 : ystep;
			src = buf;

			/* Decode it */
			for (j = y0; j < y0 + h; j++ , n++ , src += bpr)
			{
				i = (mirror & 2 ? height - j + 1 : j) * width + x;
				if (plane > bpp)
				{
					dx = mirror & 1 ? -1 : 1;
					tmp = settings->img[CHN_ALPHA] + i;
				}
				else
				{
					dx = mirror & 1 ? -bpp : bpp;
					tmp = settings->img[CHN_IMAGE] + i * bpp + plane;
				}
				stream_bits(src, tmp, w, bits1, bit0, db, dx);
				if (planar) continue;
				if (bpp == 3)
				{
					stream_bits(src, tmp + 1, w, bits1,
						bit0 + bpsamp, db, dx);
					stream_bits(src, tmp + 2, w, bits1,
						bit0 + bpsamp * 2, db, dx);
				}
				if (settings->img[CHN_ALPHA])
				{
					dx = mirror & 1 ? -1 : 1;
					tmp = settings->img[CHN_ALPHA] + i;
					stream_bits(src, tmp, w, bits1,
						bit0 + bpsamp * bpp, db, dx);
				}
				if (pr && ((n * 10) % nx >= nx - 10))
					progress_update((float)n / nx);
			}
		}

		/* Un-associate alpha & rescale image data */
		d1 = 255.0 / (double)((1 << bits1) - 1);
		j = width * height;
		tmp = settings->img[CHN_IMAGE];
		src = settings->img[CHN_ALPHA];
		if (src && (pmetric != PHOTOMETRIC_PALETTE) &&
			(sampinfo[0] != EXTRASAMPLE_UNASSALPHA))
		{
			for (i = 0; i < j; i++ , tmp += bpp)
			{
				if (!src[i]) continue;
				d = 255.0 / (double)src[i];
				src[i] = rint(d1 * src[i]);
				k = rint(d * tmp[0]);
				tmp[0] = k > 255 ? 255 : k;
				if (bpp == 1) continue;
				k = rint(d * tmp[1]);
				tmp[1] = k > 255 ? 255 : k;
				k = rint(d * tmp[2]);
				tmp[2] = k > 255 ? 255 : k;
			}
			bits1 = 8;
		}

		/* Rescale alpha */
		if (src && (bits1 < 8))
		{
			for (i = 0; i < j; i++)
			{
				src[i] = rint(d1 * src[i]);
			}
		}

		/* Rescale RGB */
		if ((bpp == 3) && (bits1 < 8))
		{
			for (i = 0; i < j * 3; i++)
			{
				tmp[i] = rint(d1 * tmp[i]);
			}
		}

		/* Load palette */
		j = 1 << bits1;
		if (pmetric == PHOTOMETRIC_PALETTE)
		{
			settings->colors = j;
			/* Analyze palette */
			for (k = i = 0; i < j; i++)
			{
				k |= red16[i] | green16[i] | blue16[i];
			}
			if (k < 256) /* Old palette format */
			{
				for (i = 0; i < j; i++)
				{
					settings->pal[i].red = red16[i];
					settings->pal[i].green = green16[i];
					settings->pal[i].blue = blue16[i];
				}
			}
			else /* New palette format */
			{
				for (i = 0; i < j; i++)
				{
					settings->pal[i].red = (red16[i] + 128) / 257;
					settings->pal[i].green = (green16[i] + 128) / 257;
					settings->pal[i].blue = (blue16[i] + 128) / 257;
				}
			}
		}

		else if (bpp == 1)
		{
			settings->colors = j--;
			k = pmetric == PHOTOMETRIC_MINISBLACK ? 0 : j;
			for (i = 0; i <= j; i++)
			{
				settings->pal[i].red = settings->pal[i].green =
					settings->pal[i].blue =
					rint(255.0 * (i ^ k) / (double)j);
			}
		}
		res = 1;
	}

fail2:	if (pr) progress_end();
	if (raster) _TIFFfree(raster);
	if (buf) _TIFFfree(buf);
fail:	TIFFClose(tif);
	return (res);
}

#define TIFFX_VERSION 0 // mtPaint's TIFF extensions version

static int save_tiff(char *file_name, ls_settings *settings)
{
/* !!! No private exts for now */
//	char buf[512];

	unsigned char *src, *row = NULL;
	uint16 rgb[256 * 3], xs[NUM_CHANNELS];
	int i, j, k, dt, xsamp = 0, cmask = CMASK_IMAGE, res = 0;
	int w = settings->width, h = settings->height, bpp = settings->bpp;
	TIFF *tif;

	/* Find out number of utility channels */
	memset(xs, 0, sizeof(xs));

/* !!! Only alpha channel as extra, for now */
	for (i = CHN_ALPHA; i <= CHN_ALPHA; i++)
//	for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
	{
		if (!settings->img[i]) continue;
		cmask |= CMASK_FOR(i);
		xs[xsamp++] = i == CHN_ALPHA ? EXTRASAMPLE_UNASSALPHA :
			EXTRASAMPLE_UNSPECIFIED;
	}
	if (xsamp)
	{
		row = malloc(w * (bpp + xsamp));
		if (!row) return -1;
	}

	TIFFSetErrorHandler(NULL);		// We don't want any echoing to the output
	TIFFSetWarningHandler(NULL);
	if (!(tif = TIFFOpen( file_name, "w" )))
	{
		free(row);
		return -1;
	}

/* !!! No private exts for now */
#if 0
	/* Write private extension info in comments */
	TIFFSetField(tif, TIFFTAG_SOFTWARE, "mtPaint 3");
	// Extensions' version, then everything useful but lacking a TIFF tag
	i = sprintf(buf, "VERSION=%d\n", TIFFX_VERSION);
	i += sprintf(buf + i, "CHANNELS=%d\n", cmask);
	i += sprintf(buf + i, "COLORS=%d\n", settings->colors);
	i += sprintf(buf + i, "TRANSPARENCY=%d\n",
		bpp == 1 ? settings->xpm_trans : settings->rgb_trans);
	TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, buf);
#endif

	/* Write regular tags */
	TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, w);
	TIFFSetField(tif, TIFFTAG_IMAGELENGTH, h);
	TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, bpp + xsamp);
	TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	if (bpp == 1)
	{
		TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_PALETTE);
		memset(rgb, 0, sizeof(rgb));
		for (i = 0; i < settings->colors; i++)
		{
			rgb[i] = settings->pal[i].red * 257;
			rgb[i + 256] = settings->pal[i].green * 257;
			rgb[i + 512] = settings->pal[i].blue * 257;
		}
		TIFFSetField(tif, TIFFTAG_COLORMAP, rgb, rgb + 256, rgb + 512);
	}
	else TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
	if (xsamp) TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, xsamp, xs);

	/* Actually write the image */
	if (!settings->silent) progress_init(_("Saving TIFF image"), 0);
	xsamp += bpp;
	for (i = 0; i < h; i++)
	{
		src = settings->img[CHN_IMAGE] + w * i * bpp;
		if (row) /* Interlace the channels */
		{
			for (dt = k = 0; k < w * bpp; k += bpp , dt += xsamp)
			{
				row[dt] = src[k];
				if (bpp == 1) continue;
				row[dt + 1] = src[k + 1];
				row[dt + 2] = src[k + 2];
			}
			for (dt = bpp , j = CHN_ALPHA; j < NUM_CHANNELS; j++)
			{
				if (!settings->img[j]) continue;
				src = settings->img[j] + w * i;
				for (k = 0; k < w; k++ , dt += xsamp)
				{
					row[dt] = src[k];
				}
				dt -= w * xsamp - 1;
			}
		}
		if (TIFFWriteScanline(tif, row ? row : src, i, 0) == -1)
		{
			res = -1;
			break;
		}
		if (!settings->silent && ((i * 20) % h >= h - 20))
			progress_update((float)i / h);
	}
	TIFFClose(tif);

	if (!settings->silent) progress_end();

	free(row);
	return (res);
}
#endif

int load_bmp( char *file_name )
{
	unsigned char buff[MAX_WIDTH*4], pix, *wrk_image;
	int width, height, readin, bitcount, compression, cols, i, j, k, bpl=0;
	FILE *fp;

	if ((fp = fopen(file_name, "rb")) == NULL) return -1;

	readin = fread(buff, 1, 54, fp);
	if ( readin < 54 ) goto fail;					// No proper header
//for (i=0; i<54; i++) printf("%3i %3i\n",i,buff[i]);

	if ( buff[0] != 'B' || buff[1] != 'M' ) goto fail;		// Signature

	width = buff[18] + (buff[19] << 8) + (buff[20] << 16) + (buff[21] << 24);
	height = buff[22] + (buff[23] << 8) + (buff[24] << 16) + (buff[25] << 24);
	bitcount = buff[28] + (buff[29] << 8);
	compression = buff[30] + (buff[31] << 8) + (buff[32] << 16) + (buff[33] << 24);
	cols = buff[46] + (buff[47] << 8) + (buff[48] << 16) + (buff[49] << 24);

	if ( width > MAX_WIDTH || height > MAX_HEIGHT )
	{
		fclose(fp);
		return TOO_BIG;
	}
//printf("BMP file %i x %i x %i bits x %i cols bpl=%i\n", width, height, bitcount, cols, bpl);

	if ( bitcount!=1 && bitcount!=4 && bitcount!=8 && bitcount!=24 ) goto fail;
	if ( compression != 0 ) goto fail;

	bpl = width;						// Calculate bytes per line
	if ( bitcount == 24 ) bpl = width*3;
	if ( bitcount == 4 ) bpl = (width+1) / 2;
	if ( bitcount == 1 ) bpl = (width+7) / 8;
	if (bpl % 4 != 0) bpl = bpl + 4 - bpl % 4;		// 4 byte boundary for pixels

	if ( bitcount == 24 )		// RGB image
	{
		mem_pal_load_def();
		if (mem_new(width, height, 3, CMASK_IMAGE))
			goto file_too_huge;
		wrk_image = mem_img[CHN_IMAGE];
		progress_init(_("Loading BMP image"),0);
		for ( j=0; j<height; j++ )
		{
			if (j%16 == 0) progress_update( ((float) j) / height );
			readin = fread(buff, 1, bpl, fp);	// Read in line of pixels
			for ( i=0; i<width; i++ )
			{
				wrk_image[ 2 + 3*(i + width*(height - 1 - j)) ] = buff[ 3*i ];
				wrk_image[ 1 + 3*(i + width*(height - 1 - j)) ] = buff[ 3*i + 1 ];
				wrk_image[ 3*(i + width*(height - 1 - j)) ] = buff[ 3*i + 2 ];
			}
		}
		progress_end();
	}
	else				// Indexed palette image
	{
		if ( cols == 0 ) cols = 1 << bitcount;
		if ( cols<2 || cols>256 ) goto fail;
		mem_cols = cols;

		readin = fread(buff, 1, cols*4, fp);		// Read in colour table
		for ( i=0; i<cols; i++ )
		{
			mem_pal[i].red = buff[2 + 4*i];
			mem_pal[i].green = buff[1 + 4*i];
			mem_pal[i].blue = buff[4*i];
		}
		if (mem_new(width, height, 1, CMASK_IMAGE))
			goto file_too_huge;
		wrk_image = mem_img[CHN_IMAGE];

		progress_init(_("Loading BMP image"),0);
		for ( j=0; j<height; j++ )
		{
			if (j%16 == 0) progress_update( ((float) j) / height );
			readin = fread(buff, 1, bpl, fp);	// Read in line of pixels

			if ( bitcount == 8 )
			{
				for ( i=0; i<width; i++ )
				{
					pix = buff[i];
					wrk_image[ i + width*(height - 1 - j) ] = pix;
				}
			}
			if ( bitcount == 4 )
			{
				for ( i=0; i<width; i=i+2 )
				{
					pix = buff[i/2];
					wrk_image[ i + width*(height - 1 - j) ] = pix / 16;
					if ( (i+1)<width )
						wrk_image[ 1 + i + width*(height - 1 - j) ] = pix % 16;
				}
			}
			if ( bitcount == 1 )
			{
				for ( i=0; i<width; i=i+8 )
				{
					pix = buff[i/8];
					k = 0;
					while ( (k<8) && (i+k)<width )
					{
						wrk_image[ i+k + width*(height - 1 - j) ]
							= pix / (1 << (7-k)) % 2;
						k++;
					}
				}
			}
		}
		progress_end();
	}

	return 1;	// Success
fail:
	fclose (fp);
	return -1;
file_too_huge:
	fclose (fp);
	return FILE_MEM_ERROR;
}

/* Macros for writing values in Intel byte order */
#define PUT16(buf, v) (buf)[0] = (v) & 0xFF; (buf)[1] = (v) >> 8;
#define PUT32(buf, v) (buf)[0] = (v) & 0xFF; (buf)[1] = ((v) >> 8) & 0xFF; \
	(buf)[2] = ((v) >> 16) & 0xFF; (buf)[3] = (v) >> 24;

#define BMP_FILESIZE  2		/* 32b */
#define BMP_DATAOFS  10		/* 32b */
#define BMP_HDR2SIZE 14		/* 32b */
#define BMP_WIDTH    18		/* 32b */
#define BMP_HEIGHT   22		/* 32b */
#define BMP_PLANES   26		/* 16b */
#define BMP_BPP      28		/* 16b */
#define BMP_COMPRESS 30		/* 32b */
#define BMP_DATASIZE 34		/* 32b */
#define BMP_COLORS   46		/* 32b */
#define BMP_ICOLORS  50		/* 32b */
#define BMP_HSIZE    54
#define BMP_H2SIZE   (BMP_HSIZE - BMP_HDR2SIZE)
#define BMP_MAXHSIZE (BMP_HSIZE + 256 * 4)

static int save_bmp(char *file_name, ls_settings *settings)
{
	unsigned char *buf, *tmp, *src;
	FILE *fp;
	int i, j, ll, hsz, dsz, fsz;
	int w = settings->width, h = settings->height, bpp = settings->bpp;

	i = w > BMP_MAXHSIZE / 4 ? w * 4 : BMP_MAXHSIZE;
	buf = malloc(i);
	if (!buf) return -1;
	memset(buf, 0, i);

	if (!(fp = fopen(file_name, "wb")))
	{
		free(buf);
		return -1;
	}

	/* Sizes of BMP parts */
	if ((bpp == 3) && settings->img[CHN_ALPHA]) bpp = 4;
	ll = (bpp * w + 3) & ~3;
	j = bpp == 1 ? settings->colors : 0;
	hsz = BMP_HSIZE + j * 4;
	dsz = ll * h;
	fsz = hsz + dsz;

	/* Prepare header */
	buf[0] = 'B'; buf[1] = 'M';
	PUT32(buf + BMP_FILESIZE, fsz);
	PUT32(buf + BMP_DATAOFS, hsz);
	PUT32(buf + BMP_HDR2SIZE, BMP_H2SIZE);
	PUT32(buf + BMP_WIDTH, w);
	PUT32(buf + BMP_HEIGHT, h);
	PUT16(buf + BMP_PLANES, 1);
	PUT16(buf + BMP_BPP, bpp * 8);
	PUT32(buf + BMP_COMPRESS, 0); /* No compression */
	PUT32(buf + BMP_DATASIZE, dsz);
	PUT32(buf + BMP_COLORS, j);
	PUT32(buf + BMP_ICOLORS, j);
	tmp = buf + BMP_HSIZE;
	for (i = 0; i < j; i++ , tmp += 4)
	{
		tmp[0] = settings->pal[i].blue;
		tmp[1] = settings->pal[i].green;
		tmp[2] = settings->pal[i].red;
	}
	fwrite(buf, 1, tmp - buf, fp);

	/* Write rows */
	if (!settings->silent) progress_init(_("Saving BMP image"), 0);
	memset(buf + ll - 4, 0, 4);
	for (i = h - 1; i >= 0; i--)
	{
		src = settings->img[CHN_IMAGE] + i * w * settings->bpp;
		if (bpp == 1) /* Indexed */
		{
			memcpy(buf, src, w);
		}
		else if (bpp == 3) /* RGB */
		{
			for (j = 0; j < w * 3; j += 3)
			{
				buf[j + 0] = src[j + 2];
				buf[j + 1] = src[j + 1];
				buf[j + 2] = src[j + 0];
			}
		}
		else /* if (bpp == 4) */ /* RGBA */
		{
			tmp = settings->img[CHN_ALPHA] + i * w;
			for (j = 0; j < w * 4; j += 4)
			{
				buf[j + 0] = src[2];
				buf[j + 1] = src[1];
				buf[j + 2] = src[0];
				buf[j + 3] = tmp[0];
				src += 3; tmp++;
			}
		}
		fwrite(buf, 1, ll, fp);
		if (!settings->silent && ((i * 20) % h >= h - 20))
			progress_update((float)(h - i) / h);
	}
	fclose(fp);

	if (!settings->silent) progress_end();

	free(buf);
	return 0;
}

int load_xpm( char *file_name )
{
	char tin[4110], col_tab[257][3], *po;
	FILE *fp;
	int fw = 0, fh = 0, fc = 0, fcpp = 0, res;
	int i, j, k, dub = 0, trans = -1;
	float grey;

	png_color t_pal[256];


	if ((fp = fopen(file_name, "r")) == NULL) return -1;

///	LOOK FOR XPM / xpm IN FIRST LINE
	res = get_next_line( tin, 4108, fp );
	if ( res < 0 ) goto fail;			// Some sort of file input error occured
	if ( strlen(tin) < 5 || strlen(tin) > 25 ) goto fail;

	if ( strstr( tin, "xpm" ) == NULL && strstr( tin, "XPM" ) == NULL ) goto fail;


	do	// Search for first occurrence of " at beginning of line
	{
		res = get_next_line( tin, 4108, fp );
		if ( res < 0 ) goto fail;			// Some sort of file input error occured
	}
	while ( tin[0] != '"' );

	sscanf(tin, "\"%i %i %i %i", &fw, &fh, &fc, &fcpp );

	if ( fc < 2 || fc > 256 || fcpp<1 || fcpp>2 )
	{
		fclose(fp);
		return NOT_INDEXED;
	}

// !!! WTF??? A bug!!!
//	if ( fw < 4 || fh < 4 ) goto fail;

	if ( fw > MAX_WIDTH || fh > MAX_HEIGHT )
	{
		fclose(fp);
		return TOO_BIG;
	}

	i=0;
	while ( i<fc )			// Read in colour palette table
	{
		do
		{
			res = get_next_line( tin, 4108, fp );
			if ( res < 0 ) goto fail;		// Some sort of file input error occured
			if ( strlen( tin ) > 32 ) goto fail;	// Too many chars - not real XPM file
		} while ( strlen( tin ) < 5 || strchr(tin, '"') == NULL );

		po = strchr(tin, '"');
		if ( po == NULL ) goto fail;			// Not real XPM file

		po++;
		if (po[0] < 32) goto fail;
		col_tab[i][0] = po[0];				// Register colour reference
		po++;
		if (fcpp == 1) col_tab[i][1] = 0;
		else
		{
			if (po[0] < 32) goto fail;
			col_tab[i][1] = po[0];
			col_tab[i][2] = 0;
			po++;
		}
		while ( po[0] != 'c' && po[0] != 0 ) po++;
		if ( po[0] == 0 || po[1] == 0 || po[2] == 0 ) goto fail;
		po = po + 2;
		if ( po[0] == '#' )
		{
			po++;
			if ( strlen(po) < 7 ) goto fail;	// Check the hex RGB lengths are OK
			if ( po[6] == '"' ) po[6] = 0;
			else
			{
				if ( strlen(po) < 13 ) goto fail;
				if ( po[12] == '"' ) po[12] = 0;
				else goto fail;
				dub = 1;				// Double hex system detected
			}
			res = read_hex_dub( po );
			if ( res<0 ) goto fail;
			t_pal[i].red = res;
			if ( dub == 0 ) po = po + 2; else po = po + 4;

			res = read_hex_dub( po );
			if ( res<0 ) goto fail;
			t_pal[i].green = res;
			if ( dub == 0 ) po = po + 2; else po = po + 4;

			res = read_hex_dub( po );
			if ( res<0 ) goto fail;
			t_pal[i].blue = res;
			if ( dub == 0 ) po = po + 2; else po = po + 4;
		}
		else	// Not a hex description so could be "None" or a colour label like "red"
		{
			t_pal[i].red = 0;
			t_pal[i].green = 0;			// Default to black
			t_pal[i].blue = 0;
			if ( check_str(4, po, "none") )
			{
				t_pal[i].red = 115;
				t_pal[i].green = 115;
				t_pal[i].blue = 0;
				trans = i;
			}
			if ( check_str(3, po, "red") )
			{	t_pal[i].red = 255;
				t_pal[i].green = 0;
				t_pal[i].blue = 0;	}
			if ( check_str(5, po, "green") )
			{	t_pal[i].red = 0;
				t_pal[i].green = 255;
				t_pal[i].blue = 0;	}
			if ( check_str(6, po, "yellow") )
			{	t_pal[i].red = 255;
				t_pal[i].green = 255;
				t_pal[i].blue = 0;	}
			if ( check_str(4, po, "blue") )
			{	t_pal[i].red = 0;
				t_pal[i].green = 0;
				t_pal[i].blue = 255;	}
			if ( check_str(7, po, "magenta") )
			{	t_pal[i].red = 255;
				t_pal[i].green = 0;
				t_pal[i].blue = 255;	}
			if ( check_str(4, po, "cyan") )
			{	t_pal[i].red = 0;
				t_pal[i].green = 255;
				t_pal[i].blue = 255;	}
			if ( check_str(4, po, "gray") )
			{
				po = po + 4;
				if ( read_hex(po[0])<0 || read_hex(po[0])<0 || read_hex(po[0])<0 )
					goto fail;
				grey = read_hex(po[0]);
				if ( read_hex(po[1]) >= 0 ) grey = 10*grey + read_hex(po[1]);
				if ( read_hex(po[2]) >= 0 ) grey = 10*grey + read_hex(po[1]);
				if ( grey<0 || grey>100 ) grey = 0;

				t_pal[i].red = mt_round( 255*grey/100 );
				t_pal[i].green = mt_round( 255*grey/100 );
				t_pal[i].blue = mt_round( 255*grey/100 );
			}
		}

		i++;
	}

	mem_pal_copy( mem_pal, t_pal );
	mem_cols = fc;
	if (mem_new(fw, fh, 1, CMASK_IMAGE))
	{
		fclose(fp);
		return FILE_MEM_ERROR;
	}
	if (mem_img[CHN_IMAGE] == NULL) goto fail;
	mem_xpm_trans = trans;

	progress_init(_("Loading XPM image"),0);
	for ( j=0; j<fh; j++ )
	{
		if (j%16 == 0) progress_update( ((float) j) / fh );
		do
		{
			res = get_next_line( tin, 4108, fp );
			if ( res < 0 ) goto fail2;		// Some sort of file input error occured
		} while ( strlen( tin ) < 5 || strchr(tin, '"') == NULL );
		po = strchr(tin, '"') + 1;
		for ( i=0; i<fw; i++ )
		{
			col_tab[256][0] = po[0];
			col_tab[256][fcpp-1] = po[fcpp-1];
			col_tab[256][fcpp] = 0;

			k = 0;
			while ( (col_tab[k][0] != col_tab[256][0] || col_tab[k][1] != col_tab[256][1])
				&& k<fc )
			{
				k++;
			}
			if ( k>=fc ) goto fail2;	// Pixel reference was not in palette

			mem_img[CHN_IMAGE][ i + fw*j ] = k;

			po = po + fcpp;
		}
	}

	fclose(fp);

	progress_end();
	return 1;
fail:
	fclose(fp);
	return -1;
fail2:
	progress_end();
	fclose(fp);
	return 1;
}

/* Partial ctype implementation for C locale;
 * space 1, digit 2, alpha 4, punctuation 8 */
static unsigned char ctypes[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 8, 8, 8, 8, 8, 8,
	8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 4,
	8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
#define ISSPACE(x) (ctypes[(unsigned char)(x)] & 1)
#define ISALPHA(x) (ctypes[(unsigned char)(x)] & 2)
#define ISALNUM(x) (ctypes[(unsigned char)(x)] & 6)
#define ISCNTRL(x) (!ctypes[(unsigned char)(x)])

static char base64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
	hex[] = "0123456789ABCDEF";

static int save_xpm(char *file_name, ls_settings *settings)
{
	unsigned char *src;
	char ctb[256 * 2 + 1], *buf, *tmp;
	FILE *fp;
	int i, j, cpp, w = settings->width, h = settings->height;


	if (settings->bpp != 1) return NOT_XPM;

	/* Extract valid C identifier from name */
	tmp = strrchr(file_name, DIR_SEP);
	tmp = tmp ? tmp + 1 : file_name;
	for (; *tmp && ISALPHA(*tmp); tmp++);
	for (i = 0; (i < 256) && ISALNUM(tmp[i]); i++);
	if (!i) return -1;

	cpp = settings->colors > 64 ? 2 : 1;
	buf = malloc(w * cpp + 16);
	if (!buf) return -1;

	if (!(fp = fopen(file_name, "w")))
	{
		free(buf);
		return -1;
	}

	if (!settings->silent)
	{
		progress_init(_("Saving XPM image"), 0);
		progress_update(0);
	}

	fprintf(fp, "/* XPM */\n" );
	fprintf(fp, "static char *%.*s_xpm[] = {\n", i, tmp);

	if ((settings->hot_x >= 0) && (settings->hot_y >= 0))
		fprintf(fp, "\"%d %d %d %d %d %d\",\n", w, h, settings->colors,
			cpp, settings->hot_x, settings->hot_y);
	else fprintf(fp, "\"%d %d %d %d\",\n", w, h, settings->colors, cpp);

	/* Create colortable */
	tmp = settings->colors > 16 ? base64 : hex;
	memset(ctb, 0, sizeof(ctb));
	for (i = 0; i < settings->colors; i++)
	{
		if (i == settings->xpm_trans)
		{
			if (cpp == 1) ctb[i] = ' ';
			else ctb[2 * i] = ctb[2 * i + 1] = ' ';
			fprintf(fp, "\"%s\tc None\",\n", ctb + i * cpp);
			continue;
		}
		if (cpp == 1) ctb[i] = tmp[i];
		else
		{
			ctb[2 * i] = hex[i >> 4];
			ctb[2 * i + 1] = hex[i & 0xF];
		}
		fprintf(fp, "\"%s\tc #%02X%02X%02X\",\n", ctb + i * cpp,
			settings->pal[i].red, settings->pal[i].green,
			settings->pal[i].blue);
	}
	if (cpp == 1) memset(ctb + i, ctb[0], 256 - i);
	else
	{
		for (; i < 256; i++)
		{
			ctb[2 * i] = ctb[0];
			ctb[2 * i + 1] = ctb[1];
		}
	}

	for (i = 0; i < h; i++)
	{
		src = settings->img[CHN_IMAGE] + i * w;
		tmp = buf;
		*tmp++ = '"';
		for (j = 0; j < w; j++)
		{
			*tmp++ = ctb[cpp * src[j]];
			if (cpp == 1) continue;
			*tmp++ = ctb[2 * src[j] + 1];
		}
		strcpy(tmp, i < h - 1 ? "\",\n" : "\"\n};\n");
		fputs(buf, fp);
		if (!settings->silent && ((i * 10) % h >= h - 10))
			progress_update((float)i / h);
	}
	fclose(fp);

	if (!settings->silent) progress_end();

	free(buf);
	return 0;
}

int grab_val( char *tin, char *txt, FILE *fp )
{
	char *po;
	int res = -1;

	res = get_next_line( tin, 250, fp );
	if ( res < 0 ) return -1;
	if ( strlen(tin) < 5 || strlen(tin) > 200 ) return -1;
	po = strstr( tin, txt );
	if ( po == NULL ) return -1;
	sscanf( po + strlen(txt), "%i", &res );

	return res;
}

int next_xbm_bits( char *tin, FILE *fp, int *sp )
{
	int res;
again:
	while ( *sp >= strlen(tin) )
	{
		res = get_next_line( tin, 250, fp );
		if ( res < 0 || strlen(tin) > 200 ) return -1;
		*sp = 0;
	}
again2:
	while ( tin[*sp] != '0' && tin[*sp] != 0 ) (*sp)++;
	if ( tin[*sp] == 0 ) goto again;
	(*sp)++;
	if ( tin[*sp] != 'x' ) goto again2;
	(*sp)++;

	return read_hex_dub( tin + *sp );
}

int load_xbm( char *file_name )
{
	char tin[256];
	FILE *fp;
	int fw, fh, xh=-1, yh=-1, res;
	int i, j, k, sp, bits;

	if ((fp = fopen(file_name, "r")) == NULL) return -1;

	fw = grab_val( tin, "width", fp );
	fh = grab_val( tin, "height", fp );

	if ( fw < 4 || fh < 4 ) goto fail;

	xh = grab_val( tin, "x_hot", fp );
	if ( xh>=0 )
	{
		yh = grab_val( tin, "y_hot", fp );
		res = get_next_line( tin, 250, fp );
		if ( res < 0 ) goto fail;
	}

	if ( fw > MAX_WIDTH || fh > MAX_HEIGHT )
	{
		fclose(fp);
		return TOO_BIG;
	}

	mem_pal[0].red = 255;
	mem_pal[0].green = 255;
	mem_pal[0].blue = 255;
	mem_pal[1].red = 0;
	mem_pal[1].green = 0;
	mem_pal[1].blue = 0;

	mem_cols = 2;
	if (mem_new(fw, fh, 1, CMASK_IMAGE))
	{
		fclose(fp);
		return FILE_MEM_ERROR;
	}
	if (mem_img[CHN_IMAGE] == NULL) goto fail;

	mem_xbm_hot_x = xh;
	mem_xbm_hot_y = yh;

	tin[1] = 0;
	sp = 10;
	progress_init(_("Loading XBM image"),0);
	for ( j=0; j<fh; j++ )
	{
		if (j%16 == 0) progress_update( ((float) j) / fh );
		for ( i=0; i<fw; i=i+8 )
		{
			bits = next_xbm_bits( tin, fp, &sp );
			if ( bits<0 ) goto fail2;
			for ( k=0; k<8; k++ )
				if ( (i+k) < mem_width )
					mem_img[CHN_IMAGE][ i+k + mem_width*j ] = (bits & (1 << k)) >> k;
		}
	}
fail2:
	progress_end();
	fclose(fp);
	return 1;
fail:
	fclose(fp);
	return -1;
}

#define BPL 12 /* Bytes per line */
#define CPB 6  /* Chars per byte */
static int save_xbm(char *file_name, ls_settings *settings)
{
	unsigned char *src;
	unsigned char row[MAX_WIDTH / 8];
	char buf[CPB * BPL + 16], *tmp;
	FILE *fp;
	int i, j, k, l, w = settings->width, h = settings->height;

	if ((settings->bpp != 1) || (settings->colors > 2)) return NOT_XBM;

	/* Extract valid C identifier from name */
	tmp = strrchr(file_name, DIR_SEP);
	tmp = tmp ? tmp + 1 : file_name;
	for (; *tmp && ISALPHA(*tmp); tmp++);
	for (i = 0; (i < 256) && ISALNUM(tmp[i]); i++);
	if (!i) return -1;

	if (!(fp = fopen(file_name, "w"))) return -1;

	fprintf(fp, "#define %.*s_width %i\n", i, tmp, w);
	fprintf(fp, "#define %.*s_height %i\n", i, tmp, h);
	if ((settings->hot_x >= 0) && (settings->hot_y >= 0))
	{
		fprintf(fp, "#define %.*s_x_hot %i\n", i, tmp, settings->hot_x);
		fprintf(fp, "#define %.*s_y_hot %i\n", i, tmp, settings->hot_y);
	}
	fprintf(fp, "static unsigned char %.*s_bits[] = {\n", i, tmp);

	if (!settings->silent) progress_init(_("Saving XBM image"), 0);

	j = k = (w + 7) >> 3; i = l = 0;
	while (TRUE)
	{
		if (j >= k)
		{
			if (i >= h) break;
			src = settings->img[CHN_IMAGE] + i * w;
			memset(row, 0, k);
			for (j = 0; j < w; j++)
			{
				if (src[j] == 1) row[j >> 3] |= 1 << (j & 7);
			}
			j = 0;
			if (!settings->silent && ((i * 10) % h >= h - 10))
				progress_update((float)i / h);
			i++;
		}
		for (; (l < BPL) && (j < k); l++ , j++)
		{
			tmp = buf + l * CPB;
			tmp[0] = ' '; tmp[1] = '0'; tmp[2] = 'x';
			tmp[3] = hex[row[j] >> 4]; tmp[4] = hex[row[j] & 0xF];
			tmp[5] = ',';
		}
		if ((l == BPL) && (j < k))
		{
			buf[BPL * CPB] = '\n'; buf[BPL * CPB + 1] = '\0';
			fputs(buf, fp);
			l = 0;
		}
	}
	strcpy(buf + l * CPB - 1, " };\n");
	fputs(buf, fp);
	fclose(fp);

	if (!settings->silent) progress_end();

	return 0;
}

int save_image(char *file_name, ls_settings *settings)
{
	png_color greypal[256];
	int i, res;

	/* Provide a grayscale palette if needed */
	if ((settings->bpp == 1) && !settings->pal)
	{
		for (i = 0; i < 256; i++)
			greypal[i].red = greypal[i].green = greypal[i].blue = i;
		settings->pal = greypal;
	}

	switch (settings->ftype)
	{
	default:
	case FT_PNG: res = save_png(file_name, settings); break;
#ifdef U_JPEG
	case FT_JPEG: res = save_jpeg(file_name, settings); break;
#endif
#ifdef U_TIFF
	case FT_TIFF: res = save_tiff(file_name, settings); break;
#endif
#ifdef U_GIF
	case FT_GIF: res = save_gif(file_name, settings); break;
#endif
	case FT_BMP: res = save_bmp(file_name, settings); break;
	case FT_XPM: res = save_xpm(file_name, settings); break;
	case FT_XBM: res = save_xbm(file_name, settings); break;
/* !!! Not implemented yet */
//	case FT_TGA:
//	case FT_PCX:
	}

	if (settings->pal == greypal) settings->pal = NULL;
	return res;
}

static void store_image_extras(ls_settings *settings)
{
	/* Stuff RGB transparency into color 255 */
	if ((settings->rgb_trans >= 0) && (settings->bpp == 3))
	{
		settings->pal[255].red = INT_2_R(settings->rgb_trans);
		settings->pal[255].green = INT_2_G(settings->rgb_trans);
		settings->pal[255].blue = INT_2_B(settings->rgb_trans);
		settings->xpm_trans = 255;
		settings->colors = 256;
	}

	/* Accept vars */
	mem_xpm_trans = settings->xpm_trans;
	mem_jpeg_quality = settings->jpeg_quality;
	mem_xbm_hot_x = settings->hot_x;
	mem_xbm_hot_y = settings->hot_y;
	preserved_gif_delay = settings->gif_delay;

	/* Accept palette */
	mem_pal_copy(mem_pal, settings->pal);
	mem_cols = settings->colors;
}

int load_image(char *file_name, int mode, int ftype)
{
	png_color pal[256];
	ls_settings settings;
	int i, tr, res;

	init_ls_settings(&settings, NULL);
	settings.mode = mode;
	settings.ftype = FT_PNG;
	settings.pal = pal;

	/* !!! Use default palette - for now */
	mem_pal_copy(pal, mem_pal_def);
	settings.colors = mem_pal_def_i;

	switch (ftype)
	{
	default:
	case FT_PNG: res = load_png(file_name, &settings); break;
#ifdef U_GIF
	case FT_GIF: res = load_gif(file_name, &settings); break;
#endif
#ifdef U_JPEG
	case FT_JPEG: res = load_jpeg(file_name, &settings); break;
#endif
#ifdef U_TIFF
	case FT_TIFF: res = load_tiff(file_name, &settings); break;
#endif
	}

	/* Animated GIF was loaded so tell user */
	if (res == FILE_GIF_ANIM)
	{
		i = alert_box(_("Warning"), _("This is an animated GIF file.  What do you want to do?"),
			_("Cancel"), _("Edit Frames"),
#ifndef WIN32
			_("View Animation")
#else
			NULL
#endif
			);

		if (i == 2) /* Ask for directory to explode frames to */
		{
			/* Needed when starting new mtpaint process later */
			preserved_gif_delay = settings.gif_delay;
			strncpy(preserved_gif_filename, file_name, 250);
			file_selector(FS_GIF_EXPLODE);
		}
		else if (i == 3)
		{
			char mess[512];

			snprintf(mess, 500, "gifview -a \"%s\" &", file_name);
			gifsicle(mess);
		}
		/* Have empty image again to avoid destroying old animation */
		create_default_image();
		mem_pal_copy(pal, mem_pal);
		res = 1;
	}

	switch (settings.mode)
	{
	case FS_PNG_LOAD: /* Image */
		/* Success OR LIB FAILURE - accept data */
		if ((res == 1) || (res == FILE_LIB_ERROR))
		{
			/* !!! Image data and dimensions committed already */

			store_image_extras(&settings);
		}
		/* Failure needing rollback */
		else if (settings.img[CHN_IMAGE])
		{
			/* !!! Too late to restore previous image */
			create_default_image();
		}
		break;
	case FS_CLIP_FILE: /* Clipboard */
		/* Convert color transparency to alpha */
		tr = settings.bpp == 3 ? settings.rgb_trans : settings.xpm_trans;
		if ((res == 1) && (tr >= 0))
		{
			/* Add alpha channel if no alpha yet */
			if (!settings.img[CHN_ALPHA])
			{
				i = settings.width * settings.height;
				/* !!! Create committed */
				mem_clip_alpha = malloc(i);
				if (mem_clip_alpha)
				{
					settings.img[CHN_ALPHA] = mem_clip_alpha;
					memset(mem_clip_alpha, 255, i);
				}
			}
			if (!settings.img[CHN_ALPHA]) res = FILE_MEM_ERROR;
			else mem_mask_colors(settings.img[CHN_ALPHA],
				settings.img[CHN_IMAGE], 0, settings.width,
				settings.height, settings.bpp, tr, tr);
		}
		/* Success - accept data */
		if (res == 1)
		{
			/* !!! Clipboard data committed already */

			/* Accept dimensions */
			mem_clip_w = settings.width;
			mem_clip_h = settings.height;
			mem_clip_bpp = settings.bpp;
		}
		/* Failure needing rollback */
		else if (settings.img[CHN_IMAGE])
		{
			/* !!! Too late to restore previous clipboard */
			free(mem_clipboard);
			free(mem_clip_alpha);
			mem_clip_mask_clear();
		}
		break;
	case FS_CHANNEL_LOAD:
		/* Success - accept data */
		if (res == 1)
		{
			/* Add frame & stuff data into it */
			undo_next_core(4, mem_width, mem_height, mem_img_bpp,
				CMASK_CURR);
			mem_undo_im_[mem_undo_pointer].img[mem_channel] =
				mem_img[mem_channel] = settings.img[CHN_IMAGE];

			if (mem_channel == CHN_IMAGE)
				store_image_extras(&settings);
		}
		/* Failure needing rollback */
		else if (settings.img[CHN_IMAGE]) free(settings.img[CHN_IMAGE]);
		break;
	}
	return (res);
}

int export_undo(char *file_name, ls_settings *settings)
{
	char new_name[300];
	int start = mem_undo_done, res = 0, lenny, i, j;

	strncpy( new_name, file_name, 256);
	lenny = strlen( file_name );

	progress_init( _("Saving UNDO images"), 0 );
	settings->silent = TRUE;

	for (j = 0; j < 2; j++)
	{
		for (i = 1; i <= start + 1; i++)
		{
			if (!res && (!j ^ (settings->mode == FS_EXPORT_UNDO)))
			{
				progress_update((float)i / (start + 1));
				sprintf(new_name + lenny, "%03i.%s", i,
					file_formats[settings->ftype].ext);
				memcpy(settings->img, mem_img, sizeof(chanlist));
				settings->pal = mem_pal;
				settings->width = mem_width;
				settings->height = mem_height;
				settings->bpp = mem_img_bpp;
				settings->colors = mem_cols;
				res = save_image(new_name, settings);
			}
			if (!j) /* Goto first image */
			{
				if (mem_undo_done > 0) mem_undo_backward();
			}
			else if (mem_undo_done < start) mem_undo_forward();
		}
	}

	progress_end();

	return res;
}

int export_ascii ( char *file_name )
{
	char ch[16] = " .,:;+=itIYVXRBM";
	int i, j;
	unsigned char pix;
	FILE *fp;

	if ((fp = fopen(file_name, "w")) == NULL) return -1;

	for ( j=0; j<mem_height; j++ )
	{
		for ( i=0; i<mem_width; i++ )
		{
			pix = mem_img[CHN_IMAGE][ i + mem_width*j ];
			fprintf(fp, "%c", ch[pix % 16]);
		}
		fprintf(fp, "\n");
	}
	fclose(fp);

	return 0;
}


int detect_image_format(char *name)
{
	unsigned char buf[66], *stop;
	int i;
	FILE *fp;

	if (!(fp = fopen(name, "rb"))) return (-1);
	i = fread(buf, 1, 64, fp);
	fclose(fp);

	/* Check all unambiguous signatures */
	if (!strncmp(buf, "\x89PNG", 4)) return (FT_PNG);
	if (!strncmp(buf, "\xFF\xD8", 2))
#ifdef U_JPEG
		return (FT_JPEG);
#else
		return (FT_NONE);
#endif
	if (!strncmp(buf, "II", 2) || !strncmp(buf, "MM", 2))
#ifdef U_TIFF
		return (FT_TIFF);
#else
		return (FT_NONE);
#endif
	if (!strncmp(buf, "GIF8", 4))
#ifdef U_GIF
		return (FT_GIF);
#else
		return (FT_NONE);
#endif
	if (!strncmp(buf, "BM", 2)) return (FT_BMP);
	/* Check layers signature and version */
	if (!strncmp(buf, LAYERS_HEADER, strlen(LAYERS_HEADER)))
	{
		stop = strchr(buf, '\n');
		if (!stop || (stop - buf > 32)) return (FT_NONE);
		i = atoi(++stop);
		if (i == 1) return (FT_LAYERS1);
/* !!! Not implemented yet */
//		if (i == 2) return (FT_LAYERS2);
		return (FT_NONE);
	}

/* !!! Not implemented yet */
#if 0
	/* Discern PCX from TGA */
	while (buf[0] == 10)
	{
		if (buf[1] > 5) break;
		if (buf[1] > 1) return (FT_PCX);
		if (buf[2] != 1) break;
		/* Ambiguity - look at name as a last resort
		 * Bias to PCX - TGAs usually have 0th byte = 0 */
		stop = strrchr(name, '.');
		if (!stop) return (FT_PCX);
		if (!strncasecmp(stop + 1, "tga", 4)) break;
		return (FT_PCX);
	}
	/* Check if this is TGA */
	if ((buf[1] < 2) && (buf[2] < 12) && ((1 << buf[2]) & 0x0E0F))
		return (FT_TGA);
#endif

	/* Simple check for XPM */
	stop = strstr(buf, "XPM");
	if (stop)
	{
		i = stop - buf;
		stop = strchr(buf, '\n');
		if (!stop || (stop - buf > i)) return (FT_XPM);
	}
	/* Check possibility of XBM by absence of control chars */
	for (i = 0; buf[i] && (buf[i] != '\n'); i++)
	{
		if (ISCNTRL(buf[i])) return (FT_NONE);
	}
	return (FT_XBM);
}
