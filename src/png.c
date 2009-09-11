/*	png.c
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

#define PNG_READ_PACK_SUPPORTED
#include <png.h>
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

#include "global.h"

#include "png.h"
#include "memory.h"
#include "otherwindow.h"
#include "mygtk.h"
#include "layer.h"


char preserved_gif_filename[256];
int preserved_gif_delay = 10;



int load_png( char *file_name, int stype )
{
	char buf[PNG_BYTES_TO_CHECK], *mess;
	unsigned char *rgb, *rgb2, *rgb3, *alpha = NULL;
	int i, row, do_prog, bit_depth, color_type, interlace_type, width, height;
	FILE *fp;
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
		if ( rgb != NULL ) free( rgb );		// Lose old clipboard
		mem_clip_mask_clear();			// Lose old clipboard mask
	}

	if ( color_type != PNG_COLOR_TYPE_PALETTE || bit_depth>8 )	// RGB PNG file
	{
		png_set_strip_16(png_ptr);
		png_set_gray_1_2_4_to_8(png_ptr);
		png_set_palette_to_rgb(png_ptr);
		png_set_gray_to_rgb(png_ptr);

		if ( stype == 0 )
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
		if ( stype == 1 )
		{
			rgb = malloc( width * height * 3 );
			if ( rgb == NULL )
			{
				png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
				fclose(fp);
				free(row_pointers);
				return -1;
			}
			mem_clip_bpp = 3;
			mem_clipboard = rgb;
			mem_clip_w = width;
			mem_clip_h = height;
		}

		if ( do_prog ) progress_init( mess, 0 );

		if (stype == 0 && alpha)
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

#if 0 /*	OLD CODE PRE HACK */
							// Check each pixel's alpha and copy across
					if ( rgb3[3] > 127 )
					{
						rgb2[0] = rgb3[0];
						rgb2[1] = rgb3[1];
						rgb2[2] = rgb3[2];
					}
					else
					{
						rgb2[0] = 115;
						rgb2[1] = 115;
						rgb2[2] = 0;
					}
#endif
					alpha++;
					rgb2 += 3;
					rgb3 += 4;
				}
//				mem_pal[255].red = 115;
//				mem_pal[255].green = 115;
//				mem_pal[255].blue = 0;
//				mem_xpm_trans = 255;
//				mem_cols = 256;		// Force full palette
			}
			free(row_pointers[0]);
		}
		else
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

		png_read_end(png_ptr, info_ptr);
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(fp);
	}
	else
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
		png_read_end(png_ptr, info_ptr);

		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(fp);
	}
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

