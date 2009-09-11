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
#include <png.h>
#include <zlib.h>
#include <stdlib.h>
#include <ctype.h>

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
	{ "TIFF", "tif", "tiff", FF_IDX | FF_RGB },
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

int load_png( char *file_name, int stype )		// 0=image, 1=clipboard
{
	char buf[PNG_BYTES_TO_CHECK], *mess, *chunk_names[] = { "", "alPh", "seLc", "maSk" };
	unsigned char *rgb, *rgb2, *rgb3, *alpha;
	int i, row, do_prog, bit_depth, color_type, interlace_type, width, height, num_uk,
		chunk_type;
	long dest_len;
	FILE *fp;
	png_unknown_chunkp uk_p;		// Pointer to unknown chunk structures
	png_bytep *row_pointers, trans;
	png_color_16p trans_rgb;

	png_structp png_ptr;
	png_infop info_ptr;
	png_uint_32 pwidth, pheight;
	png_colorp png_palette;

	if ((fp = fopen(file_name, "rb")) == NULL) return -1;
	i = fread(buf, 1, PNG_BYTES_TO_CHECK, fp);
	if ( i != PNG_BYTES_TO_CHECK ) goto fail;
	i = !png_sig_cmp(buf, (png_size_t)0, PNG_BYTES_TO_CHECK);
	if ( i<=0 ) goto fail;
	rewind( fp );

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) goto fail;

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		goto fail;
	}

	if (setjmp(png_jmpbuf(png_ptr)))
	{
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		fclose(fp);
		return -1;
	}

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 0);

	png_set_keep_unknown_chunks(png_ptr, 3, NULL, 0);	// Allow all chunks to be read

	png_read_info(png_ptr, info_ptr);

	png_get_IHDR(png_ptr, info_ptr, &pwidth, &pheight, &bit_depth, &color_type,
		&interlace_type, NULL, NULL);

	width = (int) pwidth;
	height = (int) pheight;

	if ( width > MAX_WIDTH || height > MAX_HEIGHT )
	{
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(fp);
		return TOO_BIG;
	}

	row_pointers = malloc( sizeof(png_bytep) * height );

	if (setjmp(png_jmpbuf(png_ptr)))	// If libpng generates an error now, clean up
	{
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(fp);
		free(row_pointers);
		return FILE_LIB_ERROR;
	}

	rgb = NULL;
	mess = NULL;
	alpha = NULL;
	if ( width*height > FILE_PROGRESS ) do_prog = 1;
	else do_prog = 0;

	if ( stype == 0 )
	{
		mess = _("Loading PNG image");
		rgb = mem_img[CHN_IMAGE];
	}
	if ( stype == 1 )
	{
		mess = _("Loading clipboard image");
		rgb = mem_clipboard;
		if ( rgb ) free( rgb );			// Lose old clipboard
		if ( mem_clip_alpha )
		{
			free( mem_clip_alpha );		// Lose old alpha
			mem_clip_alpha = NULL;
		}
		
		mem_clip_mask_clear();			// Lose old clipboard mask
	}

	if ( color_type != PNG_COLOR_TYPE_PALETTE || bit_depth>8 )	// RGB PNG file
	{
		png_set_strip_16(png_ptr);
		png_set_gray_1_2_4_to_8(png_ptr);
		png_set_palette_to_rgb(png_ptr);
		png_set_gray_to_rgb(png_ptr);

		if ( stype == 0 )			// Load RGB image
		{
			mem_pal_load_def();
			i = CMASK_IMAGE;
			if ((color_type == PNG_COLOR_TYPE_RGB_ALPHA) ||
				(color_type == PNG_COLOR_TYPE_GRAY_ALPHA))
				i = CMASK_RGBA;
			if (mem_new(width, height, 3, i))
				goto file_too_huge;
			rgb = mem_img[CHN_IMAGE];
			alpha = mem_img[CHN_ALPHA];
			if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
			{
				// Image has a transparent index
				png_get_tRNS(png_ptr, info_ptr, 0, 0, &trans_rgb);
				mem_pal[255].red = trans_rgb->red;
				mem_pal[255].green = trans_rgb->green;
				mem_pal[255].blue = trans_rgb->blue;
				if (color_type == PNG_COLOR_TYPE_GRAY)
				{
					if ( bit_depth==4 ) i = trans_rgb->gray * 17;
					if ( bit_depth==8 ) i = trans_rgb->gray;
					if ( bit_depth==16 ) i = trans_rgb->gray >> (bit_depth-8);
					mem_pal[255].red = i;
					mem_pal[255].green = i;
					mem_pal[255].blue = i;
				}
				mem_xpm_trans = 255;
				mem_cols = 256;		// Force full palette
			}
		}
		if ( stype == 1 )			// Load RGB clipboard
		{
			rgb = malloc( width * height * 3 );
			if ( rgb == NULL )
			{
				png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
				fclose(fp);
				free(row_pointers);
				return -1;
			}

			if ( color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
				color_type == PNG_COLOR_TYPE_GRAY_ALPHA )
			{
				alpha = malloc( width * height );
			}
			else	alpha = NULL;

			mem_clip_bpp = 3;
			mem_clipboard = rgb;
			mem_clip_alpha = alpha;
			mem_clip_w = width;
			mem_clip_h = height;
		}

		if ( do_prog ) progress_init( mess, 0 );

		if (alpha)				// 32bpp RGBA image/clipboard
		{
			row_pointers[0] = malloc(width * 4);
			if ( row_pointers[0] == NULL ) goto force_RGB;
			for (row = 0; row < height; row++)
			{
				png_read_rows(png_ptr, &row_pointers[0], NULL, 1);
				rgb2 = rgb + row*width*3;
				rgb3 = row_pointers[0];
				for (i=0; i<width; i++)
				{
					rgb2[0] = rgb3[0];
					rgb2[1] = rgb3[1];
					rgb2[2] = rgb3[2];
					alpha[0] = rgb3[3];

					alpha++;
					rgb2 += 3;
					rgb3 += 4;
				}
			}
			free(row_pointers[0]);
		}
		else					// 24bpp RGB
		{
force_RGB:
			png_set_strip_alpha(png_ptr);
			for (row = 0; row < height; row++)
			{
				if ( row%16 == 0 && do_prog ) progress_update( ((float) row) / height );
				row_pointers[row] = rgb + 3*row*width;
			}
			png_read_image(png_ptr, row_pointers);
		}

		if ( do_prog ) progress_end();
	}
	else			// Indexed Palette file
	{
		if ( stype == 1 )
		{
			rgb = malloc( width * height );
			if ( rgb == NULL ) return -1;
			mem_clip_bpp = 1;
			mem_clipboard = rgb;
			mem_clip_w = width;
			mem_clip_h = height;
		}
		if ( stype == 0 )
		{
			png_get_PLTE(png_ptr, info_ptr, &png_palette, &mem_cols);
			for ( i=0; i<mem_cols; i++ ) mem_pal[i] = png_palette[i];
			if (mem_new(width, height, 1, CMASK_IMAGE))
				goto file_too_huge;
			rgb = mem_img[CHN_IMAGE];
			if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
			{
				// Image has a transparent index
				png_get_tRNS(png_ptr, info_ptr, &trans, &i, 0);
				for ( i=0; i<256; i++ ) if ( trans[i]==0 ) break;
				if ( i>255 ) i=-1;	// No transparency found
				mem_xpm_trans = i;
			}
		}
		png_set_strip_16(png_ptr);
		png_set_strip_alpha(png_ptr);
		png_set_packing(png_ptr);

		if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
			png_set_gray_1_2_4_to_8(png_ptr);

		if ( do_prog ) progress_init( mess, 0 );
		for (row = 0; row < height; row++)
		{
			if ( row%16 == 0 && do_prog ) progress_update( ((float) row) / height );
			row_pointers[row] = rgb + row*width;
		}
		if ( do_prog ) progress_end();

		png_read_image(png_ptr, row_pointers);
	}

	png_read_end(png_ptr, info_ptr);

	num_uk = png_get_unknown_chunks(png_ptr, info_ptr, &uk_p);
