/*	png.c
	Copyright (C) 2004-2010 Mark Tyler and Dmitry Groshev

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
#define NEED_CMYK
#include <jpeglib.h>
/* !!! Since libjpeg 7, this conflicts with <windows.h>; with libjpeg 8a,
 * conflict can be avoided if windows.h is included BEFORE this - WJ */
#endif
#ifdef U_JP2
#define HANDLE_JP2
#include <openjpeg.h>
#endif
#ifdef U_JASPER
#define HANDLE_JP2
#include <jasper/jasper.h>
#endif
#ifdef U_TIFF
#define NEED_CMYK
#include <tiffio.h>
#endif
#if U_LCMS == 2
#include <lcms2.h>
/* For version 1.x compatibility */
#define icSigCmykData cmsSigCmykData
#define icSigRgbData cmsSigRgbData
#define icHeader cmsICCHeader
#elif defined U_LCMS
#include <lcms.h>
#endif

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "canvas.h"
#include "toolbar.h"
#include "layer.h"

/* All-in-one transport container for animation save/load */
typedef struct {
	frameset fset;
	ls_settings settings;
	int mode;
	/* Explode frames mode */
	int desttype;
	int error, miss, cnt;
	char *destdir;
} ani_settings;

int silence_limit, jpeg_quality, png_compression;
int tga_RLE, tga_565, tga_defdir, jp2_rate;
int apply_icc;

fformat file_formats[NUM_FTYPES] = {
	{ "", "", "", 0},
	{ "PNG", "png", "", FF_256 | FF_RGB | FF_ALPHA | FF_MULTI
		| FF_TRANS | FF_COMPZ | FF_MEM },
#ifdef U_JPEG
	{ "JPEG", "jpg", "jpeg", FF_RGB | FF_COMPJ },
#else
	{ "", "", "", 0},
#endif
#ifdef HANDLE_JP2
	{ "JPEG2000", "jp2", "", FF_RGB | FF_ALPHA | FF_COMPJ2 },
	{ "J2K", "j2k", "jpc", FF_RGB | FF_ALPHA | FF_COMPJ2 },
#else
	{ "", "", "", 0},
	{ "", "", "", 0},
#endif
#ifdef U_TIFF
/* !!! Ideal state */
//	{ "TIFF", "tif", "tiff", FF_256 | FF_RGB | FF_ALPHA | FF_MULTI | FF_LAYER
//		/* | FF_TRANS */ },
/* !!! Current state */
	{ "TIFF", "tif", "tiff", FF_256 | FF_RGB | FF_ALPHA | FF_LAYER },
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
	{ "PCX", "pcx", "", FF_256 | FF_RGB },
	{ "PBM", "pbm", "", FF_BW | FF_LAYER },
	{ "PGM", "pgm", "", FF_256 | FF_LAYER | FF_NOSAVE },
	{ "PPM", "ppm", "pnm", FF_RGB | FF_LAYER },
	{ "PAM", "pam", "", FF_BW | FF_RGB | FF_ALPHA | FF_LAYER },
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
/* SVG image - import only */
	{ "SVG", "svg", "", FF_RGB | FF_ALPHA | FF_SCALE | FF_NOSAVE },
};

int file_type_by_ext(char *name, guint32 mask)
{
	char *ext;
	int i, l = LONGEST_EXT;


	ext = strrchr(name, '.');
	if (!ext || !ext[0]) return (FT_NONE);

	/* Special case for exploded frames (*.gif.000 etc.) */
	if (!ext[strspn(ext, ".0123456789")] && memchr(name, '.', ext - name))
	{
		char *tmp = ext;

		while (*(--ext) != '.');
		if (tmp - ext - 1 < LONGEST_EXT) l = tmp - ext - 1;
	}

	ext++;
	for (i = 0; i < NUM_FTYPES; i++)
	{
		unsigned int flags = file_formats[i].flags;

		if ((flags & FF_NOSAVE) || !(flags & mask)) continue;
		if (!strncasecmp(ext, file_formats[i].ext, l))
			return (i);
		if (!file_formats[i].ext2[0]) continue;
		if (!strncasecmp(ext, file_formats[i].ext2, l))
			return (i);
	}

	return (FT_NONE);
}

/* Set palette to white and black */
static void set_bw(ls_settings *settings)
{
	static const png_color wb[2] = { { 255, 255, 255 }, { 0, 0, 0 } };
	settings->colors = 2;
	memcpy(settings->pal, wb, sizeof(wb));
}

/* Set palette to grayscale */
static void set_gray(ls_settings *settings)
{
	settings->colors = 256;
	mem_bw_pal(settings->pal, 0, 255);
}

static int check_next_frame(frameset *fset, int mode, int anim)
{
	int lim = mode != FS_LAYER_LOAD ? FRAMES_MAX : anim ? MAX_LAYERS - 1 :
		MAX_LAYERS;
	return (fset->cnt < lim);
}

static int write_out_frame(char *file_name, ani_settings *ani, ls_settings *f_set);

static int process_page_frame(char *file_name, ani_settings *ani, ls_settings *w_set)
{
	image_frame *frame;

	if (ani->settings.mode == FS_EXPLODE_FRAMES)
		return (write_out_frame(file_name, ani, w_set));

	/* Store a new frame */
// !!! Currently, frames are allocated without checking any limits
	if (!mem_add_frame(&ani->fset, w_set->width, w_set->height,
		w_set->bpp, CMASK_NONE, w_set->pal)) return (FILE_MEM_ERROR);
	frame = ani->fset.frames + (ani->fset.cnt - 1);
	frame->cols = w_set->colors;
	frame->trans = w_set->xpm_trans;
	frame->delay = 0;
	frame->x = w_set->x;
	frame->y = w_set->y;
	memcpy(frame->img, w_set->img, sizeof(chanlist));
	return (0);
}

/* Receives struct with image parameters, and channel flags;
 * returns 0 for success, or an error code;
 * success doesn't mean that anything was allocated, loader must check that;
 * loader may call this multiple times - say, for each channel */
static int allocate_image(ls_settings *settings, int cmask)
{
	size_t sz, l;
	int i, j, oldmask, mode = settings->mode;

	if ((settings->width < 1) || (settings->height < 1)) return (-1);

	if ((settings->width > MAX_WIDTH) || (settings->height > MAX_HEIGHT))
		return (TOO_BIG);

	/* Don't show progress bar where there's no need */
	if (settings->width * settings->height <= (1 << silence_limit))
		settings->silent = TRUE;

	/* Reduce cmask according to mode */
	if (mode == FS_CLIP_FILE) cmask &= CMASK_CLIP;
	else if (mode == FS_CLIPBOARD) cmask &= CMASK_RGBA;
	else if ((mode == FS_CHANNEL_LOAD) || (mode == FS_PATTERN_LOAD))
		cmask &= CMASK_IMAGE;

	/* Overwriting is allowed */
	oldmask = cmask_from(settings->img);
	cmask &= ~oldmask;
	if (!cmask) return (0); // Already allocated

	/* No utility channels without image */
	oldmask |= cmask;
	if (!(oldmask & CMASK_IMAGE)) return (-1);

	j = TRUE; // For FS_LAYER_LOAD
	sz = (size_t)settings->width * settings->height;
	switch (mode)
	{
	case FS_PNG_LOAD: /* Regular image */
		/* Reserve memory */
		j = undo_next_core(UC_CREATE | UC_GETMEM, settings->width,
			settings->height, settings->bpp, oldmask);
		/* Drop current image if not enough memory for undo */
		if (j) mem_free_image(&mem_image, FREE_IMAGE);
	case FS_EXPLODE_FRAMES: /* Frames' temporaries */
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
	case FS_CLIPBOARD:
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
	case FS_PALETTE_LOAD: /* Palette */
	case FS_PALETTE_DEF:
		return (-1); // Should not arrive here if palette is present
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

		/* Clipboard */
		if ((settings->mode == FS_CLIP_FILE) ||
			(settings->mode == FS_CLIPBOARD))
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

/* Fills temp buffer row, or returns image row if no buffer */
static unsigned char *prepare_row(unsigned char *buf, ls_settings *settings,
	int bpp, int y)
{
	unsigned char *tmp, *tmi, *tma, *tms;
	int i, j, w = settings->width, h = y * w;
	int bgr = settings->ftype == FT_PNG ? 0 : 2;

	tmi = settings->img[CHN_IMAGE] + h * settings->bpp;
	if (bpp < (bgr ? 3 : 4)) /* Return/copy image row */
	{
		if (!buf) return (tmi);
		memcpy(buf, tmi, w * bpp);
		return (buf);
	}

	/* Produce BGR / BGRx / RGBx */
	tmp = buf;
	if (settings->bpp == 1) // Indexed
	{
		png_color *pal = settings->pal;

		for (i = 0; i < w; tmp += bpp , i++)
		{
			png_color *col = pal + *tmi++;
			tmp[bgr] = col->red;
			tmp[1] = col->green;
			tmp[bgr ^ 2] = col->blue;
		}
	}
	else // RGB
	{
		for (i = 0; i < w; tmp += bpp , tmi += 3 , i++)
		{
			tmp[0] = tmi[bgr];
			tmp[1] = tmi[1];
			tmp[2] = tmi[bgr ^ 2];
		}
	}

	/* Add alpha to the mix */
	tmp = buf + 3;
	tma = settings->img[CHN_ALPHA] + h;
	if (bpp == 3); // No alpha - all done
	else if ((settings->mode != FS_CLIPBOARD) || !settings->img[CHN_SEL])
	{
		// Only alpha here
		for (i = 0; i < w; tmp += bpp , i++)
			*tmp = *tma++;
	}
	else
	{
		// Merge alpha and selection
		tms = settings->img[CHN_SEL] + h;
		for (i = 0; i < w; tmp += bpp , i++)
		{
			j = *tma++ * *tms++;
			*tmp = (j + (j >> 8) + 1) >> 8;
		}
	}

	return (buf);
}

static void ls_init(char *what, int save)
{
	char buf[256];

	sprintf(buf, save ? _("Saving %s image") : _("Loading %s image"), what);
	progress_init(buf, 0);
}

static void ls_progress(ls_settings *settings, int n, int steps)
{
	int h = settings->height;

	if (!settings->silent && ((n * steps) % h >= h - steps))
		progress_update((float)n / h);
}

#if PNG_LIBPNG_VER >= 10400 /* 1.4+ */
#define png_set_gray_1_2_4_to_8(X) png_set_expand_gray_1_2_4_to_8(X)
#endif

/* !!! libpng 1.2.17-1.2.24 was losing extra chunks if there was no callback */
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

	/* !!! libpng 1.2.17-1.2.24 needs this to read extra channels */
	png_set_read_user_chunk_fn(png_ptr, NULL, buggy_libpng_handler);

	if (!mf) png_init_io(png_ptr, fp);
	else png_set_read_fn(png_ptr, mf, png_memread);
	png_set_sig_bytes(png_ptr, PNG_BYTES_TO_CHECK);

	/* Stupid libpng handles private chunks on all-or-nothing basis */
	png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_ALWAYS, NULL, 0);

	png_read_info(png_ptr, info_ptr);
	png_get_IHDR(png_ptr, info_ptr, &pwidth, &pheight, &bit_depth, &color_type,
		&interlace_type, NULL, NULL);
	if (png_get_valid(png_ptr, info_ptr, PNG_INFO_PLTE))
	{
		png_get_PLTE(png_ptr, info_ptr, &png_palette, &settings->colors);
		memcpy(settings->pal, png_palette, settings->colors * sizeof(png_color));
		/* If palette is all we need */
		res = 1;
		if ((settings->mode == FS_PALETTE_LOAD) ||
			(settings->mode == FS_PALETTE_DEF)) goto fail3;
	}

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
		case FS_CLIPBOARD:
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

#ifdef U_LCMS
#ifdef PNG_iCCP_SUPPORTED
	/* Extract ICC profile if it's of use */
	if (!settings->icc_size)
	{
		png_charp name, icc;
		png_uint_32 len;
		int comp;

		if (png_get_iCCP(png_ptr, info_ptr, &name, &comp, &icc, &len) &&
			(len < INT_MAX) && (settings->icc = malloc(len)))
		{
			settings->icc_size = len;
			memcpy(settings->icc, icc, len);
		}
	}
#endif
#endif

fail2:	if (msg) progress_end();
	free(row_pointers);
fail3:	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
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
	int h = settings->height, w = settings->width, bpp = settings->bpp;
	int i, chunks = 0, res = -1;
	long uninit_(dest_len), res_len;
	char *mess = NULL;
	unsigned char trans[256], *tmp, *rgba_row = NULL;
	png_color_16 trans_rgb;

	/* Baseline PNG format does not support alpha for indexed images, so
	 * we have to convert them to RGBA for clipboard export - WJ */
	if (((settings->mode == FS_CLIPBOARD) || (bpp == 3)) &&
		settings->img[CHN_ALPHA])
	{
		rgba_row = malloc(w * 4);
		if (!rgba_row) return (-1);
		bpp = 4;
	}

	if (!settings->silent)
	switch(settings->mode)
	{
	case FS_PNG_SAVE:
		mess = "PNG";
		break;
	case FS_CLIP_FILE:
	case FS_CLIPBOARD:
		mess = _("Clipboard");
		break;
	case FS_COMPOSITE_SAVE:
		mess = _("Layer");
		break;
	case FS_CHANNEL_SAVE:
		mess = _("Channel");
		break;
	default:
		settings->silent = TRUE;
		break;
	}

	if (!mf && ((fp = fopen(file_name, "wb")) == NULL)) goto exit0;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);

	if (!png_ptr) goto exit1;

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) goto exit2;

	res = 0;

	if (!mf) png_init_io(png_ptr, fp);
	else png_set_write_fn(png_ptr, mf, png_memwrite, png_memflush);
	png_set_compression_level(png_ptr, settings->png_compression);

	if (bpp == 1)
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
			8, bpp == 4 ? PNG_COLOR_TYPE_RGB_ALPHA :
			PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		if (settings->pal) png_set_PLTE(png_ptr, info_ptr, settings->pal,
			settings->colors);
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

	for (i = 0; i < h; i++)
	{
		tmp = prepare_row(rgba_row, settings, bpp, i);
		png_write_row(png_ptr, (png_bytep)tmp);
		ls_progress(settings, i, 20);
	}

	/* Save private chunks into PNG file if we need to */
	tmp = NULL;
	i = bpp == 1 ? CHN_ALPHA : CHN_ALPHA + 1;
	if (settings->mode == FS_CLIPBOARD) i = NUM_CHANNELS; // Disable extensions
	for (; i < NUM_CHANNELS; i++)
	{
		if (!settings->img[i]) continue;
		if (!tmp)
		{
			/* Get size required for each zlib compress */
			w = settings->width * settings->height;
#if ZLIB_VERNUM >= 0x1200
			dest_len = compressBound(w);
#else
			dest_len = w + (w >> 8) + 32;
#endif
			res = -1;
			tmp = malloc(dest_len);	  // Temporary space for compression
			if (!tmp) break;
			res = 0;
		}
		res_len = dest_len;
		if (compress2(tmp, &res_len, settings->img[i], w,
			settings->png_compression) != Z_OK) continue;
		strncpy(unknown0.name, chunk_names[i], 5);
		unknown0.data = tmp;
		unknown0.size = res_len;
		png_set_unknown_chunks(png_ptr, info_ptr, &unknown0, 1);
		png_set_unknown_chunk_location(png_ptr, info_ptr,
			chunks++, PNG_AFTER_IDAT);
	}
	free(tmp);
	png_write_end(png_ptr, info_ptr);

	if (mess) progress_end();

	/* Tidy up */
exit2:	png_destroy_write_struct(&png_ptr, &info_ptr);
exit1:	if (fp) fclose(fp);
exit0:	free(rgba_row);
	return (res);
}

#ifdef U_GIF

/* *** PREFACE ***
 * Contrary to what GIF89 docs say, all contemporary browser implementations
 * always render background in an animated GIF as transparent. So does mtPaint,
 * for some GIF animations depend on this rule.
 * An inter-frame delay of 0 normally means that the two (or more) frames
 * are parts of same animation frame and should be rendered as one resulting
 * frame; but some (ancient) GIFs have all delays set to 0, but still contain
 * animation sequences. So the handling of zero-delay frames is user-selectable
 * in mtPaint. */

/* Animation state */
typedef struct {
	unsigned char lmap[MAX_DIM];
	image_frame prev;
	int prev_idx; // Frame index+1, so that 0 means None
	int defw, defh, bk_rect[4];
	int mode;
	/* Extra fields for paletted images */
	int global_cols, newcols, newtrans;
	png_color global_pal[256], newpal[256];
	unsigned char xlat[513];
} ani_status;