int save_png( char *file_name, int stype )	// 0=canvas 1=clipboard 2=undo 3=composite layer
{
	png_structp png_ptr;
	png_infop info_ptr;
	FILE *fp;
	int i, j, scaler, h=0, w=0, image_type=0;
	char *rgb = NULL, *mess = NULL, *alpha = NULL;
	unsigned char trans[256], *rgba_row;
	png_color_16 trans_rgb;
	png_bytep row_pointer;

	for ( i=0; i<256; i++ ) trans[i]=255;

	if ( stype == 0 || stype == 2 )
	{
		h = mem_height;
		w = mem_width;
		rgb = mem_img[CHN_IMAGE];
		alpha = mem_img[CHN_ALPHA];
		if ( stype == 0 ) mess = _("Saving PNG image");
	}
	if ( stype == 1 )
	{
		h = mem_clip_h;
		w = mem_clip_w;
		rgb = mem_clipboard;
		alpha = mem_clip_alpha;
		mess = _("Saving Clipboard image");
	}
	if ( stype == 3 )
	{
		h = layer_h;
		w = layer_w;
		rgb = layer_rgb;
		mess = _("Saving Layer image");
	}

	if ((fp = fopen(file_name, "wb")) == NULL) return -1;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);

	if (png_ptr == NULL)
	{
		fclose(fp);
		return -1;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		fclose(fp);
		png_destroy_write_struct(&png_ptr, NULL);
		return -1;
	}

	png_init_io(png_ptr, fp);
	png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

	if ( stype<3 && mem_img_bpp == 1 )
	{
		png_set_IHDR(png_ptr, info_ptr, w, h,
			8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		png_set_PLTE(png_ptr, info_ptr, mem_pal, mem_cols);
		scaler = 1;
		if ( mem_xpm_trans > -1 && mem_xpm_trans < 256 )	// Transparent index in use
		{
			trans[mem_xpm_trans] = 0;
			png_set_tRNS(png_ptr, info_ptr, trans, mem_cols, 0);
		}
	}
	else
	{
		if ( alpha == NULL ) image_type = PNG_COLOR_TYPE_RGB;
		else image_type = PNG_COLOR_TYPE_RGB_ALPHA;

		png_set_IHDR(png_ptr, info_ptr, w, h,
			8, image_type, PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

		scaler = 3;
		if ( stype!=3 && mem_xpm_trans > -1 && mem_xpm_trans < 256 &&
			alpha == NULL )
		{	// Transparent index in use - not for layers
			trans_rgb.red = mem_pal[mem_xpm_trans].red;
			trans_rgb.green = mem_pal[mem_xpm_trans].green;
			trans_rgb.blue = mem_pal[mem_xpm_trans].blue;
			png_set_tRNS(png_ptr, info_ptr, 0, 1, &trans_rgb);
		}
	}

	png_write_info(png_ptr, info_ptr);

	if ( stype < 2 ) progress_init( mess, 0 );

	if ( mem_img_bpp == 1 || alpha == NULL )		// Flat RGB/Indexed image
	{
		for (i = 0; i<h; i++)
		{
			if (i%16 == 0 && stype < 2) progress_update( ((float) i) / h );
			row_pointer = (png_bytep) (rgb + scaler*i*w);
			png_write_row(png_ptr, row_pointer);
		}
	}
	else				// RGBA image
	{
		rgba_row = malloc( w * 4 );
		if ( rgba_row != NULL )
		{
			row_pointer = (png_bytep) (rgba_row);
			for (i = 0; i<h; i++)
			{
				for ( j=0; j<w; j++ )		// Extract RGB & A pixels
				{
					rgba_row[j*4  ] = rgb[ 3*(i*w + j) ];
					rgba_row[j*4+1] = rgb[ 3*(i*w + j) + 1 ];
					rgba_row[j*4+2] = rgb[ 3*(i*w + j) + 2 ];
					rgba_row[j*4+3] = alpha[ i*w + j ];
				}
				if (i%16 == 0 && stype < 2) progress_update( ((float) i) / h );
				png_write_row(png_ptr, row_pointer);
			}
			free( rgba_row );
		}
	}

	if ( stype < 2 ) progress_end();

	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);

	fclose(fp);

	return 0;
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

int save_gif( char *file_name )
{
	if ( mem_img_bpp != 1 ) return NOT_GIF;		// GIF save must be on indexed image

	return save_gif_real( file_name, mem_img[CHN_IMAGE], mem_pal, mem_width, mem_height, mem_xpm_trans, 1 );
}


int save_gif_real( char *file_name,
	unsigned char *im, png_color *pal, int w, int h, int trans, int skip )
{
#ifdef U_GIF
	GifFileType *giffy;

	ColorMapObject *gif_map = NULL;
	GifColorType gif_pal[256];

	unsigned char gif_ext_data[8];
	int i, j;


	gif_map = MakeMapObject(256, gif_pal);
	for ( i=0; i<256; i++ )					// Prepare GIF palette
	{
		gif_map->Colors[i].Red	 = pal[i].red;
		gif_map->Colors[i].Green = pal[i].green;
		gif_map->Colors[i].Blue	 = pal[i].blue;
	}

	giffy = EGifOpenFileName(file_name, FALSE);
	if ( giffy==NULL ) goto fail;

	if ( EGifPutScreenDesc( giffy, w, h, 256, 0, gif_map )  == GIF_ERROR )
		goto fail;
	if ( trans > -1 )
	{
		gif_ext_data[0] = 1;
		gif_ext_data[1] = 0;
		gif_ext_data[2] = 0;
		gif_ext_data[3] = trans;
		EGifPutExtension( giffy, GRAPHICS_EXT_FUNC_CODE, 4, gif_ext_data );
	}
	if ( EGifPutImageDesc( giffy, 0, 0, w, h, FALSE, NULL )  == GIF_ERROR )
		goto fail;

	if ( skip == 1 ) progress_init(_("Saving GIF image"),0);

	for ( j=0; j<h; j++ )
	{
		if ( j%16 == 0 && skip == 1 ) progress_update( ((float) j) / h );
		EGifPutLine( giffy, im + j*w, w );
	}
	if ( skip == 1 ) progress_end();

	FreeMapObject( gif_map );
	EGifCloseFile(giffy);

	return 0;

fail:
	FreeMapObject( gif_map );
	EGifCloseFile(giffy);

	return -1;
#else
	return -1;
#endif
}

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


int save_jpeg( char *file_name )
{
#ifdef U_JPEG
	struct jpeg_compress_struct cinfo;
	JSAMPROW row_pointer[1];

	FILE *fp;
	int i;

	if ( mem_img_bpp == 1 ) return NOT_JPEG;		// JPEG save must be on RGB image

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (setjmp(jerr.setjmp_buffer))
	{
		jpeg_destroy_compress(&cinfo);
		return -1;
	}

	jpeg_create_compress(&cinfo);

	if (( fp = fopen(file_name, "wb") ) == NULL)
		return -1;

	jpeg_stdio_dest( &cinfo, fp );
	cinfo.image_width = mem_width;
	cinfo.image_height = mem_height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, mem_jpeg_quality, TRUE );
	jpeg_start_compress( &cinfo, TRUE );

	row_pointer[0] = mem_img[CHN_IMAGE];
	progress_init(_("Saving JPEG image"),0);
	for ( i=0; i<mem_height; i++ )
	{
		if (i%16 == 0) progress_update( ((float) i) / mem_height );
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
		row_pointer[0] = row_pointer[0] + 3*mem_width;
	}
	progress_end();

	jpeg_finish_compress( &cinfo );
	jpeg_destroy_compress( &cinfo );

	return 0;
#else
	return -1;
#endif
}


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

