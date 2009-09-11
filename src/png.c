/*	png.c
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

/* Rewritten for version 3.10 by Dmitry Groshev */

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#define PNG_READ_PACK_SUPPORTED

#include <png.h>
#include <zlib.h>
#ifdef U_GIF
#include <gif_lib.h>
#endif
#ifdef U_JPEG
#include <jpeglib.h>
#endif
#ifdef U_JP2
#include <openjpeg.h>
#endif
#ifdef U_TIFF
#include <tiffio.h>
#endif

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "canvas.h"
#include "toolbar.h"
#include "layer.h"
#include "ani.h"
#include "inifile.h"


char preserved_gif_filename[PATHBUF];
int preserved_gif_delay = 10, silence_limit, jpeg_quality, png_compression;
int tga_RLE, tga_565, tga_defdir, jp2_rate, undo_load;

fformat file_formats[NUM_FTYPES] = {
	{ "", "", "", 0},
	{ "PNG", "png", "", FF_256 | FF_RGB | FF_ALPHA | FF_MULTI
		| FF_TRANS | FF_COMPZ | FF_MEM },
#ifdef U_JPEG
	{ "JPEG", "jpg", "jpeg", FF_RGB | FF_COMPJ },
#else
	{ "", "", "", 0},
#endif
#ifdef U_JP2
	{ "JPEG2000", "jp2", "", FF_RGB | FF_ALPHA | FF_COMPJ2 },
	{ "J2K", "j2k", "", FF_RGB | FF_ALPHA | FF_COMPJ2 },
#else
	{ "", "", "", 0},
	{ "", "", "", 0},
#endif
#ifdef U_TIFF
/* !!! Ideal state */
//	{ "TIFF", "tif", "tiff", FF_256 | FF_RGB | FF_ALPHA | FF_MULTI
//		/* | FF_TRANS | FF_LAYER */ },
/* !!! Current state */
	{ "TIFF", "tif", "tiff", FF_256 | FF_RGB | FF_ALPHA },
#else
	{ "", "", "", 0},
#endif
#ifdef U_GIF
	{ "GIF", "gif", "", FF_256 | FF_ANIM | FF_TRANS },
#else
	{ "", "", "", 0},
#endif
	{ "BMP", "bmp", "", FF_256 | FF_RGB | FF_ALPHAR | FF_MEM },
	{ "XPM", "xpm", "", FF_256 | FF_RGB | FF_TRANS | FF_SPOT },
	{ "XBM", "xbm", "", FF_BW | FF_SPOT },
	{ "LSS16", "lss", "", FF_16 },
/* !!! Ideal state */
//	{ "TGA", "tga", "", FF_256 | FF_RGB | FF_ALPHA | FF_MULTI
//		| FF_TRANS | FF_COMPR },
/* !!! Current state */
	{ "TGA", "tga", "", FF_256 | FF_RGB | FF_ALPHAR | FF_TRANS | FF_COMPR },
/* !!! Not supported yet */
//	{ "PCX", "pcx", "", FF_256 | FF_RGB },
/* !!! Placeholder */
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
/* An X pixmap - not a file at all */
	{ "PIXMAP", "", "", FF_RGB | FF_NOSAVE },
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
	if ((mask & FF_256) && (ext - name > 4) &&
		!strncasecmp(ext - 4, ".gif", 4)) return (FT_GIF);

	return (FT_NONE);
}

/* Receives struct with image parameters, and channel flags;
 * returns 0 for success, or an error code;
 * success doesn't mean that anything was allocated, loader must check that;
 * loader may call this multiple times - say, for each channel */
static int allocate_image(ls_settings *settings, int cmask)
{
	size_t sz, l;
	int i, j, oldmask;

	if ((settings->width < 1) || (settings->height < 1)) return (-1);

	if ((settings->width > MAX_WIDTH) || (settings->height > MAX_HEIGHT))
		return (TOO_BIG);

	/* Don't show progress bar where there's no need */
	if (settings->width * settings->height <= (1<<silence_limit))
		settings->silent = TRUE;

	/* Reduce cmask according to mode */
	if (settings->mode == FS_CLIP_FILE) cmask &= CMASK_CLIP;
	else if ((settings->mode == FS_CHANNEL_LOAD) ||
		(settings->mode == FS_PATTERN_LOAD)) cmask &= CMASK_IMAGE;

	/* Overwriting is allowed */
	oldmask = cmask_from(settings->img);
	cmask &= ~oldmask;
	if (!cmask) return (0); // Already allocated

	/* No utility channels without image */
	oldmask |= cmask;
	if (!(oldmask & CMASK_IMAGE)) return (-1);

	j = TRUE; // For FS_LAYER_LOAD
	sz = (size_t)settings->width * settings->height;
	switch (settings->mode)
	{
	case FS_PNG_LOAD: /* Regular image */
		/* Reserve memory */
		j = undo_next_core(UC_CREATE | UC_GETMEM, settings->width,
			settings->height, settings->bpp, oldmask);
		/* Drop current image if not enough memory for undo */
		if (j) mem_free_image(&mem_image, FREE_IMAGE);
	case FS_LAYER_LOAD: /* Layers */
		/* Allocate, or at least try to */
		for (i = 0; i < NUM_CHANNELS; i++)
		{
			if (!(cmask & CMASK_FOR(i))) continue;
			l = i == CHN_IMAGE ? sz * settings->bpp : sz;
			settings->img[i] = j ? malloc(l) : mem_try_malloc(l);
			if (!settings->img[i]) return (FILE_MEM_ERROR);
		}
		break;
	case FS_CLIP_FILE: /* Clipboard */
		/* Allocate the entire batch at once */
		if (cmask & CMASK_IMAGE)
		{
			j = mem_clip_new(settings->width, settings->height,
				settings->bpp, cmask, FALSE);
			if (j) return (FILE_MEM_ERROR);
			memcpy(settings->img, mem_clip.img, sizeof(chanlist));
			break;
		}
		/* Try to add clipboard alpha and/or mask */
		for (i = 0; i < NUM_CHANNELS; i++)
		{
			if (!(cmask & CMASK_FOR(i))) continue;
			if (!(settings->img[i] = mem_clip.img[i] = malloc(sz)))
				return (FILE_MEM_ERROR);
		}
		break;
	case FS_CHANNEL_LOAD: /* Current channel */
		/* Dimensions & depth have to be the same */
		if ((settings->width != mem_width) ||
			(settings->height != mem_height) ||
			(settings->bpp != MEM_BPP)) return (-1);
		/* Reserve memory */
		j = undo_next_core(UC_CREATE | UC_GETMEM, settings->width,
			settings->height, settings->bpp, CMASK_CURR);
		if (j) return (FILE_MEM_ERROR);
		/* Allocate */
		settings->img[CHN_IMAGE] = mem_try_malloc(sz * settings->bpp);
		if (!settings->img[CHN_IMAGE]) return (FILE_MEM_ERROR);
		break;
	case FS_PATTERN_LOAD: /* Patterns */
		settings->silent = TRUE;
		/* Fixed dimensions and depth */
		if ((settings->width != PATTERN_GRID_W * 8) ||
			(settings->height != PATTERN_GRID_H * 8) ||
			(settings->bpp != 1)) return (-1);
		/* Allocate temp memory */
		settings->img[CHN_IMAGE] = calloc(1, sz);
		if (!settings->img[CHN_IMAGE]) return (FILE_MEM_ERROR);
		break;
	}
	return (0);
}

/* Receives struct with image parameters, and which channels to deallocate */
static void deallocate_image(ls_settings *settings, int cmask)
{
	int i;

	/* No deallocating image channel */
	if (!(cmask &= ~CMASK_IMAGE)) return;

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!(cmask & CMASK_FOR(i))) continue;
		if (!settings->img[i]) continue;

		free(settings->img[i]);
		settings->img[i] = NULL;

		if (settings->mode == FS_CLIP_FILE) /* Clipboard */
			mem_clip.img[i] = NULL;
	}
}

typedef struct {
	FILE *file; // for traditional use
	char *buf;  // data block
	int here; // current position
	int top;  // end of data
	int size; // currently allocated
} memFILE;

static int mfextend(memFILE *mf, size_t length)
{
	size_t l = mf->here + length, l2 = mf->size * 2;
	unsigned char *tmp = NULL;

	if (l2 > l) tmp = realloc(mf->buf, l2);
	if (!tmp) tmp = realloc(mf->buf, l2 = l);
	if (!tmp) return (FALSE);
	mf->buf = tmp;
	mf->size = l2;
	return (TRUE);
}

static size_t mfread(void *ptr, size_t size, size_t nmemb, memFILE *mf)
{
	size_t l, m;

	if (mf->file) return (fread(ptr, size, nmemb, mf->file));

	l = size * nmemb; m = mf->top - mf->here;
	if ((mf->here < 0) || (m < 0)) return (0);
	if (l > m) l = m , nmemb = m / size;
	memcpy(ptr, mf->buf + mf->here, l);
	mf->here += l;
	return (nmemb);
}

static size_t mfwrite(void *ptr, size_t size, size_t nmemb, memFILE *mf)
{
	size_t l, m;

	if (mf->file) return (fwrite(ptr, size, nmemb, mf->file));

	if (mf->here < 0) return (0);
	l = size * nmemb; m = mf->size - mf->here;
	if ((l > m) && !mfextend(mf, l)) l = m , nmemb = m / size;
	memcpy(mf->buf + mf->here, ptr, l);
// !!! Nothing in here does fseek() when writing, so no need to track mf->top
	mf->top = mf->here += l;
	return (nmemb);
}

static int mfseek(memFILE *mf, long offset, int mode)
{
// !!! For operating on tarballs, adjust fseek() params here
	if (mf->file) return (fseek(mf->file, offset, mode));

	if (mode == SEEK_SET);
	else if (mode == SEEK_CUR) offset += mf->here;
	else if (mode == SEEK_END) offset += mf->top;
	else return (-1);
	mf->here = offset;
	return (0);
}

static void ls_init(char *what, int save)
{
	char buf[256];

	sprintf(buf, save ? _("Saving %s image") : _("Loading %s image"), what);
	progress_init(buf, 0);
}

/* !!! libpng 1.2.17 or later loses extra chunks if there's no callback */
static int buggy_libpng_handler()
{
	return (0);
}

static void png_memread(png_structp png_ptr, png_bytep data, png_size_t length)
{
	memFILE *mf = (memFILE *)png_get_io_ptr(png_ptr);
//	memFILE *mf = (memFILE *)png_ptr->io_ptr;
	size_t l = mf->top - mf->here;

	if (l > length) l = length;
	memcpy(data, mf->buf + mf->here, l);
	mf->here += l;
	if (l < length) png_error(png_ptr, "Read Error");
}

static void png_memwrite(png_structp png_ptr, png_bytep data, png_size_t length)
{
	memFILE *mf = (memFILE *)png_get_io_ptr(png_ptr);
//	memFILE *mf = (memFILE *)png_ptr->io_ptr;

	if ((mf->here + length > mf->size) && !mfextend(mf, length))
		png_error(png_ptr, "Write Error");
	else
	{
		memcpy(mf->buf + mf->here, data, length);
		mf->top = mf->here += length;
	}
}

static void png_memflush(png_structp png_ptr)
{
	/* Does nothing */
}

#define PNG_BYTES_TO_CHECK 8
#define PNG_HANDLE_CHUNK_ALWAYS 3

static const char *chunk_names[NUM_CHANNELS] = { "", "alPh", "seLc", "maSk" };

static int load_png(char *file_name, ls_settings *settings, memFILE *mf)
{
	/* Description of PNG interlacing passes as X0, DX, Y0, DY */
	static const unsigned char png_interlace[8][4] = {
		{0, 1, 0, 1}, /* One pass for non-interlaced */
		{0, 8, 0, 8}, /* Seven passes for Adam7 interlaced */
		{4, 8, 0, 8},
		{0, 4, 4, 8},
		{2, 4, 0, 4},
		{0, 2, 2, 4},
		{1, 2, 0, 2},
		{0, 1, 1, 2}
	};
	static png_bytep *row_pointers;
	static char *msg;
	png_structp png_ptr;
	png_infop info_ptr;
	png_color_16p trans_rgb;
	png_unknown_chunkp uk_p;
	png_bytep trans;
	png_colorp png_palette;
	png_uint_32 pwidth, pheight;
	char buf[PNG_BYTES_TO_CHECK + 1];
	unsigned char *src, *dest, *dsta;
	long dest_len;
	FILE *fp = NULL;
	int i, j, k, bit_depth, color_type, interlace_type, num_uk, res = -1;
	int maxpass, x0, dx, y0, dy, n, nx, height, width;

	if (!mf)
	{
		if ((fp = fopen(file_name, "rb")) == NULL) return -1;
		i = fread(buf, 1, PNG_BYTES_TO_CHECK, fp);
	}
	else i = mfread(buf, 1, PNG_BYTES_TO_CHECK, mf);
	if (i != PNG_BYTES_TO_CHECK) goto fail;
	if (png_sig_cmp(buf, 0, PNG_BYTES_TO_CHECK)) goto fail;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) goto fail;

	row_pointers = NULL; msg = NULL;
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) goto fail2;

	if (setjmp(png_jmpbuf(png_ptr)))
	{
		res = FILE_LIB_ERROR;
		goto fail2;
	}

	/* !!! libpng 1.2.17+ needs this to read extra channels */
	png_set_read_user_chunk_fn(png_ptr, NULL, buggy_libpng_handler);

	if (!mf) png_init_io(png_ptr, fp);
	else png_set_read_fn(png_ptr, mf, png_memread);
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
			msg = "PNG";
			break;
		case FS_CLIP_FILE:
			msg = _("Clipboard");
			break;
		}
	}
	if (msg) ls_init(msg, 0);

	/* RGB PNG file */
	if (settings->bpp == 3)
	{
		png_set_strip_16(png_ptr);
		png_set_gray_1_2_4_to_8(png_ptr);
		png_set_palette_to_rgb(png_ptr);
		png_set_gray_to_rgb(png_ptr);

		if (png_get_valid(png_ptr, info_ptr, PNG_INFO_PLTE))
		{
			png_get_PLTE(png_ptr, info_ptr, &png_palette, &settings->colors);
			memcpy(settings->pal, png_palette, settings->colors * sizeof(png_color));
		}

		/* Is there a transparent color? */
		if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		{
			png_get_tRNS(png_ptr, info_ptr, 0, 0, &trans_rgb);
			if (color_type == PNG_COLOR_TYPE_GRAY)
			{
				i = trans_rgb->gray;
				switch (bit_depth)
				{
				case 1: i *= 0xFF; break;
				case 2: i *= 0x55; break;
				case 4: i *= 0x11; break;
				case 8: default: break;
				/* Hope libpng compiled w/o accurate transform */
				case 16: i >>= 8; break;
				}
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
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
fail:	if (fp) fclose(fp);
	return (res);
}

#ifndef PNG_AFTER_IDAT
#define PNG_AFTER_IDAT 8
#endif

static int save_png(char *file_name, ls_settings *settings, memFILE *mf)
{
	png_unknown_chunk unknown0;
	png_structp png_ptr;
	png_infop info_ptr;
	FILE *fp = NULL;
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
		mess = "PNG";
		break;
	case FS_CLIP_FILE:
		mess = _("Clipboard");
		break;
	case FS_COMPOSITE_SAVE:
		mess = _("Layer");
		break;
	case FS_CHANNEL_SAVE:
		mess = _("Channel");
		break;
	default:
		mess = NULL;
		break;
	}
	if (settings->silent) mess = NULL;

	if (!mf && ((fp = fopen(file_name, "wb")) == NULL)) goto exit0;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);

	if (!png_ptr) goto exit1;

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) goto exit2;

	res = 0;

	if (!mf) png_init_io(png_ptr, fp);
	else png_set_write_fn(png_ptr, mf, png_memwrite, png_memflush);
	png_set_compression_level(png_ptr, settings->png_compression);

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
		png_set_PLTE(png_ptr, info_ptr, settings->pal, settings->colors);
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

	if (mess) ls_init(mess, 1);

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
				if (compress2(tmp, &dest_len, settings->img[i], w,
					settings->png_compression) != Z_OK) continue;
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
exit2:	png_destroy_write_struct(&png_ptr, &info_ptr);
exit1:	if (fp) fclose(fp);
exit0:	free(rgba_row);
	return (res);
}