//printf("unknown chunks = %i\n", num_uk);
	if (num_uk)				// File contains extra unknown chunks
	{
		for (i=0; i<num_uk; i++)	// Examine each chunk
		{
//printf("unknown %i = %s size = %i\n", i, uk_p[i].name, uk_p[i].size);

			for ( chunk_type = 1; chunk_type<4; chunk_type++ )
			{
				if ( strcmp(uk_p[i].name, chunk_names[chunk_type]) == 0 ) break;
			}

			if ( stype == 0 && chunk_type<4 )		// Load chunk into image
			{
				if ( mem_img[chunk_type] )
					free( mem_img[chunk_type] );

				mem_img[chunk_type] = calloc( 1, width*height );

				if ( mem_img[chunk_type] )
				{
					dest_len = width * height;
					uncompress( mem_img[chunk_type], &dest_len,
						uk_p[i].data, uk_p[i].size );

					mem_undo_im_[mem_undo_pointer].img[chunk_type] =
						mem_img[chunk_type];
				}
			}
			if ( stype == 1 && chunk_type<4 )		// Load chunk into clipboard
			{
				if ( chunk_type == CHN_ALPHA )
				{
					dest_len = width * height;
					if ( mem_clip_alpha ) free( mem_clip_alpha );
					mem_clip_alpha = calloc( 1, dest_len );
					if ( mem_clip_alpha )
					{
						uncompress( mem_clip_alpha, &dest_len,
							uk_p[i].data, uk_p[i].size );
					}
				}
				if ( chunk_type == CHN_SEL )
				{
					dest_len = width * height;
					if ( mem_clip_mask ) free( mem_clip_mask );
					mem_clip_mask = calloc( 1, dest_len );
					if ( mem_clip_mask )
					{
						uncompress( mem_clip_mask, &dest_len,
							uk_p[i].data, uk_p[i].size );
					}
				}
			}
		}
		png_free_data(png_ptr, info_ptr, PNG_FREE_UNKN, -1);
	}

	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	fclose(fp);

	free(row_pointers);

	return 1;