/* Calculate new frame dimensions, and point-in-area bitmap */
static void ani_map_frame(ani_status *stat, ls_settings *settings)
{
	unsigned char *lmap;
	int i, j, w, h;


	/* Calculate the new dimensions */
// !!! Offsets considered nonnegative (as in GIF)
	w = settings->x + settings->width;
	if (stat->defw < w) stat->defw = w;
	h = settings->y + settings->height;
	if (stat->defh < h) stat->defh = h;

	/* The bitmap works this way: "Xth byte & (Yth byte >> 4)" tells which
	 * area(s) the pixel (X,Y) is in: bit 0 is for image (the new one),
	 * bit 1 is for underlayer (the previous composited frame), and bit 2
	 * is for hole in it (if "restore to background" was last) */
	j = stat->defw > stat->defh ? stat->defw : stat->defh;
	memset(lmap = stat->lmap, 0, j);
	// Mark new frame
	for (i = settings->x , j = i + settings->width; i < j; i++)
		lmap[i] |= 0x01; // Image bit
	for (i = settings->y , j = i + settings->height; i < j; i++)
		lmap[i] |= 0x10; // Image mask bit
	// Mark previous frame
	if (stat->prev_idx)
	{
		for (i = stat->prev.x , j = i + stat->prev.width; i < j; i++)
			lmap[i] |= 0x02; // Underlayer bit
		for (i = stat->prev.y , j = i + stat->prev.height; i < j; i++)
			lmap[i] |= 0x20; // Underlayer mask bit
	}
	// Mark disposal area
	if ((stat->bk_rect[0] < stat->bk_rect[2]) &&
		(stat->bk_rect[1] < stat->bk_rect[3])) // Add bkg rectangle
	{
		for (i = stat->bk_rect[0] , j = stat->bk_rect[2]; i < j; i++)
			lmap[i] |= 0x04; // Background bit
		for (i = stat->bk_rect[1] , j = stat->bk_rect[3]; i < j; i++)
			lmap[i] |= 0x40; // Background mask bit
	}
}

static int analyze_gif_frame(ani_status *stat, ls_settings *settings)
{
	unsigned char cmap[513], *lmap, *fg, *bg;
	png_color *pal, *prev;
	int tmpal[257], same_size, show_under;
	int i, k, l, x, y, ul, lpal, lprev, fgw, bgw, prevtr = -1;


	/* Locate the new palette */
	pal = prev = stat->global_pal;
	lpal = lprev = stat->global_cols;
	if (settings->colors > 0)
	{
		pal = settings->pal;
		lpal = settings->colors;
	}

	/* Accept new palette as final, for now */
	mem_pal_copy(stat->newpal, pal);
	stat->newcols = lpal;
	stat->newtrans = settings->xpm_trans;

	/* Prepare for new frame */
	if (stat->mode == ANM_RAW) // Raw frame mode
	{
		stat->defw = settings->width;
		stat->defh = settings->height;
		return (0);
	}
	ani_map_frame(stat, settings);
	same_size = !((stat->defw ^ settings->width) |
		(stat->defh ^ settings->height));

	for (i = 0; i < 256; i++) stat->xlat[i] = stat->xlat[i + 256] = i;
	stat->xlat[512] = stat->newtrans;

	/* First frame is exceptional */
	if (!stat->prev_idx)
	{
		// Trivial if no background gets drawn
		if (same_size) return (0);
		// Trivial if have transparent color
		if (settings->xpm_trans >= 0) return (1);
	}

	/* Disable transparency by default, enable when needed */
	stat->newtrans = -1;

	/* Now scan the dest area, filling colors bitmap */
	memset(cmap, 0, sizeof(cmap));
	fgw = settings->width;
	fg = settings->img[CHN_IMAGE] - (settings->y * fgw + settings->x);
	// Set underlayer pointer & step (ignore bpp!)
	bgw = stat->prev.width;
	bg = stat->prev.img[CHN_IMAGE] - (stat->prev.y * bgw + stat->prev.x);
	lmap = stat->lmap;
	for (y = 0; y < stat->defh; y++)
	{
		int ww = stat->defw, tp = settings->xpm_trans;
		int bmask = lmap[y] >> 4;

		for (x = 0; x < ww; x++)
		{
			int c0, bflag = lmap[x] & bmask;

			if ((bflag & 1) && ((c0 = fg[x]) != tp)) // New frame
				c0 += 256;
			else if ((bflag & 6) == 2) // Underlayer
				c0 = bg[x];
			else c0 = 512; // Background (transparency)
			cmap[c0] = 1;
		}
		fg += fgw; bg += bgw;
	}

	/* If we have underlayer */
	show_under = 0;
	if (stat->prev_idx)
	{
		// Use per-frame palette if underlayer has it
		prev = stat->prev.pal;
		lprev = stat->prev.cols;
		prevtr = stat->prev.trans;
		// Move underlayer transparency to "transparent"
		if (prevtr >= 0)
		{
			cmap[512] |= cmap[prevtr];
			cmap[prevtr] = 0;
		}
		// Check if underlayer is at all visible
		show_under = !!memchr(cmap, 1, 256);
		// Visible RGB/RGBA underlayer means RGB/RGBA frame
		if (show_under && (stat->prev.bpp == 3)) goto RGB;
	}

	/* Now, check if either frame's palette is enough */
	ul = 2; // Default is new palette
	if (show_under)
	{
		l = lprev > lpal ? lprev : lpal;
		k = lprev > lpal ? lpal : lprev;
		for (ul = 3 , i = 0; ul && (i < l); i++)
		{
			int tf2 = cmap[i] * 2 + cmap[256 + i];
			if (tf2 && ((i >= k) ||
				(PNG_2_INT(prev[i]) != PNG_2_INT(pal[i]))))
				ul &= ~tf2; // Exclude mismatched palette(s)
		}
		if (ul == 1) // Need old palette
		{
			mem_pal_copy(stat->newpal, prev);
			stat->newcols = lprev;
		}
	}
	while (ul) // Place transparency
	{
		if (cmap[512]) // Need transparency
		{
			int i, l = prevtr, nc = stat->newcols;

			/* If cannot use old transparent index */
			if ((l < 0) || (l >= nc) || (cmap[l] | cmap[l + 256]))
				l = settings->xpm_trans;
			/* If cannot use new one either */
			if ((l < 0) || (l >= nc) || (cmap[l] | cmap[l + 256]))
			{
				/* Try to find unused palette slot */
				for (l = -1 , i = 0; (l < 0) && (i < nc); i++)
					if (!(cmap[i] | cmap[i + 256])) l = i;
			}
			if (l < 0) /* Try to add a palette slot */
			{
				png_color *c;

				if (nc >= 256) break; // Failure
				l = stat->newcols++;
				c = stat->newpal + l;
				c->red = c->green = c->blue = 0;
			}
			// Modify mapping
			if (prevtr >= 0) stat->xlat[prevtr] = l;
			stat->xlat[512] = stat->newtrans = l;
		}
		// Successfully mapped everything - use paletted mode
		return (same_size ? 0 : 1);
	}

	/* Try to build combined palette */
	for (ul = i = 0; (ul < 257) && (i < 512); i++)
	{
		png_color *c;
		int j, v;

		if (!cmap[i]) continue;
		c = (i < 256 ? prev : pal - 256) + i;
		v = PNG_2_INT(*c);
		for (j = 0; (j < ul) && (tmpal[j] != v); j++);
		if (j == ul) tmpal[ul++] = v;
		stat->xlat[i] = j;
	}
	// Add transparent color
	if ((ul < 257) && cmap[512])
	{
		// Modify mapping
		if (prevtr >= 0) stat->xlat[prevtr] = ul;
		stat->xlat[512] = stat->newtrans = ul;
		tmpal[ul++] = 0;
	}
	if (ul < 257) // Success!
	{
		png_color *c = stat->newpal;
		for (i = 0; i < ul; i++ , c++) // Build palette
		{
			int v = tmpal[i];
			c->red = INT_2_R(v);
			c->green = INT_2_G(v);
			c->blue = INT_2_B(v);
		}
		stat->newcols = ul;
		// Use paletted mode
		return (same_size ? 0 : 1);
	}

	/* Tried everything in vain - fall back to RGB/RGBA */
RGB:	if (stat->global_cols > 0) // Use default palette if present
	{
		mem_pal_copy(stat->newpal, stat->global_pal);
		stat->newcols = stat->global_cols;
	}
	stat->newtrans = -1; // No color-key transparency
	// RGBA if underlayer with alpha, or transparent backround, is visible
	if ((show_under && stat->prev.img[CHN_ALPHA]) || cmap[512])
		return (4);
	// RGB otherwise
	return (3);
}

/* Convenience wrapper - expand palette to RGB triples */
static unsigned char *pal2rgb(unsigned char *rgb, png_color *pal)
{
	int i;

	if (!pal) return (rgb + 768); // Nothing to expand
	for (i = 0; i < 256; i++ , rgb += 3 , pal++)
	{
		rgb[0] = pal->red;
		rgb[1] = pal->green;
		rgb[2] = pal->blue;
	}
	return (rgb);
}

static void composite_gif_frame(frameset *fset, ani_status *stat,
	ls_settings *settings)
{
	unsigned char *dest, *fg0, *bg0 = NULL, *lmap = stat->lmap;
	image_frame *frame = fset->frames + (fset->cnt - 1), *bkf = &stat->prev;
	int disposal, w, fgw, bgw = 0, urgb = 0, tp = settings->xpm_trans;


	frame->trans = stat->newtrans;
	/* In raw mode, just store the offsets */
	if (stat->mode == ANM_RAW)
	{
		frame->x = settings->x;
		frame->y = settings->y;
		goto done;
	}
	/* Read & clear disposal mode */
	disposal = frame->flags & FM_DISPOSAL;
	frame->flags ^= disposal ^ FM_DISP_REMOVE;

	w = frame->width;
	dest = frame->img[CHN_IMAGE];
	fgw = settings->width;
	fg0 = settings->img[CHN_IMAGE] ? settings->img[CHN_IMAGE] -
		(settings->y * fgw + settings->x) : dest; // Always indexed (1 bpp)
	/* Pointer to absent underlayer is no problem - it just won't get used */
	bgw = bkf->width;
	bg0 = bkf->img[CHN_IMAGE] - (bkf->y * bgw + bkf->x) * bkf->bpp;
	urgb = bkf->bpp != 1;

	if (frame->bpp == 1) // To indexed
	{
		unsigned char *fg = fg0, *bg = bg0, *xlat = stat->xlat;
		int x, y;

		for (y = 0; y < frame->height; y++)
		{
			int bmask = lmap[y] >> 4;

			for (x = 0; x < w; x++)
			{
				int c0, bflag = lmap[x] & bmask;

				if ((bflag & 1) && ((c0 = fg[x]) != tp)) // New frame
					c0 += 256;
				else if ((bflag & 6) == 2) // Underlayer
					c0 = bg[x];
				else c0 = 512; // Background (transparent)
				*dest++ = xlat[c0];
			}
			fg += fgw; bg += bgw;
		}
	}
	else // To RGB
	{
		unsigned char rgb[513 * 3], *tmp, *fg = fg0, *bg = bg0;
		int x, y, bpp = urgb + urgb + 1;

		/* Setup global palette map: underlayer, image, background */
		tmp = pal2rgb(rgb, bkf->pal);
		tmp = pal2rgb(tmp, settings->pal);
		tmp[0] = tmp[1] = tmp[2] = 0;
		frame->trans = -1; // No color-key transparency

		for (y = 0; y < frame->height; y++)
		{
			int bmask = lmap[y] >> 4;

			for (x = 0; x < w; x++)
			{
				unsigned char *src;
				int c0, bflag = lmap[x] & bmask;

				if ((bflag & 1) && ((c0 = fg[x]) != tp)) // New frame
					src = rgb + (256 * 3) + (c0 * 3);
				else if ((bflag & 6) == 2) // Underlayer
					src = urgb ? bg + x * 3 : rgb + bg[x] * 3;
				else src = rgb + 512 * 3; // Background (black)
				dest[0] = src[0];
				dest[1] = src[1];
				dest[2] = src[2];
				dest += 3;
			}
			fg += fgw; bg += bgw * bpp;
		}
	}

	if (frame->img[CHN_ALPHA]) // To alpha
	{
		unsigned char *fg = fg0, *bg = NULL;
		int x, y, af = 0, utp = -1;

		dest = frame->img[CHN_ALPHA];
		utp = bkf->bpp == 1 ? bkf->trans : -1;
		af = !!bkf->img[CHN_ALPHA]; // Underlayer has alpha
		bg = bkf->img[af ? CHN_ALPHA : CHN_IMAGE] - (bkf->y * bgw + bkf->x);

		for (y = 0; y < frame->height; y++)
		{
			int bmask = lmap[y] >> 4;

			for (x = 0; x < w; x++)
			{
				int c0, bflag = lmap[x] & bmask;

				if ((bflag & 1) && (fg[x] != tp)) // New frame
					c0 = 255;
				else if ((bflag & 6) == 2) // Underlayer
				{
					c0 = bg[x];
					if (!af) c0 = c0 != utp ? 255 : 0;
				}
				else c0 = 0; // Background (transparent)
				*dest++ = c0;
			}
			fg += fgw; bg += bgw;
		}
	}

	/* Prepare the disposal action */
	memset(&stat->bk_rect, 0, sizeof(stat->bk_rect)); // Clear old
	switch (disposal)
	{
	case FM_DISP_REMOVE: // Dispose to background
		// Image-sized hole in underlayer
		stat->bk_rect[2] = (stat->bk_rect[0] = settings->x) +
			settings->width;
		stat->bk_rect[3] = (stat->bk_rect[1] = settings->y) +
			settings->height;
		// Fallthrough
	case FM_DISP_LEAVE: // Don't dispose
		stat->prev = *frame; // Current frame becomes underlayer
		if (!stat->prev.pal) stat->prev.pal = fset->pal;
		if (stat->prev_idx &&
			(fset->frames[stat->prev_idx - 1].flags & FM_NUKE))
			/* Remove the unref'd frame */
			mem_remove_frame(fset, stat->prev_idx - 1);
		stat->prev_idx = fset->cnt;
		break;
	case FM_DISP_RESTORE: // Dispose to previous
		// Underlayer stays unchanged
		break;
	}
done:	if ((fset->cnt > 1) && (stat->prev_idx != fset->cnt - 1) &&
		(fset->frames[fset->cnt - 2].flags & FM_NUKE))
	{
		/* Remove the next-to-last frame */
		mem_remove_frame(fset, fset->cnt - 2);
		if (stat->prev_idx > fset->cnt)
			stat->prev_idx = fset->cnt;
	}
}

static int convert_gif_palette(png_color *pal, ColorMapObject *cmap)
{
	int i, j;

	if (!cmap) return (-1);
	j = cmap->ColorCount;
	if ((j > 256) || (j < 1)) return (-1);
	for (i = 0; i < j; i++)
	{
		pal[i].red = cmap->Colors[i].Red;
		pal[i].green = cmap->Colors[i].Green;
		pal[i].blue = cmap->Colors[i].Blue;
	}
	return (j);
}

static int load_gif_frame(GifFileType *giffy, ls_settings *settings)
{
	/* GIF interlace pattern: Y0, DY, ... */
	static const unsigned char interlace[10] =
		{ 0, 1, 0, 8, 4, 8, 2, 4, 1, 2 };
	int i, k, kx, n, w, h, dy, res;


	if (DGifGetImageDesc(giffy) == GIF_ERROR) return (-1);

	/* Get local palette if any */
	if (giffy->Image.ColorMap)
		settings->colors = convert_gif_palette(settings->pal, giffy->Image.ColorMap);
	if (settings->colors < 0) return (-1); // No palette at all
	/* If palette is all we need */
	if ((settings->mode == FS_PALETTE_LOAD) ||
		(settings->mode == FS_PALETTE_DEF)) return (EXPLODE_FAILED);

	/* Store actual image parameters */
	settings->x = giffy->Image.Left;
	settings->y = giffy->Image.Top;
	settings->width = w = giffy->Image.Width;
	settings->height = h = giffy->Image.Height;
	settings->bpp = 1;

	if ((res = allocate_image(settings, CMASK_IMAGE))) return (res);
	res = FILE_LIB_ERROR;

	if (!settings->silent) ls_init("GIF", 0);

	if (giffy->Image.Interlace) k = 2 , kx = 10;
	else k = 0 , kx = 2;

	for (n = 0; k < kx; k += 2)
	{
		dy = interlace[k + 1];
		for (i = interlace[k]; i < h; n++ , i += dy)
		{
			if (DGifGetLine(giffy, settings->img[CHN_IMAGE] +
				i * w, w) == GIF_ERROR) goto fail;
			ls_progress(settings, n, 10);
		}
	}
	res = 1;
fail:	if (!settings->silent) progress_end();
	return (res);
}