#ifdef U_GIF
static int load_gif(char *file_name, ls_settings *settings)
{
	/* GIF interlace pattern: Y0, DY, ... */
	static const unsigned char interlace[10] =
		{ 0, 1, 0, 8, 4, 8, 2, 4, 1, 2 };
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

			if (!settings->silent) ls_init("GIF", 0);

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
#ifndef WIN32
	mode_t mode;
#endif


	/* GIF save must be on indexed image */
	if (settings->bpp != 1) return WRONG_FORMAT;

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

	if (!settings->silent) ls_init("GIF", 1);

	for (i = 0; i < h; i++)
	{
		EGifPutLine(giffy, settings->img[CHN_IMAGE] + i * w, w);
		if (!settings->silent && ((i * 20) % h >= h - 20))
			progress_update((float)i / h);
	}
	if (!settings->silent) progress_end();
	msg = 0;

fail:	EGifCloseFile(giffy);
#ifndef WIN32
	/* giflib creates files with 0600 permissions, which is nasty - WJ */
	mode = umask(0022);
	umask(mode);
	chmod(file_name, 0666 & ~mode);
#endif
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
	static int pr;
	struct jpeg_decompress_struct cinfo;
	unsigned char *memp;
	FILE *fp;
	int i, width, height, bpp, res = -1;


	if ((fp = fopen(file_name, "rb")) == NULL) return (-1);

	pr = 0;
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
	bpp = 3;
	if (cinfo.output_components == 1) /* Greyscale */
	{
		settings->colors = 256;
		mem_scale_pal(settings->pal, 0, 0,0,0, 255, 255,255,255);
		bpp = 1;
	}
	settings->width = width = cinfo.output_width;
	settings->height = height = cinfo.output_height;
	settings->bpp = bpp;
	if ((res = allocate_image(settings, CMASK_IMAGE))) goto fail;
	res = -1;
	pr = !settings->silent;

	if (pr) ls_init("JPEG", 0);

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


	if (settings->bpp == 1) return WRONG_FORMAT;

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
	if (!settings->silent) ls_init("JPEG", 1);
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

#ifdef U_JP2

/* *** PREFACE ***
 * OpenJPEG version 1.1.1 is wasteful in the extreme, with memory overhead of
 * several times the unpacked image size. So it can fail to handle even such
 * resolutions that fit into available memory with lots of room to spare. */

static void stupid_callback(const char *msg, void *client_data)
{
}

static int load_jpeg2000(char *file_name, ls_settings *settings)
{
	opj_dparameters_t par;
	opj_dinfo_t *dinfo;
	opj_cio_t *cio = NULL;
	opj_image_t *image = NULL;
	opj_image_comp_t *comp;
	opj_event_mgr_t useless_events; // !!! Silently made mandatory in v1.2
	unsigned char xtb[256], *dest, *buf = NULL;
	FILE *fp;
	int i, j, k, l, w, h, w0, nc, pr, step, delta, shift;
	int *src, cmask = CMASK_IMAGE, codec = CODEC_JP2, res;


	if ((fp = fopen(file_name, "rb")) == NULL) return (-1);

	/* Read in the entire file */
	fseek(fp, 0, SEEK_END);
	l = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = malloc(l);
	res = FILE_MEM_ERROR;
	if (!buf) goto ffail;
	res = FILE_LIB_ERROR;
	i = fread(buf, 1, l, fp);
	if (i < l) goto ffail;
	fclose(fp);
	if ((buf[0] == 0xFF) && (buf[1] == 0x4F)) codec = CODEC_J2K;

	/* Decompress it */
	dinfo = opj_create_decompress(codec);
	if (!dinfo) goto lfail;
	memset(&useless_events, 0, sizeof(useless_events));
	useless_events.error_handler = useless_events.warning_handler =
		useless_events.info_handler = stupid_callback;
	opj_set_event_mgr((opj_common_ptr)dinfo, &useless_events, stderr);
	opj_set_default_decoder_parameters(&par);
	opj_setup_decoder(dinfo, &par);
	cio = opj_cio_open((opj_common_ptr)dinfo, buf, l);
	if (!cio) goto lfail;
	if ((pr = !settings->silent)) ls_init("JPEG2000", 0);
	image = opj_decode(dinfo, cio);
	opj_cio_close(cio);
	opj_destroy_decompress(dinfo);
	free(buf);
	if (!image) goto ifail;
	
	/* Analyze what we got */
// !!! OpenJPEG 1.1.1 does *NOT* properly set image->color_space !!!
	if (image->numcomps < 3) /* Guess this is paletted */
	{
		settings->bpp = 1;
		settings->colors = 256;
		mem_scale_pal(settings->pal, 0, 0,0,0, 255, 255,255,255);
	}
	else settings->bpp = 3;
	if ((nc = settings->bpp) < image->numcomps) nc++ , cmask = CMASK_RGBA;
	comp = image->comps;
	settings->width = w = (comp->w + (1 << comp->factor) - 1) >> comp->factor;
	settings->height = h = (comp->h + (1 << comp->factor) - 1) >> comp->factor;
	for (i = 1; i < nc; i++) /* Check if all components are the same size */
	{
		comp++;
		if ((w != (comp->w + (1 << comp->factor) - 1) >> comp->factor) ||
			(h != (comp->h + (1 << comp->factor) - 1) >> comp->factor))
			goto ifail;
	}
	if ((res = allocate_image(settings, cmask))) goto ifail;

	/* Unpack data */
	for (i = 0 , comp = image->comps; i < nc; i++ , comp++)
	{
		if (i < settings->bpp) /* Image */
		{
			dest = settings->img[CHN_IMAGE] + i;
			step = settings->bpp;
		}
		else /* Alpha */
		{
			dest = settings->img[CHN_ALPHA];
			if (!dest) break; /* No alpha allocated */
			step = 1;
		}
		w0 = comp->w;
		delta = comp->sgnd ? 1 << (comp->prec - 1) : 0;
		shift = comp->prec > 8 ? comp->prec - 8 : 0;
		set_xlate(xtb, comp->prec - shift);
		for (j = 0; j < h; j++)
		{
			src = comp->data + j * w0;
			for (k = 0; k < w; k++)
			{
				*dest = xtb[(src[k] + delta) >> shift];
				dest += step;
			}
		}
	}
	res = 1;
ifail:	if (pr) progress_end();
	opj_image_destroy(image);
	return (res);
lfail:	opj_destroy_decompress(dinfo);
	free(buf);
	return (res);
ffail:	free(buf);
	fclose(fp);
	return (res);
}

static int save_jpeg2000(char *file_name, ls_settings *settings)
{
	opj_cparameters_t par;
	opj_cinfo_t *cinfo;
	opj_image_cmptparm_t channels[4];
	opj_cio_t *cio = NULL;
	opj_image_t *image;
	opj_event_mgr_t useless_events; // !!! Silently made mandatory in v1.2
	unsigned char *src;
	FILE *fp;
	int i, j, k, nc, step;
	int *dest, w = settings->width, h = settings->height, res = -1;


	if (settings->bpp == 1) return WRONG_FORMAT;

	if ((fp = fopen(file_name, "wb")) == NULL) return -1;

	/* Create intermediate structure */
	nc = settings->img[CHN_ALPHA] ? 4 : 3;
	memset(channels, 0, sizeof(channels));
	for (i = 0; i < nc; i++)
	{
		channels[i].prec = channels[i].bpp = 8;
		channels[i].dx = channels[i].dy = 1;
		channels[i].w = settings->width;
		channels[i].h = settings->height;
	}
	image = opj_image_create(nc, channels, CLRSPC_SRGB);
	if (!image) goto ffail;
	image->x0 = image->y0 = 0;
	image->x1 = w; image->y1 = h;

	/* Fill it */
	if (!settings->silent) ls_init("JPEG2000", 1);
	k = w * h;
	for (i = 0; i < nc; i++)
	{
		if (i < 3)
		{
			src = settings->img[CHN_IMAGE] + i;
			step = 3;
		}
		else
		{
			src = settings->img[CHN_ALPHA];
			step = 1;
		}
		dest = image->comps[i].data;
		for (j = 0; j < k; j++ , src += step) dest[j] = *src;
	}

	/* Compress it */
	cinfo = opj_create_compress(settings->ftype == FT_JP2 ? CODEC_JP2 : CODEC_J2K);
	if (!cinfo) goto fail;
	memset(&useless_events, 0, sizeof(useless_events));
	useless_events.error_handler = useless_events.warning_handler =
		useless_events.info_handler = stupid_callback;
	opj_set_event_mgr((opj_common_ptr)cinfo, &useless_events, stderr);
	opj_set_default_encoder_parameters(&par);
	par.tcp_numlayers = 1;
	par.tcp_rates[0] = settings->jp2_rate;
	par.cp_disto_alloc = 1;
	opj_setup_encoder(cinfo, &par, image);
	cio = opj_cio_open((opj_common_ptr)cinfo, NULL, 0);
	if (!cio) goto fail;
	if (!opj_encode(cinfo, cio, image, NULL)) goto fail;

	/* Write it */
	k = cio_tell(cio);
	if (fwrite(cio->buffer, 1, k, fp) == k) res = 0;

fail:	if (cio) opj_cio_close(cio);
	opj_destroy_compress(cinfo);
	opj_image_destroy(image);
	if (!settings->silent) progress_end();
ffail:	fclose(fp);
	return (res);
}
#endif

/* Slow-but-sure universal bitstream parsers; may read extra byte at the end */
static void stream_MSB(unsigned char *src, unsigned char *dest, int cnt,
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

static void stream_LSB(unsigned char *src, unsigned char *dest, int cnt,
	int bits, int bit0, int bitstep, int step)
{
	int i, j, v, mask = (1 << bits) - 1;

	for (i = 0; i < cnt; i++)
	{
		j = bit0 >> 3;
		v = (src[j + 1] << 8) | src[j];
		v >>= bit0 & 7;
		*dest = (unsigned char)(v & mask);
		bit0 += bitstep;
		dest += step;
	}
}

#ifdef U_TIFF

/* *** PREFACE ***
 * TIFF is a bitch, and libtiff is a joke. An unstable and buggy joke, at that.
 * It's a fact of life - and when some TIFFs don't load or are mangled, that
 * also is a fact of life. Installing latest libtiff may help - or not; sending
 * a bugreport with the offending file attached may help too - but again, it's
 * not guaranteed. But the common varieties of TIFF format should load OK. */

static int load_tiff(char *file_name, ls_settings *settings)
{
	char cbuf[1024];
	TIFF *tif;
	uint16 bpsamp, sampp, xsamp, pmetric, planar, orient, sform;
	uint16 *sampinfo, *red16, *green16, *blue16;
	uint32 width, height, tw = 0, th = 0, rps = 0;
	uint32 *tr, *raster = NULL;
	unsigned char xtable[256], *tmp, *src, *buf = NULL;
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
	/* Photoshop writes alpha as EXTRASAMPLE_UNSPECIFIED anyway */
	if (xsamp) cmask = CMASK_RGBA;

	/* !!! No alpha support for RGB mode yet */
	if (argb) cmask = CMASK_IMAGE;

	if ((res = allocate_image(settings, cmask))) goto fail;
	res = -1;

	if ((pr = !settings->silent)) ls_init("TIFF", 0);

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

		/* !!! Assume 16-, 32- and 64-bit data follow machine's
		 * endianness, and everything else is packed big-endian way -
		 * like TIFF 6.0 spec says; but TIFF 5.0 and before specs said
		 * differently, so let's wait for examples to see if I'm right
		 * or not; as for 24- and 128-bit, even different libtiff
		 * versions handle them differently, so I leave them alone
		 * for now - WJ */

		bit0 = (G_BYTE_ORDER == G_LITTLE_ENDIAN) &&
			((bpsamp == 16) || (bpsamp == 32) ||
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
				stream_MSB(src, tmp, w, bits1, bit0, db, dx);
				if (planar) continue;
				if (bpp == 3)
				{
					stream_MSB(src, tmp + 1, w, bits1,
						bit0 + bpsamp, db, dx);
					stream_MSB(src, tmp + 2, w, bits1,
						bit0 + bpsamp * 2, db, dx);
				}
				if (settings->img[CHN_ALPHA])
				{
					dx = mirror & 1 ? -1 : 1;
					tmp = settings->img[CHN_ALPHA] + i;
					stream_MSB(src, tmp, w, bits1,
						bit0 + bpsamp * bpp, db, dx);
				}
				if (pr && ((n * 10) % nx >= nx - 10))
					progress_update((float)n / nx);
			}
		}

		/* Prepare to rescale what we've got */
		memset(xtable, 0, 256);
		set_xlate(xtable, bits1);

		/* Un-associate alpha & rescale image data */
		j = width * height;
		tmp = settings->img[CHN_IMAGE];
		src = settings->img[CHN_ALPHA];
		while (src) /* Have alpha */
		{
			/* Unassociate alpha */
			if ((pmetric != PHOTOMETRIC_PALETTE) &&
				(sampinfo[0] == EXTRASAMPLE_ASSOCALPHA))
			{
				mem_demultiply(tmp, src, j, bpp);
				if (bits1 >= 8) break;
				bits1 = 8;
			}
			else if (bits1 >= 8) break;

			/* Rescale alpha */
			for (i = 0; i < j; i++)
			{
				src[i] = xtable[src[i]];
			}
			break;
		}

		/* Rescale RGB */
		if ((bpp == 3) && (bits1 < 8))
		{
			for (i = 0; i < j * 3; i++)
			{
				tmp[i] = xtable[tmp[i]];
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
			mem_scale_pal(settings->pal, k, 0,0,0, j ^ k, 255,255,255);
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
	if (!settings->silent) ls_init("TIFF", 1);
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

/* Macros for accessing values in Intel byte order */
#define GET16(buf) (((buf)[1] << 8) + (buf)[0])
#define GET32(buf) (((buf)[3] << 24) + ((buf)[2] << 16) + ((buf)[1] << 8) + (buf)[0])
#define PUT16(buf, v) (buf)[0] = (v) & 0xFF; (buf)[1] = (v) >> 8;
#define PUT32(buf, v) (buf)[0] = (v) & 0xFF; (buf)[1] = ((v) >> 8) & 0xFF; \
	(buf)[2] = ((v) >> 16) & 0xFF; (buf)[3] = (v) >> 24;

/* Version 2 fields */
#define BMP_FILESIZE  2		/* 32b */
#define BMP_DATAOFS  10		/* 32b */
#define BMP_HDR2SIZE 14		/* 32b */
#define BMP_WIDTH    18		/* 32b */
#define BMP_HEIGHT   22		/* 32b */
#define BMP_PLANES   26		/* 16b */
#define BMP_BPP      28		/* 16b */
#define BMP2_HSIZE   30
/* Version 3 fields */
#define BMP_COMPRESS 30		/* 32b */
#define BMP_DATASIZE 34		/* 32b */
#define BMP_COLORS   46		/* 32b */
#define BMP_ICOLORS  50		/* 32b */
#define BMP3_HSIZE   54
/* Version 4 fields */
#define BMP_RMASK    54		/* 32b */
#define BMP_GMASK    58		/* 32b */
#define BMP_BMASK    62		/* 32b */
#define BMP_AMASK    66		/* 32b */
#define BMP_CSPACE   70		/* 32b */
#define BMP4_HSIZE  122
#define BMP5_HSIZE  138
#define BMP_MAXHSIZE (BMP5_HSIZE + 256 * 4)

static int load_bmp(char *file_name, ls_settings *settings, memFILE *mf)
{
	guint32 masks[4], m;
	unsigned char hdr[BMP5_HSIZE], xlat[256], *dest, *tmp, *buf = NULL;
	memFILE fake_mf;
	FILE *fp = NULL;
	int shifts[4], bpps[4];
	int def_alpha = FALSE, cmask = CMASK_IMAGE, comp = 0, res = -1;
	int i, j, k, n, ii, w, h, bpp;
	int l, bl, rl, step, skip, dx, dy;


	if (!mf)
	{
		if (!(fp = fopen(file_name, "rb"))) return (-1);
		memset(mf = &fake_mf, 0, sizeof(fake_mf));
		fake_mf.file = fp;
	}

	/* Read the largest header */
	k = mfread(hdr, 1, BMP5_HSIZE, mf);

	/* Check general validity */
	if (k < BMP2_HSIZE) goto fail; /* Least supported header size */
	if ((hdr[0] != 'B') || (hdr[1] != 'M')) goto fail; /* Signature */
	l = GET32(hdr + BMP_HDR2SIZE) + BMP_HDR2SIZE;
	if (k < l) goto fail;

	/* Check format */
	if (GET16(hdr + BMP_PLANES) != 1) goto fail; /* Only one plane */
	w = GET32(hdr + BMP_WIDTH);
	h = GET32(hdr + BMP_HEIGHT);
	bpp = GET16(hdr + BMP_BPP);
	if (l >= BMP3_HSIZE) comp = GET32(hdr + BMP_COMPRESS);
	/* Only 1, 4, 8, 16, 24 and 32 bpp allowed */
	switch (bpp)
	{
	case 1: if (comp) goto fail; /* No compression */
		break;
	case 4: if (comp && ((comp != 2) || (h < 0))) goto fail; /* RLE4 */
		break;
	case 8: if (comp && ((comp != 1) || (h < 0))) goto fail; /* RLE8 */
		break;
	case 16: case 24: case 32:
		if (comp && (comp != 3)) goto fail; /* Bitfields */
		shifts[3] = bpps[3] = masks[3] = 0; /* No alpha by default */
		if (comp == 3)
		{
			/* V3-style bitfields? */
			if ((l == BMP3_HSIZE) &&
				(GET32(hdr + BMP_DATAOFS) >= BMP_AMASK))
				l = BMP_AMASK;
			if (l < BMP_AMASK) goto fail;
			masks[0] = GET32(hdr + BMP_RMASK);
			masks[1] = GET32(hdr + BMP_GMASK);
			masks[2] = GET32(hdr + BMP_BMASK);
			if (l >= BMP_AMASK + 4)
				masks[3] = GET32(hdr + BMP_AMASK);
			if (masks[3]) cmask = CMASK_RGBA;

			/* Convert masks into bit lengths and offsets */
			for (i = 0; i < 4; i++)
			{
				/* Bit length - just count bits */
				j = (masks[i] & 0x55555555) +
					((masks[i] >> 1) & 0x55555555);
				j = (j & 0x33333333) + ((j >> 2) & 0x33333333);
				j = (j & 0x0F0F0F0F) + ((j >> 4) & 0x0F0F0F0F);
				j = (j & 0x00FF00FF) + ((j >> 8) & 0x00FF00FF);
				j = (j & 0xFFFF) + (j >> 16);
				/* Bit offset - add bits _before_ mask */
				m = ((~masks[i] + 1) & masks[i]) - 1;
				k = (m & 0x55555555) + ((m >> 1) & 0x55555555);
				k = (k & 0x33333333) + ((k >> 2) & 0x33333333);
				k = (k & 0x0F0F0F0F) + ((k >> 4) & 0x0F0F0F0F);
				k = (k & 0x00FF00FF) + ((k >> 8) & 0x00FF00FF);
				k = (k & 0xFFFF) + (k >> 16) + j;
				if (j > 8) j = 8;
				shifts[i] = k - j;
				bpps[i] = j;
			}
		}
		else if (bpp == 16)
		{
			shifts[0] = 10;
			shifts[1] = 5;
			shifts[2] = 0;
			bpps[0] = bpps[1] = bpps[2] = 5;
		}
		else
		{
			shifts[0] = 16;
			shifts[1] = 8;
			shifts[2] = 0;
			bpps[0] = bpps[1] = bpps[2] = 8;
			if (bpp == 32) /* Consider alpha present by default */
			{
				shifts[3] = 24;
				bpps[3] = 8;
				cmask = CMASK_RGBA;
				def_alpha = TRUE; /* Uncertain if alpha */
			}
		}
		break;
	default: goto fail;
	}

	/* Allocate buffer and image */
	settings->width = w;
	settings->height = abs(h);
	settings->bpp = bpp < 16 ? 1 : 3;
	rl = ((w * bpp + 31) >> 3) & ~3; /* Row data length */
	/* For RLE, load all image at once */
	if (comp && (bpp < 16))
		bl = GET32(hdr + BMP_FILESIZE) - GET32(hdr + BMP_DATAOFS);
	/* Otherwise, only one row at a time */
	else bl = rl;
	/* To accommodate full palette and bitparser's extra step */
	buf = malloc(bl < 1024 ? 1024 + 1 : bl + 1);
	res = FILE_MEM_ERROR;
	if (!buf) goto fail2;
	if ((res = allocate_image(settings, cmask))) goto fail2;
	res = -1;

	/* Load palette, if any */
	j = 0;
	if (bpp < 16)
	{
		if (l >= BMP_COLORS + 4) j = GET32(hdr + BMP_COLORS);
		if (!j) j = 1 << bpp;
		k = GET32(hdr + BMP_DATAOFS) - l;
		k /= l < BMP3_HSIZE ? 3 : 4;
		if (!k) goto fail2; /* No palette in file */
		if (k < j) j = k;
	}
	if (j)
	{
		settings->colors = j;
		mfseek(mf, l, SEEK_SET);
		k = l < BMP3_HSIZE ? 3 : 4;
		i = mfread(buf, 1, j * k, mf);
		if (i < j * k) goto fail2; /* Cannot read palette */
		tmp = buf;
		for (i = 0; i < j; i++)
		{
			settings->pal[i].red = tmp[2];
			settings->pal[i].green = tmp[1];
			settings->pal[i].blue = tmp[0];
			tmp += k;
		}
	}

	if (!settings->silent) ls_init("BMP", 0);

	mfseek(mf, GET32(hdr + BMP_DATAOFS), SEEK_SET); /* Seek to data */
	if (h < 0) /* Prepare row loop */
	{
		step = 1;
		i = 0;
		h = -h;
	}
	else
	{
		step = -1;
		i = h - 1;
	}
	res = FILE_LIB_ERROR;

	if ((comp != 1) && (comp != 2)) /* No RLE */
	{
		for (n = 0; (i < h) && (i >= 0); n++ , i += step)
		{
			j = mfread(buf, 1, rl, mf);
			if (j < rl) goto fail3;
			if (bpp < 16) /* Indexed */
			{
				dest = settings->img[CHN_IMAGE] + w * i;
				stream_MSB(buf, dest, w, bpp, 0, bpp, 1);
			}
			else /* RGB */
			{
				dest = settings->img[CHN_IMAGE] + w * i * 3;
				stream_LSB(buf, dest + 0, w, bpps[0],
					shifts[0], bpp, 3);
				stream_LSB(buf, dest + 1, w, bpps[1],
					shifts[1], bpp, 3);
				stream_LSB(buf, dest + 2, w, bpps[2],
					shifts[2], bpp, 3);
				if (settings->img[CHN_ALPHA])
					stream_LSB(buf, settings->img[CHN_ALPHA] +
						w * i, w, bpps[3], shifts[3], bpp, 1);
			}
			if (!settings->silent && ((n * 10) % h >= h - 10))
				progress_update((float)n / h);
		}

		/* Rescale shorter-than-byte RGBA components */
		if (bpp > 8)
		for (i = 0; i < 4; i++)
		{
			if (bpps[i] >= 8) continue;
			k = 3;
			if (i == 3)
			{
				tmp = settings->img[CHN_ALPHA];
				if (!tmp) continue;
				k = 1;
			}
			else tmp = settings->img[CHN_IMAGE] + i;
			set_xlate(xlat, bpps[i]);
			n = w * h;
			for (j = 0; j < n; j++ , tmp += k) *tmp = xlat[*tmp];
		}

		res = 1;
	}
	else /* RLE - always bottom-up */
	{
		k = mfread(buf, 1, bl, mf);
		if (k < bl) goto fail3;
		memset(settings->img[CHN_IMAGE], 0, w * h);
		skip = j = 0;

		dest = settings->img[CHN_IMAGE] + w * i;
		for (tmp = buf; tmp - buf + 1 < k; )
		{
			/* Don't fail on out-of-bounds writes */
			if (*tmp) /* Fill block */
			{
				dx = n = *tmp;
				if (j + n > w) dx = j > w ? 0 : w - j;
				if (bpp == 8) /* 8-bit */
				{
					memset(dest + j, tmp[1], dx);
					j += n; tmp += 2;
					continue;
				}
				for (ii = 0; ii < dx; ii++) /* 4-bit */
				{
					dest[j++] = tmp[1] >> 4;
					if (++ii >= dx) break;
					dest[j++] = tmp[1] & 0xF;
				}
				j += n - dx;
				tmp += 2;
				continue;
			}
			if (tmp[1] > 2) /* Copy block */
			{
				dx = n = tmp[1];
				if (j + n > w) dx = j > w ? 0 : w - j;
				tmp += 2;
				if (bpp == 8) /* 8-bit */
				{
					memcpy(dest + j, tmp, dx);
					j += n; tmp += (n + 1) & ~1;
					continue;
				}
				for (ii = 0; ii < dx; ii++) /* 4-bit */
				{
					dest[j++] = *tmp >> 4;
					if (++ii >= dx) break;
					dest[j++] = *tmp++ & 0xF;
				}
				j += n - dx;
				tmp += (((n + 3) & ~3) - (dx & ~1)) >> 1;
				continue;
			}
			if (tmp[1] == 2) /* Skip block */
			{
				dx = tmp[2] + j;
				dy = tmp[3];
				if ((dx > w) || (i - dy < 0)) goto fail3;
			}
			else /* End-of-something block */
			{
				dx = 0;
				dy = tmp[1] ? i + 1 : 1;
			}
			/* Transparency detected first time? */
			if (!skip && ((dy != 1) || dx || (j < w)))
			{
				if ((res = allocate_image(settings,
					CMASK_FOR(CHN_ALPHA)))) goto fail3;
				res = FILE_LIB_ERROR;
				skip = 1;
				if (settings->img[CHN_ALPHA]) /* Got alpha */
				{
					memset(settings->img[CHN_ALPHA], 255, w * h);
					skip = 2;
				}
			}
			/* Row skip */
			for (ii = 0; ii < dy; ii++ , i--)
			{
				if (skip > 1) memset(settings->img[CHN_ALPHA] +
					w * i + j, 0, w - j);
				j = 0;
				if (!settings->silent && ((i * 10) % h >= h - 10))
					progress_update((float)(h - i - 1) / h);
			}
			/* Column skip */
			if (skip > 1) memset(settings->img[CHN_ALPHA] +
				w * i + j, 0, dx - j);
			j = dx;
			if (tmp[1] == 1) /* End-of-file block */
			{
				res = 1;
				break;
			}
			dest = settings->img[CHN_IMAGE] + w * i;
			tmp += 2 + tmp[1];
		}
	}

	/* Check if alpha channel is valid */
	if (def_alpha && settings->img[CHN_ALPHA])
	{
		tmp = settings->img[CHN_ALPHA];
		j = settings->width * settings->height;
		for (i = 0; !tmp[i] && (i < j); i++);
		/* Delete all-zero "alpha" */
		if (i >= j) deallocate_image(settings, CMASK_FOR(CHN_ALPHA));
	}

fail3:	if (!settings->silent) progress_end();
fail2:	free(buf);
fail:	if (fp) fclose(fp);
	return (res);
}

/* Use BMP4 instead of BMP3 for images with alpha */
/* #define USE_BMP4 */ /* Most programs just use 32-bit RGB BMP3 for RGBA */

static int save_bmp(char *file_name, ls_settings *settings, memFILE *mf)
{
	unsigned char *buf, *tmp, *src;
	memFILE fake_mf;
	FILE *fp = NULL;
	int i, j, ll, hsz0, hsz, dsz, fsz;
	int w = settings->width, h = settings->height, bpp = settings->bpp;

	i = w > BMP_MAXHSIZE / 4 ? w * 4 : BMP_MAXHSIZE;
	buf = malloc(i);
	if (!buf) return (-1);
	memset(buf, 0, i);

	if (!mf)
	{
		if (!(fp = fopen(file_name, "wb")))
		{
			free(buf);
			return (-1);
		}
		memset(mf = &fake_mf, 0, sizeof(fake_mf));
		fake_mf.file = fp;
	}

	/* Sizes of BMP parts */
	if ((bpp == 3) && settings->img[CHN_ALPHA]) bpp = 4;
	ll = (bpp * w + 3) & ~3;
	j = bpp == 1 ? settings->colors : 0;

#ifdef USE_BMP4
	hsz0 = bpp == 4 ? BMP4_HSIZE : BMP3_HSIZE;
#else
	hsz0 = BMP3_HSIZE;
#endif
	hsz = hsz0 + j * 4;
	dsz = ll * h;
	fsz = hsz + dsz;

	/* Prepare header */
	buf[0] = 'B'; buf[1] = 'M';
	PUT32(buf + BMP_FILESIZE, fsz);
	PUT32(buf + BMP_DATAOFS, hsz);
	i = hsz0 - BMP_HDR2SIZE;
	PUT32(buf + BMP_HDR2SIZE, i);
	PUT32(buf + BMP_WIDTH, w);
	PUT32(buf + BMP_HEIGHT, h);
	PUT16(buf + BMP_PLANES, 1);
	PUT16(buf + BMP_BPP, bpp * 8);
#ifdef USE_BMP4
	i = bpp == 4 ? 3 : 0; /* Bitfield "compression" / no compression */
	PUT32(buf + BMP_COMPRESS, i);
#else
	PUT32(buf + BMP_COMPRESS, 0); /* No compression */
#endif
	PUT32(buf + BMP_DATASIZE, dsz);
	PUT32(buf + BMP_COLORS, j);
	PUT32(buf + BMP_ICOLORS, j);
#ifdef USE_BMP4
	if (bpp == 4)
	{
		memset(buf + BMP_RMASK, 0, BMP4_HSIZE - BMP_RMASK);
		buf[BMP_RMASK + 2] = buf[BMP_GMASK + 1] = buf[BMP_BMASK + 0] =
			buf[BMP_AMASK + 3] = 0xFF; /* Masks for 8-bit BGRA */
		buf[BMP_CSPACE] = 1; /* Device-dependent RGB */
	}
#endif
	tmp = buf + hsz0;
	for (i = 0; i < j; i++ , tmp += 4)
	{
		tmp[0] = settings->pal[i].blue;
		tmp[1] = settings->pal[i].green;
		tmp[2] = settings->pal[i].red;
	}
	mfwrite(buf, 1, tmp - buf, mf);

	/* Write rows */
	if (!settings->silent) ls_init("BMP", 1);
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
		mfwrite(buf, 1, ll, mf);
		if (!settings->silent && ((i * 20) % h >= h - 20))
			progress_update((float)(h - i) / h);
	}
	if (fp) fclose(fp);

	if (!settings->silent) progress_end();

	free(buf);
	return 0;
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
#define ISALPHA(x) (ctypes[(unsigned char)(x)] & 4)
#define ISALNUM(x) (ctypes[(unsigned char)(x)] & 6)
#define ISCNTRL(x) (!ctypes[(unsigned char)(x)])

/* Reads text and cuts out C-style comments */
static char *fgetsC(char *buf, int len, FILE *f)
{
	static int in_comment;
	char *res;
	int i, l, in_string = 0, has_chars = 0;

	if (!len) /* Init */
	{
		*buf = '\0';
		in_comment = 0;
		return (NULL);
	}

	while (TRUE)
	{
		/* Read a line */
		buf[0] = '\0';
		res = fgets(buf, len, f);
		if (res != buf) return (res);

		/* Scan it for comments */
		l = strlen(buf);
		for (i = 0; i < l; i++)
		{
			if (in_string)
			{
				if ((buf[i] == '"') && (buf[i - 1] != '\\'))
					in_string = 0; /* Close a string */
				continue;
			}
			if (in_comment)
			{
				if ((buf[i] == '/') && i && (buf[i - 1] == '*'))
				{
					/* Replace comment by a single space */
					buf[in_comment - 1] = ' ';
					memcpy(buf + in_comment, buf + i + 1, l - i);
					l = in_comment + l - i - 1;
					i = in_comment - 1;
					in_comment = 0;
				}
				continue;
			}
			if (!ISSPACE(buf[i])) has_chars++;
			if (buf[i] == '"')
				in_string = 1; /* Open a string */
			else if ((buf[i] == '*') && i && (buf[i - 1] == '/'))
			{
				/* Open a comment */
				in_comment = i;
				has_chars -= 2;
			}
		}
		/* For simplicity, have strings terminate on the same line */
		if (in_string) return (NULL);

		/* All line is a comment - read the next one */
		if (in_comment == 1) continue;

		/* Cut off and remember non-closed comment */
		if (in_comment)
		{
			buf[in_comment - 1] = '\0';
			in_comment = 1;
		}

		/* All line is whitespace - read the next one */
		if (!has_chars) continue;
		
		return (res);
	}
}

/* "One at a time" hash function */
static guint32 hashf(guint32 seed, char *key, int len)
{
	int i;

	for (i = 0; i < len; i++)
	{
		seed += key[i];
		seed += seed << 10;
		seed ^= seed >> 6;
	}
	seed += seed << 3;
	seed ^= seed >> 11;
	seed += seed << 15;
	return (seed);
} 

#define HASHSEED 0x811C9DC5
#define HASH_RND(X) ((X) * 0x10450405 + 1)
#define HSIZE 16384
#define HMASK 0x1FFF
/* For cuckoo hashing of 4096 items into 16384 slots */
#define MAXLOOP 39

/* This is the limit from libXPM */
#define XPM_MAXCOL 4096

/* Cuckoo hash of IDs for load or RGB triples for save */
typedef struct {
	short hash[HSIZE];
	char *keys;
	int step, cpp, cnt;
	guint32 seed;
} str_hash;

static int ch_find(str_hash *cuckoo, char *str)
{
	guint32 key;
	int k, idx, step = cuckoo->step, cpp = cuckoo->cpp;

	key = hashf(cuckoo->seed, str, cpp);
	k = (key & HMASK) * 2;
	while (TRUE)
	{
		idx = cuckoo->hash[k];
		if (idx && !strncmp(cuckoo->keys + (idx - 1) * step, str, cpp))
			return (idx);
		if (k & 1) return (0); /* Not found */
		k = ((key >> 16) & HMASK) * 2 + 1;
	}
}

static int ch_insert(str_hash *cuckoo, char *str)
{
	char *p, *keys;
	guint32 key;
	int i, j, k, n, idx, step, cpp;

	n = ch_find(cuckoo, str);
	if (n) return (n - 1);

	keys = cuckoo->keys;
	step = cuckoo->step; cpp = cuckoo->cpp;
	if (cuckoo->cnt >= XPM_MAXCOL) return (-1);
	p = keys + cuckoo->cnt++ * step;
	memcpy(p, str, cpp); p[cpp] = 0;

	for (n = cuckoo->cnt; n <= cuckoo->cnt; n++)
	{	
		idx = n;
		/* Normal cuckoo process */
		for (i = 0; i < MAXLOOP; i++)
		{
			key = hashf(cuckoo->seed, keys + (idx - 1) * step, cpp);
			key >>= (i & 1) << 4;
			j = (key & HMASK) * 2 + (i & 1);
			k = cuckoo->hash[j];
			cuckoo->hash[j] = idx;
			idx = k;
			if (!idx) break;
		}
		if (!idx) continue;
		/* Failed insertion - mutate seed */
		cuckoo->seed = HASH_RND(cuckoo->seed);
		memset(cuckoo->hash, 0, sizeof(short) * HSIZE);
		n = 1; /* Rehash everything */
	}
	return (cuckoo->cnt - 1);
}

#define XPM_COL_DEFS 5

/* Comments are allowed where valid; but missing newlines or newlines where
 * should be none aren't tolerated */
static int load_xpm(char *file_name, ls_settings *settings)
{
	static const char *cmodes[XPM_COL_DEFS] =
		{ "c", "g", "g4", "m", "s" };
	unsigned char *src, *dest, pal[XPM_MAXCOL * 3];
	char lbuf[4096], tstr[20], *buf = lbuf;
	char ckeys[XPM_MAXCOL * 32], *cdefs[XPM_COL_DEFS], *r, *r2;
	str_hash cuckoo;
	FILE *fp;
	int w, h, cols, cpp, hx, hy, lsz = 4096, res = -1, bpp = 1, trans = -1;
	int i, j, k, l;


	if (!(fp = fopen(file_name, "r"))) return (-1);

	/* Read the header - accept XPM3 and nothing else */
	j = 0; fscanf(fp, " /* XPM */%n", &j);
	if (!j) goto fail;
	fgetsC(lbuf, 0, fp); /* Reset reader */

	/* Read the "intro sequence" */
	if (!fgetsC(lbuf, 4096, fp)) goto fail;
	/* !!! Skip validation; libXpm doesn't do it, and some weird tools
	 * write this line differently - WJ */
//	j = 0; sscanf(lbuf, " static char * %*[^[][] = {%n", &j);
//	if (!j) goto fail;

	/* Read the values section */
	if (!fgetsC(lbuf, 4096, fp)) goto fail;
	i = sscanf(lbuf, " \"%d%d%d%d%d%d", &w, &h, &cols, &cpp, &hx, &hy);
	if (i == 4) hx = hy = -1;
	else if (i != 6) goto fail;
	/* Extension marker is ignored, as are extensions themselves */

	/* More than 4096 colors or no colors at all aren't accepted */
	if ((cols < 1) || (cols > XPM_MAXCOL)) goto fail;
	/* Stupid colors per pixel values aren't either */
	if ((cpp < 1) || (cpp > 31)) goto fail;

	/* RGB image if more than 256 colors */
	if (cols > 256) bpp = 3;

	/* Store values */
	settings->width = w;
	settings->height = h;
	settings->bpp = bpp;
	if (bpp == 1) settings->colors = cols;
	settings->hot_x = hx;
	settings->hot_y = hy;
	settings->xpm_trans = -1;

	/* Allocate row buffer and image */
	i = w * cpp + 4 + 1024;
	if (i > lsz) buf = malloc(lsz = i);
	res = FILE_MEM_ERROR;
	if (!buf) goto fail;
	if ((res = allocate_image(settings, CMASK_IMAGE))) goto fail2;
	res = -1;

	if (!settings->silent) ls_init("XPM", 0);

	/* Init hash */
	memset(&cuckoo, 0, sizeof(cuckoo));
	cuckoo.keys = ckeys;
	cuckoo.step = 32;
	cuckoo.cpp = cpp;
	cuckoo.seed = HASHSEED;

	/* Read colormap */
	dest = pal;
	sprintf(tstr, " \"%%n%%*%dc %%n", cpp);
	for (i = 0; i < cols; i++ , dest += 3)
	{
		if (!fgetsC(lbuf, 4096, fp)) goto fail3;

		/* Parse color ID */
		k = 0; sscanf(lbuf, tstr, &k, &l);
		if (!k) goto fail3;

		/* Insert color into hash */
		ch_insert(&cuckoo, lbuf + k);

		/* Parse color definitions */
		if (!(r = strchr(lbuf + l, '"'))) goto fail3;
		*r = '\0';
		memset(cdefs, 0, sizeof(cdefs));
		k = -1; r2 = NULL;
		for (r = strtok(lbuf + l, " \t\n"); r; )
		{
			for (j = 0; j < XPM_COL_DEFS; j++)
			{
				if (!strcmp(r, cmodes[j])) break;
			}
			if (j < XPM_COL_DEFS) /* Key */
			{
				k = j; r2 = NULL;
			}
			else if (!r2) /* Color name */
			{
				if (k < 0) goto fail3;
				cdefs[k] = r2 = r;
			}
			else /* Add next part of name */
			{
				l = strlen(r2);
				r2[l] = ' ';
				if ((l = r - r2 - l - 1))
					for (; (*(r - l) = *r); r++);
			}
			r = strtok(NULL, " \t\n");
		}
		if (!r2) goto fail3; /* Key w/o name */

		/* Translate the best one */
		for (j = 0; j < XPM_COL_DEFS; j++)
		{
			GdkColor col;

			if (!cdefs[j]) continue;
			if (!strcasecmp(cdefs[j], "none")) /* Transparent */
			{
				trans = i;
				break;
			}
			if (!gdk_color_parse(cdefs[j], &col)) continue;
			dest[0] = (col.red + 128) / 257;
			dest[1] = (col.green + 128) / 257;
			dest[2] = (col.blue + 128) / 257;
			break;
		}
		/* Not one understandable color */
		if (j >= XPM_COL_DEFS) goto fail3;
	}

	/* Create palette */
	if (bpp == 1)
	{
		dest = pal;
		for (i = 0; i < cols; i++ , dest += 3)
		{
			settings->pal[i].red = dest[0];
			settings->pal[i].green = dest[1];
			settings->pal[i].blue = dest[2];
		}
		if (trans >= 0)
		{
			settings->xpm_trans = trans;
			settings->pal[trans].red = settings->pal[trans].green = 115;
			settings->pal[trans].blue = 0;
		}
	}

	/* Find an unused color for transparency */
	else if (trans >= 0)
	{
		char cmap[XPM_MAXCOL + 1];

		memset(cmap, 0, sizeof(cmap));
		dest = pal;
		for (i = 0; i < cols; i++ , dest += 3)
		{
			j = MEM_2_INT(dest, 0);
			if (j < XPM_MAXCOL) cmap[j] = 1;
		}
		settings->rgb_trans = j = strlen(cmap);
		dest = pal + trans * 3;
		dest[0] = INT_2_R(j);
		dest[1] = INT_2_G(j);
		dest[2] = INT_2_B(j);
	}

	/* Now, read the image */
	res = FILE_LIB_ERROR;
	dest = settings->img[CHN_IMAGE];
	for (i = 0; i < h; i++)
	{
		if (!fgetsC(buf, lsz, fp)) goto fail3;
		if (!(r = strchr(buf, '"'))) goto fail3;
		if (++r - buf + w * cpp >= lsz) goto fail3;
		for (j = 0; j < w; j++ , dest += bpp)
		{
			k = ch_find(&cuckoo, r);
			if (!k) goto fail3;
			r += cpp;
			if (bpp == 1) *dest = k - 1;
			else
			{
				src = (pal - 3) + k * 3;
				dest[0] = src[0];
				dest[1] = src[1];
				dest[2] = src[2];
			}
		}
		if (!settings->silent && ((i * 10) % h >= h - 10))
			progress_update((float)i / h);
	}
	res = 1;

fail3:	if (!settings->silent) progress_end();
fail2:	if (buf != lbuf) free(buf);
fail:	fclose(fp);
	return (res);
}

static const char base64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
	hex[] = "0123456789ABCDEF";

static int save_xpm(char *file_name, ls_settings *settings)
{
	unsigned char rgbmem[XPM_MAXCOL * 4], *src;
	const char *ctb;
	char ws[3], *buf, *tmp;
	str_hash cuckoo;
	FILE *fp;
	int bpp = settings->bpp, w = settings->width, h = settings->height;
	int i, j, k, l, cpp, cols, trans = -1;


	/* Extract valid C identifier from name */
	tmp = strrchr(file_name, DIR_SEP);
	tmp = tmp ? tmp + 1 : file_name;
	for (; *tmp && !ISALPHA(*tmp); tmp++);
	for (l = 0; (l < 256) && ISALNUM(tmp[l]); l++);
	if (!l) return -1;

	/* Collect RGB colors */
	if (bpp == 3)
	{
		/* Init hash */
		memset(&cuckoo, 0, sizeof(cuckoo));
		cuckoo.keys = rgbmem;
		cuckoo.step = 4;
		cuckoo.cpp = 3;
		cuckoo.seed = HASHSEED;

		j = w * h;
		src = settings->img[CHN_IMAGE];
		for (i = 0; i < j; i++ , src += 3)
		{
			if (ch_insert(&cuckoo, src) < 0)
				return (WRONG_FORMAT); /* Too many colors */
		}
		cols = cuckoo.cnt;
		trans = settings->rgb_trans;
		/* RGB to index */
		if (trans > -1)
		{
			char trgb[3];
			trgb[0] = INT_2_R(trans);
			trgb[1] = INT_2_G(trans);
			trgb[2] = INT_2_B(trans);
			trans = ch_find(&cuckoo, trgb) - 1;
		}
	}

	/* Process indexed colors */
	else
	{
		cols = settings->colors;
		src = rgbmem;
		for (i = 0; i < cols; i++ , src += 4)
		{
			src[0] = settings->pal[i].red;
			src[1] = settings->pal[i].green;
			src[2] = settings->pal[i].blue;
		}
		trans = settings->xpm_trans;
	}

	cpp = cols > 64 ? 2 : 1;
	buf = malloc(w * cpp + 16);
	if (!buf) return -1;

	if (!(fp = fopen(file_name, "w")))
	{
		free(buf);
		return -1;
	}

	if (!settings->silent) ls_init("XPM", 1);

	fprintf(fp, "/* XPM */\n" );
	fprintf(fp, "static char *%.*s_xpm[] = {\n", l, tmp);

	if ((settings->hot_x >= 0) && (settings->hot_y >= 0))
		fprintf(fp, "\"%d %d %d %d %d %d\",\n", w, h, cols, cpp,
			settings->hot_x, settings->hot_y);
	else fprintf(fp, "\"%d %d %d %d\",\n", w, h, cols, cpp);

	/* Create colortable */
	ctb = cols > 16 ? base64 : hex;
	ws[1] = ws[2] = '\0';
	for (i = 0; i < cols; i++)
	{
		if (i == trans)
		{
			ws[0] = ' ';
			if (cpp > 1) ws[1] = ' ';
			fprintf(fp, "\"%s\tc None\",\n", ws);
			continue;
		}
		ws[0] = ctb[i & 63];
		if (cpp > 1) ws[1] = ctb[i >> 6];
		src = rgbmem + i * 4;
		fprintf(fp, "\"%s\tc #%02X%02X%02X\",\n", ws,
			src[0], src[1], src[2]);
	}

	w *= bpp;
	for (i = 0; i < h; i++)
	{
		src = settings->img[CHN_IMAGE] + i * w;
		tmp = buf;
		*tmp++ = '"';
		for (j = 0; j < w; j += bpp, tmp += cpp)
		{
			k = bpp == 1 ? src[j] : ch_find(&cuckoo, src + j) - 1;
			if (k == trans) tmp[0] = tmp[1] = ' ';
			else
			{
				tmp[0] = ctb[k & 63];
				tmp[1] = ctb[k >> 6];
			}
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

static int load_xbm(char *file_name, ls_settings *settings)
{
	static const char XPMtext[] = "0123456789ABCDEFabcdef,} \t\n",
		XPMval[] = {
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
			10, 11, 12, 13, 14, 15, 16, 16, 16, 16, 16 };
	unsigned char ctb[256], *dest;
	char lbuf[4096];
	FILE *fp;
	int w , h, hx = -1, hy = -1, bpn = 16, res = -1;
	int i, j, k, c, v = 0;


	if (!(fp = fopen(file_name, "r"))) return (-1);

	/* Read & parse what serves as header to XBM */
	fgetsC(lbuf, 0, fp); /* Reset reader */
	/* Width and height - required part in fixed order */
	if (!fgetsC(lbuf, 4096, fp)) goto fail;
	if (!sscanf(lbuf, "#define %*s%n %d", &i, &w)) goto fail;
	if (strncmp(lbuf + i - 5, "width", 5)) goto fail;
	if (!fgetsC(lbuf, 4096, fp)) goto fail;
	if (!sscanf(lbuf, "#define %*s%n %d", &i, &h)) goto fail;
	if (strncmp(lbuf + i - 6, "height", 6)) goto fail;
	/* Hotspot X and Y - optional part in fixed order */
	if (!fgetsC(lbuf, 4096, fp)) goto fail;
	if (sscanf(lbuf, "#define %*s%n %d", &i, &hx))
	{
		if (strncmp(lbuf + i - 5, "x_hot", 5)) goto fail;
		if (!fgetsC(lbuf, 4096, fp)) goto fail;
		if (!sscanf(lbuf, "#define %*s%n %d", &i, &hy)) goto fail;
		if (strncmp(lbuf + i - 5, "y_hot", 5)) goto fail;
		if (!fgetsC(lbuf, 4096, fp)) goto fail;
	}
	/* "Intro" string */
	j = 0; sscanf(lbuf, " static short %*[^[]%n[] = {%n", &i, &j);
	if (!j)
	{
		bpn = 8; /* X11 format - 8-bit data */
		j = 0; sscanf(lbuf, " static unsigned char %*[^[]%n[] = {%n", &i, &j);
		if (!j) sscanf(lbuf, " static char %*[^[]%n[] = {%n", &i, &j);
		if (!j) goto fail;
	}
	if (strncmp(lbuf + i - 4, "bits", 4)) goto fail;

	/* Store values */
	settings->width = w;
	settings->height = h;
	settings->bpp = 1;
	settings->hot_x = hx;
	settings->hot_y = hy;
	/* Palette is white and black */
	settings->colors = 2;
	settings->pal[0].red = settings->pal[0].green = settings->pal[0].blue = 255;
	settings->pal[1].red = settings->pal[1].green = settings->pal[1].blue = 0;

	/* Allocate image */
	if ((res = allocate_image(settings, CMASK_IMAGE))) goto fail;

	/* Prepare to read data */
	memset(ctb, 17, sizeof(ctb));
	for (i = 0; XPMtext[i]; i++)
	{
		ctb[(unsigned char)XPMtext[i]] = XPMval[i];
	}

	/* Now, read the image */
	if (!settings->silent) ls_init("XBM", 0);
	res = FILE_LIB_ERROR;
	dest = settings->img[CHN_IMAGE];
	for (i = 0; i < h; i++)
	{
		for (j = k = 0; j < w; j++ , k--)
		{
			if (!k) /* Get next value, the way X itself does */
			{
				v = 0;
				while (TRUE)
				{
					if ((c = getc(fp)) == EOF) goto fail2;
					c = ctb[c & 255];
					if (c < 16) /* Accept hex digits */
					{
						v = (v << 4) + c;
						k++;
					}
					/* Silently ignore out-of-place chars */
					else if (c > 16) continue;
					/* Stop on delimiters after digits */
					else if (k) break;
				}
				k = bpn;
			}
			*dest++ = v & 1;
			v >>= 1;
		}	
		if (!settings->silent && ((i * 10) % h >= h - 10))
			progress_update((float)i / h);
	}
	res = 1;

fail2:	if (!settings->silent) progress_end();
fail:	fclose(fp);
	return (res);
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

	if ((settings->bpp != 1) || (settings->colors > 2)) return WRONG_FORMAT;

	/* Extract valid C identifier from name */
	tmp = strrchr(file_name, DIR_SEP);
	tmp = tmp ? tmp + 1 : file_name;
	for (; *tmp && !ISALPHA(*tmp); tmp++);
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

	if (!settings->silent) ls_init("XBM", 1);

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

/*
 * Those who don't understand PCX are condemned to reinvent it, poorly. :-)
 */

#define LSS_WIDTH   4 /* 16b */
#define LSS_HEIGHT  6 /* 16b */
#define LSS_PALETTE 8 /* 16 * 3 * 8b */
#define LSS_HSIZE   56

static int load_lss(char *file_name, ls_settings *settings)
{
	unsigned char hdr[LSS_HSIZE], *dest, *tmp, *buf = NULL;
	FILE *fp;
	int i, j, k, w, h, bl, idx, last, cnt, res = -1;


	if (!(fp = fopen(file_name, "rb"))) return (-1);

	/* Read the header */
	k = fread(hdr, 1, LSS_HSIZE, fp);

	/* Check general validity */
	if (k < LSS_HSIZE) goto fail; /* Least supported header size */
	if (strncmp(hdr, "\x3D\xF3\x13\x14", 4)) goto fail; /* Signature */

	w = GET16(hdr + LSS_WIDTH);
	h = GET16(hdr + LSS_HEIGHT);
	settings->width = w;
	settings->height = h;
	settings->bpp = 1;
	settings->colors = 16;

	/* Read palette */
	tmp = hdr + LSS_PALETTE;
	for (i = 0; i < 16; i++)
	{
		settings->pal[i].red = tmp[0] << 2 | tmp[0] >> 4;
		settings->pal[i].green = tmp[1] << 2 | tmp[1] >> 4;
		settings->pal[i].blue = tmp[2] << 2 | tmp[2] >> 4;
		tmp += 3;
	}

	/* Load all image at once */
	fseek(fp, 0, SEEK_END);
	bl = ftell(fp) - LSS_HSIZE;
	fseek(fp, LSS_HSIZE, SEEK_SET);
	i = (w * h * 3) >> 1;
	if (bl > i) bl = i; /* Cannot possibly be longer */
	buf = malloc(bl);
	res = FILE_MEM_ERROR;
	if (!buf) goto fail2;
	if ((res = allocate_image(settings, CMASK_IMAGE))) goto fail2;

	if (!settings->silent) ls_init("LSS16", 0);

	res = FILE_LIB_ERROR;
	j = fread(buf, 1, bl, fp);
	if (j < bl) goto fail3;

	dest = settings->img[CHN_IMAGE];
	idx = 0; bl += bl;
	for (i = 0; i < h; i++)
	{
		last = 0; idx = (idx + 1) & ~1;
		for (j = 0; j < w; )
		{
			if (idx >= bl) goto fail3;
			k = (buf[idx >> 1] >> ((idx & 1) << 2)) & 0xF; ++idx;
			if (k != last)
			{
				dest[j++] = last = k;
				continue;
			}
			if (idx >= bl) goto fail3;
			cnt = (buf[idx >> 1] >> ((idx & 1) << 2)) & 0xF; ++idx;
			if (!cnt)
			{
				if (idx >= bl) goto fail3;
				cnt = (buf[idx >> 1] >> ((idx & 1) << 2)) & 0xF; ++idx;
				if (idx >= bl) goto fail3;
				k = (buf[idx >> 1] >> ((idx & 1) << 2)) & 0xF; ++idx;
				cnt = (k << 4) + cnt + 16;
			}
			if (cnt > w - j) cnt = w - j;
			memset(dest + j, last, cnt);
			j += cnt;
		}
		dest += w;
	}
	res = 1;

fail3:	if (!settings->silent) progress_end();
fail2:	free(buf);
fail:	fclose(fp);
	return (res);
}

static int save_lss(char *file_name, ls_settings *settings)
{
	unsigned char *buf, *tmp, *src;
	FILE *fp;
	int i, j, k, last, cnt, idx;
	int w = settings->width, h = settings->height;


	if ((settings->bpp != 1) || (settings->colors > 16)) return WRONG_FORMAT;

	i = w > LSS_HSIZE ? w : LSS_HSIZE;
	buf = malloc(i);
	if (!buf) return -1;
	memset(buf, 0, i);

	if (!(fp = fopen(file_name, "wb")))
	{
		free(buf);
		return -1;
	}

	/* Prepare header */
	buf[0] = 0x3D; buf[1] = 0xF3; buf[2] = 0x13; buf[3] = 0x14;
	PUT16(buf + LSS_WIDTH, w);
	PUT16(buf + LSS_HEIGHT, h);
	j = settings->colors > 16 ? 16 : settings->colors;
	tmp = buf + LSS_PALETTE;
	for (i = 0; i < j; i++)
	{
		tmp[0] = settings->pal[i].red >> 2;
		tmp[1] = settings->pal[i].green >> 2;
		tmp[2] = settings->pal[i].blue >> 2;
		tmp += 3;
	}
	fwrite(buf, 1, LSS_HSIZE, fp);

	/* Write rows */
	if (!settings->silent) ls_init("LSS16", 1);
	src = settings->img[CHN_IMAGE];
	for (i = 0; i < h; i++)
	{
		memset(buf, 0, w);
		last = cnt = idx = 0;
		for (j = 0; j < w; )
		{
			for (; j < w; j++)
			{
				k = *src++ & 0xF;
				if ((k != last) || (cnt >= 255 + 16)) break;
				cnt++;
			}
			if (cnt)
			{
				buf[idx >> 1] |= last << ((idx & 1) << 2); ++idx;
				if (cnt >= 16)
				{
					++idx; /* Insert zero */
					cnt -= 16;
					buf[idx >> 1] |= (cnt & 0xF) <<
						((idx & 1) << 2); ++idx;
					cnt >>= 4;
				}
				buf[idx >> 1] |= cnt << ((idx & 1) << 2); ++idx;
			}
			if (j++ >= w) break; /* Final repeat */
			if (k == last)
			{
				cnt = 1;
				continue; /* Chain of repeats */
			}
			cnt = 0;
			buf[idx >> 1] |= k << ((idx & 1) << 2); ++idx;
			last = k;
		}			
		idx = (idx + 1) & ~1;
		fwrite(buf, 1, idx >> 1, fp);
		if (!settings->silent && ((i * 10) % h >= h - 10))
			progress_update((float)i / h);
	}
	fclose(fp);

	if (!settings->silent) progress_end();

	free(buf);
	return 0;
}

/* *** PREFACE ***
 * No other format has suffered so much at the hands of inept coders. With TGA,
 * exceptions are the rule, and files perfectly following the specification are
 * impossible to find. While I did my best to handle the format's perversions
 * that I'm aware of, there surely exist other kinds of weird TGAs that will
 * load wrong, or not at all. If you encounter one such, send a bugreport with
 * the file attached to it. */

/* TGA header */
#define TGA_IDLEN     0 /*  8b */
#define TGA_PALTYPE   1 /*  8b */
#define TGA_IMGTYPE   2 /*  8b */
#define TGA_PALSTART  3 /* 16b */
#define TGA_PALCOUNT  5 /* 16b */
#define TGA_PALBITS   7 /*  8b */
#define TGA_X0        8 /* 16b */
#define TGA_Y0       10 /* 16b */
#define TGA_WIDTH    12 /* 16b */
#define TGA_HEIGHT   14 /* 16b */
#define TGA_BPP      16 /*  8b */
#define TGA_DESC     17 /*  8b */
#define TGA_HSIZE    18

/* Image descriptor bits */
#define TGA_ALPHA 0x0F
#define TGA_R2L   0x10
#define TGA_T2B   0x20
#define TGA_IL    0xC0 /* Interleave mode - obsoleted in TGA 2.0 */

/* TGA footer */
#define TGA_EXTOFS 0 /* 32b */
#define TGA_DEVOFS 4 /* 32b */
#define TGA_SIGN   8
#define TGA_FSIZE  26

/* TGA extension area */
#define TGA_EXTLEN  0   /* 16b */
#define TGA_SOFTID  426 /* 41 bytes */
#define TGA_SOFTV   467 /* 16b */
#define TGA_ATYPE   494 /* 8b */
#define TGA_EXTSIZE 495

static int load_tga(char *file_name, ls_settings *settings)
{
	unsigned char hdr[TGA_HSIZE], ftr[TGA_FSIZE], ext[TGA_EXTSIZE];
	unsigned char pal[256 * 4], xlat5[32], xlat67[128];
	unsigned char *buf = NULL, *dest, *dsta, *src = NULL, *srca = NULL;
	FILE *fp;
	int i, k, w, h, bpp, ftype, ptype, ibpp, rbits, abits;
	int rle, real_alpha = FALSE, assoc_alpha = FALSE, wmode = 0, res = -1;
	int fl, fofs, iofs, buflen;
	int ix, ishift, imask, ax, ashift, amask;
	int start, xstep, xstepb, ystep, bstart, bstop, ccnt, rcnt, strl, y;


	if (!(fp = fopen(file_name, "rb"))) return (-1);

	/* Read the header */
	k = fread(hdr, 1, TGA_HSIZE, fp);
	if (k < TGA_HSIZE) goto fail;

	/* TGA has no signature as such - so check fields one by one */
	ftype = hdr[TGA_IMGTYPE];
	if (!(ftype & 3) || (ftype & 0xF4)) goto fail; /* Invalid type */
	/* Fail on interleave, because of lack of example files */
	if (hdr[TGA_DESC] & TGA_IL) goto fail;
	rle = ftype & 8;

	iofs = TGA_HSIZE + hdr[TGA_IDLEN];

	rbits = hdr[TGA_BPP];
	if (!rbits) goto fail; /* Zero bpp */
	abits = hdr[TGA_DESC] & TGA_ALPHA;
	if (abits > rbits) goto fail; /* Weird alpha */
	/* Workaround for a rather frequent bug */
	if (abits == rbits) abits = 0;
	ibpp = (rbits + 7) >> 3;
	rbits -= abits;

	ptype = hdr[TGA_PALTYPE];
	switch (ftype & 3)
	{
	case 1: /* Paletted */
	{
		int pbpp, i, j, k, tmp, mask;
		png_color *pptr;

		if (ptype != 1) goto fail; /* Invalid palette */
		/* Don't want to bother with overlong palette without even
		 * having one example where such a thing exists - WJ */
		if (rbits > 8) goto fail;

		k = GET16(hdr + TGA_PALSTART);
		if (k >= 1 << rbits) goto fail; /* Weird palette start */
		j = GET16(hdr + TGA_PALCOUNT);
		if (!j || (k + j > 1 << rbits)) goto fail; /* Weird size */
		ptype = hdr[TGA_PALBITS];
		/* The options are quite limited here in practice */
		if (!ptype || (ptype > 32) || ((ptype & 7) && (ptype != 15)))
			goto fail;
		pbpp = (ptype + 7) >> 3;

		/* Read the palette */
		fseek(fp, iofs, SEEK_SET);
		i = fread(pal + k * pbpp, 1, j * pbpp, fp);
		if (i < j * pbpp) goto fail;
		iofs += j * pbpp;

		/* Store the palette */
		settings->colors = j + k;
		memset(settings->pal, 0, 256 * 3);
		if (pbpp == 2) set_xlate(xlat5, 5);
		pptr = settings->pal + k;
		for (i = 0; i < j; i++)
		{
			switch (pbpp)
			{
			case 1: /* 8-bit greyscale */
				pptr[i].red = pptr[i].green = pptr[i].blue = pal[i];
				break;
			case 2: /* 5:5:5 BGR */
				pptr[i].blue = xlat5[pal[i + i] & 0x1F];
				pptr[i].green = xlat5[(((pal[i + i + 1] << 8) +
					pal[i + i]) >> 5) & 0x1F];
				pptr[i].red = xlat5[(pal[i + i + 1] >> 2) & 0x1F];
				break;
			case 3: case 4: /* 8:8:8 BGR */
				pptr[i].blue = pal[i * pbpp + 0];
				pptr[i].green = pal[i * pbpp + 1];
				pptr[i].red = pal[i * pbpp + 2];
				break;
			}
		}

		/* Cannot have transparent color at all? */
		if ((j <= 1) || ((ptype != 15) && (ptype != 32))) break;

		/* Test if there are different alphas */
		mask = ptype == 15 ? 0x80 : 0xFF;
		tmp = pal[pbpp - 1] & mask;
		for (i = 1; (i < j) && ((pal[i * pbpp - 1] & mask) == tmp); i++);
		if (i >= j) break;
		/* For 15 bpp, assume the less frequent value is transparent */
		tmp = 0;
		if (ptype == 15)
		{
			for (i = 0; i < j; i++) tmp += pal[i + i - 1] & mask;
			if (tmp >> 7 < j >> 1) tmp = 0x80; /* Transparent if set */
		}
		/* Search for first transparent color */
		for (i = 0; (i < j) && ((pal[i * pbpp - 1] & mask) != tmp); i++);
		if (i >= j) break; /* If 32-bit and not one alpha is zero */
		settings->xpm_trans = i + k;
		break;
	}
	case 2: /* RGB */
		/* Options are very limited - and bugs abound. Presence or
		 * absence of attribute bits can't be relied upon. */
		switch (rbits)
		{
		case 16: /* 5:5:5 BGR or 5:6:5 BGR or 5:5:5:1 BGRA */
			if (abits) goto fail;
			if (tga_565)
			{
				set_xlate(xlat5, 5);
				set_xlate(xlat67, 6);
				wmode = 4;
				break;
			}
			rbits = 15;
			/* Fallthrough */
		case 15: /* 5:5:5 BGR or 5:5:5:1 BGRA */
			if (abits > 1) goto fail;
			abits = 1; /* Here it's unreliable to uselessness */
			set_xlate(xlat5, 5);
			wmode = 2;
			break;
		case 32: /* 8:8:8 BGR or 8:8:8:8 BGRA */
			if (abits) goto fail;
			rbits = 24; abits = 8;
			wmode = 6;
			break;
		case 24: /* 8:8:8 BGR or 8:8:8:8 BGRA */
			if (abits && (abits != 8)) goto fail;
			wmode = 6;
			break;
		default: goto fail;
		}
		break;
	case 3: /* Greyscale */
		/* Not enough examples - easier to handle all possibilities */
		/* Create palette */
		settings->colors = rbits > 8 ? 256 : 1 << rbits;
		mem_scale_pal(settings->pal, 0, 0,0,0,
			settings->colors - 1, 255,255,255);
		break;
	}
	/* Prepare for reading bitfields */
	i = abits > 8 ? abits - 8 : 0;
	abits -= i; i += rbits;
	ax = i >> 3;
	ashift = i & 7;
	amask = (1 << abits) - 1;
	i = rbits > 8 ? rbits - 8 : 0;
	rbits -= i;
	ix = i >> 3;
	ishift = i & 7;
	imask = (1 << rbits) - 1;

	/* Now read the footer if one is available */
	fseek(fp, 0, SEEK_END);
	fl = ftell(fp);
	while (fl >= iofs + TGA_FSIZE)
	{
		fseek(fp, fl - TGA_FSIZE, SEEK_SET);
		k = fread(ftr, 1, TGA_FSIZE, fp);
		if (k < TGA_FSIZE) break;
		if (strcmp(ftr + TGA_SIGN, "TRUEVISION-XFILE.")) break;
		fofs = GET32(ftr + TGA_EXTOFS);
		if ((fofs < iofs) || (fofs + TGA_EXTSIZE + TGA_FSIZE > fl))
			break; /* Invalid location */
		fseek(fp, fofs, SEEK_SET);
		k = fread(ext, 1, TGA_EXTSIZE, fp);
		if ((k < TGA_EXTSIZE) ||
			/* !!! 3D Studio writes 494 into this field */
			(GET16(ext + TGA_EXTLEN) < TGA_EXTSIZE - 1))
			break; /* Invalid size */
		if ((ftype & 3) != 1) /* Premultiplied alpha? */
			assoc_alpha = ext[TGA_ATYPE] == 4;
		/* Can believe alpha bits contain alpha if this field says so */
		real_alpha |= assoc_alpha | (ext[TGA_ATYPE] == 3);
/* !!! No private extensions for now */
#if 0
		if (strcmp(ext + TGA_SOFTID, "mtPaint")) break;
		if (GET16(ext + TGA_SOFTV) <= 310) break;
		/* !!! Read and interpret developer directory */
#endif
		break;
	}

	/* Allocate buffer and image */
	settings->width = w = GET16(hdr + TGA_WIDTH);
	settings->height = h = GET16(hdr + TGA_HEIGHT);
	settings->bpp = bpp = (ftype & 3) == 2 ? 3 : 1;
	buflen = ibpp * w;
	if (rle && (w < 129)) buflen = ibpp * 129;
	buf = malloc(buflen + 1); /* One extra byte for bitparser */
	res = FILE_MEM_ERROR;
	if (!buf) goto fail2;
	if ((res = allocate_image(settings, abits ? CMASK_RGBA : CMASK_IMAGE)))
		goto fail2;
	/* Don't even try reading alpha if nowhere to store it */
	if (abits && settings->img[CHN_ALPHA]) wmode |= 1;
	res = -1;

	if (!settings->silent) ls_init("TGA", 0);

	fseek(fp, iofs, SEEK_SET); /* Seek to data */
	/* Prepare loops */
	start = 0; xstep = 1; ystep = 0;
	if (hdr[TGA_DESC] & TGA_R2L)
	{
		/* Right-to-left */
		start = w - 1;
		xstep = -1;
		ystep = 2 * w;
	}
	if (!(hdr[TGA_DESC] & TGA_T2B))
	{
		/* Bottom-to-top */
		start += (h - 1) * w;
		ystep -= 2 * w;
	}
	xstepb = xstep * bpp;
	res = FILE_LIB_ERROR;

	dest = settings->img[CHN_IMAGE] + start * bpp;
	dsta = settings->img[CHN_ALPHA] + start;
	y = ccnt = rcnt = 0;
	bstart = bstop = buflen;
	strl = w;
	while (TRUE)
	{
		int j;

		j = bstop - bstart;
		if (j < ibpp)
		{
			if (bstop < buflen) goto fail3; /* Truncated file */
			memcpy(buf, buf + bstart, j);
			bstart = 0;
			bstop = j + fread(buf + j, 1, buflen - j, fp);
			if (!rle) /* Uncompressed */
			{
				if (bstop < buflen) goto fail3; /* Truncated file */
				rcnt = w; /* "Copy block" a row long */
			}
		}
		while (TRUE)
		{
			/* Read pixels */
			if (rcnt)
			{
				int l;

				l = rcnt < strl ? rcnt : strl;
				if (bstart + ibpp * l > bstop)
					l = (bstop - bstart) / ibpp;
				rcnt -= l; strl -= l;
				while (l--)
				{
					switch (wmode)
					{
					case 1: /* Generic alpha */
						*dsta = (((buf[bstart + ax + 1] << 8) +
							buf[bstart + ax]) >> ashift) & amask;
					case 0: /* Generic single channel */
						*dest = (((buf[bstart + ix + 1] << 8) +
							buf[bstart + ix]) >> ishift) & imask;
						break;
					case 3: /* One-bit alpha for 16 bpp */
						*dsta = buf[bstart + 1] >> 7;
					case 2: /* 5:5:5 BGR */
						dest[0] = xlat5[(buf[bstart + 1] >> 2) & 0x1F];
						dest[1] = xlat5[(((buf[bstart + 1] << 8) +
							buf[bstart]) >> 5) & 0x1F];
						dest[2] = xlat5[buf[bstart] & 0x1F];
						break;
					case 5: /* Cannot happen */
					case 4: /* 5:6:5 BGR */
						dest[0] = xlat5[buf[bstart + 1] >> 3];
						dest[1] = xlat67[(((buf[bstart + 1] << 8) +
							buf[bstart]) >> 5) & 0x3F];
						dest[2] = xlat5[buf[bstart] & 0x1F];
						break;
					case 7: /* One-byte alpha for 32 bpp */
						*dsta = buf[bstart + 3];
					case 6: /* 8:8:8 BGR */
						dest[0] = buf[bstart + 2];
						dest[1] = buf[bstart + 1];
						dest[2] = buf[bstart + 0];
						break;
					}
					dest += xstepb;
					dsta += xstep;
					bstart += ibpp;
				}
				if (!strl || rcnt) break; /* Row end or buffer end */
			}
			/* Copy pixels */
			if (ccnt)
			{
				int i, l;

				l = ccnt < strl ? ccnt : strl;
				ccnt -= l; strl -= l;
				for (i = 0; i < l; i++ , dest += xstepb)
				{
					dest[0] = src[0];
					if (bpp == 1) continue;
					dest[1] = src[1];
					dest[2] = src[2];
				}
				if (wmode & 1) memset(xstep < 0 ?
					dsta - l + 1 : dsta, *srca, l);
				dsta += xstep * l;
				if (!strl || ccnt) break; /* Row end or buffer end */
			}
			/* Read block header */
			if (bstart >= bstop) break; /* Nothing in buffer */
			rcnt = buf[bstart++];
			if (rcnt > 0x7F) /* Repeat block - one read + some copies */
			{
				ccnt = rcnt & 0x7F;
				rcnt = 1;
				src = dest;
				srca = dsta;
			}
			else ++rcnt; /* Copy block - several reads */
		}
		if (strl) continue; /* It was buffer end */
		if (!settings->silent && ((y * 10) % h >= h - 10))
			progress_update((float)y / h);
		if (++y >= h) break; /* All done */
		dest += ystep * bpp;
		if (dsta) dsta += ystep;
		strl = w;
	}
	res = 1;

	/* Check if alpha channel is valid */
	if (!real_alpha && settings->img[CHN_ALPHA])
	{
		unsigned char *tmp = settings->img[CHN_ALPHA];
		int i, j = w * h, k = tmp[0];

		for (i = 1; (tmp[i] == k) && (i < j); i++);
		/* Delete flat "alpha" */
		if (i >= j) deallocate_image(settings, CMASK_FOR(CHN_ALPHA));
	}

	/* Check if alpha in 16-bpp BGRA is inverse */
	if (settings->img[CHN_ALPHA] && (wmode == 3) && !assoc_alpha)
	{
		unsigned char *timg, *talpha;
		int i, j = w * h, k = 0, l;

		timg = settings->img[CHN_IMAGE];
		talpha = settings->img[CHN_ALPHA];
		for (i = 0; i < j; i++)
		{
			l = 5;
			if (!(timg[0] | timg[1] | timg[2])) l = 1;
			else if ((timg[0] & timg[1] & timg[2]) == 255) l = 4;
			k |= l << talpha[i];
			if (k == 0xF) break; /* Colors independent of alpha */
			timg += 3;
		}
		/* If 0-covered parts more colorful than 1-covered, invert alpha */
		if ((k & 5) > ((k >> 1) & 5))
		{
			for (i = 0; i < j; i++) talpha[i] ^= 1;
		}
	}

	/* Rescale alpha */
	if (settings->img[CHN_ALPHA] && (abits < 8))
	{
		unsigned char *tmp = settings->img[CHN_ALPHA];
		int i, j = w * h;

		set_xlate(xlat67, abits);
		for (i = 0; i < j; i++) tmp[i] = xlat67[tmp[i]];
	}

	/* Unassociate alpha */
	if (settings->img[CHN_ALPHA] && assoc_alpha && (abits > 1))
	{
		mem_demultiply(settings->img[CHN_IMAGE],
			settings->img[CHN_ALPHA], w * h, bpp);
	}

fail3:	if (!settings->silent) progress_end();
fail2:	free(buf);
fail:	fclose(fp);
	return (res);
}

static int save_tga(char *file_name, ls_settings *settings)
{
	unsigned char hdr[TGA_HSIZE], ftr[TGA_FSIZE], pal[256 * 4];
	unsigned char *buf, *src, *srca, *dest;
	FILE *fp;
	int i, j, y0, y1, vstep, pcn, pbpp = 3;
	int w = settings->width, h = settings->height, bpp = settings->bpp;
	int rle = settings->tga_RLE;

	/* Indexed images not supposed to have alpha in TGA standard */
	if ((bpp == 3) && settings->img[CHN_ALPHA]) bpp = 4;
	i = w * bpp;
	if (rle) i += i + (w >> 7) + 3;
	buf = malloc(i);
	if (!buf) return -1;

	if (!(fp = fopen(file_name, "wb")))
	{
		free(buf);
		return -1;
	}

	/* Prepare header */
	memset(hdr, 0, TGA_HSIZE);
	switch (bpp)
	{
	case 1: /* Indexed */
		hdr[TGA_PALTYPE] = 1;
		hdr[TGA_IMGTYPE] = 1;
		PUT16(hdr + TGA_PALCOUNT, settings->colors);
		if ((settings->xpm_trans >= 0) &&
			(settings->xpm_trans < settings->colors)) pbpp = 4;
		hdr[TGA_PALBITS] = pbpp * 8;
		break;
	case 4: /* RGBA */
		hdr[TGA_DESC] = 8;
	case 3: /* RGB */
		hdr[TGA_IMGTYPE] = 2;
		break;
	}
	hdr[TGA_BPP] = bpp * 8;
	PUT16(hdr + TGA_WIDTH, w);
	PUT16(hdr + TGA_HEIGHT, h);
	if (rle) hdr[TGA_IMGTYPE] |= 8;
	if (!tga_defdir) hdr[TGA_DESC] |= TGA_T2B;
	fwrite(hdr, 1, TGA_HSIZE, fp);

	/* Write palette */
	if (bpp == 1)
	{
		dest = pal;
		for (i = 0; i < settings->colors; i++ , dest += pbpp)
		{
			dest[0] = settings->pal[i].blue;
			dest[1] = settings->pal[i].green;
			dest[2] = settings->pal[i].red;
			if (pbpp > 3) dest[3] = 255;
		}
		/* Mark transparent color */
		if (pbpp > 3) pal[settings->xpm_trans * 4 + 3] = 0;
		fwrite(pal, 1, dest - pal, fp);
	}

	/* Write rows */
	if (!settings->silent) ls_init("TGA", 1);
	if (tga_defdir)
	{
		y0 = h - 1; y1 = -1; vstep = -1;
	}
	else
	{
		y0 = 0; y1 = h; vstep = 1;
	}
	for (i = y0 , pcn = 0; i != y1; i += vstep , pcn++)
	{
		src = settings->img[CHN_IMAGE] + i * w * settings->bpp;
		/* Fill uncompressed row */
		if (bpp == 1) memcpy(buf, src, w);
		else
		{
			srca = settings->img[CHN_ALPHA] + i * w;
			dest = buf;
			for (j = 0; j < w; j++ , dest += bpp)
			{
				dest[0] = src[2];
				dest[1] = src[1];
				dest[2] = src[0];
				src += 3;
				if (bpp > 3) dest[3] = *srca++;
			}
		}
		src = buf;
		dest = buf + w * bpp;
		if (rle) /* Compress */
		{
			unsigned char *tmp;
			int k, l;

			for (j = 1; j <= w; j++)
			{
				tmp = srca = src;
				src += bpp;
				/* Scan row for repeats */
				for (; j < w; j++ , src += bpp)
				{
					switch (bpp)
					{
					case 4: if (src[3] != srca[3]) break;
					case 3: if (src[2] != srca[2]) break;
					case 2: if (src[1] != srca[1]) break;
					case 1: if (src[0] != srca[0]) break;
					default: continue;
					}
					/* Useful repeat? */
					if (src - srca > bpp + 2) break;
					srca = src;
				}
				/* Avoid too-short repeats at row ends */
				if (src - srca <= bpp + 2) srca = src;
				/* Create copy blocks */
				for (k = (srca - tmp) / bpp; k > 0; k -= 128)
				{
					l = k > 128 ? 128 : k;
					*dest++ = l - 1;
					memcpy(dest, tmp, l *= bpp);
					dest += l; tmp += l;
				}
				/* Create repeat blocks */
				for (k = (src - srca) / bpp; k > 0; k -= 128)
				{
					l = k > 128 ? 128 : k;
					*dest++ = l + 127;
					memcpy(dest, srca, bpp);
					dest += bpp;
				}
			}
		}
		fwrite(src, 1, dest - src, fp);
		if (!settings->silent && ((pcn * 20) % h >= h - 20))
			progress_update((float)pcn / h);
	}

	/* Write footer */
	memcpy(ftr + TGA_SIGN, "TRUEVISION-XFILE.", TGA_FSIZE - TGA_SIGN);
/* !!! No private extensions for now */
	memset(ftr, 0, TGA_SIGN);
	fwrite(ftr, 1, TGA_FSIZE, fp);

	fclose(fp);

	if (!settings->silent) progress_end();

	free(buf);
	return 0;
}

/* Put screenshots and X pixmaps on an equal footing with regular files */

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#endif

static int load_pixmap(char *pixmap_id, ls_settings *settings)
{
#if GTK_MAJOR_VERSION == 1
	GdkWindow *mainwin = (GdkWindow *)&gdk_root_parent;
#else /* #if GTK_MAJOR_VERSION == 2 */
	GdkWindow *mainwin = gdk_get_default_root_window();
#endif
	int w, h, res = -1;

	if (pixmap_id) // Pixmap by ID
	{
/* This ugly code imports X Window System's pixmaps; this allows mtPaint to
 * receive images from programs such as XPaint */
#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
		GdkPixmap *pm;
		int d, dd;

		gdk_error_trap_push(); // No guarantee that we got a valid pixmap
		pm = gdk_pixmap_foreign_new(*(Pixmap *)pixmap_id);
		gdk_error_trap_pop(); // The above call returns NULL on failure anyway
		if (!pm) return (-1);
		dd = gdk_visual_get_system()->depth;
#if GTK_MAJOR_VERSION == 1
		gdk_window_get_geometry(pm, NULL, NULL, &w, &h, &d);
#else /* #if GTK_MAJOR_VERSION == 2 */
		gdk_drawable_get_size(pm, &w, &h);
		d = gdk_drawable_get_depth(pm);
#endif
		settings->width = w;
		settings->height = h;
		settings->bpp = 3;
		if ((d == 1) || (d == dd))
			res = allocate_image(settings, CMASK_IMAGE);
		if (!res) res = wj_get_rgb_image(d == 1 ? NULL : mainwin, pm,
			settings->img[CHN_IMAGE], 0, 0, w, h) ? 1 : -1;
#if GTK_MAJOR_VERSION == 1
		/* Don't let gdk_pixmap_unref() destroy another process's pixmap -
		 * implement freeing the GdkPixmap structure here instead */
		gdk_xid_table_remove(((GdkWindowPrivate *)pm)->xwindow);
		g_dataset_destroy(pm);
		g_free(pm);
#else /* #if GTK_MAJOR_VERSION == 2 */
		gdk_pixmap_unref(pm);
#endif
#endif
	}
	else // NULL means a screenshot
	{
		w = settings->width = gdk_screen_width();
		h = settings->height = gdk_screen_height();
		settings->bpp = 3;
		res = allocate_image(settings, CMASK_IMAGE);
		if (!res) res = wj_get_rgb_image(mainwin, NULL,
			settings->img[CHN_IMAGE], 0, 0, w, h) ? 1 : -1;
	}
	return (res);
}

static int save_image_x(char *file_name, ls_settings *settings, memFILE *mf)
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

	/* Validate transparent color (for now, forbid out-of-palette RGB
	 * transparency altogether) */
	if (settings->xpm_trans >= settings->colors)
		settings->xpm_trans = settings->rgb_trans = -1;

	switch (settings->ftype)
	{
	default:
	case FT_PNG: res = save_png(file_name, settings, mf); break;
#ifdef U_JPEG
	case FT_JPEG: res = save_jpeg(file_name, settings); break;
#endif
#ifdef U_JP2
	case FT_JP2:
	case FT_J2K: res = save_jpeg2000(file_name, settings); break;
#endif
#ifdef U_TIFF
	case FT_TIFF: res = save_tiff(file_name, settings); break;
#endif
#ifdef U_GIF
	case FT_GIF: res = save_gif(file_name, settings); break;
#endif
	case FT_BMP: res = save_bmp(file_name, settings, mf); break;
	case FT_XPM: res = save_xpm(file_name, settings); break;
	case FT_XBM: res = save_xbm(file_name, settings); break;
	case FT_LSS: res = save_lss(file_name, settings); break;
	case FT_TGA: res = save_tga(file_name, settings); break;
/* !!! Not implemented yet */
//	case FT_PCX:
	}

	if (settings->pal == greypal) settings->pal = NULL;
	return res;
}

int save_image(char *file_name, ls_settings *settings)
{
	return (save_image_x(file_name, settings, NULL));
}

int save_mem_image(unsigned char **buf, int *len, ls_settings *settings)
{
	memFILE mf;
	int res;

	if (!(file_formats[settings->ftype].flags & FF_WMEM)) return (-1);

	memset(&mf, 0, sizeof(mf));
	mf.buf = malloc(mf.size = 0x4000 - 64);
	/* Be silent when saving to memory */
	settings->silent = TRUE;
	res = save_image_x(NULL, settings, &mf);
	if (res) free(mf.buf);
	else *buf = mf.buf , *len = mf.top;
	return (res);
}

static void store_image_extras(image_info *image, image_state *state,
	ls_settings *settings)
{
	/* Stuff RGB transparency into color 255 */
	if ((settings->rgb_trans >= 0) && (settings->bpp == 3))
	{
		int i;

		for ( i=0; i<settings->colors; i++ )	// Look for transparent colour in palette
		{
			if ( RGB_2_INT(settings->pal[i].red, settings->pal[i].green,
				settings->pal[i].blue) == settings->rgb_trans ) break;
		}

		if ( i < settings->colors ) settings->xpm_trans = i;
		else
		{		// Colour not in palette so force it into last entry
			settings->pal[255].red = INT_2_R(settings->rgb_trans);
			settings->pal[255].green = INT_2_G(settings->rgb_trans);
			settings->pal[255].blue = INT_2_B(settings->rgb_trans);
			settings->xpm_trans = 255;
			settings->colors = 256;
		}
	}

	/* Accept vars which make sense */
	state->xpm_trans = settings->xpm_trans;
	state->xbm_hot_x = settings->hot_x;
	state->xbm_hot_y = settings->hot_y;
	preserved_gif_delay = settings->gif_delay;

	/* Accept palette */
	mem_pal_copy(image->pal, settings->pal);
	image->cols = settings->colors;
}

static int load_image_x(char *file_name, memFILE *mf, int mode, int ftype)
{
	layer_image *lim = NULL;
	png_color pal[256];
	ls_settings settings;
	int i, tr, res, undo = FALSE;

	/* Prepare layer slot */
	if (mode == FS_LAYER_LOAD)
	{
		lim = layer_table[layers_total].image;
		if (!lim) lim = layer_table[layers_total].image =
			alloc_layer(0, 0, 1, 0, NULL);
		else if (layers_total) mem_free_image(&lim->image_, FREE_IMAGE);
		if (!lim) return (FILE_MEM_ERROR);
	}

	init_ls_settings(&settings, NULL);
	/* Image loads can be undoable */
	if (mode == FS_PNG_LOAD) undo = undo_load;
	/* 0th layer load is a non-undoable image load */
	else if ((mode == FS_LAYER_LOAD) && !layers_total) mode = FS_PNG_LOAD;
	settings.mode = mode;
	settings.ftype = ftype;
	settings.pal = pal;
	/* Clear hotspot & transparency */
	settings.hot_x = settings.hot_y = -1;
	settings.xpm_trans = settings.rgb_trans = -1;
	/* Be silent if working from memory */
	if (mf) settings.silent = TRUE;

	/* !!! Use default palette - for now */
	mem_pal_copy(pal, mem_pal_def);
	settings.colors = mem_pal_def_i;

	switch (ftype)
	{
	default:
	case FT_PNG: res = load_png(file_name, &settings, mf); break;
#ifdef U_GIF
	case FT_GIF: res = load_gif(file_name, &settings); break;
#endif
#ifdef U_JPEG
	case FT_JPEG: res = load_jpeg(file_name, &settings); break;
#endif
#ifdef U_JP2
	case FT_JP2:
	case FT_J2K: res = load_jpeg2000(file_name, &settings); break;
#endif
#ifdef U_TIFF
	case FT_TIFF: res = load_tiff(file_name, &settings); break;
#endif
	case FT_BMP: res = load_bmp(file_name, &settings, mf); break;
	case FT_XPM: res = load_xpm(file_name, &settings); break;
	case FT_XBM: res = load_xbm(file_name, &settings); break;
	case FT_LSS: res = load_lss(file_name, &settings); break;
	case FT_TGA: res = load_tga(file_name, &settings); break;
/* !!! Not implemented yet */
//	case FT_PCX:
	case FT_PIXMAP: res = load_pixmap(file_name, &settings); break;
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
			strncpy(preserved_gif_filename, file_name, PATHBUF);
			file_selector(FS_GIF_EXPLODE);
		}
		else if (i == 3)
		{
			char *tmp = g_strdup_printf("gifview -a \"%s\" &", file_name);
			gifsicle(tmp);
			g_free(tmp);
		}
	}

	switch (settings.mode)
	{
	case FS_PNG_LOAD: /* Image */
		/* Success, or lib failure with single image - commit load */
		if ((res == 1) || (!lim && (res == FILE_LIB_ERROR)))
		{
			if (!mem_img[CHN_IMAGE] || !undo)
				mem_new(settings.width, settings.height,
					settings.bpp, 0);
			else undo_next_core(UC_DELETE, settings.width,
				settings.height, settings.bpp, CMASK_ALL);
			memcpy(mem_img, settings.img, sizeof(chanlist));
			store_image_extras(&mem_image, &mem_state, &settings);
			update_undo(&mem_image);
			mem_undo_prepare();
			if (lim) layer_copy_from_main(0);
		}
		/* Failure */
		else
		{
			mem_free_chanlist(settings.img);
			/* If loader managed to delete image before failing */
			if (!mem_img[CHN_IMAGE]) create_default_image();
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
		if (res == 1); /* !!! Clipboard data committed already */
		/* Failure needing rollback */
		else if (settings.img[CHN_IMAGE])
		{
			/* !!! Too late to restore previous clipboard */
			mem_free_image(&mem_clip, FREE_ALL);
		}
		break;
	case FS_CHANNEL_LOAD:
		/* Success - commit load */
		if (res == 1)
		{
			/* Add frame & stuff data into it */
			undo_next_core(UC_DELETE, mem_width, mem_height, mem_img_bpp,
				CMASK_CURR);
			mem_img[mem_channel] = settings.img[CHN_IMAGE];
			update_undo(&mem_image);
// !!! This is frequently harmful
//			if (mem_channel == CHN_IMAGE)
//				store_image_extras(&mem_image, &mem_state, &settings);
			mem_undo_prepare();
		}
		/* Failure */
		else free(settings.img[CHN_IMAGE]);
		break;
	case FS_LAYER_LOAD: /* Layer */
		/* Success - commit load */
		if (res == 1)
		{
			mem_alloc_image(0, &lim->image_, settings.width,
				settings.height, settings.bpp,
				cmask_from(settings.img), settings.img);
			store_image_extras(&lim->image_, &lim->state_, &settings);
			update_undo(&lim->image_);
		}
		/* Failure */
		else mem_free_chanlist(settings.img);
		break;
	case FS_PATTERN_LOAD:
		/* Success - rebuild patterns */
		if ((res == 1) && (settings.colors == 2))
			set_patterns(settings.img[CHN_IMAGE]);
		free(settings.img[CHN_IMAGE]);
		break;
	}
	/* Don't report animated GIF as failure */
	return (res == FILE_GIF_ANIM ? 1 : res);
}

int load_image(char *file_name, int mode, int ftype)
{
	return (load_image_x(file_name, NULL, mode, ftype));
}

int load_mem_image(unsigned char *buf, int len, int mode, int ftype)
{
	memFILE mf;

	if (ftype == FT_PIXMAP) // Special case: buf points to a pixmap ID
		return (load_image_x(buf, NULL, FS_CLIP_FILE, FT_PIXMAP));

	if (!(file_formats[ftype].flags & FF_RMEM)) return (-1);

	memset(&mf, 0, sizeof(mf));
	mf.buf = buf; mf.top = mf.size = len;
	return (load_image_x(NULL, &mf, mode, ftype));
}

int export_undo(char *file_name, ls_settings *settings)
{
	char new_name[PATHBUF + 32];
	int start = mem_undo_done, res = 0, lenny, i, j;
	int deftype = settings->ftype, miss = 0;

	strncpy( new_name, file_name, PATHBUF);
	lenny = strlen( file_name );

	ls_init("UNDO", 1);
	settings->silent = TRUE;

	for (j = 0; j < 2; j++)
	{
		for (i = 1; i <= start + 1; i++)
		{
			if (!res && (!j ^ (settings->mode == FS_EXPORT_UNDO)))
			{
				progress_update((float)i / (start + 1));
				settings->ftype = deftype;
				if (!(file_formats[deftype].flags & FF_SAVE_MASK))
				{
					settings->ftype = FT_PNG;
					miss++;
				}
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

	if (miss && !res)
	{
		snprintf(new_name, 300, _("%d out of %d frames could not be saved as %s - saved as PNG instead"),
			miss, mem_undo_done, file_formats[deftype].name);
		alert_box(_("Warning"), new_name, _("OK"), NULL, NULL);
	}

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
	if (!memcmp(buf, "\x89PNG", 4)) return (FT_PNG);
	if (!memcmp(buf, "\xFF\xD8", 2))
#ifdef U_JPEG
		return (FT_JPEG);
#else
		return (FT_NONE);
#endif
	if (!memcmp(buf, "\0\0\0\x0C\x6A\x50\x20\x20\x0D\x0A\x87\x0A", 12))
#ifdef U_JP2
		return (FT_JP2);
#else
		return (FT_NONE);
#endif
	if (!memcmp(buf, "\xFF\x4F", 2))
#ifdef U_JP2
		return (FT_J2K);
#else
		return (FT_NONE);
#endif
	if (!memcmp(buf, "II", 2) || !memcmp(buf, "MM", 2))
#ifdef U_TIFF
		return (FT_TIFF);
#else
		return (FT_NONE);
#endif
	if (!memcmp(buf, "GIF8", 4))
#ifdef U_GIF
		return (FT_GIF);
#else
		return (FT_NONE);
#endif
	if (!memcmp(buf, "BM", 2)) return (FT_BMP);

	if (!memcmp(buf, "\x3D\xF3\x13\x14", 4)) return (FT_LSS);

	/* Check layers signature and version */
	if (!memcmp(buf, LAYERS_HEADER, strlen(LAYERS_HEADER)))
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
#endif
	/* Check if this is TGA */
	if ((buf[1] < 2) && (buf[2] < 12) && ((1 << buf[2]) & 0x0E0F))
		return (FT_TGA);

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

#ifdef WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define HANDBOOK_LOCATION_WIN "..\\docs\\index.html"

#else /* Linux */

#define HANDBOOK_BROWSER "firefox"
#define HANDBOOK_LOCATION "/usr/doc/mtpaint/index.html"
#define HANDBOOK_LOCATION2 "/usr/share/doc/mtpaint/index.html"

#include "spawn.h"

#endif

int valid_file( char *filename )		// Can this file be opened for reading?
{
	FILE *fp;

	fp = fopen(filename, "r");
	if (!fp) return (errno == ENOENT ? -1 : 1);
	else
	{
		fclose(fp);
		return 0;
	}
}

int show_html(char *browser, char *docs)
{
	char buf[PATHBUF], buf2[PATHBUF];
	int i=-1;
#ifdef WIN32
	char *r;

	if (!docs || !docs[0])
	{
		/* Use default path relative to installdir */
		i = GetModuleFileNameA(NULL, buf, PATHBUF);
		if (!i) return (-1);
		r = strrchr(buf, '\\');
		if (!r) return (-1);
		r[1] = 0;
		strnncat(buf, HANDBOOK_LOCATION_WIN, PATHBUF);
		docs = buf;
	}
#else /* Linux */
	char *argv[5];

	if (!docs || !docs[0])
	{
		docs = HANDBOOK_LOCATION;
		if (valid_file(docs) < 0) docs = HANDBOOK_LOCATION2;
	}
#endif
	else docs = gtkncpy(buf, docs, PATHBUF);

	if ((valid_file(docs) < 0))
	{
		alert_box( _("Error"),
			_("I am unable to find the documentation.  Either you need to download the mtPaint Handbook from the web site and install it, or you need to set the correct location in the Preferences window."),
 			_("OK"), NULL, NULL );
		return (-1);
	}

#ifdef WIN32
	if (browser && !browser[0]) browser = NULL;
	if (browser) browser = gtkncpy(buf2, browser, PATHBUF);

	if ((unsigned int)ShellExecuteA(NULL, "open", browser ? browser : docs,
		browser ? docs : NULL, NULL, SW_SHOW) <= 32) i = -1;
	else i = 0;
#else
	argv[1] = docs;
	argv[2] = NULL;
	/* Try to use default browser */
	if (!browser || !browser[0])
	{
		argv[0] = "xdg-open";
		i = spawn_process(argv, NULL);
		if (!i) return (0); // System has xdg-utils installed
		// No xdg-utils - try "BROWSER" variable then
		browser = getenv("BROWSER");
	}
	else browser = gtkncpy(buf2, browser, PATHBUF);

	if (!browser) browser = HANDBOOK_BROWSER;

	argv[0] = browser;
	i = spawn_process(argv, NULL);
#endif
	if (i) alert_box( _("Error"),
		_("There was a problem running the HTML browser.  You need to set the correct program name in the Preferences window."),
		_("OK"), NULL, NULL );
	return (i);
}