fail:
	fclose(fp);
	return -1;
file_too_huge:
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	fclose(fp);
	free(row_pointers);
	return FILE_MEM_ERROR;
}

#ifndef PNG_AFTER_IDAT
#define PNG_AFTER_IDAT 8
#endif

static int save_png(char *file_name, ls_settings *settings)
{
	static char *chunk_names[] = { "", "alPh", "seLc", "maSk" };
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


int load_gif( char *file_name, int *delay )
{
#ifdef U_GIF
	ColorMapObject *cmap = NULL;
	GifFileType *giffy;
	GifRecordType gif_rec;
	GifByteType *byte_ext;

	GifByteType *CodeBlock;
	int CodeSize;

	int width = -1, height = -1, cols = -1, i, j, k, val, transparency =-1;
	int interlaced_offset[] = { 0, 4, 2, 1 }, interlaced_jumps[] = { 8, 8, 4, 2 };
	int do_prog = 0, frames = 0, res = 1;

	giffy = DGifOpenFileName( file_name );

	if ( giffy == NULL ) return -1;

	do
	{
		if ( DGifGetRecordType(giffy, &gif_rec) == GIF_ERROR) goto fail;
		if ( gif_rec == IMAGE_DESC_RECORD_TYPE )
		{
//printf("Frames = %i\n", frames);
			frames++;
			if ( DGifGetImageDesc(giffy) == GIF_ERROR ) goto fail;
			if ( frames == 1 )	// Only read the first frame in
			{

				if ( giffy->SColorMap != NULL ) cmap = giffy->SColorMap;
				else if ( giffy->Image.ColorMap != NULL ) cmap = giffy->Image.ColorMap;

				if ( cmap == NULL ) goto fail;

				cols = cmap->ColorCount;
				if ( cols > 256 || cols < 2 )
				{
					DGifCloseFile(giffy);
					return NOT_INDEXED;
				}
				mem_cols = cols;

				for ( i=0; i<mem_cols; i++ )
				{
					mem_pal[i].red = cmap->Colors[i].Red;
					mem_pal[i].green = cmap->Colors[i].Green;
					mem_pal[i].blue = cmap->Colors[i].Blue;
				}

//				width = giffy->SWidth;
//				height = giffy->SHeight;
				width = giffy->Image.Width;
				height = giffy->Image.Height;

				if ( width > MAX_WIDTH || height > MAX_HEIGHT )
				{
					DGifCloseFile(giffy);
					return TOO_BIG;
				}
				if ( width*height > FILE_PROGRESS ) do_prog = 1;
				if (mem_new(width, height, 1, CMASK_IMAGE))
					goto fail_too_huge;

				if ( do_prog ) progress_init(_("Loading GIF image"),0);
				if ( giffy->Image.Interlace )
				{
				 for ( k=0; k<4; k++ )
				 {
				  for ( j=interlaced_offset[k]; j<mem_height; j=j+interlaced_jumps[k] )
				  {
				   if ( j%16 == 0 && do_prog )
				    progress_update( ((float) j) / mem_height );
				   DGifGetLine( giffy, mem_img[CHN_IMAGE] + j*mem_width, mem_width );
				  }
				 }
				}
				else for ( j=0; j<mem_height; j++ )
					{
						if ( j%16 == 0 && do_prog )
							progress_update( ((float) j) / mem_height );
						DGifGetLine( giffy, mem_img[CHN_IMAGE] + j*mem_width, mem_width );
					}

				if ( do_prog ) progress_end();
			}
			else	// Subsequent frames not read in
			{
				if ( DGifGetCode(giffy, &CodeSize, &CodeBlock) == GIF_ERROR )
					goto fail;
				while (CodeBlock != NULL)
				{
					if ( DGifGetCodeNext(giffy, &CodeBlock) == GIF_ERROR )
						goto fail;
				}
			}
		}

		if ( gif_rec == EXTENSION_RECORD_TYPE )
		{
			if (DGifGetExtension(giffy, &val, &byte_ext) == GIF_ERROR) goto fail;
			while (byte_ext != NULL)
			{
				if ( val == GRAPHICS_EXT_FUNC_CODE )
				{
					if ( byte_ext[1] % 2 == 1 && frames <= 1 )
					{
						transparency = byte_ext[4];
						*delay = byte_ext[2] + (byte_ext[3]<<8);
					}
				}
				if (DGifGetExtensionNext(giffy, &byte_ext) == GIF_ERROR) goto fail;
			}
		}
	}
	while ( gif_rec != TERMINATE_RECORD_TYPE );

//printf("Total frames = %i\n", frames);
	if ( frames > 1 ) res = FILE_GIF_ANIM;

	mem_xpm_trans = transparency;

	DGifCloseFile(giffy);
	return res;

fail:
	DGifCloseFile(giffy);
	return -1;
fail_too_huge:
	DGifCloseFile(giffy);
	return FILE_MEM_ERROR;
#else
	return -1;
#endif
}

#ifdef U_GIF
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

#endif

int load_jpeg( char *file_name )
{
#ifdef U_JPEG
	struct jpeg_decompress_struct cinfo;
	FILE *fp;
	int width, height, i, do_prog;
	unsigned char *memp;

	jpeg_create_decompress(&cinfo);
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer))
	{
		jpeg_destroy_decompress(&cinfo);
		return -1;
	}

	if (( fp = fopen(file_name, "rb") ) == NULL)
		return -1;

	jpeg_stdio_src( &cinfo, fp );

	if (setjmp(jerr.setjmp_buffer))
	{
		jpeg_destroy_decompress( &cinfo );
		fclose(fp);
		return -1;
	}

	jpeg_read_header( &cinfo, TRUE );

	jpeg_start_decompress( &cinfo );
	width = cinfo.output_width;
	height = cinfo.output_height;

	if ( width > MAX_WIDTH || height > MAX_HEIGHT )
	{
		jpeg_finish_decompress( &cinfo );
		jpeg_destroy_decompress( &cinfo );
		return TOO_BIG;
	}

	if ( width*height > FILE_PROGRESS ) do_prog = 1;
	else do_prog = 0;

	if ( cinfo.output_components == 1 )
	{
		mem_cols = 256;
		mem_scale_pal( 0, 0,0,0, 255, 255, 255, 255 );
		if (mem_new(width, height, 1, CMASK_IMAGE))
			goto fail_too_huge;		// Greyscale
	}
	else
	{
		mem_pal_load_def();
		if (mem_new(width, height, 3, CMASK_IMAGE))
			goto fail_too_huge;		// RGB
	}
	memp = mem_img[CHN_IMAGE];

	if (setjmp(jerr.setjmp_buffer))			// If libjpeg causes errors now its too late
	{
		if ( do_prog ) progress_end();

		jpeg_destroy_decompress( &cinfo );
		fclose(fp);

		return FILE_LIB_ERROR;
	}

	if ( do_prog ) progress_init(_("Loading JPEG image"),0);

	for ( i=0; i<height; i++ )
	{
		if ( i%16 == 0 && do_prog ) progress_update( ((float) i) / height );
		jpeg_read_scanlines( &cinfo, &memp, 1 );
		memp = memp + width*mem_img_bpp;
	}
	if ( do_prog ) progress_end();

	jpeg_finish_decompress( &cinfo );
	jpeg_destroy_decompress( &cinfo );
	fclose(fp);

	return 1;