static int load_gif_frames(char *file_name, ani_settings *ani)
{
	/* GIF disposal codes mapping */
	static const unsigned short gif_disposal[8] = {
		FM_DISP_LEAVE, FM_DISP_LEAVE, FM_DISP_REMOVE, FM_DISP_RESTORE,
		/* Handling (reserved) "4" same as "3" is what Mozilla does */
		FM_DISP_RESTORE, FM_DISP_LEAVE, FM_DISP_LEAVE, FM_DISP_LEAVE
	};
	GifFileType *giffy;
	GifRecordType gif_rec;
	GifByteType *byte_ext;
	png_color w_pal[256];
	ani_status stat;
	image_frame *frame;
	ls_settings w_set, init_set;
	int res, val, disposal, bpp, cmask, lastzero = FALSE;


	if (!(giffy = DGifOpenFileName(file_name))) return (-1);

	/* Init state structure */
	memset(&stat, 0, sizeof(stat));
	stat.mode = ani->mode;
	stat.defw = giffy->SWidth;
	stat.defh = giffy->SHeight;
	stat.global_cols = convert_gif_palette(stat.global_pal, giffy->SColorMap);

	/* Init temp container */
	init_set = ani->settings;
	init_set.colors = 0; // Nonzero will signal local palette
	init_set.pal = w_pal;
	init_set.xpm_trans = -1;
	init_set.gif_delay = 0;
	disposal = FM_DISP_LEAVE;

	/* Init frameset */
	if (stat.global_cols > 0) // Set default palette
	{
		res = FILE_MEM_ERROR;
		if (!(ani->fset.pal = malloc(SIZEOF_PALETTE))) goto fail;
		mem_pal_copy(ani->fset.pal, stat.global_pal);
	}

	while (TRUE)
	{
		res = -1;
		if (DGifGetRecordType(giffy, &gif_rec) == GIF_ERROR) goto fail;
		if (gif_rec == TERMINATE_RECORD_TYPE) break;
		else if (gif_rec == EXTENSION_RECORD_TYPE)
		{
			if (DGifGetExtension(giffy, &val, &byte_ext) == GIF_ERROR) goto fail;
			while (byte_ext)
			{
				if (val == GRAPHICS_EXT_FUNC_CODE)
				{
				/* !!! In practice, Graphics Control Extension
				 * affects not only "the first block to follow"
				 * as docs say, but EVERY following block - WJ */
					init_set.xpm_trans = byte_ext[1] & 1 ?
						byte_ext[4] : -1;
					init_set.gif_delay = byte_ext[2] +
						(byte_ext[3] << 8);
					disposal = gif_disposal[(byte_ext[1] >> 2) & 7];
				}
				if (DGifGetExtensionNext(giffy, &byte_ext) == GIF_ERROR) goto fail;
			}
		}
		else if (gif_rec == IMAGE_DESC_RECORD_TYPE)
		{
			res = FILE_TOO_LONG;
			if (!check_next_frame(&ani->fset, ani->settings.mode, TRUE))
				goto fail;
			w_set = init_set;
			res = load_gif_frame(giffy, &w_set);
			if (res != 1) goto fail;
			/* Analyze how we can merge the frames */
			bpp = analyze_gif_frame(&stat, &w_set);
			cmask = !bpp ? CMASK_NONE : bpp > 3 ? CMASK_RGBA : CMASK_IMAGE;
			/* Allocate a new frame */
// !!! Currently, frames are allocated without checking any limits
			res = FILE_MEM_ERROR;
			if (!mem_add_frame(&ani->fset, stat.defw, stat.defh,
				bpp > 1 ? 3 : 1, cmask, stat.newpal)) goto fail;
			frame = ani->fset.frames + (ani->fset.cnt - 1);
			frame->cols = stat.newcols;
			frame->delay = w_set.gif_delay;
			frame->flags = disposal; // Pass to compositing
			/* Tag zero-delay frame for deletion if requested */
			if ((lastzero = (stat.mode == ANM_NOZERO) &&
				!w_set.gif_delay)) frame->flags |= FM_NUKE;
			if (!bpp) // Same bpp & dimensions - reassign the chanlist
			{
				memcpy(frame->img, w_set.img, sizeof(chanlist));
				memset(w_set.img, 0, sizeof(chanlist));
			}
			/* Do actual compositing, remember disposal method */
			composite_gif_frame(&ani->fset, &stat, &w_set);
			// !!! "frame" pointer may be invalid past this point
			mem_free_chanlist(w_set.img);
			memset(w_set.img, 0, sizeof(chanlist));
			/* Write out those frames worthy to be stored */
			if ((ani->settings.mode == FS_EXPLODE_FRAMES) && !lastzero)
			{
				res = write_out_frame(file_name, ani, NULL);
				if (res) goto fail;
			}
		}
	}
	/* Write out the final frame if not written before */
	if ((ani->settings.mode == FS_EXPLODE_FRAMES) && lastzero)
	{
		res = write_out_frame(file_name, ani, NULL);
		if (res) goto fail;
	}
	res = 1;
fail:	mem_free_chanlist(w_set.img);
	DGifCloseFile(giffy);
	return (res);
}

static int load_gif(char *file_name, ls_settings *settings)
{
	GifFileType *giffy;
	GifRecordType gif_rec;
	GifByteType *byte_ext;
	int res, val, frame = 0;
	int delay = settings->gif_delay, trans = -1, disposal = 0;


	if (!(giffy = DGifOpenFileName(file_name))) return (-1);

	/* Get global palette */
	settings->colors = convert_gif_palette(settings->pal, giffy->SColorMap);

	while (TRUE)
	{
		res = -1;
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
				res = FILE_HAS_FRAMES;
				goto fail;
			}
			settings->gif_delay = delay;
			settings->xpm_trans = trans;
			res = load_gif_frame(giffy, settings);
			if (res != 1) goto fail;
		}
	}
	res = 1;
fail:	DGifCloseFile(giffy);
	return (res);
}

static int save_gif(char *file_name, ls_settings *settings)
{
	ColorMapObject *gif_map;
	GifFileType *giffy;
	unsigned char gif_ext_data[8];
	int i, nc, w = settings->width, h = settings->height, msg = -1;
#ifndef WIN32
	mode_t mode;
#endif


	/* GIF save must be on indexed image */
	if (settings->bpp != 1) return WRONG_FORMAT;

	/* Get the next power of 2 for colormap size */
	nc = settings->colors - 1;
	nc |= nc >> 1; nc |= nc >> 2; nc |= nc >> 4;
	nc += !nc + 1; // No less than 2 colors

	gif_map = MakeMapObject(nc, NULL);
	if (!gif_map) return -1;

	giffy = EGifOpenFileName(file_name, FALSE);
	if (!giffy) goto fail0;

	for (i = 0; i < settings->colors; i++)
	{
		gif_map->Colors[i].Red	 = settings->pal[i].red;
		gif_map->Colors[i].Green = settings->pal[i].green;
		gif_map->Colors[i].Blue	 = settings->pal[i].blue;
	}
	for (; i < nc; i++)
	{
		gif_map->Colors[i].Red = gif_map->Colors[i].Green = 
			gif_map->Colors[i].Blue	= 0;
	}

	if (EGifPutScreenDesc(giffy, w, h, nc, 0, gif_map) == GIF_ERROR)
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
		ls_progress(settings, i, 20);
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

#ifdef NEED_CMYK
#ifdef U_LCMS
/* Guard against cmsHTRANSFORM changing into something overlong in the future */
typedef char cmsHTRANSFORM_Does_Not_Fit_Into_Pointer[2 * (sizeof(cmsHTRANSFORM) <= sizeof(char *)) - 1];

static int init_cmyk2rgb(ls_settings *settings, unsigned char *icc, int len,
	int inverted)
{
	cmsHPROFILE from, to;
	cmsHTRANSFORM how = NULL;

	from = cmsOpenProfileFromMem((void *)icc, len);
	if (!from) return (TRUE); // Unopenable now, unopenable ever
	to = cmsCreate_sRGBProfile();
	if (cmsGetColorSpace(from) == icSigCmykData)
		how = cmsCreateTransform(from, inverted ? TYPE_CMYK_8_REV :
			TYPE_CMYK_8, to, TYPE_RGB_8, INTENT_PERCEPTUAL, 0);
	if (from) cmsCloseProfile(from);
	cmsCloseProfile(to);
	if (!how) return (FALSE); // Better luck the next time

	settings->icc = (char *)how;
	settings->icc_size = -2;
	return (TRUE);
}

static void done_cmyk2rgb(ls_settings *settings)
{
	if (settings->icc_size != -2) return;
	cmsDeleteTransform((cmsHTRANSFORM)settings->icc);
	settings->icc = NULL;
	settings->icc_size = -1; // Not need profiles anymore
}

#else /* No LCMS */
#define done_cmyk2rgb(X)
#endif

static void cmyk2rgb(unsigned char *dest, unsigned char *src, int cnt,
	int inverted, ls_settings *settings)
{
	unsigned char xb;
	int j, k, r, g, b;

#ifdef U_LCMS
	/* Convert CMYK to RGB using LCMS if possible */
	if (settings->icc_size == -2)
	{
		cmsDoTransform((cmsHTRANSFORM)settings->icc, src, dest, cnt);
		return;
	}
#endif
	/* Simple CMYK->RGB conversion */
	xb = inverted ? 0 : 255;
	for (j = 0; j < cnt; j++ , src += 4 , dest += 3)
	{
		k = src[3] ^ xb;
		r = (src[0] ^ xb) * k;
		dest[0] = (r + (r >> 8) + 1) >> 8;
		g = (src[1] ^ xb) * k;
		dest[1] = (g + (g >> 8) + 1) >> 8;
		b = (src[2] ^ xb) * k;
		dest[2] = (b + (b >> 8) + 1) >> 8;
	}
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
	unsigned char *memp, *memx = NULL;
	FILE *fp;
	int i, width, height, bpp, res = -1, inv = 0;
#ifdef U_LCMS
	unsigned char *icc = NULL;
#endif

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

#ifdef U_LCMS
	/* Request ICC profile aka APP2 data be preserved */
	if (!settings->icc_size)
		jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF);
#endif

	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);

	bpp = 3;
	switch (cinfo.out_color_space)
	{
	case JCS_RGB: break;
	case JCS_GRAYSCALE:
		set_gray(settings);
		bpp = 1;
		break;
	case JCS_CMYK:
		/* Photoshop writes CMYK data inverted */
		inv = cinfo.saw_Adobe_marker;
		if ((memx = malloc(cinfo.output_width * 4))) break;
		res = FILE_MEM_ERROR;
		// Fallthrough
	default: goto fail; /* Unsupported colorspace */
	}

	settings->width = width = cinfo.output_width;
	settings->height = height = cinfo.output_height;
	settings->bpp = bpp;
	if ((res = allocate_image(settings, CMASK_IMAGE))) goto fail;
	res = -1;
	pr = !settings->silent;

#ifdef U_LCMS
#define PARTHDR 14
	while (!settings->icc_size)
	{
		jpeg_saved_marker_ptr mk;
		unsigned char *tmp, *parts[256];
		int i, part, nparts = -1, icclen = 0, lparts[256];

		/* List parts */
		memset(parts, 0, sizeof(parts));
		for (mk = cinfo.marker_list; mk; mk = mk->next)
		{
			if ((mk->marker != JPEG_APP0 + 2) ||
				(mk->data_length < PARTHDR) ||
				strcmp(mk->data, "ICC_PROFILE")) continue;
			part = GETJOCTET(mk->data[13]);
			if (nparts < 0) nparts = part;
			if (nparts != part) break;
			part = GETJOCTET(mk->data[12]);
			if (!part-- || (part >= nparts) || parts[part]) break;
			parts[part] = (unsigned char *)(mk->data + PARTHDR);
			icclen += lparts[part] = mk->data_length - PARTHDR;
		}
		if (nparts < 0) break;

		icc = tmp = malloc(icclen);
		if (!icc) break;

		/* Assemble parts */
		for (i = 0; i < nparts; i++)
		{
			if (!parts[i]) break;
			memcpy(tmp, parts[i], lparts[i]);
			tmp += lparts[i];
		}
		if (i < nparts) break; // Sequence had a hole

		/* If profile is needed right now, for CMYK->RGB */
		if (memx && init_cmyk2rgb(settings, icc, icclen, inv))
			break; // Transform is ready, so drop the profile

		settings->icc = icc;
		settings->icc_size = icclen;
		icc = NULL; // Leave the profile be
		break;
	}
	free(icc);
#undef PARTHDR
#endif

	if (pr) ls_init("JPEG", 0);

	for (i = 0; i < height; i++)
	{
		memp = settings->img[CHN_IMAGE] + width * i * bpp;
		jpeg_read_scanlines(&cinfo, memx ? &memx : &memp, 1);
		if (memx) cmyk2rgb(memp, memx, width, inv, settings);
		ls_progress(settings, i, 20);
	}
	done_cmyk2rgb(settings);
	jpeg_finish_decompress(&cinfo);
	res = 1;

fail:	if (pr) progress_end();
	jpeg_destroy_decompress(&cinfo);
	fclose(fp);
	free(memx);
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
		ls_progress(settings, i, 20);
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
 * OpenJPEG 1.x is wasteful in the extreme, with memory overhead of about
 * 7 times the unpacked image size. So it can fail to handle even such
 * resolutions that fit into available memory with lots of room to spare.
 * Still, JasPer is an even worse memory hog, if a somewhat faster one.
 * Another thing - Linux builds of OpenJPEG cannot properly encode an opacity
 * channel (fixed in SVN on 06.11.09, revision 541)
 * And JP2 images with 4 channels, produced by OpenJPEG, cause JasPer
 * to die horribly - WJ */

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
	res = -1;
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
		set_gray(settings);
		settings->bpp = 1;
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

#ifdef U_JASPER

/* *** PREFACE ***
 * JasPer is QUITE a memory waster, with peak memory usage nearly TEN times the
 * unpacked image size. But what is worse, its API is 99% undocumented.
 * And to add insult to injury, it reacts to some invalid JP2 files (4-channel
 * ones written by OpenJPEG) by abort()ing, instead of returning error - WJ */

static int jasper_init;

static int load_jpeg2000(char *file_name, ls_settings *settings)
{
	jas_image_t *img;
	jas_stream_t *inp;
	jas_matrix_t *mx;
	jas_seqent_t *src;
	char *fmt;
	unsigned char xtb[256], *dest;
	int nc, cspace, mode, slots[4];
	int bits, shift, delta, chan, step;
	int i, j, k, n, nx, w, h, bpp, pr = 0, res = -1;


	/* Init the dumb library */
	if (!jasper_init) jas_init();
	jasper_init = TRUE;
	/* Open the file */
	inp = jas_stream_fopen(file_name, "rb");
	if (!inp) return (-1);
	/* Validate format */
	fmt = jas_image_fmttostr(jas_image_getfmt(inp));
	if (!fmt || strcmp(fmt, settings->ftype == FT_JP2 ? "jp2" : "jpc"))
		goto ffail;

	/* Decode the file into a halfbaked pile of bytes */
	if ((pr = !settings->silent)) ls_init("JPEG2000", 0);
	img = jas_image_decode(inp, -1, NULL);
	jas_stream_close(inp);
	if (!img) goto dfail;
	/* Analyze the pile's contents */
	nc = jas_image_numcmpts(img);
	mode = jas_clrspc_fam(cspace = jas_image_clrspc(img));
	if (mode == JAS_CLRSPC_FAM_GRAY) bpp = 1;
	else if (mode == JAS_CLRSPC_FAM_RGB) bpp = 3;
	else goto ifail;
	if (bpp == 3)
	{
		slots[0] = jas_image_getcmptbytype(img,
			JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_RGB_R));
		slots[1] = jas_image_getcmptbytype(img,
			JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_RGB_G));
		slots[2] = jas_image_getcmptbytype(img,
			JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_RGB_B));
		if ((slots[1] < 0) | (slots[2] < 0)) goto ifail;
	}
	else
	{
		slots[0] = jas_image_getcmptbytype(img,
			JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_GRAY_Y));
		set_gray(settings);
	}
	if (slots[0] < 0) goto ifail;
	if (nc > bpp)
	{
		slots[bpp] = jas_image_getcmptbytype(img, JAS_IMAGE_CT_OPACITY);
/* !!! JasPer has a bug - it doesn't write out component definitions if color
 * channels are in natural order, thus losing the types of any extra components.
 * (See where variable "needcdef" in src/libjasper/jp2/jp2_enc.c gets unset.)
 * Then on reading, type will be replaced by component's ordinal number - WJ */
		if (slots[bpp] < 0) slots[bpp] = jas_image_getcmptbytype(img, bpp);
		/* Use an unlabeled extra component for alpha if no labeled one */
		if (slots[bpp] < 0)
			slots[bpp] = jas_image_getcmptbytype(img, JAS_IMAGE_CT_UNKNOWN);
		nc = bpp + (slots[bpp] >= 0); // Ignore extra channels if no alpha
	}
	w = jas_image_cmptwidth(img, slots[0]);
	h = jas_image_cmptheight(img, slots[0]);
	for (i = 1; i < nc; i++) /* Check if all components are the same size */
	{
		if ((jas_image_cmptwidth(img, slots[i]) != w) ||
			(jas_image_cmptheight(img, slots[i]) != h)) goto ifail;
	}

	/* Allocate "matrix" */
	res = FILE_MEM_ERROR;
	mx = jas_matrix_create(1, w);
	if (!mx) goto ifail;
	/* Allocate image */
	settings->width = w;
	settings->height = h;
	settings->bpp = bpp;
	if ((res = allocate_image(settings, nc > bpp ? CMASK_RGBA : CMASK_IMAGE)))
		goto mfail;
	if (!settings->img[CHN_ALPHA]) nc = bpp;
	res = 1;