int save_tiff( char *file_name )
{
#ifdef U_TIFF
	int j;
	TIFF *tif;

	if ( mem_img_bpp == 1 ) return NOT_TIFF;		// TIFF save must be on RGB image

	TIFFSetErrorHandler(NULL);		// We don't want any echoing to the output
	TIFFSetWarningHandler(NULL);
	tif = TIFFOpen( file_name, "w" );

	if ( tif == NULL ) return -1;

	TIFFSetField( tif, TIFFTAG_IMAGEWIDTH, mem_width );
	TIFFSetField( tif, TIFFTAG_IMAGELENGTH, mem_height );
	TIFFSetField( tif, TIFFTAG_SAMPLESPERPIXEL, 3 );
	TIFFSetField( tif, TIFFTAG_BITSPERSAMPLE, 8 );
	TIFFSetField( tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE );
	TIFFSetField( tif, TIFFTAG_PLANARCONFIG, 1 );
	TIFFSetField( tif, TIFFTAG_PHOTOMETRIC, 2 );

	progress_init(_("Saving TIFF image"),0);
	for ( j=0; j<mem_height; j++ )
	{
		if (j%16 == 0) progress_update( ((float) j) / mem_height );
		if ( TIFFWriteScanline( tif, mem_img[CHN_IMAGE] + j*3*mem_width, j, 0 ) == -1 ) goto fail;
	}
	progress_end();
	TIFFClose(tif);
	return 0;
fail:
	progress_end();
	TIFFClose(tif);
	return -1;
#else
	return -1;
#endif
}

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