fail_too_huge:
	jpeg_finish_decompress( &cinfo );
	jpeg_destroy_decompress( &cinfo );
	fclose(fp);

	return FILE_MEM_ERROR;
#else
	return -1;
#endif
}

#ifdef U_JPEG
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


int load_tiff( char *file_name )
{
#ifdef U_TIFF
	unsigned int width, height, i, j;
	unsigned char red, green, blue, *wrk_image;
	uint32 *raster = NULL;
	int do_prog = 0;
	TIFF *tif;

	TIFFSetErrorHandler(NULL);		// We don't want any echoing to the output
	TIFFSetWarningHandler(NULL);

	tif = TIFFOpen( file_name, "r" );
	if ( tif == NULL ) return -1;

	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

	if ( width > MAX_WIDTH || height > MAX_HEIGHT )
	{
		_TIFFfree(raster);
		TIFFClose(tif);
		return TOO_BIG;
	}

	if ( width*height > FILE_PROGRESS ) do_prog = 1;

	mem_pal_load_def();
	if (mem_new(width, height, 3, CMASK_IMAGE))		// RGB
	{
		_TIFFfree(raster);
		TIFFClose(tif);
		return FILE_MEM_ERROR;
	}
	wrk_image = mem_img[CHN_IMAGE];

	raster = (uint32 *) _TIFFmalloc ( width * height * sizeof (uint32));

	if ( raster == NULL ) goto fail;		// Not enough memory

	if ( TIFFReadRGBAImage(tif, width, height, raster, 0) )
	{
		if ( do_prog ) progress_init(_("Loading TIFF image"),0);
		for ( j=0; j<height; j++ )
		{
			if ( j%16 == 0 && do_prog ) progress_update( ((float) j) / height );
			for ( i=0; i<width; i++ )
			{
				red = TIFFGetR( raster[(height-j-1) * width + i] );
				green = TIFFGetG( raster[(height-j-1) * width + i] );
				blue = TIFFGetB( raster[(height-j-1) * width + i] );
				wrk_image[ 3*(i + width*j) ] = red;
				wrk_image[ 1 + 3*(i + width*j) ] = green;
				wrk_image[ 2 + 3*(i + width*j) ] = blue;
			}
		}
		if ( do_prog ) progress_end();
		_TIFFfree(raster);
		TIFFClose(tif);
		return 1;
	}
fail:
	_TIFFfree(raster);
	TIFFClose(tif);
	return -1;
#else
	return -1;
#endif
}