#if U_LCMS
	/* JasPer implements CMS internally, but without lcms, it makes no sense
	 * to provide all the interface stuff for this one rare format - WJ */
	while (!settings->icc_size && (bpp == 3) && (cspace != JAS_CLRSPC_SRGB))
	{
		jas_cmprof_t *prof;
		jas_image_t *timg;

		res = FILE_LIB_ERROR;
		prof = jas_cmprof_createfromclrspc(JAS_CLRSPC_SRGB);
		if (!prof) break;
		timg = jas_image_chclrspc(img, prof, JAS_CMXFORM_INTENT_PER);
		jas_cmprof_destroy(prof);
		if (!timg) break;
		jas_image_destroy(img);
		img = timg;
		res = 1; // Success - further code is fail-proof
		break;
	}
#endif

	/* Unravel the ugly thing into proper format */
	nx = h * nc;
	for (i = n = 0; i < nc; i++)
	{
		if (i < bpp) /* Image */
		{
			dest = settings->img[CHN_IMAGE] + i;
			step = settings->bpp;
		}
		else /* Alpha */
		{
			dest = settings->img[CHN_ALPHA];
			step = 1;
		}
		chan = slots[i];
		bits = jas_image_cmptprec(img, chan);
		delta = jas_image_cmptsgnd(img, chan) ? 1 << (bits - 1) : 0;
		shift = bits > 8 ? bits - 8 : 0;
		set_xlate(xtb, bits - shift);
		for (j = 0; j < h; j++ , n++)
		{
			jas_image_readcmpt(img, chan, 0, j, w, 1, mx);
			src = jas_matrix_getref(mx, 0, 0);
			for (k = 0; k < w; k++)
			{
				*dest = xtb[(unsigned)(src[k] + delta) >> shift];
				dest += step;
			}
			if (pr && ((n * 10) % nx >= nx - 10))
				progress_update((float)n / nx);
		}
	}

mfail:	jas_matrix_destroy(mx);
ifail:	jas_image_destroy(img);
dfail:	if (pr) progress_end();
	return (res);
ffail:	jas_stream_close(inp);
	return (-1);
}

static int save_jpeg2000(char *file_name, ls_settings *settings)
{
	static const jas_image_cmpttype_t chans[4] = {
		JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_RGB_R),
		JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_RGB_G),
		JAS_IMAGE_CT_COLOR(JAS_IMAGE_CT_RGB_B),
		JAS_IMAGE_CT_OPACITY };
	jas_image_cmptparm_t cp[4];
	jas_image_t *img;
	jas_stream_t *outp;
	jas_matrix_t *mx;
	jas_seqent_t *dest;
	char buf[256], *opts = NULL;
	unsigned char *src;
	int w = settings->width, h = settings->height, res = -1;
	int i, j, k, n, nx, nc, step, pr;


	if (settings->bpp == 1) return WRONG_FORMAT;

	/* Init the dumb library */
	if (!jasper_init) jas_init();
	jasper_init = TRUE;
	/* Open the file */
	outp = jas_stream_fopen(file_name, "wb");
	if (!outp) return (-1);
	/* Setup component parameters */
	memset(cp, 0, sizeof(cp)); // Zero out all that needs zeroing
	cp[0].hstep = cp[0].vstep = 1;
	cp[0].width = w; cp[0].height = h;
	cp[0].prec = 8;
	cp[3] = cp[2] = cp[1] = cp[0];
	/* Create image structure */
	nc = 3 + !!settings->img[CHN_ALPHA];
	img = jas_image_create(nc, cp, JAS_CLRSPC_SRGB);
	if (!img) goto fail;
	/* Allocate "matrix" */
	mx = jas_matrix_create(1, w);
	if (!mx) goto fail2;

	if ((pr = !settings->silent)) ls_init("JPEG2000", 1);

	/* Fill image structure */
	nx = h * nc;
	nx += nx / 10 + 1; // Show "90% done" while compressing
	for (i = n = 0; i < nc; i++)
	{
	/* !!! The only workaround for JasPer losing extra components' types on
	 * write is to reorder the RGB components - but then, dumb readers, such
	 * as ones in Mozilla and GTK+, would read them in wrong order - WJ */
		jas_image_setcmpttype(img, i, chans[i]);
		if (i < 3) /* Image */
		{
			src = settings->img[CHN_IMAGE] + i;
			step = settings->bpp;
		}
		else /* Alpha */
		{
			src = settings->img[CHN_ALPHA];
			step = 1;
		}
		for (j = 0; j < h; j++ , n++)
		{
			dest = jas_matrix_getref(mx, 0, 0);
			for (k = 0; k < w; k++)
			{
				dest[k] = *src;
				src += step;
			}
			jas_image_writecmpt(img, i, 0, j, w, 1, mx);
			if (pr && ((n * 10) % nx >= nx - 10))
				if (progress_update((float)n / nx)) goto fail3;
		}
	}

	/* Compress it */
	if (pr) progress_update(0.9);
	if (settings->jp2_rate) // Lossless if NO "rate" option passed
		sprintf(opts = buf, "rate=%g", 1.0 / settings->jp2_rate);
	if (!jas_image_encode(img, outp, jas_image_strtofmt(
		settings->ftype == FT_JP2 ? "jp2" : "jpc"), opts)) res = 0;
	jas_stream_flush(outp);
	if (pr) progress_update(1.0);

fail3:	if (pr) progress_end();
	jas_matrix_destroy(mx);
fail2:	jas_image_destroy(img);
fail:	jas_stream_close(outp);
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

static int load_tiff_frame(TIFF *tif, ls_settings *settings)
{
	char cbuf[1024];
	uint16 bpsamp, sampp, xsamp, pmetric, planar, orient, sform;
	uint16 *sampinfo, *red16, *green16, *blue16;
	uint32 width, height, tw = 0, th = 0, rps = 0;
	uint32 *tr, *raster = NULL;
	unsigned char *tmp, *buf = NULL;
	int bpp = 3, cmask = CMASK_IMAGE, argb = FALSE, pr = FALSE;
	int i, j, mirror, res;


	/* Let's learn what we've got */
	TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &sampp);
	TIFFGetFieldDefaulted(tif, TIFFTAG_EXTRASAMPLES, &xsamp, &sampinfo);
	if (!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &pmetric))
	{
		/* Defaults like in libtiff */
		if (sampp - xsamp == 1) pmetric = PHOTOMETRIC_MINISBLACK;
		else if (sampp - xsamp == 3) pmetric = PHOTOMETRIC_RGB;
		else return (-1);
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

	/* Extract position from it */
	settings->x = settings->y = 0;
	while (TRUE)
	{
		float xres, yres, dxu = 0, dyu = 0;

		if (!TIFFGetField(tif, TIFFTAG_XPOSITION, &dxu) &&
			!TIFFGetField(tif, TIFFTAG_YPOSITION, &dyu)) break;
		// Have position, now need resolution
		if (!TIFFGetField(tif, TIFFTAG_XRESOLUTION, &xres)) break;
		// X resolution we have, what about Y?
		yres = xres; // Default
		TIFFGetField(tif, TIFFTAG_YRESOLUTION, &yres);
		// Convert ResolutionUnits (whatever they are) to pixels
		settings->x = rint(dxu * xres);
		settings->y = rint(dyu * yres);
		break;
	}

	/* Let's decide how to store it */
	if ((width > MAX_WIDTH) || (height > MAX_HEIGHT)) return (-1);
	settings->width = width;
	settings->height = height;
	if ((sform != SAMPLEFORMAT_UINT) && (sform != SAMPLEFORMAT_INT) &&
		(sform != SAMPLEFORMAT_VOID)) argb = TRUE;
	else	switch (pmetric)
		{
		case PHOTOMETRIC_PALETTE:
		{
			png_color *cp = settings->pal;
			int i, j, k, na = 0, nd = 1; /* Old palette format */

			if (bpsamp > 8)
			{
				argb = TRUE;
				break;
			}
			if (!TIFFGetField(tif, TIFFTAG_COLORMAP,
				&red16, &green16, &blue16)) return (-1);

			settings->colors = j = 1 << bpsamp;
			/* Analyze palette */
			for (k = i = 0; i < j; i++)
			{
				k |= red16[i] | green16[i] | blue16[i];
			}
			if (k > 255) na = 128 , nd = 257; /* New palette format */

			for (i = 0; i < j; i++ , cp++)
			{
				cp->red = (red16[i] + na) / nd;
				cp->green = (green16[i] + na) / nd;
				cp->blue = (blue16[i] + na) / nd;
			}
			/* If palette is all we need */
			if ((settings->mode == FS_PALETTE_LOAD) ||
				(settings->mode == FS_PALETTE_DEF))
				return (EXPLODE_FAILED);
			/* Fallthrough */
		}
		case PHOTOMETRIC_MINISWHITE:
		case PHOTOMETRIC_MINISBLACK:
			bpp = 1; break;
		case PHOTOMETRIC_RGB:
			break;
		case PHOTOMETRIC_SEPARATED:
			/* Leave non-CMYK separations to libtiff */
			if (sampp - xsamp == 4) break;
		default:
			argb = TRUE;
		}

	/* libtiff can't handle this and neither can we */
	if (argb && !TIFFRGBAImageOK(tif, cbuf)) return (-1);

	settings->bpp = bpp;
	/* Photoshop writes alpha as EXTRASAMPLE_UNSPECIFIED anyway */
	if (xsamp) cmask = CMASK_RGBA;

	/* !!! No alpha support for RGB mode yet */
	if (argb) cmask = CMASK_IMAGE;

	if ((res = allocate_image(settings, cmask))) return (res);
	res = -1;

#ifdef U_LCMS
#ifdef TIFFTAG_ICCPROFILE
	/* Extract ICC profile if it's of use */
	if (!settings->icc_size)
	{
		uint32 size;
		unsigned char *data;

		/* TIFFTAG_ICCPROFILE was broken beyond hope in libtiff 3.8.0
		 * (see libtiff 3.8.1+ changelog entry for 2006-01-04) */
		if (!strstr(TIFFGetVersion(), " 3.8.0") &&
			TIFFGetField(tif, TIFFTAG_ICCPROFILE, &size, &data) &&
			(size < INT_MAX) &&
			/* If profile is needed right now, for CMYK->RGB */
			!((pmetric == PHOTOMETRIC_SEPARATED) && !argb &&
				init_cmyk2rgb(settings, data, size, FALSE)) &&
			(settings->icc = malloc(size)))
		{
			settings->icc_size = size;
			memcpy(settings->icc, data, size);
		}
	}
#endif
#endif

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
			ls_progress(settings, height - i, 10);
		}

		_TIFFfree(raster);
		raster = NULL;

/* !!! Now it would be good to read in alpha ourselves - but not yet... */

		res = 1;
	}

	/* Read & interpret it ourselves */
	else
	{
		unsigned char xtable[256], *src, *tbuf = NULL;
		int xstep = tw ? tw : width, ystep = th ? th : rps;
		int aalpha, tsz = 0, wbpp = bpp;
		int bpr, bits1, bit0, db, n, nx;
		int j, k, x0, y0, bsz, plane, nplanes;


		if (pmetric == PHOTOMETRIC_SEPARATED) // Needs temp buffer
			tsz = xstep * ystep * (wbpp = 4);
		nplanes = planar ? wbpp + !!settings->img[CHN_ALPHA] : 1;

		bsz = (tw ? TIFFTileSize(tif) : TIFFStripSize(tif)) + 1;
		bpr = tw ? TIFFTileRowSize(tif) : TIFFScanlineSize(tif);

		buf = _TIFFmalloc(bsz + tsz);
		res = FILE_MEM_ERROR;
		if (!buf) goto fail2;
		res = FILE_LIB_ERROR;
		if (tsz) tbuf = buf + bsz; // Temp buffer for CMYK->RGB

		/* Flag associated alpha */
		aalpha = settings->img[CHN_ALPHA] &&
			(pmetric != PHOTOMETRIC_PALETTE) &&
			(sampinfo[0] == EXTRASAMPLE_ASSOCALPHA);

		bits1 = bpsamp > 8 ? 8 : bpsamp;

		/* Setup greyscale palette */
		if ((bpp == 1) && (pmetric != PHOTOMETRIC_PALETTE))
		{
			/* Demultiplied values are 0..255 */
			j = aalpha ? 256 : 1 << bits1;
			settings->colors = j--;
			k = pmetric == PHOTOMETRIC_MINISBLACK ? 0 : j;
			mem_bw_pal(settings->pal, k, j ^ k);
		}

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

		/* Prepare to rescale what we've got */
		memset(xtable, 0, 256);
		set_xlate(xtable, bits1);

		/* Progress steps */
		nx = ((width + xstep - 1) / xstep) * nplanes * height;

		/* Read image tile by tile - considering strip a wide tile */
		for (n = y0 = 0; y0 < height; y0 += ystep)
		for (x0 = 0; x0 < width; x0 += xstep)
		for (plane = 0; plane < nplanes; plane++)
		{
			unsigned char *tmp, *tmpa;
			int i, j, k, x, y, w, h, dx, dxa, dy, dys;

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
				x = width - x0;
				w = x < xstep ? x : xstep;
				x -= w;
			}
			else
			{
				x = x0;
				w = x + xstep > width ? width - x : xstep;
			}
			if (mirror & 2) /* Y mirror */
			{
				y = height - y0;
				h = y < ystep ? y : ystep;
				y -= h;
			}
			else
			{
				y = y0;
				h = y + ystep > height ? height - y : ystep;
			}

			/* Prepare pointers */
			dx = dxa = 1; dy = width;
			i = y * width + x;
			tmp = tmpa = settings->img[CHN_ALPHA] + i;
			if (plane >= wbpp); // Alpha
			else if (tbuf) // CMYK
			{
				dx = 4; dy = w;
				tmp = tbuf + plane;
			}
			else // RGB/indexed
			{
				dx = bpp;
				tmp = settings->img[CHN_IMAGE] + plane + i * bpp;
			}
			dy *= dx; dys = bpr;
			src = buf;
			/* Account for horizontal mirroring */
			if (mirror & 1)
			{
				// Write bytes backward
				tmp += (w - 1) * dx; tmpa += w - 1;
				dx = -dx; dxa = -1;
			}
			/* Account for vertical mirroring */
			if (mirror & 2)
			{
				// Read rows backward
				src += (h - 1) * dys;
				dys = -dys;
			}

			/* Decode it */
			for (j = 0; j < h; j++ , n++ , src += dys , tmp += dy)
			{
				if (pr && ((n * 10) % nx >= nx - 10))
					progress_update((float)n / nx);

				stream_MSB(src, tmp, w, bits1, bit0, db, dx);
				if (planar) continue;
				for (k = 1; k < wbpp; k++)
				{
					stream_MSB(src, tmp + k, w, bits1,
						bit0 + bpsamp * k, db, dx);
				}
				if (settings->img[CHN_ALPHA])
				{
					stream_MSB(src, tmpa, w, bits1,
						bit0 + bpsamp * wbpp, db, dxa);
					tmpa += width;
				}
			}

			/* Convert CMYK to RGB if needed */
			if (!tbuf || (planar && (plane != 3))) continue;
			if (bits1 < 8)	// Rescale to 8-bit
				do_xlate(xtable, tbuf, w * h * 4);
			cmyk2rgb(tbuf, tbuf, w * h, FALSE, settings);
			src = tbuf;
			tmp = settings->img[CHN_IMAGE] + (y * width + x) * 3;
			w *= 3;
			for (i = 0; i < h; i++ , tmp += width * 3 , src += w)
				memcpy(tmp, src, w);
		}
		done_cmyk2rgb(settings);

		j = width * height;
		tmp = settings->img[CHN_IMAGE];
		src = settings->img[CHN_ALPHA];

		/* Unassociate alpha */
		if (aalpha)
		{
			if (wbpp > 3) // Converted from CMYK
			{
				unsigned char *img = tmp;
				int i, k, a;

				if (bits1 < 8) do_xlate(xtable, src, j);
				bits1 = 8; // No further rescaling needed

				/* Remove white background */
				for (i = 0; i < j; i++ , img += 3)
				{
					a = src[i] - 255;
					k = a + img[0];
					img[0] = k < 0 ? 0 : k;
					k = a + img[1];
					img[1] = k < 0 ? 0 : k;
					k = a + img[2];
					img[2] = k < 0 ? 0 : k;
				}
			}
			mem_demultiply(tmp, src, j, bpp);
			tmp = NULL; // Image is done
		}

		if (bits1 < 8)
		{
			/* Rescale alpha */
			if (src) do_xlate(xtable, src, j);
			/* Rescale RGB */
			if (tmp && (wbpp == 3)) do_xlate(xtable, tmp, j * 3);
		}
		res = 1;
	}

fail2:	if (pr) progress_end();
	if (raster) _TIFFfree(raster);
	if (buf) _TIFFfree(buf);
	return (res);
}