int save_bmp( char *file_name )
{
	unsigned char buff[MAX_WIDTH*4], *wrk_image = mem_img[CHN_IMAGE];
	int written, i, j, bpl = mem_width * mem_img_bpp, filesize, headsize;
	FILE *fp;

	if ( bpl % 4 != 0 ) bpl = bpl + 4 - (bpl % 4);		// Adhere to 4 byte boundaries
	filesize = 54 + bpl * mem_height;
	if ( mem_img_bpp == 1 ) filesize = filesize + mem_cols*4;
	headsize = filesize - bpl * mem_height;

	if ((fp = fopen(file_name, "wb")) == NULL) return -1;

	for ( i=0; i<54; i++ ) buff[i] = 0;	// Flush

	buff[0] = 'B'; buff[1] = 'M';		// Signature

	buff[2] = filesize % 256;
	buff[3] = (filesize >> 8) % 256;
	buff[4] = (filesize >> 16) % 256;
	buff[5] = (filesize >> 24) % 256;

	buff[10] = headsize % 256;
	buff[11] = headsize / 256;

	buff[14] = 40; buff[26] = 1;

	buff[18] = mem_width % 256; buff[19] = mem_width / 256;	buff[20] = 0; buff[21] = 0;
	buff[22] = mem_height % 256; buff[23] = mem_height / 256; buff[24] = 0; buff[25] = 0;

	buff[28] = mem_img_bpp*8; buff[29] = 0;			// Bits per pixel
	buff[30] = 0; buff[31] = 0; buff[32] = 0; buff[33] = 0;		// No compression

	buff[34] = bpl*mem_height % 256;
	buff[35] = (bpl*mem_height >> 8) % 256;
	buff[36] = (bpl*mem_height >> 16) % 256;
	buff[37] = (bpl*mem_height >> 24) % 256;

	buff[38] = 18; buff[39] = 11;
	buff[42] = 18; buff[43] = 11;

	if ( mem_img_bpp != 3 )
	{
		buff[46] = mem_cols % 256; buff[47] = mem_cols / 256;
		buff[50] = buff[46]; buff[51] = buff[47];
	}
	

	written = fwrite(buff, 1, 54, fp);
	if ( written < 54 ) goto fail;		// Some sort of botch up occured

	progress_init(_("Saving BMP image"), 0);
	if ( mem_img_bpp == 3 )		// RGB image
	{
		for ( j=mem_height-1; j>=0; j-- )
		{
			if (j%16 == 0) progress_update( ((float) mem_height - j) / mem_height );
			for ( i=0; i<mem_width; i++ )
			{
				buff[ 3*i ] = wrk_image[ 2 + 3*(i + mem_width*j) ];
				buff[ 3*i + 1 ] = wrk_image[ 1 + 3*(i + mem_width*j) ];
				buff[ 3*i + 2 ] = wrk_image[ 3*(i + mem_width*j) ];
			}
			fwrite(buff, 1, bpl, fp);
		}
	}
	else				// Indexed palette image
	{
		memset(buff, 0, mem_cols * 4);
		for ( i=0; i<mem_cols; i++ )
		{
			buff[2 + 4*i] = mem_pal[i].red;
			buff[1 + 4*i] = mem_pal[i].green;
			buff[4*i] = mem_pal[i].blue;
		}
		fwrite(buff, 1, mem_cols*4, fp);		// Write colour table

		for ( j=mem_height-1; j>=0; j-- )
		{
			if (j%16 == 0) progress_update( ((float) mem_height - j) / mem_height );

			for ( i=0; i<mem_width; i++ )
				buff[i] = wrk_image[ i + mem_width*j ];

			fwrite(buff, 1, bpl, fp);	// Read in line of pixels
		}
	}
	progress_end();

	fclose(fp);
	return 0;	// Success
fail:
	fclose(fp);
	return -1;
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

	if ( fw < 4 || fh < 4 ) goto fail;

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

int save_xpm( char *file_name )
{
	unsigned char pix;
	char tin[4110], col_tab[257][3], *po;
	FILE *fp;
	int fcpp, i, j;

	if ( mem_img_bpp != 1 ) return NOT_XPM;

	if ((fp = fopen(file_name, "w")) == NULL) return -1;

	if ( mem_cols < 16 ) fcpp = 1;
	else fcpp = 2;

	po = file_name + strlen(file_name) - 1;
	while ( po>=file_name && po[0] != '/' && po[0] != '\\' ) po--;
	po++;

	if ( strlen(po) < 5 ) goto fail;	// name = '.xpm' = wrong

	progress_init(_("Saving XPM image"),0);

	po[strlen(po) - 4] = 0;			// Chop off '.xpm'

	fprintf( fp, "/* XPM */\n" );
	fprintf( fp, "static char *%s_xpm[] = {\n", po );
	fprintf( fp, "\"%i %i %i %i\",\n", mem_width, mem_height, mem_cols, fcpp );

	po[strlen(po)] = '.';			// Restore full file name

	for ( i=0; i<mem_cols; i++ )
	{
		if ( fcpp == 1 )
		{
			col_tab[i][0] = get_hex( i % 16 );
			col_tab[i][1] = 0;
			if ( i == mem_xpm_trans ) col_tab[i][0] = ' ';
		}
		else
		{
			col_tab[i][0] = get_hex( i / 16 );
			col_tab[i][1] = get_hex( i % 16 );
			col_tab[i][2] = 0;
			if ( i == mem_xpm_trans )
			{
				col_tab[i][0] = ' ';
				col_tab[i][1] = ' ';
			}
		}
		if ( i == mem_xpm_trans )
			fprintf( fp, "\"%s\tc None\",\n", col_tab[i] );
		else
		{
			tin[0] = '#';
			tin[1] = get_hex( mem_pal[i].red / 16 );
			tin[2] = get_hex( mem_pal[i].red % 16 );
			tin[3] = get_hex( mem_pal[i].green / 16 );
			tin[4] = get_hex( mem_pal[i].green % 16 );
			tin[5] = get_hex( mem_pal[i].blue / 16 );
			tin[6] = get_hex( mem_pal[i].blue % 16 );
			tin[7] = 0;
			fprintf( fp, "\"%s\tc %s\",\n", col_tab[i], tin );
		}
	}

	for ( j=0; j<mem_height; j++ )
	{
		if (j%16 == 0) progress_update( ((float) j) / mem_height );
		fprintf( fp, "\"" );
		for ( i=0; i<mem_width; i++ )
		{
			pix = mem_img[CHN_IMAGE][ i + j*mem_width ];
			if ( pix>=mem_cols ) pix = 0;
			fprintf( fp, "%s", col_tab[pix] );
		}
		if ( j<(mem_height-1) ) fprintf( fp, "\",\n" );
		else fprintf( fp, "\"\n};\n" );
	}

	progress_end();
	fclose(fp);
	return 0;
fail:
	fclose(fp);
	return -1;
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

int save_xbm( char *file_name )
{
	unsigned char pix;
	char *po;
	int i, j, k, l, bits;
	FILE *fp;

	if ( mem_img_bpp != 1 || mem_cols != 2 ) return NOT_XBM;

	if ((fp = fopen(file_name, "w")) == NULL) return -1;

	po = file_name + strlen(file_name) - 1;
	while ( po>=file_name && po[0] != '/' && po[0] != '\\' ) po--;
	po++;

	if ( strlen(po) < 5 ) goto fail;	// name = '.xbm' = wrong

	po[strlen(po) - 4] = 0;			// Chop off '.xbm'

	fprintf( fp, "#define %s_width %i\n", po, mem_width );
	fprintf( fp, "#define %s_height %i\n", po, mem_height );
	if ( mem_xbm_hot_x>=0 && mem_xbm_hot_y>=0 )
	{
		fprintf( fp, "#define %s_x_hot %i\n", po, mem_xbm_hot_x );
		fprintf( fp, "#define %s_y_hot %i\n", po, mem_xbm_hot_y );
	}
	fprintf( fp, "static unsigned char %s_bits[] = {\n", po );

	po[strlen(po)] = '.';			// Restore full file name

	progress_init(_("Saving XBM image"),0);
	l=0;
	for ( j=0; j<mem_height; j++ )
	{
		if (j%16 == 0) progress_update( ((float) j) / mem_height );
		for ( i=0; i<mem_width; i=i+8 )
		{
			bits = 0;
			for ( k=0; k<8; k++ )
				if ( (i+k) < mem_width )
				{
					pix = mem_img[CHN_IMAGE][ i+k + mem_width*j ];
					if ( pix > 1 ) pix = 0;
					bits = bits + (pix << k);
				}
			fprintf( fp, " 0x%c%c", get_hex(bits / 16), get_hex(bits % 16) );
			if ( !(j == (mem_height-1) && (i+8)>=mem_width ) )
				fprintf( fp, "," );
			l++;
			if ( l%12 == 0 ) fprintf( fp, "\n" );
		}
	}
	progress_end();
	fprintf( fp, " };\n" );

	fclose(fp);
	return 1;
fail:
	fclose(fp);
	return -1;
}

int file_extension_get( char *file_name )
{
	char *po;

	if ( strlen(file_name) > 3 )
	{
		po = file_name + strlen(file_name) - 4;
		if ( check_str( 4, ".png", po ) ) return EXT_PNG;
		if ( check_str( 4, ".xpm", po ) ) return EXT_XPM;
		if ( check_str( 4, ".xbm", po ) ) return EXT_XBM;
		if ( check_str( 4, ".jpg", po ) ) return EXT_JPEG;
		if ( check_str( 4, "jpeg", po ) ) return EXT_JPEG;
		if ( check_str( 4, ".gif", po ) ) return EXT_GIF;
		if ( check_str( 4, ".tif", po ) ) return EXT_TIFF;
		if ( check_str( 4, "tiff", po ) ) return EXT_TIFF;
		if ( check_str( 4, ".bmp", po ) ) return EXT_BMP;
		if ( check_str( 4, ".gpl", po ) ) return EXT_GPL;
		if ( check_str( 4, ".txt", po ) ) return EXT_TXT;
	}

	return EXT_NONE;
}

int save_image( char *file_name )	// Save current canvas to file - sense extension to set type
{
	char	tname[260],
		*ftypes[9] = {".xpm", ".xbm", ".jpg", "jpeg", ".gif", ".tif", "tiff", ".bmp", ".png" },
		*ff1, *pbest = file_name;
	int i, j, res = 0;

	j = 8;					// Default = PNG
	for ( i=0; i<9; i++ )			// Get rightmost match of this file extension
	{
		ff1 = file_name + strlen(file_name) - 4;
		while ( ff1 >= file_name )			// Get rightmost match
		{
			if ( strncasecmp( ff1, ftypes[i], 4 ) == 0 ) break;
			ff1--;
		}
		if ( ff1 > pbest )
		{
			pbest = ff1;
			j = i;
		}
	}
	// We have to check the whole filename in case its the Gifsicle filename *.gif.???

	if ( j==0 ) res = save_xpm( file_name );
	if ( j==1 ) res = save_xbm( file_name );
	if ( j==2 ) res = save_jpeg( file_name );
	if ( j==3 ) res = save_jpeg( file_name );
	if ( j==4 ) res = save_gif( file_name );
	if ( j==5 ) res = save_tiff( file_name );
	if ( j==6 ) res = save_tiff( file_name );
	if ( j==7 ) res = save_bmp( file_name );
	if ( j==8 ) res = save_png( file_name, 0 );

	if ( res==0 )
	{
		if ( j==8 ) snprintf(tname, 256, "%s_c0", file_name);
		else snprintf(tname, 256, "%s.png_c0", file_name);

		if ( mem_img[CHN_ALPHA] )
		{
			if ( !(j==8 && mem_img_bpp==3) )
			{
				// Alpha channel needs to be saved as a separate file as not RGB & PNG
				res = save_channel( tname, mem_img[CHN_ALPHA], mem_width, mem_height);
			}
		}

		if ( res==0 && mem_img[CHN_SEL] )
		{
			tname[strlen(tname)-1] = '1';
			res = save_channel( tname, mem_img[CHN_SEL], mem_width, mem_height);
		}

		if ( res==0 && mem_img[CHN_MASK] )
		{
			tname[strlen(tname)-1] = '2';
			res = save_channel( tname, mem_img[CHN_MASK], mem_width, mem_height);
		}
	}

	return res;
}

int export_undo ( char *file_name, int type )	// type 0 = normal, 1=reverse
{
	char new_name[300];
	int start = mem_undo_done, res = 0, lenny, i, j;

	strncpy( new_name, file_name, 256);
	lenny = strlen( file_name );

	progress_init( _("Saving UNDO images"), 0 );

	i = 1;
	j = mem_undo_done + 1;
	while ( i <= j )
	{
		if ( type == 1 )
		{
			progress_update( ((float) i) / (start+1) );
			sprintf( new_name + lenny, "%03i.png", i );
			if ( res==0 ) res = save_png( new_name, 2 );		// Save image
		}
		if ( mem_undo_done > 0 ) mem_undo_backward();			// Goto first image
		i++;
	}

	i = 1;
	while ( i <= (start+1) )
	{
		if ( type == 0 )
		{
			progress_update( ((float) i) / (start+1) );
			sprintf( new_name + lenny, "%03i.png", i );
			if ( res==0 ) res = save_png( new_name, 2 );		// Save image
		}

		if ( mem_undo_done < start ) mem_undo_forward();
		i++;
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


int save_channel( char *filename, unsigned char *image, int w, int h )
{
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep row_pointer;
	png_color grey_pal[256];

	FILE *fp;
	int i;


	for ( i=0; i<256; i++ )
	{
		grey_pal[i].red = i;
		grey_pal[i].green = i;
		grey_pal[i].blue = i;
	}

	if ((fp = fopen(filename, "wb")) == NULL) return -1;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);

	if (png_ptr == NULL)
	{
		fclose(fp);
		return -1;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		fclose(fp);
		png_destroy_write_struct(&png_ptr, NULL);
		return -1;
	}

	png_init_io(png_ptr, fp);
	png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

	png_set_IHDR(png_ptr, info_ptr, w, h,
		8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_PLTE(png_ptr, info_ptr, grey_pal, 256);

	png_write_info(png_ptr, info_ptr);

	for (i = 0; i<h; i++)
	{
		row_pointer = (png_bytep) (image + i*w);
		png_write_row(png_ptr, row_pointer);
	}

	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);

	fclose(fp);

	return 0;
}