#define TIFFX_VERSION 0 // mtPaint's TIFF extensions version

#ifdef U_TIFF
static int save_tiff(char *file_name, ls_settings *settings)
{
/* !!! No private exts for now */
//	char buf[512];

	unsigned char *src, *row = NULL;
	uint16 rgb[256 * 3], xs[NUM_CHANNELS];
/* !!! No extra channels for now */
//	int i, j, k, dt, xsamp = 0, cmask = CMASK_IMAGE, res = 0;

int i, xsamp = 0, res = 0;

	int w = settings->width, h = settings->height, bpp = settings->bpp;
	TIFF *tif;

/* !!! No extra channels for now */
#if 0
	/* Find out number of utility channels */
	memset(xs, 0, sizeof(xs));
	for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
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
#endif

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
/* !!! No extra channels for now */
#if 0
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
#endif
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


int load_channel( char *filename, unsigned char *image, int w, int h )
{
	char buf[PNG_BYTES_TO_CHECK];
	int i, bit_depth, color_type, interlace_type;
	FILE *fp;

	png_bytep *row_pointers;
	png_structp png_ptr;
	png_infop info_ptr;
	png_uint_32 pwidth, pheight;


	if ((fp = fopen(filename, "rb")) == NULL) return -1;
	i = fread(buf, 1, PNG_BYTES_TO_CHECK, fp);
	if ( i != PNG_BYTES_TO_CHECK ) goto fail;

	i = !png_sig_cmp(buf, (png_size_t)0, PNG_BYTES_TO_CHECK);
	if ( i<=0 ) goto fail;

	rewind( fp );
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png_ptr == NULL) goto fail;

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		goto fail;
	}

	if (setjmp(png_jmpbuf(png_ptr)))
	{
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		fclose(fp);
		return -1;
	}

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 0);
	png_read_info(png_ptr, info_ptr);
	png_get_IHDR(png_ptr, info_ptr, &pwidth, &pheight, &bit_depth, &color_type,
		&interlace_type, NULL, NULL);

	if ( pwidth != w || pheight != h  || color_type != PNG_COLOR_TYPE_PALETTE || bit_depth!=8 )
	{
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		goto fail;			// Wrong image geometry so bail out
	}

	row_pointers = malloc( sizeof(png_bytep) * pheight );

	if (setjmp(png_jmpbuf(png_ptr)))	// If libpng generates an error now, clean up
	{
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(fp);
		free(row_pointers);
		return -1;
	}

	png_set_packing(png_ptr);

	for (i = 0; i < pheight; i++) row_pointers[i] = image + i*pwidth;

	png_read_image(png_ptr, row_pointers);
	png_read_end(png_ptr, info_ptr);

	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	fclose(fp);
	free(row_pointers);

	return 0;

fail:
	fclose(fp);
	return -1;
}