static int load_tiff_frames(char *file_name, ani_settings *ani)
{
	TIFF *tif;
	ls_settings w_set;
	int res;


	/* We don't want any echoing to the output */
	TIFFSetErrorHandler(NULL);
	TIFFSetWarningHandler(NULL);

	if (!(tif = TIFFOpen(file_name, "r"))) return (-1);

	while (TRUE)
	{
		res = FILE_TOO_LONG;
		if (!check_next_frame(&ani->fset, ani->settings.mode, FALSE))
			goto fail;
		w_set = ani->settings;
		res = load_tiff_frame(tif, &w_set);
		if (res != 1) goto fail;
		res = process_page_frame(file_name, ani, &w_set);
		if (res) goto fail;
		/* Try to get next frame */
		if (!TIFFReadDirectory(tif)) break;
	}
	res = 1;
fail:	TIFFClose(tif);
	return (res);
}

static int load_tiff(char *file_name, ls_settings *settings)
{
	TIFF *tif;
	int res;


	/* We don't want any echoing to the output */
	TIFFSetErrorHandler(NULL);
	TIFFSetWarningHandler(NULL);

	if (!(tif = TIFFOpen(file_name, "r"))) return (-1);
	res = load_tiff_frame(tif, settings);
	if ((res == 1) && TIFFReadDirectory(tif)) res = FILE_HAS_FRAMES;
	TIFFClose(tif);
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
		ls_progress(settings, i, 20);
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
	guint32 masks[4];
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
				j = bitcount(masks[i]);
				/* Bit offset - add in bits _before_ mask */
				k = bitcount(masks[i] - 1) + 1;
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

	/* Load palette if needed */
	if (bpp < 16)
	{
		unsigned char tbuf[1024];

		j = 0;
		if (l >= BMP_COLORS + 4) j = GET32(hdr + BMP_COLORS);
		if (!j) j = 1 << bpp;
		k = GET32(hdr + BMP_DATAOFS) - l;
		k /= l < BMP3_HSIZE ? 3 : 4;
		if (k < j) j = k;
		if (!j || (j > 256)) goto fail; /* Wrong palette size */
		settings->colors = j;
		mfseek(mf, l, SEEK_SET);
		k = l < BMP3_HSIZE ? 3 : 4;
		i = mfread(tbuf, 1, j * k, mf);
		if (i < j * k) goto fail; /* Cannot read palette */
		tmp = tbuf;
		for (i = 0; i < j; i++)
		{
			settings->pal[i].red = tmp[2];
			settings->pal[i].green = tmp[1];
			settings->pal[i].blue = tmp[0];
			tmp += k;
		}
		/* If palette is all we need */
		res = 1;
		if ((settings->mode == FS_PALETTE_LOAD) ||
			(settings->mode == FS_PALETTE_DEF)) goto fail;
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
	/* To accommodate bitparser's extra step */
	buf = malloc(bl + 1);
	res = FILE_MEM_ERROR;
	if (!buf) goto fail;
	if ((res = allocate_image(settings, cmask))) goto fail2;

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
			ls_progress(settings, n, 10);
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
				ls_progress(settings, h - i - 1, 10);
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
		/* Delete all-zero "alpha" */
		if (is_filled(settings->img[CHN_ALPHA], 0,
			settings->width * settings->height))
			deallocate_image(settings, CMASK_FOR(CHN_ALPHA));
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
	unsigned char *buf, *tmp;
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
	if (((settings->mode == FS_CLIPBOARD) || (bpp == 3)) &&
		settings->img[CHN_ALPHA]) bpp = 4;
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
		prepare_row(buf, settings, bpp, i);
		mfwrite(buf, 1, ll, mf);
		ls_progress(settings, h - i, 20);
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
#define WHITESPACE "\t\n\v\f\r "

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

	/* Init hash */
	memset(&cuckoo, 0, sizeof(cuckoo));
	cuckoo.keys = ckeys;
	cuckoo.step = 32;
	cuckoo.cpp = cpp;
	cuckoo.seed = HASHSEED;

	/* Read colormap */
// !!! When/if huge numbers of colors get allowed, will need a progressbar here
	dest = pal;
	sprintf(tstr, " \"%%n%%*%dc %%n", cpp);
	for (i = 0; i < cols; i++ , dest += 3)
	{
		if (!fgetsC(lbuf, 4096, fp)) goto fail;

		/* Parse color ID */
		k = 0; sscanf(lbuf, tstr, &k, &l);
		if (!k) goto fail;

		/* Insert color into hash */
		ch_insert(&cuckoo, lbuf + k);

		/* Parse color definitions */
		if (!(r = strchr(lbuf + l, '"'))) goto fail;
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
				if (k < 0) goto fail;
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
		if (!r2) goto fail; /* Key w/o name */

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
		if (j >= XPM_COL_DEFS) goto fail;
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
		/* If palette is all we need */
		res = 1;
		if ((settings->mode == FS_PALETTE_LOAD) ||
			(settings->mode == FS_PALETTE_DEF)) goto fail;
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

	/* Allocate row buffer and image */
	i = w * cpp + 4 + 1024;
	if (i > lsz) buf = malloc(lsz = i);
	res = FILE_MEM_ERROR;
	if (!buf) goto fail;
	if ((res = allocate_image(settings, CMASK_IMAGE))) goto fail2;

	if (!settings->silent) ls_init("XPM", 0);

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
		ls_progress(settings, i, 10);
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

/* Extract valid C identifier from filename */
static char *extract_ident(char *fname, int *len)
{
	char *tmp;
	int l;

	tmp = strrchr(fname, DIR_SEP);
	tmp = tmp ? tmp + 1 : fname;
	for (; *tmp && !ISALPHA(*tmp); tmp++);
	for (l = 0; (l < 256) && ISALNUM(tmp[l]); l++);
	*len = l;
	return (tmp);
}

static int save_xpm(char *file_name, ls_settings *settings)
{
	unsigned char rgbmem[XPM_MAXCOL * 4], *src;
	const char *ctb;
	char ws[3], *buf, *tmp;
	str_hash cuckoo;
	FILE *fp;
	int bpp = settings->bpp, w = settings->width, h = settings->height;
	int i, j, k, l, cpp, cols, trans = -1;


	tmp = extract_ident(file_name, &l);
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
		ls_progress(settings, i, 10);
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
	set_bw(settings);

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
		ls_progress(settings, i, 10);
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
			ls_progress(settings, i, 10);
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
	/* If palette is all we need */
	res = 1;
	if ((settings->mode == FS_PALETTE_LOAD) ||
		(settings->mode == FS_PALETTE_DEF)) goto fail;

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
		ls_progress(settings, i, 10);
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
		int pbpp, i, j, k, l, tmp, mask;
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
		l = j * pbpp;

		/* Read the palette */
		fseek(fp, iofs, SEEK_SET);
		if (fread(pal + k * pbpp, 1, l, fp) != l) goto fail;
		iofs += l;

		/* Store the palette */
		settings->colors = j + k;
		memset(settings->pal, 0, 256 * 3);
		if (pbpp == 2) set_xlate(xlat5, 5);
		pptr = settings->pal + k;
		for (i = 0; i < l; i += pbpp , pptr++)
		{
			switch (pbpp)
			{
			case 1: /* 8-bit greyscale */
				pptr->red = pptr->green = pptr->blue = pal[i];
				break;
			case 2: /* 5:5:5 BGR */
				pptr->blue = xlat5[pal[i] & 0x1F];
				pptr->green = xlat5[(((pal[i + 1] << 8) +
					pal[i]) >> 5) & 0x1F];
				pptr->red = xlat5[(pal[i + 1] >> 2) & 0x1F];
				break;
			case 3: case 4: /* 8:8:8 BGR */
				pptr->blue = pal[i + 0];
				pptr->green = pal[i + 1];
				pptr->red = pal[i + 2];
				break;
			}
		}
		/* If palette is all we need */
		res = 1;
		if ((settings->mode == FS_PALETTE_LOAD) ||
			(settings->mode == FS_PALETTE_DEF)) goto fail;

		/* Cannot have transparent color at all? */
		if ((j <= 1) || ((ptype != 15) && (ptype != 32))) break;

		/* Test if there are different alphas */
		mask = ptype == 15 ? 0x80 : 0xFF;
		tmp = pal[pbpp - 1] & mask;
		for (i = 2; (i <= j) && ((pal[i * pbpp - 1] & mask) == tmp); i++);
		if (i > j) break;
		/* For 15 bpp, assume the less frequent value is transparent */
		tmp = 0;
		if (ptype == 15)
		{
			for (i = 0; i < j; i++) tmp += pal[i + i + 1] & mask;
			if (tmp >> 7 < j >> 1) tmp = 0x80; /* Transparent if set */
		}
		/* Search for first transparent color */
		for (i = 1; (i <= j) && ((pal[i * pbpp - 1] & mask) != tmp); i++);
		if (i > j) break; /* If 32-bit and not one alpha is zero */
		settings->xpm_trans = i + k - 1;
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
		mem_bw_pal(settings->pal, 0, settings->colors - 1);
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
	if (!buf) goto fail;
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
		ls_progress(settings, y, 10);
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
		prepare_row(buf, settings, bpp, i); /* Fill uncompressed row */
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
		ls_progress(settings, pcn, 20);
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

/* PCX header */
#define PCX_ID        0 /*  8b */
#define PCX_VER       1 /*  8b */
#define PCX_ENC       2 /*  8b */
#define PCX_BPP       3 /*  8b */
#define PCX_X0        4 /* 16b */
#define PCX_Y0        6 /* 16b */
#define PCX_X1        8 /* 16b */
#define PCX_Y1       10 /* 16b */
#define PCX_HDPI     12 /* 16b */
#define PCX_VDPI     14 /* 16b */
#define PCX_PAL      16 /* 8b*3*16 */
#define PCX_NPLANES  65 /*  8b */
#define PCX_LINELEN  66 /* 16b */
#define PCX_PALTYPE  68 /* 16b */
#define PCX_HRES     70 /* 16b */
#define PCX_VRES     72 /* 16b */
#define PCX_HSIZE   128

#define PCX_BUFSIZE 16384 /* Bytes read at a time */

/* Default EGA/VGA palette */
static const png_color def_pal[16] = {
{0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
{0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
{0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
{0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
};

static void copy_rgb_pal(png_color *dest, unsigned char *src, int cnt)
{
	while (cnt-- > 0)
	{
		dest->red = src[0];
		dest->green = src[1];
		dest->blue = src[2];
		dest++; src += 3;
	}
}

static int load_pcx(char *file_name, ls_settings *settings)
{
	static const unsigned char planarconfig[8] = {
		0x11, /* BW */  0x12, /* 4c */  0x31, /* 8c */
		0x41, /* 16c */ 0x14, /* 16c */ 0x18, /* 256c */
		0x38, /* RGB */	0x48  /* RGBA */ };
	unsigned char hdr[PCX_HSIZE], pbuf[769];
	unsigned char *buf, *row, *dest, *tmp;
	FILE *fp;
	int ver, bits, planes, ftype;
	int y, ccnt, bstart, bstop, strl, plane;
	int w, h, cols, buflen, bpp = 3, res = -1;


	if (!(fp = fopen(file_name, "rb"))) return (-1);

	/* Read the header */
	if (fread(hdr, 1, PCX_HSIZE, fp) < PCX_HSIZE) goto fail;

	/* PCX has no real signature - so check fields one by one */
	if ((hdr[PCX_ID] != 10) || (hdr[PCX_ENC] != 1)) goto fail;
	ver = hdr[PCX_VER];
	if (ver > 5) goto fail;

	bits = hdr[PCX_BPP];
	planes = hdr[PCX_NPLANES];
	if ((bits | planes) > 15) goto fail;
	if (!(tmp = memchr(planarconfig, (planes << 4) | bits, 8))) goto fail;
	ftype = tmp - planarconfig;

	/* Prepare palette */
	if (ftype < 6)
	{
		bpp = 1;
		settings->colors = cols = 1 << (bits * planes);
		/* BW (0 is black) */
		if (cols == 2)
		{
			settings->pal[0] = def_pal[0];
			settings->pal[1] = def_pal[15];
		}
		/* Default 256-color palette - assumed greyscale */
		else if ((ver == 3) && (cols == 256)) set_gray(settings);
		/* Default 16-color palette */
		else if ((ver == 3) && (cols == 16))
			memcpy(settings->pal, def_pal, sizeof(def_pal));
	/* !!! CGA palette is evil: what the PCX spec describes is the way it
	 * was handled by PC Paintbrush 3.0, while 4.0 was using an entirely
	 * different, undocumented encoding for palette selection.
	 * The only seemingly sane way to differentiate the two is to look at
	 * paletteinfo field: zeroed in 3.0, set in 4.0+ - WJ */
		else if (cols == 4)
		{
			/* Bits 2:1:0 in index: color burst:palette:intensity */
			static const unsigned char cga_pals[8 * 3] = {
				2, 4, 6,  10, 12, 14,
				3, 5, 7,  11, 13, 15,
				3, 4, 7,  11, 12, 15,
				3, 4, 7,  11, 12, 15 };
			int i, idx = hdr[PCX_PAL + 3] >> 5; // PB 3.0

			if (GET16(hdr + PCX_PALTYPE)) // PB 4.0
			{
				/* Pick green palette if G>B in slot 1 */
				i = hdr[PCX_PAL + 5] >= hdr[PCX_PAL + 4];
				/* Pick bright palette if max(G,B) > 200 */
				idx = i * 2 + (hdr[PCX_PAL + 4 + i] > 200);
			}

			settings->pal[0] = def_pal[hdr[PCX_PAL] >> 4];
			for (i = 1 , idx *= 3; i < 4; i++)
				settings->pal[i] = def_pal[cga_pals[idx++]];
		}
		/* VGA palette - read from file */
		else if (cols == 256)
		{
			if ((fseek(fp, -769, SEEK_END) < 0) ||
				(fread(pbuf, 1, 769, fp) < 769) ||
				(pbuf[0] != 0x0C)) goto fail;
			copy_rgb_pal(settings->pal, pbuf + 1, 256);
		}
		/* 8 or 16 colors - read from header */
		else copy_rgb_pal(settings->pal, hdr + PCX_PAL, cols);

		/* If palette is all we need */
		res = 1;
		if ((settings->mode == FS_PALETTE_LOAD) ||
			(settings->mode == FS_PALETTE_DEF)) goto fail;
	}

	/* Allocate buffer and image */
	settings->width = w = GET16(hdr + PCX_X1) - GET16(hdr + PCX_X0) + 1;
	settings->height = h = GET16(hdr + PCX_Y1) - GET16(hdr + PCX_Y0) + 1;
	settings->bpp = bpp;
	buflen = GET16(hdr + PCX_LINELEN);
	res = -1;
	if (buflen < ((w * bits + 7) >> 3)) goto fail;
	/* To accommodate bitparser's extra step */
	buf = malloc(PCX_BUFSIZE + buflen + 1);
	res = FILE_MEM_ERROR;
	if (!buf) goto fail;
	row = buf + PCX_BUFSIZE;
	if ((res = allocate_image(settings, ftype > 6 ? CMASK_RGBA : CMASK_IMAGE)))
		goto fail2;

	/* Read and decode the file */
	if (!settings->silent) ls_init("PCX", 0);
	res = FILE_LIB_ERROR;
	fseek(fp, PCX_HSIZE, SEEK_SET);
	dest = settings->img[CHN_IMAGE];
	if (bits == 1) memset(dest, 0, w * h); // Write will be by OR
	y = plane = ccnt = 0;
	bstart = bstop = PCX_BUFSIZE;
	strl = buflen;
	while (TRUE)
	{
		unsigned char v;

		/* Keep the buffer filled */
		if (bstart >= bstop)
		{
			bstart -= bstop;
			bstop = fread(buf, 1, PCX_BUFSIZE, fp);
			if (bstop <= bstart) goto fail3; /* Truncated file */
		}

		/* Decode data */
		v = buf[bstart];
		if (ccnt) /* Middle of a run */
		{
			int l = strl < ccnt ? strl : ccnt;
			memset(row + buflen - strl, v, l);
			strl -= l; ccnt -= l;
		}
		else if (v >= 0xC0) /* Start of a run */
		{
			ccnt = v & 0x3F;
			bstart++;
		}
		else row[buflen - strl--] = v;
		bstart += !ccnt;
		if (strl) continue;

		/* Store a line */
		if (bits == 1) // N planes of 1-bit data (MSB first)
		{
			unsigned char uninit_(v), *tmp = row;
			int i, n = 7 - plane;

			for (i = 0; i < w; i++ , v += v)
			{
				if (!(i & 7)) v = *tmp++;
				dest[i] |= (v & 0x80) >> n;
			}
		}
		else if (plane < 3) // BPP planes of 2/4/8-bit data (MSB first)
			stream_MSB(row, dest + plane, w, bits, 0, bits, bpp);
		else if (settings->img[CHN_ALPHA]) // 8-bit alpha plane
			memcpy(settings->img[CHN_ALPHA] + y * w, row, w);

		if (++plane >= planes)
		{
			ls_progress(settings, y, 10);
			if (++y >= h) break;
			dest += w * bpp;
			plane = 0;
		}
		strl = buflen;
	}
	res = 1;

fail3:	if (!settings->silent) progress_end();
fail2:	free(buf);
fail:	fclose(fp);
	return (res);
}

static int save_pcx(char *file_name, ls_settings *settings)
{
	unsigned char *buf, *src, *dest;
	FILE *fp;
	int w = settings->width, h = settings->height, bpp = settings->bpp;
	int i, l, plane, cnt;


	/* Allocate buffer */
	i = w * 2; // Buffer one plane, with worst-case RLE expansion factor 2
	if (i < PCX_HSIZE) i = PCX_HSIZE;
	if (i < 769) i = 769; // For palette
	buf = calloc(1, i); // Zeroing out is for header
	if (!buf) return (-1);
	
	if (!(fp = fopen(file_name, "wb")))
	{
		free(buf);
		return (-1);
	}

	/* Prepare header */
	memcpy(buf, "\x0A\x05\x01\x08", 4); // Version 5 PCX, 8 bits/plane
	PUT16(buf + PCX_X1, w - 1);
	PUT16(buf + PCX_Y1, h - 1);
	PUT16(buf + PCX_HDPI, 300); // GIMP sets DPI to this value
	PUT16(buf + PCX_VDPI, 300);
	buf[PCX_NPLANES] = bpp;
	PUT16(buf + PCX_LINELEN, w);
	buf[PCX_PALTYPE] = 1;
	fwrite(buf, 1, PCX_HSIZE, fp);

	/* Compress & write pixel rows */
	if (!settings->silent) ls_init("PCX", 1);
	src = settings->img[CHN_IMAGE];
	for (i = 0; i < h; i++ , src += w * bpp)
	{
		for (plane = 0; plane < bpp; plane++)
		{
			unsigned char v, *tmp = src + plane;

			dest = buf; cnt = 0; l = w;
			while (l > 0)
			{
				v = *tmp; tmp += bpp; cnt++;
				if ((--l <= 0) || (cnt == 0x3F) || (v != *tmp))
				{
					if ((cnt > 1) || (v >= 0xC0))
						*dest++ = cnt | 0xC0;
					*dest++ = v; cnt = 0;
				}
			}
			fwrite(buf, 1, dest - buf, fp);
		}
		ls_progress(settings, i, 20);
	}

	/* Write palette */
	if (bpp == 1)
	{
		png_color *col = settings->pal;

		memset(dest = buf + 1, 0, 768);
		buf[0] = 0x0C;
		for (i = 0; i < settings->colors; i++ , dest += 3 , col++)
		{
			dest[0] = col->red;
			dest[1] = col->green;
			dest[2] = col->blue;
		}
		fwrite(buf, 1, 769, fp);
	}

	fclose(fp);

	if (!settings->silent) progress_end();

	free(buf);
	return (0);
}

typedef void (*cvt_func)(unsigned char *dest, unsigned char *src, int len,
	int bpp, int step, int maxval);

static void convert_16b(unsigned char *dest, unsigned char *src, int len,
	int bpp, int step, int maxval)
{
	int i, v, m = maxval * 2;

	if (!(step -= bpp)) bpp *= len , len = 1;
	step *= 2;
	while (len-- > 0)
	{
		i = bpp;
		while (i--)
		{
			v = (src[0] << 8) + src[1];
			src += 2;
			*dest++ = (v * (255 * 2) + maxval) / m;
		}
		src += step;
	}	
}

static void copy_bytes(unsigned char *dest, unsigned char *src, int len,
	int bpp, int step, int maxval)
{
	int i;

	if (!(step -= bpp)) bpp *= len , len = 1;
	while (len-- > 0)
	{
		i = bpp;
		while (i--) *dest++ = *src++;
		src += step;
	}	
}

static void extend_bytes(unsigned char *dest, int len, int maxval)
{
	unsigned char tb[256];
	int i, j, m = maxval * 2;

	memset(tb, 255, 256);
	for (i = 0 , j = maxval; i <= maxval; i++ , j += 255 * 2)
		tb[i] = j / m;

	for (j = 0; j < len; j++ , dest++) *dest = tb[*dest];
}

static int check_next_pnm(FILE *fp, char id)
{
	char buf[2];

	if (fread(buf, 2, 1, fp))
	{
		fseek(fp, -2, SEEK_CUR);
		if ((buf[0] == 'P') && (buf[1] == id)) return (FILE_HAS_FRAMES);
	}
	return (1);
}

/* PAM loader does not support nonstandard types "GRAYSCALEFP" and "RGBFP",
 * because handling format variations which aren't found in the wild
 * is a waste of code - WJ */

static int load_pam_frame(FILE *fp, ls_settings *settings)
{
	static const char *typenames[] = {
		"BLACKANDWHITE", "BLACKANDWHITE_ALPHA",
		"GRAYSCALE", "GRAYSCALE_ALPHA",
		"RGB", "RGB_ALPHA",
		"CMYK", "CMYK_ALPHA" };
	static const char depths[] = { 1, 2, 1, 2, 3, 4, 4, 5 };
	cvt_func cvt_stream;
	char wbuf[512], *t1, *t2, *tail;
	unsigned char *dest, *buf = NULL;
	int maxval = 0, w = 0, h = 0, depth = 0, ftype = -1;
	int i, j, l, ll, bpp, trans, vl, res;


	/* Read header */
	if (!fgets(wbuf, sizeof(wbuf), fp) || strncmp(wbuf, "P7", 2))
		return (-1);
	while (TRUE)
	{
		if (!fgets(wbuf, sizeof(wbuf), fp)) return (-1);
		if (!wbuf[0] || (wbuf[0] == '#')) continue; // Empty line or comment
		t2 = NULL;
		t1 = wbuf + strspn(wbuf, WHITESPACE);
		l = strcspn(t1, WHITESPACE);
		if (t1[l])
		{
			t2 = t1 + l + strspn(t1 + l, WHITESPACE);
			t2[strcspn(t2, WHITESPACE)] = '\0';
		}
		t1[l] = '\0';
		if (!strcmp(t1, "ENDHDR")) break;
		if (!strcmp(t1, "TUPLTYPE"))
		{
			if (!t2) continue;
			for (i = 1; typenames[i]; i++)
			{
				if (strcmp(t2, typenames[i])) continue;
				ftype = i;
				break;
			}	
			continue;
		}

		if (!t2) return (-1); // Failure - other fields are numeric
		i = strtol(t2, &tail, 10);
		if (*tail) return (-1);

		if (!strcmp(t1, "HEIGHT")) h = i;
		else if (!strcmp(t1, "WIDTH")) w = i;
		else if (!strcmp(t1, "DEPTH")) depth = i;
		else if (!strcmp(t1, "MAXVAL")) maxval = i;
		else return (-1); // Unknown IDs not allowed
	}
	/* Interpret unknown content as RGB or grayscale */
	if (ftype < 0) ftype = depth >= 3 ? 4 : 2;

	/* Validate */
	if ((depth < depths[ftype]) || (depth > 16) ||
		(maxval < 1) || (maxval > 65535)) return (-1);
	bpp = ftype < 4 ? 1 : 3;
	trans = ftype & 1;
	vl = maxval < 256 ? 1 : 2;
	ll = w * depth * vl;
	if (ftype < 2) // BW
	{
		set_bw(settings);
		if (maxval > 1) return (-1);
	}
	else if (bpp == 1) set_gray(settings); // Grayscale

	/* Allocate row buffer if cannot read directly into image */
	if (trans || (vl > 1) || (bpp != depth))
	{
		buf = malloc(ll);
		if (!buf) return (FILE_MEM_ERROR);
	}

	/* Allocate image */
	settings->width = w;
	settings->height = h;
	settings->bpp = bpp;
	res = allocate_image(settings, trans ? CMASK_RGBA : CMASK_IMAGE);
	if (res) goto fail;

	/* Read the image */
	if (!settings->silent) ls_init("PAM", 0);
	res = FILE_LIB_ERROR;
	cvt_stream = vl > 1 ? convert_16b : copy_bytes;
	for (i = 0; i < h; i++)
	{
		dest = buf ? buf : settings->img[CHN_IMAGE] + ll * i;
		j = fread(dest, 1, ll, fp);
		if (j < ll) goto fail2;
		ls_progress(settings, i, 10);

		if (!buf) continue; // Nothing else to do here
		if (settings->img[CHN_ALPHA]) // Have alpha - parse it
		{
			cvt_stream(settings->img[CHN_ALPHA] + w * i,
				buf + depths[ftype] * vl - vl, w, 1, depth, maxval);
		}
		dest = settings->img[CHN_IMAGE] + w * bpp * i;
		if (ftype >= 6) // CMYK
		{
			cvt_stream(buf, buf, w, 4, depth, maxval);
			if (maxval < 255) extend_bytes(buf, w * 4, maxval);
			cmyk2rgb(dest, buf, w, FALSE, settings);
		}
		else cvt_stream(dest, buf, w, bpp, depth, maxval);
	}

	/* Check for next frame */
	res = check_next_pnm(fp, '7');

fail2:	if (maxval < 255) // Extend what we've read
	{
		j = w * h;
		if (settings->img[CHN_ALPHA])
			extend_bytes(settings->img[CHN_ALPHA], j, maxval);
		j *= bpp;
		dest = settings->img[CHN_IMAGE];
		if (ftype >= 6); // CMYK is done already
		else if (ftype > 1) extend_bytes(dest, j, maxval);
		else // Convert BW from 1-is-white to 1-is-black
		{
			for (i = 0; i < j; i++ , dest++) *dest = !*dest;
		}
	}
	if (!settings->silent) progress_end();

fail:	free(buf);
	return (res);
}

#define PNM_BUFSIZE 4096
typedef struct {
	FILE *f;
	int ptr, end, eof, comment;
	char buf[PNM_BUFSIZE + 2];
} pnmbuf;

/* What PBM documentation says is NOT what Netpbm actually does; skipping a
 * comment in file header, it does not consume the newline after it - WJ */
static void pnm_skip_comment(pnmbuf *pnm)
{
	pnm->comment = !pnm->buf[pnm->ptr += strcspn(pnm->buf + pnm->ptr, "\r\n")];
}

static char *pnm_gets(pnmbuf *pnm, int data)
{
	int k, l;

	while (TRUE)
	{
		while (pnm->ptr < pnm->end)
		{
			l = pnm->ptr + strspn(pnm->buf + pnm->ptr, WHITESPACE);
			if (pnm->buf[l] == '#')
			{
				if (data) return (NULL);
				pnm->ptr = l;
				pnm_skip_comment(pnm);
				continue;
			}
			k = l + strcspn(pnm->buf + l, WHITESPACE "#");
			if (pnm->buf[k] || pnm->eof)
			{
				pnm->ptr = k + 1;
				if (pnm->buf[k] == '#')
				{
					if (data) return (NULL);
					pnm_skip_comment(pnm);
				}
				pnm->buf[k] = '\0';
				return (pnm->buf + l);
			}
			memmove(pnm->buf, pnm->buf + l, pnm->end -= l);
			pnm->ptr = 0;
			break;
		}
		if (pnm->eof) return (NULL);
		if (pnm->ptr >= pnm->end) pnm->ptr = pnm->end = 0;
		l = PNM_BUFSIZE - pnm->end;
		if (l <= 0) return (NULL); // A "token" of 4096 chars means failure
		pnm->end += k = fread(pnm->buf + pnm->end, 1, l, pnm->f);
		pnm->eof = k < l;
		if (pnm->comment) pnm_skip_comment(pnm);
	}
}

static int pnm_endhdr(pnmbuf *pnm, int plain)
{
	while (pnm->comment)
	{
		pnm_skip_comment(pnm);
		if (!pnm->comment) break;
		if (pnm->eof) return (FALSE);
		pnm->end = fread(pnm->buf, 1, PNM_BUFSIZE, pnm->f);
		pnm->eof = pnm->end < PNM_BUFSIZE;
	}
	/* Last whitespace in header already got consumed while parsing */

	/* Buffer will remain in use in plain mode */
	if (!plain && (pnm->ptr < pnm->end))
		fseek(pnm->f, pnm->ptr - pnm->end, SEEK_CUR);
	return (TRUE);
}

static int load_pnm_frame(FILE *fp, ls_settings *settings)
{
	pnmbuf pnm;
	char *s, *tail;
	unsigned char *dest;
	int i, l, m, w, h, bpp, maxval, plain, mode, fid, res;


	/* Identify*/
	memset(&pnm, 0, sizeof(pnm));
	pnm.f = fp;
	fid = settings->ftype == FT_PBM ? 0 : settings->ftype == FT_PGM ? 1 : 2;
	if (!(s = pnm_gets(&pnm, FALSE))) return (-1);
	if ((s[0] != 'P') || ((s[1] != fid + '1') && (s[1] != fid + '4')))
		 return (-1);
	plain = s[1] < '4';

	/* Read header */
	if (!(s = pnm_gets(&pnm, FALSE))) return (-1);
	w = strtol(s, &tail, 10);
	if (*tail) return (-1);
	if (!(s = pnm_gets(&pnm, FALSE))) return (-1);
	h = strtol(s, &tail, 10);
	if (*tail) return (-1);
	bpp = maxval = 1;
	if (settings->ftype == FT_PBM) set_bw(settings);
	else
	{
		if (!(s = pnm_gets(&pnm, FALSE))) return (-1);
		maxval = strtol(s, &tail, 10);
		if (*tail) return (-1);
		if ((maxval <= 0) || (maxval > 65535)) return (-1);
		if (settings->ftype == FT_PGM) set_gray(settings);
		else bpp = 3;
	}
	if (!pnm_endhdr(&pnm, plain)) return (-1);

	/* Store values */
	settings->width = w;
	settings->height = h;
	settings->bpp = bpp;

	/* Allocate image */
	if ((res = allocate_image(settings, CMASK_IMAGE))) return (res);

	/* Now, read the image */
	mode = settings->ftype == FT_PBM ? plain /* 0 and 1 */ :
		plain ? 2 : maxval < 255 ? 3 : maxval > 255 ? 4 : 5;
	s = "";
	if (!settings->silent) ls_init("PNM", 0);
	res = FILE_LIB_ERROR;
	l = w * bpp;
	m = maxval * 2;
	for (i = 0; i < h; i++)
	{
		dest = settings->img[CHN_IMAGE] + l * i;
		switch (mode)
		{
		case 0: /* Raw packed bits */
		{
#if PNM_BUFSIZE * 8 < MAX_WIDTH
#error "Buffer too small to read PBM row all at once"
#endif
			int i, j, k;
			unsigned char *tp = pnm.buf;

			k = (w + 7) >> 3;
			j = fread(tp, 1, k, fp);
			for (i = 0; i < w; i++)
				*dest++ = (tp[i >> 3] >> (~i & 7)) & 1;
			if (j < k) goto fail2;
			break;
		}
		case 3: /* Raw byte values - extend later */
		case 5: /* Raw 0..255 values - trivial */
			if (fread(dest, 1, l, fp) < l) goto fail2;
			break;
		case 1: /* Chars "0" and "1" */
		{
			int i;
			unsigned char ch;

			for (i = 0; i < l; i++)
			{
				if (!s[0] && !(s = pnm_gets(&pnm, TRUE)))
					goto fail2;
				ch = *s++ - '0';
				if (ch > 1) goto fail2;
				*dest++ = ch;
			}
			break;
		}
		case 2: /* Integers in ASCII */
		{
			int i, n;

			for (i = 0; i < l; i++)
			{
				if (!(s = pnm_gets(&pnm, TRUE))) goto fail2;
				n = strtol(s, &tail, 10);
				if (*tail) goto fail2;
				if ((n < 0) || (n > maxval)) goto fail2;
				n = (n * (255 * 2) + maxval) / m;
				*dest++ = n;
			}
			break;
		}
		case 4: /* Raw ushorts in MSB order */
		{
			int i, j, k, ll;

			for (ll = l * 2; ll > 0; ll -= k)
			{
				k = PNM_BUFSIZE < ll ? PNM_BUFSIZE : ll;
				j = fread(pnm.buf, 1, k, fp);
				i = j >> 1;
				convert_16b(dest, pnm.buf, i, 1, 1, maxval);
				dest += i;
				if (j < k) goto fail2;
			}
			break;
		}
		}
		ls_progress(settings, i, 10);
	}
	res = 1;

	/* Check for next frame */
	if (!plain) res = check_next_pnm(fp, fid + '4');

fail2:	if (mode == 3) // Extend what we've read
		extend_bytes(settings->img[CHN_IMAGE], l * h, maxval);
	if (!settings->silent) progress_end();

	return (res);
}

static int load_pnm_frames(char *file_name, ani_settings *ani)
{
	FILE *fp;
	ls_settings w_set;
	int res, is_pam = ani->settings.ftype == FT_PAM, next = TRUE;


	if (!(fp = fopen(file_name, "rb"))) return (-1);
	while (next)
	{
		res = FILE_TOO_LONG;
		if (!check_next_frame(&ani->fset, ani->settings.mode, FALSE))
			goto fail;
		w_set = ani->settings;
		res = (is_pam ? load_pam_frame : load_pnm_frame)(fp, &w_set);
		next = res == FILE_HAS_FRAMES;
		if ((res != 1) && !next) goto fail;
		res = process_page_frame(file_name, ani, &w_set);
		if (res) goto fail;
	}
	res = 1;
fail:	fclose(fp);
	return (res);
}

static int load_pnm(char *file_name, ls_settings *settings)
{
	FILE *fp;
	int res;

	if (!(fp = fopen(file_name, "rb"))) return (-1);
	res = (settings->ftype == FT_PAM ? load_pam_frame :
		load_pnm_frame)(fp, settings);
	fclose(fp);
	return (res);
}

static int save_pbm(char *file_name, ls_settings *settings)
{
	unsigned char buf[MAX_WIDTH / 8], *src;
	FILE *fp;
	int i, j, l, w = settings->width, h = settings->height;


	if ((settings->bpp != 1) || (settings->colors > 2)) return WRONG_FORMAT;

	if (!(fp = fopen(file_name, "wb"))) return (-1);

	if (!settings->silent) ls_init("PBM", 1);
	fprintf(fp, "P4\n%d %d\n", w, h);

	/* Write rows */
	src = settings->img[CHN_IMAGE];
	l = (w + 7) >> 3;
	for (i = 0; i < h; i++)
	{
		memset(buf, 0, l);
		for (j = 0; j < w; j++)
			buf[j >> 3] |= (*src++ == 1) << (~j & 7);
		fwrite(buf, l, 1, fp);
		ls_progress(settings, i, 20);
	}
	fclose(fp);

	if (!settings->silent) progress_end();

	return (0);
}

static int save_ppm(char *file_name, ls_settings *settings)
{
	FILE *fp;
	int i, l, m, w = settings->width, h = settings->height;


	if (settings->bpp != 3) return WRONG_FORMAT;

	if (!(fp = fopen(file_name, "wb"))) return (-1);

	if (!settings->silent) ls_init("PPM", 1);
	fprintf(fp, "P6\n%d %d\n255\n", w, h);

	/* Write rows */
	m = (l = w * 3) * h;
	// Write entire file at once if no progressbar
	if (settings->silent) l = m;
	for (i = 0; m > 0; m -= l , i++)
	{
		fwrite(settings->img[CHN_IMAGE] + l * i, l, 1, fp);
		ls_progress(settings, i, 20);
	}
	fclose(fp);

	if (!settings->silent) progress_end();

	return (0);
}

static int save_pam(char *file_name, ls_settings *settings)
{
	unsigned char xv, xa, *dest, *src, *srca, *buf = NULL;
	FILE *fp;
	int ibpp = settings->bpp, w = settings->width, h = settings->height;
	int i, j, bpp;


	if ((ibpp != 3) && (settings->colors > 2)) return WRONG_FORMAT;

	bpp = ibpp + !!settings->img[CHN_ALPHA];
	/* For BW: image XOR 1, alpha AND 1 */
	xa = (xv = ibpp == 1) ? 1 : 255;
	if (bpp != 3) // BW needs inversion, and alpha, interlacing
	{
		buf = malloc(w * bpp);
		if (!buf) return (-1);
	}

	if (!(fp = fopen(file_name, "wb")))
	{
		free(buf);
		return (-1);
	}

	if (!settings->silent) ls_init("PAM", 1);
	fprintf(fp, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH %d\nMAXVAL %d\n"
		"TUPLTYPE %s%s\nENDHDR\n", w, h, bpp, ibpp == 1 ? 1 : 255,
		ibpp == 1 ? "BLACKANDWHITE" : "RGB", bpp > ibpp ? "_ALPHA" : "");

	for (i = 0; i < h; i++)
	{
		src = settings->img[CHN_IMAGE] + i * w * ibpp;
		if ((dest = buf))
		{
			srca = NULL;
			if (settings->img[CHN_ALPHA])
				srca = settings->img[CHN_ALPHA] + i * w;
			for (j = 0; j < w; j++)
			{
				*dest++ = *src++ ^ xv;
				if (ibpp > 1)
				{
					*dest++ = *src++;
					*dest++ = *src++;
				}
				if (srca) *dest++ = *srca++ & xa;
			}
			src = buf;
		}
		fwrite(src, 1, w * bpp, fp);
		ls_progress(settings, i, 20);
	}
	fclose(fp);

	if (!settings->silent) progress_end();
	free(buf);

	return (0);
}

/* Put screenshots and X pixmaps on an equal footing with regular files */

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11

#include <X11/Xlib.h>
#include <gdk/gdkx.h>

/* It's unclear who should free clipboard pixmaps and when, so I do the same
 * thing Qt does, destroying the next-to-last allocated pixmap each time a new
 * one is allocated - WJ */

static int save_pixmap(ls_settings *settings, memFILE *mf)
{
	static GdkPixmap *exported[2];
	unsigned char *src, *dest, *sel, *buf = NULL;
	int i, j, l, w = settings->width, h = settings->height;

	/* !!! Pixmap export used only for FS_CLIPBOARD, where the case of
	 * selection without alpha is already prevented */
	if ((settings->bpp == 1) || settings->img[CHN_ALPHA])
	{
		buf = malloc(w * 3);
		if (!buf) return (-1);
	}

	if (exported[0])
	{
		if (exported[1])
		{
			/* Someone might have destroyed the X pixmap already,
			 * so get ready to live through an X error */
			gdk_error_trap_push();
			gdk_pixmap_unref(exported[1]);
			gdk_error_trap_pop();
		}
		exported[1] = exported[0];
	}
	exported[0] = gdk_pixmap_new(main_window->window, w, h, -1);
	if (!exported[0])
	{
		free(buf);
		return (-1);
	}

	/* Plain RGB - copy it whole */
	if (!buf) gdk_draw_rgb_image(exported[0], main_window->style->black_gc,
		0, 0, w, h, GDK_RGB_DITHER_NONE, settings->img[CHN_IMAGE], w * 3);
	/* Something else - render & copy row by row */
	else
	{
		l = w * settings->bpp;
		for (i = 0; i < h; i++)
		{
			src = settings->img[CHN_IMAGE] + l * i;
			dest = buf;
			if (settings->bpp == 3) memcpy(dest, src, l);
			else /* Indexed to RGB */
			{
				png_color *pal = settings->pal;

				for (j = 0; j < w; j++ , dest += 3)
				{
					png_color *col = pal + *src++;
					dest[0] = col->red;
					dest[1] = col->green;
					dest[2] = col->blue;
				}
			}
			/* There is no way to send alpha to XPaint, so I use
			 * alpha (and selection if any) to blend image with
			 * white and send the result - WJ */
			if (settings->img[CHN_ALPHA])
			{
				src = settings->img[CHN_ALPHA] + w * i;
				sel = settings->img[CHN_SEL] ?
					settings->img[CHN_SEL] + w * i : NULL;
				dest = buf;
				for (j = 0; j < w; j++)
				{
					int ii, jj, k = *src++;

					if (sel)
					{
						k *= *sel++;
						k = (k + (k >> 8) + 1) >> 8;
					}
					for (ii = 0; ii < 3; ii++)
					{
						jj = 255 * 255 + (*dest - 255) * k;
						*dest++ = (jj + (jj >> 8) + 1) >> 8;
					}
				}
			}
			gdk_draw_rgb_image(exported[0], main_window->style->black_gc,
				0, i, w, 1, GDK_RGB_DITHER_NONE, buf, w * 3);
		}
	}
	free(buf);

	/* !!! '(void *)' is there to make GCC 4 shut up - WJ */
	*(Pixmap *)(void *)&mf->buf = GDK_WINDOW_XWINDOW(exported[0]);
	mf->top = sizeof(Pixmap);
	return (0);
}

#else /* Pixmap export fails by definition in absence of X */
#define save_pixmap(A,B) (-1)
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

/* Handle SVG import using gdk-pixbuf */

#if (GDK_PIXBUF_MAJOR > 2) || ((GDK_PIXBUF_MAJOR == 2) && (GDK_PIXBUF_MINOR >= 4))

#define MAY_HANDLE_SVG

static int svg_ftype = -1;

static int svg_supported()
{
	GSList *tmp, *ff;
	int i, res = FALSE;

	ff = gdk_pixbuf_get_formats();
	for (tmp = ff; tmp; tmp = tmp->next)
	{
		gchar **mime = gdk_pixbuf_format_get_mime_types(tmp->data);

		for (i = 0; mime[i]; i++)
		{
			res |= strstr(mime[i], "image/svg") == mime[i];
		}
		g_strfreev(mime);
		if (res) break;
	} 
	g_slist_free(ff);
	return (res);
}

static int load_svg(char *file_name, ls_settings *settings)
{
	GdkPixbuf *pbuf;
	GError *err = NULL;
	guchar *src;
	unsigned char *dest, *dsta;
	int i, j, w, h, bpp, cmask, skip, res = -1;


#if (GDK_PIXBUF_MAJOR == 2) && (GDK_PIXBUF_MINOR < 8)
	/* 2.4 can constrain size only while preserving aspect ratio;
	 * 2.6 can constrain size fully, but not partially */
	if (settings->req_w && settings->req_h)
		pbuf = gdk_pixbuf_new_from_file_at_scale(file_name,
			settings->req_w, settings->req_h, FALSE, &err);
	else pbuf = gdk_pixbuf_new_from_file(file_name, &err);
#else
	/* 2.8+ is full-featured */
	pbuf = gdk_pixbuf_new_from_file_at_scale(file_name,
		settings->req_w ? settings->req_w : -1,
		settings->req_h ? settings->req_h : -1,
		!(settings->req_w && settings->req_h), &err);
#endif
	if (!pbuf)
	{
		if ((err->domain == GDK_PIXBUF_ERROR) &&
			(err->code == GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY))
			res = FILE_MEM_ERROR;
		g_error_free(err);
		return (res);
	}
	/* Prevent images loading wrong in case gdk-pixbuf ever starts using
	 * something other than 8-bit RGB/RGBA without me noticing - WJ */
	if (gdk_pixbuf_get_bits_per_sample(pbuf) != 8) goto fail;

	bpp = gdk_pixbuf_get_n_channels(pbuf);
	if (bpp == 4) cmask = CMASK_RGBA;
	else if (bpp == 3) cmask = CMASK_IMAGE;
	else goto fail;
	settings->width = w = gdk_pixbuf_get_width(pbuf);
	settings->height = h = gdk_pixbuf_get_height(pbuf);
	settings->bpp = 3;
	if ((res = allocate_image(settings, cmask))) goto fail;

	skip = gdk_pixbuf_get_rowstride(pbuf) - w * bpp;
	src = gdk_pixbuf_get_pixels(pbuf);
	dest = settings->img[CHN_IMAGE];
	dsta = settings->img[CHN_ALPHA];
	for (i = 0; i < h; i++ , src += skip)
	for (j = 0; j < w; j++ , src += bpp , dest += 3)
	{
		dest[0] = src[0];
		dest[1] = src[1];
		dest[2] = src[2];
		if (dsta) *dsta++ = src[3];
	}
	res = 1;

fail:	g_object_unref(pbuf);
	return (res);
}

#endif

/* Handle textual palette file formats - GIMP's GPL and mtPaint's own TXT */

static void to_pal(png_color *c, int *rgb)
{
	c->red = rgb[0] < 0 ? 0 : rgb[0] > 255 ? 255 : rgb[0];
	c->green = rgb[1] < 0 ? 0 : rgb[1] > 255 ? 255 : rgb[1];
	c->blue = rgb[2] < 0 ? 0 : rgb[2] > 255 ? 255 : rgb[2];
}

static int load_txtpal(char *file_name, ls_settings *settings)
{
	char lbuf[4096];
	FILE *fp;
	png_color *c = settings->pal;
	int i, rgb[3], n = 0, res = -1;


	if (!(fp = fopen(file_name, "r"))) return (-1);
	if (!fgets(lbuf, 4096, fp)) goto fail;
	if (settings->ftype == FT_GPL)
	{
		if (strstr(lbuf, "GIMP Palette") != lbuf) goto fail;
		while (fgets(lbuf, 4096, fp) && (n < 256))
		{
			/* Just ignore invalid/unknown lines */
			if (sscanf(lbuf, "%d %d %d", rgb + 0, rgb + 1, rgb + 2) != 3)
				continue;
			to_pal(c++, rgb);
			n++;
		}
	}
	else
	{
		if (sscanf(lbuf, "%i", &n) != 1) goto fail;
		/* No further validation of anything at all */
		n = n < 2 ? 2 : n > 256 ? 256 : n;
		for (i = 0; i < n; i++)
		{
			fscanf(fp, "%i,%i,%i\n", rgb + 0, rgb + 1, rgb + 2);
			to_pal(c++, rgb);
		}
	}
	settings->colors = n;
	if (n > 0) res = 1;

fail:	fclose(fp);
	return (res);
}

static int save_txtpal(char *file_name, ls_settings *settings)		
{
	FILE *fp;
	char *tpl;
	png_color *cp;
	int i, l, n = settings->colors;

	if ((fp = fopen(file_name, "w")) == NULL) return (-1);

	if (settings->ftype == FT_GPL)	// .gpl file
	{
		tpl = extract_ident(file_name, &l);
		if (!l) tpl = "mtPaint" , l = strlen("mtPaint");
		fprintf(fp, "GIMP Palette\nName: %.*s\nColumns: 16\n#\n", l, tpl);
		tpl = "%3i %3i %3i\tUntitled\n";
	}
	else // .txt file
	{
		fprintf(fp, "%i\n", n);
		tpl = "%i,%i,%i\n";
	}

	cp = settings->pal;
	for (i = 0; i < n; i++ , cp++)
		fprintf(fp, tpl, cp->red, cp->green, cp->blue);

	fclose(fp);
	return (0);
}

static int save_image_x(char *file_name, ls_settings *settings, memFILE *mf)
{
	ls_settings setw = *settings; // Make a copy to safely modify
	png_color greypal[256];
	int res;

	/* Prepare to handle clipboard export */
	if (setw.mode != FS_CLIPBOARD); // not export
	else if (setw.ftype & FTM_EXTEND) setw.mode = FS_CLIP_FILE; // to mtPaint
	else if (setw.img[CHN_SEL] && !setw.img[CHN_ALPHA])
	{
		/* Pass clipboard mask as alpha if there is no alpha already */
		setw.img[CHN_ALPHA] = setw.img[CHN_SEL];
		setw.img[CHN_SEL] = NULL;
	}
	setw.ftype &= FTM_FTYPE;

	/* Provide a grayscale palette if needed */
	if ((setw.bpp == 1) && !setw.pal)
		mem_bw_pal(setw.pal = greypal, 0, 255);

	/* Validate transparent color (for now, forbid out-of-palette RGB
	 * transparency altogether) */
	if (setw.xpm_trans >= setw.colors)
		setw.xpm_trans = setw.rgb_trans = -1;

	switch (setw.ftype)
	{
	default:
	case FT_PNG: res = save_png(file_name, &setw, mf); break;
#ifdef U_JPEG
	case FT_JPEG: res = save_jpeg(file_name, &setw); break;
#endif
#ifdef HANDLE_JP2
	case FT_JP2:
	case FT_J2K: res = save_jpeg2000(file_name, &setw); break;
#endif
#ifdef U_TIFF
	case FT_TIFF: res = save_tiff(file_name, &setw); break;
#endif
#ifdef U_GIF
	case FT_GIF: res = save_gif(file_name, &setw); break;
#endif
	case FT_BMP: res = save_bmp(file_name, &setw, mf); break;
	case FT_XPM: res = save_xpm(file_name, &setw); break;
	case FT_XBM: res = save_xbm(file_name, &setw); break;
	case FT_LSS: res = save_lss(file_name, &setw); break;
	case FT_TGA: res = save_tga(file_name, &setw); break;
	case FT_PCX: res = save_pcx(file_name, &setw); break;
	case FT_PBM: res = save_pbm(file_name, &setw); break;
	case FT_PPM: res = save_ppm(file_name, &setw); break;
	case FT_PAM: res = save_pam(file_name, &setw); break;
	case FT_PIXMAP: res = save_pixmap(&setw, mf); break;
	/* Palette files */
	case FT_GPL:
	case FT_TXT: res = save_txtpal(file_name, &setw); break;
/* !!! Not implemented yet */
//	case FT_PAL:
	}

	return (res);
}

int save_image(char *file_name, ls_settings *settings)
{
	return (save_image_x(file_name, settings, NULL));
}

int save_mem_image(unsigned char **buf, int *len, ls_settings *settings)
{
	memFILE mf;
	int res;

	memset(&mf, 0, sizeof(mf));
	if ((settings->ftype & FTM_FTYPE) == FT_PIXMAP)
	{
		/* !!! Evil hack: we abuse memFILE struct, storing pixmap XID
		 * in buffer pointer, and then copy it into passed-in buffer
		 * pointer - WJ */
		res = save_image_x(NULL, settings, &mf);
		if (!res) *buf = mf.buf , *len = mf.top;
		return (res);
	}

	if (!(file_formats[settings->ftype & FTM_FTYPE].flags & FF_WMEM))
		return (-1);

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
#if U_LCMS
	/* Apply ICC profile */
	while ((settings->icc_size > 0) && (settings->bpp == 3))
	{
		cmsHPROFILE from, to;
		cmsHTRANSFORM how = NULL;
		int l = settings->icc_size - sizeof(icHeader);
		unsigned char *iccdata = settings->icc + sizeof(icHeader);

		/* Do nothing if the profile seems to be the default sRGB one */
		if ((l == 3016) && (hashf(HASHSEED, iccdata, l) == 0xBA0A8E52UL) &&
			(hashf(HASH_RND(HASHSEED), iccdata, l) == 0x94C42C77UL)) break;

		from = cmsOpenProfileFromMem((void *)settings->icc,
			settings->icc_size);
		to = cmsCreate_sRGBProfile();
		if (from && (cmsGetColorSpace(from) == icSigRgbData))
			how = cmsCreateTransform(from, TYPE_RGB_8,
				to, TYPE_RGB_8, INTENT_PERCEPTUAL, 0);
		if (how)
		{
			unsigned char *img = settings->img[CHN_IMAGE];
			size_t l = settings->width, sz = l * settings->height;
			int i, j;

			if (!settings->silent)
				progress_init(_("Applying colour profile"), 1);
			else if (sz < UINT_MAX) l = sz;
			j = sz / l;
			for (i = 0; i < j; i++ , img += l * 3)
			{
				if (!settings->silent && ((i * 20) % j >= j - 20))
					if (progress_update((float)i / j)) break;
				cmsDoTransform(how, img, img, l);
			}
			progress_end();
			cmsDeleteTransform(how);
		}
		if (from) cmsCloseProfile(from);
		cmsCloseProfile(to);
		break;
	}
#endif
// !!! Changing any values is frequently harmful in this mode, so don't do it
	if (settings->mode == FS_CHANNEL_LOAD) return;

	/* Stuff RGB transparency into color 255 */
	if ((settings->rgb_trans >= 0) && (settings->bpp == 3))
	{
		int i;

		// Look for transparent colour in palette
		for (i = 0; i < settings->colors; i++)
		{
			if (PNG_2_INT(settings->pal[i]) == settings->rgb_trans)
				break;
		}

		if (i < settings->colors) settings->xpm_trans = i;
		else
		{	// Colour not in palette so force it into last entry
			settings->pal[255].red = INT_2_R(settings->rgb_trans);
			settings->pal[255].green = INT_2_G(settings->rgb_trans);
			settings->pal[255].blue = INT_2_B(settings->rgb_trans);
			settings->xpm_trans = 255;
			settings->colors = 256;
		}
	}

	/* Accept vars which make sense */
	state->xbm_hot_x = settings->hot_x;
	state->xbm_hot_y = settings->hot_y;
	preserved_gif_delay = settings->gif_delay;

	/* Accept palette */
	image->trans = settings->xpm_trans;
	mem_pal_copy(image->pal, settings->pal);
	image->cols = settings->colors;
}

static int load_image_x(char *file_name, memFILE *mf, int mode, int ftype)
{
	layer_image *lim = NULL;
	png_color pal[256];
	ls_settings settings;
	int i, tr, res, res0, undo = ftype & FTM_UNDO;


	/* Clipboard import - from mtPaint, or from something other? */
	if ((mode == FS_CLIPBOARD) && (ftype & FTM_EXTEND)) mode = FS_CLIP_FILE;
	ftype &= FTM_FTYPE;

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
#ifdef U_LCMS
	/* Set size to -1 when we don't want color profile */
	if (!apply_icc || ((mode == FS_CHANNEL_LOAD) ? (MEM_BPP != 3) :
		(mode != FS_PNG_LOAD) && (mode != FS_LAYER_LOAD)))
		settings.icc_size = -1;
#endif
	/* 0th layer load is just an image load */
	if ((mode == FS_LAYER_LOAD) && !layers_total) mode = FS_PNG_LOAD;
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
	case FT_PNG: res0 = load_png(file_name, &settings, mf); break;
#ifdef U_GIF
	case FT_GIF: res0 = load_gif(file_name, &settings); break;
#endif
#ifdef U_JPEG
	case FT_JPEG: res0 = load_jpeg(file_name, &settings); break;
#endif
#ifdef HANDLE_JP2
	case FT_JP2:
	case FT_J2K: res0 = load_jpeg2000(file_name, &settings); break;
#endif
#ifdef U_TIFF
	case FT_TIFF: res0 = load_tiff(file_name, &settings); break;
#endif
	case FT_BMP: res0 = load_bmp(file_name, &settings, mf); break;
	case FT_XPM: res0 = load_xpm(file_name, &settings); break;
	case FT_XBM: res0 = load_xbm(file_name, &settings); break;
	case FT_LSS: res0 = load_lss(file_name, &settings); break;
	case FT_TGA: res0 = load_tga(file_name, &settings); break;
	case FT_PCX: res0 = load_pcx(file_name, &settings); break;
	case FT_PBM:
	case FT_PGM:
	case FT_PPM:
	case FT_PAM: res0 = load_pnm(file_name, &settings); break;
	case FT_PIXMAP: res0 = load_pixmap(file_name, &settings); break;
#ifdef MAY_HANDLE_SVG
	case FT_SVG: res0 = load_svg(file_name, &settings); break;
#endif
	/* Palette files */
	case FT_GPL:
	case FT_TXT: res0 = load_txtpal(file_name, &settings); break;
/* !!! Not implemented yet */
//	case FT_PAL:
	}

	/* Consider animated GIF a success */
	res = res0 == FILE_HAS_FRAMES ? 1 : res0;

	switch (mode)
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
			/* Report whether the file is animated */
			res = res0;
		}
		/* Failure */
		else
		{
			mem_free_chanlist(settings.img);
			/* If loader managed to delete image before failing */
			if (!mem_img[CHN_IMAGE]) create_default_image();
		}
		break;
	case FS_CLIPBOARD: /* Imported clipboard */
		if ((res == 1) && mem_clip_alpha && !mem_clip_mask)
		{
			/* "Alpha" likely means clipboard mask here */
			mem_clip_mask = mem_clip_alpha;
			mem_clip_alpha = NULL;
			memcpy(settings.img, mem_clip.img, sizeof(chanlist));
		}
		/* Fallthrough */
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
			if (mem_channel == CHN_IMAGE)
				store_image_extras(&mem_image, &mem_state, &settings);
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
				settings.height, settings.bpp, 0, NULL);
			memcpy(lim->image_.img, settings.img, sizeof(chanlist));
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
	case FS_PALETTE_LOAD:
	case FS_PALETTE_DEF:
		/* Drop image channels if any */
		mem_free_chanlist(settings.img);
		/* This "failure" in this context serves as shortcut */
		if (res == EXPLODE_FAILED) res = 1;
		/* Failure - do nothing */
// !!! In case of _image_ format here, re-recognize as palette and retry
		if ((res != 1) || (settings.colors <= 0));
		/* Replace default palette */
		else if (mode == FS_PALETTE_DEF)
		{
			mem_pal_copy(mem_pal_def, pal);
			mem_pal_def_i = settings.colors;
		}
		/* Change current palette */
		else
		{
			mem_undo_next(UNDO_PAL);
			mem_pal_copy(mem_pal, pal);
			mem_cols = settings.colors;
		}
		break;
	}
	free(settings.icc);
	return (res);
}

int load_image(char *file_name, int mode, int ftype)
{
	return (load_image_x(file_name, NULL, mode, ftype));
}

int load_mem_image(unsigned char *buf, int len, int mode, int ftype)
{
	memFILE mf;

	if ((ftype & FTM_FTYPE) == FT_PIXMAP)
		/* Special case: buf points to a pixmap ID */
		return (load_image_x(buf, NULL, mode, ftype));

	if (!(file_formats[ftype & FTM_FTYPE].flags & FF_RMEM)) return (-1);

	memset(&mf, 0, sizeof(mf));
	mf.buf = buf; mf.top = mf.size = len;
	return (load_image_x(NULL, &mf, mode, ftype));
}

// !!! The only allowed modes for now are FS_LAYER_LOAD and FS_EXPLODE_FRAMES
static int load_frames_x(ani_settings *ani, int ani_mode, char *file_name,
	int mode, int ftype)
{
	png_color pal[256];


	ftype &= FTM_FTYPE;
	ani->mode = ani_mode;
	init_ls_settings(&ani->settings, NULL);
	ani->settings.mode = mode;
	ani->settings.ftype = ftype;
	ani->settings.pal = pal;
	/* Clear hotspot & transparency */
	ani->settings.hot_x = ani->settings.hot_y = -1;
	ani->settings.xpm_trans = ani->settings.rgb_trans = -1;
	/* No load progressbar when exploding frames */
	if (ani_mode == FS_EXPLODE_FRAMES) ani->settings.silent = TRUE;

	/* !!! Use default palette - for now */
	mem_pal_copy(pal, mem_pal_def);
	ani->settings.colors = mem_pal_def_i;

	switch (ftype)
	{
//	case FT_PNG: return (load_apng_frames(file_name, ani));
#ifdef U_GIF
	case FT_GIF: return (load_gif_frames(file_name, ani));
#endif
#ifdef U_TIFF
	case FT_TIFF: return (load_tiff_frames(file_name, ani));
#endif
	case FT_PBM:
	case FT_PGM:
	case FT_PPM:
	case FT_PAM: return (load_pnm_frames(file_name, ani));
	}
	return (-1);
}

int load_frameset(frameset *frames, int ani_mode, char *file_name, int mode,
	int ftype)
{
	ani_settings ani;
	int res;


	memset(&ani, 0, sizeof(ani_settings));
	res = load_frames_x(&ani, ani_mode, file_name, mode, ftype);

	/* Treat out-of-memory error as fatal, to avoid worse things later */
	if ((res == FILE_MEM_ERROR) || !ani.fset.cnt)
		mem_free_frames(&ani.fset);
	/* Pass too-many-frames error along */
	else if (res == FILE_TOO_LONG);
	/* Consider all other errors partial failures */
	else if (res != 1) res = FILE_LIB_ERROR;

	/* Just pass the frameset to the outside, for now */
	*frames = ani.fset;
	return (res);
}

/* Write out the last frame to indexed sequence, and delete it */
static int write_out_frame(char *file_name, ani_settings *ani, ls_settings *f_set)
{
	ls_settings w_set;
	image_frame *frame = ani->fset.frames + ani->fset.cnt - 1;
	char new_name[PATHBUF + 32], *tmp;
	int n, deftype = ani->desttype, res;


	/* Show progress, for unknown final count */
	n = nextpow2(ani->cnt);
	if (n < 16) n = 16;
	progress_update((float)ani->cnt / n);

	tmp = strrchr(file_name, DIR_SEP);
	if (!tmp) tmp = file_name;
	else tmp++;
	snprintf(new_name, PATHBUF, "%s%c%s.", ani->destdir, DIR_SEP, tmp);
	tmp = new_name + strlen(new_name);
	sprintf(tmp, "%03d", ani->cnt);

	if (f_set) w_set = *f_set;
	else
	{
		init_ls_settings(&w_set, NULL);
		memcpy(w_set.img, frame->img, sizeof(chanlist));
		w_set.width = frame->width;
		w_set.height = frame->height;
		w_set.pal = frame->pal ? frame->pal : ani->fset.pal;
		w_set.bpp = frame->bpp;
		w_set.colors = frame->cols;
		w_set.xpm_trans = frame->trans;
	}
	w_set.ftype = deftype;
	w_set.silent = TRUE;
	if (!(file_formats[deftype].flags & FF_SAVE_MASK_FOR(w_set)))
	{
		w_set.ftype = FT_PNG;
		ani->miss++;
	}
	w_set.mode = ani->mode; // Only FS_EXPLODE_FRAMES for now

	res = ani->error = save_image(new_name, &w_set);
	if (!res) ani->cnt++;

	if (f_set) // Delete
	{
		mem_free_chanlist(f_set->img);
		memset(f_set->img, 0, sizeof(chanlist));
	}
	// Set for deletion
	else frame->flags |= FM_NUKE;
	return (res);
}

static void warn_miss(int miss, int total, int ftype)
{
	char *txt = g_strdup_printf(_("%d out of %d frames could not be saved as %s - saved as PNG instead"),
		miss, total, file_formats[ftype].name);
	alert_box(_("Warning"), txt, NULL);
	g_free(txt);
}

int explode_frames(char *dest_path, int ani_mode, char *file_name, int ftype,
	int desttype)
{
	ani_settings ani;
	int res;


	memset(&ani, 0, sizeof(ani_settings));
	ani.desttype = desttype;
	ani.destdir = dest_path;

	progress_init(_("Explode frames"), 0);
	progress_update(0.0);
	res = load_frames_x(&ani, ani_mode, file_name, FS_EXPLODE_FRAMES, ftype);
	progress_update(1.0);
	if (res == 1); // Everything went OK
	else if (res == FILE_MEM_ERROR); // Report memory problem
	else if (ani.error) // Sequence write failure - soft or hard?
		res = ani.cnt ? FILE_EXP_BREAK : EXPLODE_FAILED;
	else if (ani.cnt) // Failed to read some middle frame
		res = FILE_LIB_ERROR;
	mem_free_frames(&ani.fset);
	progress_end();

	if (ani.miss && (res == 1))
		warn_miss(ani.miss, ani.cnt, ftype & FTM_FTYPE);

	return (res);
}

int export_undo(char *file_name, ls_settings *settings)
{
	char new_name[PATHBUF + 32];
	int start = mem_undo_done, res = 0, lenny, i, j;
	int deftype = settings->ftype, miss = 0;

	strncpy(new_name, file_name, PATHBUF);
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

	if (miss && !res) warn_miss(miss, mem_undo_done, deftype);

	return (res);
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

static int do_detect_format(char *name, FILE *fp)
{
	unsigned char buf[66], *stop;
	int i;

	i = fread(buf, 1, 64, fp);
	buf[64] = '\0';

	/* Check all unambiguous signatures */
	if (!memcmp(buf, "\x89PNG", 4)) return (FT_PNG);
	if (!memcmp(buf, "\xFF\xD8", 2))
#ifdef U_JPEG
		return (FT_JPEG);
#else
		return (FT_NONE);
#endif
	if (!memcmp(buf, "\0\0\0\x0C\x6A\x50\x20\x20\x0D\x0A\x87\x0A", 12))
#ifdef HANDLE_JP2
		return (FT_JP2);
#else
		return (FT_NONE);
#endif
	if (!memcmp(buf, "\xFF\x4F", 2))
#ifdef HANDLE_JP2
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

	if (!memcmp(buf, "P7", 2)) return (FT_PAM);
	if ((buf[0] == 'P') && (buf[1] >= '1') && (buf[1] <= '6'))
	{
		static const unsigned char pnms[3] = { FT_PBM, FT_PGM, FT_PPM };
		return (pnms[(buf[1] - '1') % 3]);
	}

	if (!memcmp(buf, "GIMP Palette", strlen("GIMP Palette"))) return (FT_GPL);

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

	/* Assume generic XML is SVG */
	i = 0; sscanf(buf, " <?xml %n", &i);
	if (!i) sscanf(buf, " <svg%n", &i);
#ifdef MAY_HANDLE_SVG
	if (i)
	{ /* SVG support need be checked at runtime */
		if (svg_ftype < 0) svg_ftype = svg_supported() ? FT_SVG : FT_NONE;
		return (svg_ftype);
	}
#else
	if (i) return (FT_NONE);
#endif

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
		if (!strcasecmp(stop + 1, "tga")) break;
		return (FT_PCX);
	}

	/* Check if this is TGA */
	if ((buf[1] < 2) && (buf[2] < 12) && ((1 << buf[2]) & 0x0E0F))
		return (FT_TGA);

	/* Simple check for "txt" palette format */
	if ((sscanf(buf, "%i", &i) == 1) && (i > 0) && (i <= 256)) return (FT_TXT);

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

int detect_file_format(char *name, int need_palette)
{
	FILE *fp;
	int i, f;

	if (!(fp = fopen(name, "rb"))) return (-1);
	i = do_detect_format(name, fp);
	f = file_formats[i].flags;
	if (need_palette)
	{
#if 0
		/* Check the raw "pal" format */
		if (!(f & (FF_16 | FF_256 | FF_PALETTE)))
		{
			int l;
			fseek(fp, 0, SEEK_END);
			l = ftell(fp);
			i = l && (l <= 768) && !(l % 3) ? FT_PAL : FT_NONE;
		}
#else
		if (!(f & (FF_16 | FF_256 | FF_PALETTE))) i = FT_NONE;
#endif
	}
	else if (!(f & (FF_IMAGE | FF_LAYER))) i = FT_NONE;
	fclose(fp);
	return (i);
}

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
