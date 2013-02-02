/*	font.c
	Copyright (C) 2007-2011 Mark Tyler and Dmitry Groshev

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

#ifdef U_FREETYPE

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "viewer.h"
#include "canvas.h"
#include "inifile.h"
#include "font.h"

#include <iconv.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#if (GTK_MAJOR_VERSION == 1) && defined(U_NLS)
#include <langinfo.h>
#endif





/*
	-----------------------------------------------------------------
	|			Definitions & Structs			|
	-----------------------------------------------------------------
*/

#define MT_TEXT_MONO      1	/* Force mono rendering */
#define MT_TEXT_ROTATE_NN 2	/* Use nearest neighbour rotation on bitmap fonts */
#define MT_TEXT_OBLIQUE   4	/* Apply Oblique matrix transformation to scalable fonts */

#define FONTSEL_KEY "mtfontsel"

#define FONT_INDEX_FILENAME ".mtpaint_fonts"

#define TX_TOGG_ANTIALIAS 0
#define TX_TOGG_BACKGROUND 1
#define TX_TOGG_ANGLE 2
#define TX_TOGG_OBLIQUE 3
#define TX_TOGGS 4

#define TX_SPIN_BACKGROUND 0
#define TX_SPIN_ANGLE 1
#define TX_SPIN_SIZE 2
#define TX_SPINS 3

#define CLIST_FONTNAME 0
#define CLIST_FONTSTYLE 1
#define CLIST_FONTSIZE 2
#define CLIST_FONTFILE 3
#define CLIST_DIRECTORIES 4
#define FONTSEL_CLISTS 5

#define FONTSEL_CLISTS_MAXCOL 3

#define TX_ENTRY_TEXT 0
#define TX_ENTRY_DIRECTORY 1
#define TX_ENTRIES 2

#define TX_MAX_DIRS 100


typedef struct filenameNODE filenameNODE;
struct filenameNODE
{
	char		*filename;		// Filename of font
	int		face_index;		// Face index within font file
	filenameNODE	*next;			// Pointer to next filename, NULL=no more
};

typedef struct sizeNODE sizeNODE;
struct sizeNODE
{
	int		size;			// Font size.  0=Scalable
	filenameNODE	*filename;		// Pointer to first filename
	sizeNODE	*next;			// Pointer to next size, NULL=no more
};

typedef struct styleNODE styleNODE;
struct styleNODE
{
	char		*style_name;		// Style name
	sizeNODE	*size;			// Pointer to first size of this font
	styleNODE	*next;			// Pointer to next style, NULL=no more
};

typedef struct fontNODE fontNODE;
struct fontNODE
{
	char		*font_name;		// Font name
	int		directory;		// Which directory is this font in?
	styleNODE	*style;			// Pointer to first style of this font
	fontNODE	*next;			// Pointer to next font family, NULL=no more
};


typedef struct
{
	int		sort_column,			// Which column is being sorted in Font clist
			preview_w, preview_h,		// Preview area geometry
			update[FONTSEL_CLISTS]		// Delayed update flags
			;

	GtkWidget	*window,			// Font selection window
			*clist[FONTSEL_CLISTS],		// All clists
			*sort_arrows[FONTSEL_CLISTS_MAXCOL],
			*toggle[TX_TOGGS],		// Toggle buttons
			*spin[TX_SPINS],		// Spin buttons
			*entry[TX_ENTRIES],		// Text entries
			*preview_area			// Preview drawing area
			;

	unsigned char	*preview_rgb;			// Preview image in RGB memory

	GtkSortType	sort_direction;			// Sort direction of Font name clist

	fontNODE	*head_node;			// Pointer to head node
	styleNODE	*current_style_node;		// Current style node head
	sizeNODE	*current_size_node;		// Current size node head
	filenameNODE	*current_filename_node;		// Current filename node head
} mtfontsel;

static wjmem *font_mem;
static fontNODE *global_font_node;
static char *font_text;


/*
	-----------------------------------------------------------------
	|			FreeType Rendering Code			|
	-----------------------------------------------------------------
*/

static void ft_draw_bitmap(
		unsigned char	*mem,
		int		w,
		FT_Bitmap	*bitmap,
		FT_Int		x,
		FT_Int		y,
		int		ppb
		)
{
	unsigned char *dest = mem + y * w + x, *src = bitmap->buffer;
	int i, j;

	for (j = 0; j < bitmap->rows; j++)
	{
		if (ppb == 1)		// 8 bits per pixel greyscale
		{
//			memcpy(dest, src, bitmap->width);
			for (i = 0; i < bitmap->width; i++) dest[i] |= src[i];
		}
		else if (ppb == 8)	// 1 bit per pixel mono
		{
			for (i = 0; i < bitmap->width; i++) dest[i] |=
				(((int)src[i >> 3] >> (~i & 7)) & 1) * 255;
		}			
		dest += w; src += bitmap->pitch;
	}
}

static inline void extend(int *rxy, int x0, int y0, int x1, int y1)
{
	if (x0 < rxy[0]) rxy[0] = x0;
	if (y0 < rxy[1]) rxy[1] = y0;
	if (x1 > rxy[2]) rxy[2] = x1;
	if (y1 > rxy[3]) rxy[3] = y1;
}


/*
Render text to a new chunk of memory. NULL return = failure, otherwise points to memory.
int characters required to print unicode strings correctly.
*/
static unsigned char *mt_text_render(
		char	*text,
		int	characters,
		char	*filename,
		char	*encoding,
		double	size,
		int	face_index,
		double	angle,
		int	flags,
		int	*width,
		int	*height
		)
{
	unsigned char	*mem = NULL;
#ifdef WIN32
	const char	*txtp1;
#else
	char		*txtp1;
#endif
	char		*txtp2;
	double		ca, sa, angle_r = angle / 180 * M_PI;
	int		bx, by, bw, bh, bits, ppb, fix_w, fix_h, scalable;
	int		Y1, Y2, X1, X2, ll, line, xflag;
	int		minxy[4] = { MAX_WIDTH, MAX_HEIGHT, -MAX_WIDTH, -MAX_HEIGHT };
	size_t		s, ssize1 = characters, ssize2 = characters * 4 + 5;
	iconv_t		cd;
	FT_Library	library;
	FT_Face		face;
	FT_Matrix	matrix;
	FT_Vector	pen, uninit_(pen0);
	FT_Error	error;
	FT_UInt		glyph_index;
	FT_Int32	unichar, *txt2, *tmp2;
	FT_Int32	load_flags = FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT;

//printf("\n%s %i %s %s %f %i %f %i\n", text, characters, filename, encoding, size, face_index, angle, flags);

	if ( flags & MT_TEXT_MONO ) load_flags |= FT_LOAD_TARGET_MONO;

	if (characters < 1) return NULL;

	error = FT_Init_FreeType( &library );
	if (error) return NULL;

	error = FT_New_Face( library, filename, face_index, &face );
	if (error) goto fail2;

	scalable = FT_IS_SCALABLE(face);

	if (!scalable)
	{
		ca = 1.0; sa = 0.0; // Transform, if any, is for later

/* !!! Linux .pcf fonts require requested height to match ppem rounded up;
 * Windows .fon fonts, conversely, require width & height. So we try both - WJ */
		fix_w = face->available_sizes[0].width;
		fix_h = face->available_sizes[0].height;
		error = FT_Set_Pixel_Sizes(face, fix_w, fix_h);
		if (error)
		{
			fix_w = (face->available_sizes[0].x_ppem + 32) >> 6;
			fix_h = (face->available_sizes[0].y_ppem + 32) >> 6;
			error = FT_Set_Pixel_Sizes(face, fix_w, fix_h);
		}
		if (error) goto fail1;

// !!! FNT fonts have special support in FreeType - maybe use it?
		Y1 = face->size->metrics.ascender;
		Y2 = face->size->metrics.descender;
	}
	else
	{
		ca = cos(angle_r); sa = sin(angle_r);
		/* Ignore embedded bitmaps if transforming the font */
		if (angle_r) load_flags |= FT_LOAD_NO_BITMAP;

		matrix.yy = matrix.xx = (FT_Fixed)(ca * 0x10000L);
		matrix.xy = -(matrix.yx = (FT_Fixed)(sa * 0x10000L));
		if (flags & MT_TEXT_OBLIQUE)
		{
			matrix.xy = (FT_Fixed)((0.25 * ca - sa) * 0x10000L);
			matrix.yy = (FT_Fixed)((0.25 * sa + ca) * 0x10000L);
			load_flags |= FT_LOAD_NO_BITMAP;
		}

		error = FT_Set_Char_Size( face, size*64, 0, 0, 0 );
		if (error) goto fail1;

		Y1 = FT_MulFix(face->ascender, face->size->metrics.y_scale);
		Y2 = FT_MulFix(face->descender, face->size->metrics.y_scale);
	}

	txt2 = calloc(1, ssize2 + 4);
	if (!txt2) goto fail1;

	txtp1 = text;
	txtp2 = (char *)txt2;

	/* !!! To handle non-Unicode fonts properly, is just too costly;
	 * instead we map 'em to ISO 8859-1 and hope for the best - WJ */
	if (FT_Select_Charmap(face, FT_ENCODING_UNICODE))
		FT_Set_Charmap(face, face->charmaps[0]); // Fallback

	/* Convert input string to UTF-32, using native byte order */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	cd = iconv_open("UTF-32LE", encoding);
#else /* G_BYTE_ORDER == G_BIG_ENDIAN */
	cd = iconv_open("UTF-32BE", encoding);
#endif

	if ( cd == (iconv_t)(-1) ) goto fail0;

	s = iconv(cd, &txtp1, &ssize1, &txtp2, &ssize2);
	iconv_close(cd);
	if (s == (size_t)(-1)) goto fail0;
	characters = (txtp2 - (char *)txt2) / sizeof(*txt2); // Converted length
	txt2[characters] = 0; // Terminate the line

	xflag = X1 = X2 = 0;
	while (TRUE)
	{
		pen.x = pen.y = 0;
		ll = line = 0;
		tmp2 = txt2;

		while (TRUE)
		{
			unichar = *tmp2++;
			if (!unichar || (unichar == 0x0A)) // EOL or newline
			{
				// Remember right boundary
				if (ll && face->glyph)
				{
					int tx = pen.x - pen0.x - face->glyph->advance.x;
					int ty = pen.y - pen0.y - face->glyph->advance.y;
					int tdx = face->glyph->metrics.horiBearingX +
						face->glyph->metrics.width - 64;
					/* Project pen offset onto rotated X axis */
					tdx += tx * ca + ty * sa;
					if (tdx > X2) X2 = tdx;
				}
				if (!unichar) break;
				ll = (Y1 - Y2) * ++line;
				pen.x = ll * sa;
				pen.y = ll * -ca;
				ll = 0; // Reset horizontal index
				continue;
			}

			glyph_index = FT_Get_Char_Index(face, unichar);

			if (scalable)	// Cannot rotate fixed fonts
				FT_Set_Transform(face, &matrix, &pen);

			error = FT_Load_Glyph( face, glyph_index, load_flags );
			if ( error ) continue;

			// Remember left boundary
			if (!ll++)
			{
				int tx = face->glyph->metrics.horiBearingX;
				if (!xflag++) X1 = X2 = tx; // First glyph
				if (tx < X1) X1 = tx;
				pen0 = pen;
			}

			switch (face->glyph->bitmap.pixel_mode)
			{
				case FT_PIXEL_MODE_GRAY:	ppb = 1; break;
				case FT_PIXEL_MODE_GRAY2:	ppb = 2; break;
				case FT_PIXEL_MODE_GRAY4:	ppb = 4; break;
				case FT_PIXEL_MODE_MONO:	ppb = 8; break;
				default: continue; // Unsupported mode
			}

			bx = face->glyph->bitmap_left;
			by = -face->glyph->bitmap_top;
			bw = face->glyph->bitmap.width;
			bh = face->glyph->bitmap.rows;
			bits = bw && bh;

			// Bitmap glyphs don't get offset by FreeType
			if (!scalable || (bits && !face->glyph->outline.n_points))
			{
				bx += pen.x >> 6;
				by -= pen.y >> 6;
			}
			pen.x += face->glyph->advance.x;
			pen.y += face->glyph->advance.y;

			// Remember bitmap bounds
			if (!mem && bits)
				extend(minxy, bx, by, bx + bw - 1, by + bh - 1);

			// Draw bitmap onto clipboard memory in pass 1
			if (mem) ft_draw_bitmap(mem, *width, &face->glyph->bitmap,
				bx - minxy[0], by - minxy[1], ppb);
		}

		if (mem) break; // Done

		/* Adjust bounds for full-height rectangle; ignore possible
		 * rounding issues, for their effect is purely visual - WJ */
		by = Y2 + (Y2 - Y1) * line;
		while (TRUE)
		{
			bx = X1;
			while (TRUE)
			{
				int Ax, Ay;

				Ax = bx * ca - by * sa;
				if (Ax < 0) Ax = -(-Ax / 64);
				else Ax = (Ax + 63) / 64;
				if (Ax < minxy[0]) minxy[0] = Ax;
				if (Ax > minxy[2]) minxy[2] = Ax;

				Ay = by * ca + bx * sa;
				if (Ay < 0) Ay = (63 - Ay) / 64;
				else Ay = -(Ay / 64);
				if (Ay < minxy[1]) minxy[1] = Ay;
				if (Ay > minxy[3]) minxy[3] = Ay;

				if (bx == X2) break;
				bx = X2;
			}
			if (by == Y1) break; // Done
			by = Y1;
		}

		// Set up new clipboard
		mem = calloc(1, (*width = minxy[2] - minxy[0] + 1) *
			(*height = minxy[3] - minxy[1] + 1));
		if (!mem) break; // Allocation failed so bail out
	}

	if (mem && !scalable && (angle!=0 || size>=2) )	// Rotate/Scale the bitmap font
	{
		chanlist old_img = {NULL, NULL, NULL, NULL}, new_img = {NULL, NULL, NULL, NULL};
		int nw, nh, ow = *width, oh = *height, ch = CHN_MASK,
			smooth = FALSE,		// FALSE=nearest neighbour, TRUE=smooth
			scale = size		// Scale factor
			;

		if ( scale >= 2 )		// Scale the bitmap up
		{
			nw = ow*scale;
			nh = oh*scale;
			old_img[ch] = mem;
			new_img[ch] = calloc( 1, nw*nh );
			if ( new_img[ch] )
			{
				if ( !mem_image_scale_real(old_img, ow, oh, 1, new_img, nw, nh,
							0, FALSE, FALSE) )
				{
					mem = new_img[ch];
					free( old_img[ch] );		// Scaling succeeded
					*width = nw;
					*height = nh;
					ow = nw;
					oh = nh;
				}
				else
				{
					free( new_img[ch] );		// Scaling failed
				}
			}
		}

		if ( angle != 0 )
		{
			mem_rotate_geometry(ow, oh, angle, &nw, &nh);

			if ( !(flags & MT_TEXT_ROTATE_NN) )	// Smooth rotation requested
				smooth = TRUE;

			old_img[ch] = mem;
			new_img[ch] = calloc( 1, nw*nh );

			if ( new_img[ch] )
			{
//printf("old = %i,%i  new = %i,%i\n", ow, oh, nw, nh);

				mem_rotate_free_real(old_img, new_img, ow, oh,
					nw, nh, 1, -angle, smooth, FALSE, FALSE, TRUE);

				mem = new_img[ch];
				*width = nw;
				*height = nh;
				free( old_img[ch] );
			}
		}
	}

fail0:
	free(txt2);
fail1:
	FT_Done_Face(face);
fail2:
	FT_Done_FreeType(library);

	return mem;
}


/*
	-----------------------------------------------------------------
	|			Font Indexing Code			|
	-----------------------------------------------------------------
*/

#define SIZE_SHIFT 10
#define MAXLEN 256
#define SLOT_FONT 0
#define SLOT_DIR 1
#define SLOT_STYLE 2
#define SLOT_SIZE 3
#define SLOT_FILENAME 4
#define SLOT_TOT 5



static void trim_tab( char *buf, char *txt)
{
	char *st;

	buf[0] = 0;
	if (txt) strncpy0(buf, txt, MAXLEN);
	for ( st=buf; st[0]!=0; st++ ) if ( st[0]=='\t' ) st[0]=' ';
	if ( buf[0] == 0 ) snprintf(buf, MAXLEN, "_None");
}

static void font_dir_search(FT_Library *ft_lib, int dirnum, FILE *fp, char *dir)
{	// Search given directory for font files - recursively traverse directories
	FT_Face		face;
	DIR		*dp;
	struct dirent	*ep;
	struct stat	buf;
	char		full_name[PATHBUF], tmp[2][MAXLEN];
	int		face_index;


	dp = opendir(dir);
	if (!dp) return;

	while ( (ep = readdir(dp)) )
	{
		file_in_dir(full_name, dir, ep->d_name, PATHBUF);

		if ( stat(full_name, &buf)<0 ) continue;	// Get file details

#ifdef WIN32
		if ( S_ISDIR(buf.st_mode) )
#else
		if ( ep->d_type == DT_DIR || S_ISDIR(buf.st_mode) )
#endif
		{	// Subdirectory so recurse
			if (strcmp(ep->d_name, ".") && strcmp(ep->d_name, ".."))
				font_dir_search( ft_lib, dirnum, fp, full_name );
			continue;
		}
		// File so see if its a font
		for (	face_index = 0;
			!FT_New_Face( *ft_lib, full_name, face_index, &face );
			face_index++ )
		{
			int size_type = 0;

			if (!FT_IS_SCALABLE(face)) size_type =
				face->available_sizes[0].height +
				(face->available_sizes[0].width << SIZE_SHIFT) +
				(face_index << (SIZE_SHIFT * 2));

// I use a tab character as a field delimeter, so replace any in the strings with a space

			trim_tab( tmp[0], face->family_name );
			trim_tab( tmp[1], face->style_name );

			fprintf(fp, "%s\t%i\t%s\t%i\t%s\n",
				tmp[0], dirnum, tmp[1], size_type, full_name);

			if ( (face_index+1) >= face->num_faces )
			{
				FT_Done_Face(face);
				break;
			}
			FT_Done_Face(face);
		}
	}
	closedir(dp);
}

static void font_index_create(char *filename, char **dir_in)
{	// dir_in points to NULL terminated sequence of directories to search for fonts
	FT_Library	library;
	int		i;
	FILE		*fp;


	if (FT_Init_FreeType(&library)) return;

	if ((fp = fopen(filename, "w")))
	{
		for (i = 0; dir_in[i]; i++)
			font_dir_search(&library, i, fp, dir_in[i]);
		fclose(fp);
	}

	FT_Done_FreeType(library);
}


static void font_mem_clear()		// Empty whole structure from memory
{
	free(font_text); font_text = NULL;
	wjmemfree(font_mem); font_mem = NULL;
	global_font_node = NULL;
}

#define newNODE(X) wjmalloc(font_mem, sizeof(X), ALIGNOF(X))

static int font_mem_add(char *font, int dirn, char *style, int fsize,
	char *filename)
{// Add new font data to memory structure. Returns TRUE if successful.
	int		bm_index;
	fontNODE	*fo;
	styleNODE	*st;
	sizeNODE	*ze;
	filenameNODE	*fl;


	bm_index = fsize >> SIZE_SHIFT * 2;
	fsize &= (1 << SIZE_SHIFT * 2) - 1;

	for (fo = global_font_node; fo; fo = fo->next)
	{
		if (!strcmp(fo->font_name, font) && (fo->directory == dirn))
			break;	// Font family+directory already exists
	}

	if (!fo)		// Set up new structure as no match currently exists
	{
		fo = newNODE(fontNODE);
		if (!fo) return (FALSE);	// Memory failure
		fo->next = global_font_node;
		global_font_node = fo;

		fo->directory = dirn;
		fo->font_name = font;
/*
Its more efficient to load the newest font family/style/size as the new head because
when checking subsequent new items, its more likely that the next match will be the
head (or near it).  If you add the new item to the end of the list you are ensuring
that wasted searches will happen as the more likely match will be at the end.
MT 22-8-2007
*/
	}

	for (st = fo->style; st; st = st->next)
	{
		if (!strcmp(st->style_name, style))
			break;	// Font style already exists
	}

	if (!st)		// Set up new structure as no match currently exists
	{
		st = newNODE(styleNODE);
		if (!st) return (FALSE);	// Memory failure
		st->next = fo->style;
		fo->style = st;				// New head style

		st->style_name = style;
	}

	for (ze = st->size; ze; ze = ze->next)
	{
		if ( ze->size == fsize ) break;		// Font size already exists
	}

	if (!ze)		// Set up new structure
	{
		ze = newNODE(sizeNODE);
		if (!ze) return (FALSE);	// Memory failure
		ze->next = st->size;
		st->size = ze;			// New head size
		ze->size = fsize;
	}

/*
Always create a new filename node.  If any filenames are duplicates we don't care.
If the user is stupid enough to pass dupicates then they must have their stupidity
shown to them in glorious technicolour so they don't do it again!  ;-)
MT 24-8-2007
*/
	fl = newNODE(filenameNODE);
	if (!fl) return (FALSE);	// Memory failure

	fl->next = ze->filename;		// Old first filename (maybe NULL)
	ze->filename = fl;			// This is the new first filename

	fl->filename = filename;
	fl->face_index = bm_index;

	return (TRUE);
}

static void font_index_load(char *filename)
{
	char *buf, *tmp, *tail, *slots[SLOT_TOT];
	int i, dir, size;


	font_mem = wjmemnew(0, 0);
	font_text = slurp_file(filename);
	if (!font_mem || !font_text)
	{
		font_mem_clear();
		return;
	}

	for (buf = font_text + 1; *buf; buf = tmp)
	{
		buf += strspn(buf, "\r\n");
		if (!*buf) break;
		tmp = buf + strcspn(buf, "\r\n");
		if (*tmp) *tmp++ = 0;
		for (i = 0; i < SLOT_TOT; i++)
		{
			slots[i] = buf;
			buf += strcspn(buf, "\t");
			if (*buf) *buf++ = 0;
		}
		dir = strtol(slots[SLOT_DIR], &tail, 10);
		if (*tail) break;
		size = strtol(slots[SLOT_SIZE], &tail, 10);
		if (*tail) break;
		if (!font_mem_add(slots[0], dir, slots[2], size, slots[4]))
		{	// Memory failure
			font_mem_clear();
			return;
		}
	}
}

#if 0
static void font_index_display(struct fontNODE	*head)
{
	int		families=0, styles=0, sizes=0, filenames=0;
	fontNODE	*fo = head;
	styleNODE	*st;
	sizeNODE	*ze;
	filenameNODE	*fl;
	size_t		nspace = 0, sspace = 0;


	while (fo)
	{
		printf("%s (%i)\n", fo->font_name, fo->directory);
		nspace += strlen(fo->font_name) + 1;
		sspace += sizeof(*fo);
		families ++;
		st = fo->style;
		while (st)
		{
			printf("\t%s\n", st->style_name);
			nspace += strlen(st->style_name) + 1;
			sspace += sizeof(*st);
			styles ++;
			ze = st->size;
			while (ze)
			{
				printf("\t\t%i x %i\n", ze->size % (1<<SIZE_SHIFT),
						(ze->size >> SIZE_SHIFT) % (1<<SIZE_SHIFT)
						);
				sspace += sizeof(*ze);
				sizes ++;
				fl = ze->filename;
				while (fl)
				{
					printf("\t\t\t%3i %s\n", fl->face_index, fl->filename);
					nspace += strlen(fl->filename) + 1;
					sspace += sizeof(*fl);
					filenames++;
					fl = fl->next;
				}
				ze = ze->next;
			}
			st = st->next;
		}
		fo = fo->next;
	}
	printf("\nMemory Used\t%'zu + %'zu (%.1fK)\n"
		"Font Families\t%i\nFont Styles\t%i\nFont Sizes\t%i\nFont Filenames\t%i\n\n",
		nspace, sspace, (double)(nspace + sspace) / 1024,
		families, styles, sizes, filenames);
}
#endif

/*
	-----------------------------------------------------------------
	|			GTK+ Front End Code			|
	-----------------------------------------------------------------
*/


static unsigned char *render_to_1bpp(int *w, int *h)
{
	double angle = 0;
	unsigned char *text_1bpp;
	int flags = 0, size = 1;


	if ( inifile_get_gboolean( "fontTypeBitmap", TRUE ) )
		size = inifile_get_gint32( "fontSizeBitmap", 1 );
	else
		size = inifile_get_gint32( "fontSize", 12 );

	if ( mem_img_bpp == 1 || !inifile_get_gboolean( "fontAntialias0", TRUE ) )
	{
		flags |= MT_TEXT_MONO;
		flags |= MT_TEXT_ROTATE_NN;	// RGB image without AA = nearest neighbour rotation
	}
	if ( inifile_get_gboolean( "fontAntialias3", TRUE ) )
		flags |= MT_TEXT_OBLIQUE;
	if ( inifile_get_gboolean( "fontAntialias2", FALSE ) )
		angle = ((double)inifile_get_gint32( "fontAngle", 0 ))/100;

	text_1bpp = mt_text_render(
			inifile_get( "textString", "" ),
			strlen( inifile_get( "textString", "" ) ),
			inifile_get( "lastTextFilename", "" ),

#if GTK_MAJOR_VERSION == 1
#ifdef U_NLS
			nl_langinfo(CODESET),
	// this only works on international version of mtPaint, as setlocale is needed
#else
			"ISO-8859-1",	// Non-international verson so it must be this
#endif
#else /* if GTK_MAJOR_VERSION == 2 */
			"UTF-8",
#endif

			size,
			inifile_get_gint32( "lastTextFace", 0 ),
			angle, flags, w, h );

	return text_1bpp;
}

static void font_preview_update(mtfontsel *fp)		// Re-render the preview text and update it
{
	unsigned char *text_1bpp;
	int w=1, h=1;


	if ( !fp ) return;

	if ( fp->preview_rgb )		// Remove old rendering
	{
		free( fp->preview_rgb );
		fp->preview_rgb = NULL;
	}

	text_1bpp = render_to_1bpp(&w, &h);

	if ( text_1bpp )
	{
		fp->preview_rgb = calloc( 1, 3*w*h );
		if ( fp->preview_rgb )
		{
			int i, j = w*h;
			unsigned char *src = text_1bpp, *dest = fp->preview_rgb;

			for ( i=0; i<j; i++ )
			{
				dest[0] = dest[1] = dest[2] = *src++ ^ 255;
				dest += 3;
			}

			fp->preview_w = w;
			fp->preview_h = h;
		}
		free( text_1bpp );
//printf("font preview update %i x %i\n", w, h);
	}

	gtk_widget_set_usize( fp->preview_area, w, h );
	gtk_widget_queue_draw( fp->preview_area );
		// Show the world the fruits of my hard labour!  ;-)
// FIXME - GTK+1 has rendering problems when the area becomes smaller - old artifacts are left behind
}



static void font_gui_create_index(char *filename) // Create index file with settings from ~/.mtpaint
{
	char buf[128], *dirs[TX_MAX_DIRS + 1];
	int i, j = inifile_get_gint32("font_dirs", 0 );


	memset(dirs, 0, sizeof(dirs));
	for ( i=0; i<j; i++ )
	{
		snprintf(buf, 128, "font_dir%i", i);
		dirs[i] = inifile_get( buf, "" );
//printf("%s\n", dirs[i] );
	}

	progress_init( _("Creating Font Index"), 0 );
	font_index_create( filename, dirs );
	progress_end();
}


static void delete_text( GtkWidget *widget, gpointer data )
{
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(widget), FONTSEL_KEY);


	if ( !fp ) fp = data;
	if ( !fp ) return;

	win_store_pos(fp->window, "paste_text_window");
	gtk_widget_destroy( fp->window );

	if (fp->preview_rgb) free(fp->preview_rgb);
	free(fp);
}

void ft_render_text()		// FreeType equivalent of render_text()
{
	unsigned char *text_1bpp;
	int w=1, h=1;


	text_1bpp = render_to_1bpp(&w, &h);

	if (text_1bpp && make_text_clipboard(text_1bpp, w, h, 1))
		text_paste = TEXT_PASTE_FT;
	else text_paste = TEXT_PASTE_NONE;
}

static void update_clist(mtfontsel *mem, int cl, int what)
{
	GtkWidget *w = mem->clist[cl];
	GtkCList *clist = GTK_CLIST(w);

	if (!GTK_WIDGET_MAPPED(w)) /* Is frozen anyway */
	{
		what += (what & 1) * 3; // Flag a waiting refresh
		mem->update[cl] |= what;
		return;
	}

	what |= mem->update[cl];
	mem->update[cl] = 0;
	if (what & 4) /* Do a delayed refresh */
	{
		gtk_clist_freeze(clist);
		gtk_clist_thaw(clist);
	}
	if ((what & 2) && clist->selection) /* Do a scroll */
		gtk_clist_moveto(clist, (int)(clist->selection->data), 0, 0.5, 0);
}

static void font_clist_update(GtkWidget *clist, gpointer user_data)
{
	mtfontsel *mem = gtk_object_get_data(GTK_OBJECT(clist), FONTSEL_KEY);
	int cl = (int)user_data;

	update_clist(mem, cl, 0);
}

static void font_clist_centralise(mtfontsel *mem)
{
	int i;

	/* Ensure selections are visible and central */
	for (i = 0; i < FONTSEL_CLISTS; i++)
		update_clist(mem, i, 2);
}

static void read_font_controls(mtfontsel *fp)
{
	int i;
	char txt[128];

	inifile_set("textString",
		(char *)gtk_entry_get_text(GTK_ENTRY(fp->entry[TX_ENTRY_TEXT])));

	for ( i=0; i<TX_TOGGS; i++ )
	{
		snprintf(txt, 128, "fontAntialias%i", i);
		inifile_set_gboolean( txt,
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fp->toggle[i])));
	}

	inifile_set_gint32("fontAngle",
		rint(read_float_spin(fp->spin[TX_SPIN_ANGLE]) * 100.0));

	if ( inifile_get_gboolean( "fontTypeBitmap", TRUE ) )
		inifile_set_gint32("fontSizeBitmap", read_spin(fp->spin[TX_SPIN_SIZE]));
	else
		inifile_set_gint32("fontSize", read_spin(fp->spin[TX_SPIN_SIZE]) );

	if (mem_channel == CHN_IMAGE)
		inifile_set_gint32( "fontBackground", read_spin(fp->spin[TX_SPIN_BACKGROUND]));
}

static gint paste_text_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(widget), FONTSEL_KEY);


	if ( !fp ) return FALSE;

	read_font_controls(fp);
	ft_render_text();
	if (mem_clipboard) pressed_paste(TRUE);

	delete_text( widget, data );

	return FALSE;
}

static void font_clist_adjust_cols(mtfontsel *mem, int cl)
{
	GtkCList *clist = GTK_CLIST(mem->clist[cl]);
	int i;

	/* Adjust column widths for new data */
 	for (i = 0; i < FONTSEL_CLISTS_MAXCOL; i++)
	{
		gtk_clist_set_column_width(clist, i,
			5 + gtk_clist_optimal_column_width(clist, i));
	}
}

static void populate_font_clist( mtfontsel *mem, int cl )
{
	int i, j, row, select_row = -1, real_size = 0;
	char txt[32], buf[128], buf2[256];
	gchar *row_text[FONTSEL_CLISTS_MAXCOL] = {NULL, NULL, NULL};
	GtkCList *clist = GTK_CLIST(mem->clist[cl]);


	gtk_clist_freeze(clist);
	gtk_clist_clear(clist);

	if (cl == CLIST_FONTNAME)
	{
		fontNODE *fn = mem->head_node;
		char *last_font_name = inifile_get( "lastFontName", "" );
		int last_font_name_dir = inifile_get_gint32( "lastFontNameDir", 0 ),
			last_font_name_bitmap = inifile_get_gint32( "lastFontNameBitmap", 0 ),
			bitmap_font;

		while (fn)
		{
			snprintf(txt, 32, "%3i", 1+fn->directory);
			gtkuncpy(buf2, fn->font_name, 256);	// Transfer to UTF-8 in GTK+2
			row_text[2] = buf2;
			row_text[1] = NULL;
			row_text[0] = txt;
			if ( fn->style->size->size != 0 )
			{
				row_text[1] = "B";
				bitmap_font = 1;
			} else	bitmap_font = 0;
			row = gtk_clist_append(clist, row_text);
			gtk_clist_set_row_data(clist, row, (gpointer)fn);

			if ( !strcmp(fn->font_name, last_font_name) &&
				last_font_name_dir == fn->directory &&
				last_font_name_bitmap == bitmap_font
				)
					select_row = row;

			fn = fn->next;
		}
	}
	else if (cl == CLIST_FONTSTYLE)
	{
		static const char *default_styles[] =
			{ "Regular", "Medium", "Book", "Roman", NULL };
		char *last_font_style = inifile_get( "lastFontStyle", "" );
		styleNODE *sn = mem->current_style_node;
		int default_row = -1;

		while (sn)
		{
			gtkuncpy(buf2, sn->style_name, 256);	// Transfer to UTF-8 in GTK+2
			row_text[0] = buf2;
			row = gtk_clist_append(clist, row_text);
			gtk_clist_set_row_data(clist, row, (gpointer)sn);

			for ( i=0; default_styles[i]; i++ )
			{
				if ( !strcmp(sn->style_name, default_styles[i] ) )
					default_row = row;
				if ( !strcmp(sn->style_name, last_font_style ) )
				{
					select_row = row;
				}
			}
			if ( select_row < 0 ) select_row = default_row;
				// Last style not found so use default

			sn = sn->next;
		}
	}
	else if (cl == CLIST_FONTSIZE)
	{
		sizeNODE *zn = mem->current_size_node;
		int old_bitmap_geometry = inifile_get_gint32( "lastfontBitmapGeometry", 0 );

		if ( zn && zn->size == 0 )		// Scalable font so populate with selection
		{
			static const unsigned char sizes[] =
				{ 8, 9, 10, 11, 12, 13, 14, 16, 18, 20,
				22, 24, 26, 28, 32, 36, 40, 48, 56, 64, 72, 0 };
			int last_size = inifile_get_gint32("fontSize", 12);

			for ( i=0; sizes[i]>0 ; i++ )
			{
				snprintf(txt, 32, "%2i", sizes[i]);
				row_text[0] = txt;
				row = gtk_clist_append(clist, row_text);
				gtk_clist_set_row_data(clist, row, (gpointer)zn);
				if ( sizes[i] == last_size ) select_row = row;
			}
			real_size = last_size;
		}
		else while (zn)
		{
			i = (zn->size >> SIZE_SHIFT ) % (1<<SIZE_SHIFT);
			j = zn->size % (1<<SIZE_SHIFT);

			snprintf(txt, 32, "%2i x %2i", i, j);
			row_text[0] = txt;
			row = gtk_clist_append(clist, row_text);
			gtk_clist_set_row_data(clist, row, (gpointer)zn);

			if ( old_bitmap_geometry == zn->size ) select_row = row;

			zn = zn->next;
		}
	}
	else if (cl == CLIST_FONTFILE)
	{
		filenameNODE *fn = mem->current_filename_node;
		char *s, *last_filename = inifile_get( "lastTextFilename", "" );

		while (fn)
		{
			s = strrchr(fn->filename, DIR_SEP);
			if (!s) s = fn->filename; else s++;
			gtkuncpy(buf2, s, 256);			// Transfer to UTF-8 in GTK+2
			snprintf(txt, 32, "%3i", fn->face_index);
			row_text[0] = buf2;
			row_text[1] = txt;
			row = gtk_clist_append(clist, row_text);
			gtk_clist_set_row_data(clist, row, (gpointer)fn);

			if ( !strcmp(fn->filename, last_filename) ) select_row = row;

			fn = fn->next;
		}
	}
	else if (cl == CLIST_DIRECTORIES)
	{
		j = inifile_get_gint32("font_dirs", 0 );
		for ( i=0; i<j; i++ )
		{
			snprintf(buf, 128, "font_dir%i", i);
			snprintf(txt, 32, "%3i", i+1);
			gtkuncpy(buf2, inifile_get( buf, "" ), 256);	// Transfer to UTF-8 in GTK+2
			row_text[0] = txt;
			row_text[1] = buf2;
			gtk_clist_append(clist, row_text);
//printf("%s %s\n", row_text[0], row_text[1]);
		}
	}

	font_clist_adjust_cols(mem, cl);

	if ( select_row>=0 )	// Select chosen item _before_ the sort
	{
		gtk_clist_select_row(clist, select_row, 0);
		gtk_clist_sort(clist);
//printf("current selection = %i\n", (int) (GTK_CLIST(mem->clist[cl])->selection->data) );
	}
	else		// Select 1st item _after_ the sort
	{
		gtk_clist_sort(clist);
		gtk_clist_select_row(clist, 0, 0);
	}

	gtk_clist_thaw(clist);
	update_clist(mem, cl, 3);

	if ( real_size )
		gtk_spin_button_set_value( GTK_SPIN_BUTTON( mem->spin[TX_SPIN_SIZE] ), real_size );
// This hack is needed to ensure any scalable size that is not in clist is correctly chosen
}


static void click_add_font_dir(GtkWidget *widget, gpointer user)
{
	int i = inifile_get_gint32("font_dirs", 0 );
	char txt[PATHBUF], buf[32];
	gchar *row_text[FONTSEL_CLISTS_MAXCOL] = {NULL, NULL, NULL};
	mtfontsel *fp = user;


	row_text[1] = (gchar *)gtk_entry_get_text( GTK_ENTRY(fp->entry[TX_ENTRY_DIRECTORY]) );
	gtkncpy( txt, row_text[1], PATHBUF);

	if ( strlen(txt)>0 && i<TX_MAX_DIRS )
	{
		snprintf(buf, 32, "font_dir%i", i);
		inifile_set( buf, txt );

		snprintf(buf, 32, "%3i", i+1);
		row_text[0] = buf;
		gtk_clist_append( GTK_CLIST(fp->clist[CLIST_DIRECTORIES]), row_text );

		i++;
		inifile_set_gint32("font_dirs", i );
	}
}

static void click_remove_font_dir(GtkWidget *widget, gpointer user)
{
	char txt[32], txt2[32];
	int i, delete_row = -1, row_tot = inifile_get_gint32("font_dirs", 0 );
	mtfontsel *fp = user;
	GtkCList *clist = GTK_CLIST(fp->clist[CLIST_DIRECTORIES]);


	if (clist->selection) delete_row = (int)(clist->selection->data);

	if ((delete_row < 0) || (delete_row >= row_tot)) return;

	gtk_clist_remove(clist, delete_row); // Delete current row in clist

	for ( i=delete_row; i<(row_tot-1); i++)
	{
			// Re-number clist numbers in first column
		snprintf(txt, 32, "%3i", i+1);
		gtk_clist_set_text(clist, i, 0, txt);

			// Re-work inifile items
		snprintf(txt, 32, "font_dir%i", i);
		snprintf(txt2, 32, "font_dir%i", i+1);
		inifile_set( txt, inifile_get(txt2, "") );
	}

	snprintf(txt, 32, "font_dir%i", row_tot-1);	// Flush last (unwanted) item inifile
	inifile_set( txt, "" );
	inifile_get( txt, "" );

	inifile_set_gint32("font_dirs", row_tot-1 );
}

static void click_create_font_index(GtkWidget *widget, gpointer user)
{
	mtfontsel *fp = user;


	if ( inifile_get_gint32("font_dirs", 0 ) > 0 )
	{
		int i;
		char txt[PATHBUF];

		file_in_homedir(txt, FONT_INDEX_FILENAME, PATHBUF);
		font_gui_create_index(txt);			// Create new index file

		for (i=0; i<=CLIST_FONTFILE; i++ )		// Empty all clists
			gtk_clist_clear( GTK_CLIST(fp->clist[i]) );

		font_mem_clear();		// Empty memory of current nodes
		font_index_load(txt);
		fp->head_node = global_font_node;
		fp->current_style_node = NULL;			// Now empty
		fp->current_size_node = NULL;			// Now empty
		fp->current_filename_node = NULL;		// Now empty

			// Populate clists
		populate_font_clist(fp, CLIST_FONTNAME);
		font_clist_adjust_cols(fp, CLIST_FONTNAME);	// Needed to ensure all fonts visible
		font_clist_centralise(fp);
	}
	else alert_box(_("Error"),
		_("You must select at least one directory to search for fonts."), NULL);
}

static void silent_update_size_spin( mtfontsel *fp )
{
	GtkAdjustment *adj = gtk_spin_button_get_adjustment( GTK_SPIN_BUTTON(fp->spin[TX_SPIN_SIZE]) );
	int size;


	if ( inifile_get_gboolean( "fontTypeBitmap", TRUE ) )
		size = inifile_get_gint32("fontSizeBitmap", 1 );
	else
		size = inifile_get_gint32("fontSize", 12 );

// We must block update events before setting the size spin button to void double updates

	gtk_signal_handler_block_by_data( GTK_OBJECT(adj), (gpointer)fp );
	gtk_spin_button_set_value( GTK_SPIN_BUTTON( fp->spin[TX_SPIN_SIZE] ), size );
	gtk_signal_handler_unblock_by_data( GTK_OBJECT(adj), (gpointer)fp );
}

static void font_clist_select_row(GtkCList *clist, gint row, gint column,
	GdkEventButton *event, gpointer user)
{
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(clist), FONTSEL_KEY);
	void *rd = gtk_clist_get_row_data(clist, row);
	int cl = (int) user;


//printf("fp = %i row = %i user = %i\n", (int) fp, row, (int) user);
	if (!fp || !rd) return;

	if (cl == CLIST_FONTNAME)
	{
		fontNODE *fn = rd;
		int bitmap_font = !!fn->style->size->size;

		inifile_set_gboolean( "fontTypeBitmap", bitmap_font );
		gtk_widget_set_sensitive( fp->toggle[TX_TOGG_OBLIQUE],
			!bitmap_font );

		silent_update_size_spin( fp );			// Update size spin

		fp->current_style_node = fn->style;		// New style head node
//		fp->current_size_node = NULL;			// Now invalid
//		fp->current_filename_node = NULL;		// Now invalid
		populate_font_clist( fp, CLIST_FONTSTYLE );	// Update style list
		inifile_set( "lastFontName", fn->font_name );
		inifile_set_gint32( "lastFontNameDir", fn->directory );
		inifile_set_gint32( "lastFontNameBitmap", bitmap_font );
	}
	else if (cl == CLIST_FONTSTYLE)
	{
		styleNODE *sn = rd;

		fp->current_size_node = sn->size;		// New size head node
//		fp->current_filename_node = NULL;		// Now invalid

		populate_font_clist( fp, CLIST_FONTSIZE );
		inifile_set( "lastFontStyle", sn->style_name );
	}
	else if (cl == CLIST_FONTSIZE)
	{
		sizeNODE *zn = rd;
		gchar *celltext;

		if (!gtk_clist_get_text(clist, row, 0, &celltext)); // Error
		else if (!zn->size)	// Scalable so remember size
		{
			int j;
			sscanf(celltext, "%i", &j);
			inifile_set_gint32( "fontSize", j );

			silent_update_size_spin( fp );		// Update size spin
		}
		else			// Non-Scalable so remember index
		{
			inifile_set_gint32( "lastfontBitmapGeometry", zn->size );
		}
		fp->current_filename_node = zn->filename;	// New filename head node
		populate_font_clist( fp, CLIST_FONTFILE );	// Update filename list
	}
	else if (cl == CLIST_FONTFILE)
	{
		filenameNODE *fn = rd;

		inifile_set( "lastTextFilename", fn->filename );
		inifile_set_gint32( "lastTextFace", fn->face_index );
		font_preview_update( fp );		// Update the font preview area
	}
}


static void font_clist_column_button( GtkWidget *widget, gint col, gpointer user)
{
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(widget), FONTSEL_KEY);
	int cl = (int) user;
	GtkSortType direction;


	if (!fp) return;
//printf("cl=%i\n", cl);

	// reverse the sorting direction if the list is already sorted by this col
	if ( fp->sort_column == col )
		direction = (fp->sort_direction == GTK_SORT_ASCENDING
				? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
	else
	{
		direction = GTK_SORT_ASCENDING;

		gtk_widget_hide( fp->sort_arrows[fp->sort_column] );    // Hide old arrow
		gtk_widget_show( fp->sort_arrows[col] );	    // Show new arrow
		fp->sort_column = col;
	}

	gtk_arrow_set(GTK_ARROW( fp->sort_arrows[col] ),
		direction == GTK_SORT_ASCENDING ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING,
		GTK_SHADOW_IN);

	fp->sort_direction = direction;

	gtk_clist_set_sort_type( GTK_CLIST(fp->clist[cl]), direction );
	gtk_clist_set_sort_column( GTK_CLIST(fp->clist[cl]), col );
	gtk_clist_sort( GTK_CLIST(fp->clist[cl]) );
}

static GtkWidget *make_font_clist(int i, mtfontsel *mem)
{
	static const int clist_text_cols[FONTSEL_CLISTS] = { 3, 1, 1, 2, 2 };
	char *clist_text_titles[FONTSEL_CLISTS][FONTSEL_CLISTS_MAXCOL] = {
		{ "", "", _("Font") }, { _("Style"), "", "" },
		{ _("Size"), "", "" }, { _("Filename"), _("Face"), "" },
		{ "", _("Directory"), "" } };
	GtkWidget *w, *scrolledwindow, *temp_hbox;
	GtkCList *clist;
	int j;


	scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
		i == CLIST_FONTNAME ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC,
		GTK_POLICY_AUTOMATIC);

	mem->clist[i] = w = gtk_clist_new(clist_text_cols[i]);
	gtk_container_add(GTK_CONTAINER(scrolledwindow), w);
	gtk_object_set_data(GTK_OBJECT(w), FONTSEL_KEY, mem);
	if (i == CLIST_FONTSTYLE) gtk_widget_set_usize(w, 100, -2);

	clist = GTK_CLIST(w);

	for (j = 0; j < clist_text_cols[i]; j++)
	{
		temp_hbox = gtk_hbox_new( FALSE, 0 );
		pack(temp_hbox, gtk_label_new(clist_text_titles[i][j]));
		gtk_widget_show_all(temp_hbox);

		if (i == CLIST_FONTNAME) mem->sort_arrows[j] =
			pack_end(temp_hbox, gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_IN));

		gtk_clist_set_column_widget(clist, j, temp_hbox);
		gtk_clist_set_column_resizeable(clist, j, FALSE);
		if (i == CLIST_FONTSIZE) gtk_clist_set_column_justification(
			clist, j, GTK_JUSTIFY_CENTER);
	}

	if ( i == CLIST_FONTNAME )
	{
		mem->sort_column = 2;
		gtk_widget_show( mem->sort_arrows[mem->sort_column] );	// Show sort arrow
		gtk_clist_set_sort_column(clist, mem->sort_column);
		gtk_arrow_set(GTK_ARROW( mem->sort_arrows[mem->sort_column] ),
			( mem->sort_direction == GTK_SORT_ASCENDING ?
				GTK_ARROW_DOWN : GTK_ARROW_UP), GTK_SHADOW_IN);
		gtk_signal_connect(GTK_OBJECT(clist), "click_column",
				GTK_SIGNAL_FUNC(font_clist_column_button), (gpointer)i);
	}
	else
	{
#if GTK_MAJOR_VERSION == 1
		for (j = 0; j < clist_text_cols[i]; j++)
			gtk_clist_column_title_passive(clist, j);
#else /* if GTK_MAJOR_VERSION == 2 */
		gtk_clist_column_titles_passive(clist);
#endif
	}

	gtk_clist_column_titles_show(clist);
	gtk_clist_set_selection_mode(clist, GTK_SELECTION_BROWSE);

	gtk_signal_connect(GTK_OBJECT(clist), "select_row",
		GTK_SIGNAL_FUNC(font_clist_select_row), (gpointer)i);

	/* This will apply delayed updates when they can take effect */
	gtk_signal_connect_after(GTK_OBJECT(clist), "map",
		GTK_SIGNAL_FUNC(font_clist_update), (gpointer)i);

	return (scrolledwindow);
}


static void init_font_lists()		//	LIST INITIALIZATION
{
	char txt[PATHBUF];

	/* Get font directories if we don't have any */
	if (inifile_get_gint32("font_dirs", 0) <= 0)
	{
#ifdef WIN32
		int new_dirs = 1;
		char *windir = getenv("WINDIR");

		file_in_dir(txt, windir && *windir ? windir : "C:\\WINDOWS",
			"Fonts", PATHBUF);
		inifile_set("font_dir0", txt);
#else
		int new_dirs = 0;
		FILE *fp;
		char buf[4096], buf2[128], *s;

		if (!(fp = fopen("/etc/X11/xorg.conf", "r")))
			fp = fopen("/etc/X11/XF86Config", "r");

		// If these files are not found the user will have to manually enter directories

		if (fp)
		{
			while (fgets(buf, 4090, fp))
			{
				s = strstr(buf, "FontPath");
				if (!s) continue;

				s = strstr(buf, ":unscaled\"");
				if (!s) s = strrchr(buf, '"');
				if (!s) continue;
				*s = '\0';

				s = strchr(buf, '"');
				if (!s) continue;

				snprintf(buf2, 128, "font_dir%i", new_dirs);
				inifile_set(buf2, s + 1);
				if (++new_dirs >= TX_MAX_DIRS) break;
			}
			fclose(fp);
		}

		if (!new_dirs && (fp = fopen("/etc/fonts/fonts.conf", "r")))
		{
			char *s1, *s2;

			for (s1 = NULL; s1 || (s1 = fgets(buf, 4090, fp)); s1 = s2)
			{
				s2 = strstr(s1, "</dir>");
				if (!s2) continue;
				*s2 = '\0';
				s2 += 6;

				s = strstr(s1, "<dir>");
				if (!s) continue;

				snprintf(buf2, 128, "font_dir%i", new_dirs);
				inifile_set(buf2, s + 5);
				if (++new_dirs >= TX_MAX_DIRS) break;
			}
			fclose(fp);
		}

		/* Add user's font directory */
		file_in_homedir(txt, ".fonts", PATHBUF);
		snprintf(buf2, 128, "font_dir%i", new_dirs++);
		inifile_set(buf2, txt);
#endif
		inifile_set_gint32("font_dirs", new_dirs);
	}

	file_in_homedir(txt, FONT_INDEX_FILENAME, PATHBUF);
	font_index_load(txt);	// Does a valid ~/.mtpaint_fonts index exist?
	if (!global_font_node)	// Index file not loaded
	{
		font_gui_create_index(txt);
		font_index_load(txt);	// Try for second and last time
	}
}


static gboolean preview_expose_event(GtkWidget *widget, GdkEventExpose *event)
{
	int x = event->area.x, y = event->area.y;
	int w = event->area.width, h = event->area.height;
	int w2, h2;
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(widget), FONTSEL_KEY);


	if (!fp || !fp->preview_rgb) return (FALSE);
#if GTK_MAJOR_VERSION == 1
	/* !!! GTK+2 clears background automatically */
	gdk_window_clear_area(widget->window, x, y, w, h);
#endif

	w2 = fp->preview_w - x; if (w > w2) w = w2;
	h2 = fp->preview_h - y; if (h > h2) h = h2;
	if ((w < 1) || (h < 1)) return (FALSE);

	gdk_draw_rgb_image(widget->window, widget->style->black_gc,
		x, y, w, h, GDK_RGB_DITHER_NONE,
		fp->preview_rgb + (y * fp->preview_w + x) * 3, fp->preview_w * 3);

	return (FALSE);
}

static void font_entry_changed(GtkWidget *widget, gpointer user)	// A GUI entry has changed
{
	mtfontsel *fp = user;

	read_font_controls(fp);
	font_preview_update(fp);		// Update the font preview area
}


void pressed_mt_text()
{
	int		i;
	mtfontsel	*mem;
	GtkWidget	*vbox, *vbox2, *hbox, *notebook, *page, *scrolledwindow;
	GtkWidget	*button, *entry, *preview;
	GdkColor	*c;
	GtkRcStyle	*rc;
	GtkAccelGroup* ag = gtk_accel_group_new();


	if ( !global_font_node ) init_font_lists();

	mem = calloc(1, sizeof(mtfontsel));
	mem->head_node = global_font_node;

	mem->window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Paste Text"), GTK_WIN_POS_NONE, TRUE );
//	gtk_window_set_default_size( GTK_WINDOW(mem->window), 400, 450 );
	win_restore_pos(mem->window, "paste_text_window", 0, 0, 400, 450);

	gtk_object_set_data(GTK_OBJECT(mem->window), FONTSEL_KEY, mem);
//printf("mem = %i\n", (int)mem);

	notebook = gtk_notebook_new();
	gtk_container_add (GTK_CONTAINER (mem->window), notebook);
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);

//	TAB 1 - TEXT

	page = add_new_page(notebook, _("Text"));

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (page), hbox);

	vbox = pack5(hbox, gtk_vbox_new(FALSE, 0));
	xpack5(vbox, make_font_clist(CLIST_FONTNAME, mem));

	vbox = xpack5(hbox, gtk_vbox_new(FALSE, 0));
	hbox = xpack5(vbox, gtk_hbox_new(FALSE, 0));

	xpack5(hbox, make_font_clist(CLIST_FONTSTYLE, mem));

	vbox2 = xpack(hbox, gtk_vbox_new(FALSE, 0));
	xpack(vbox2, make_font_clist(CLIST_FONTSIZE, mem));
	mem->spin[TX_SPIN_SIZE] = pack(vbox2,
		add_a_spin(inifile_get_gint32("fontSize", 12), 1, 500));
#if GTK2VERSION >= 4 /* GTK+ 2.4+ */
	gtk_entry_set_alignment( GTK_ENTRY(&(GTK_SPIN_BUTTON( mem->spin[TX_SPIN_SIZE] )->entry)), 0.5);
#endif

	xpack5(hbox, make_font_clist(CLIST_FONTFILE, mem));

//	Text entry box

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	add_with_frame(vbox, _("Text"), hbox);
	mem->entry[TX_ENTRY_TEXT] = entry = xpack(hbox, gtk_entry_new());
	gtk_entry_set_text(GTK_ENTRY(entry),
		inifile_get("textString", _("Enter Text Here")));
	gtk_signal_connect(GTK_OBJECT(entry), "changed",
		GTK_SIGNAL_FUNC(font_entry_changed), (gpointer)mem);
	accept_ctrl_enter(entry);


//	PREVIEW AREA

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	add_with_frame_x(vbox, _("Preview"), hbox, 5, TRUE);

	scrolledwindow = xpack(hbox, gtk_scrolled_window_new(NULL, NULL));
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	mem->preview_area = preview = gtk_drawing_area_new();
	gtk_object_set_data(GTK_OBJECT(preview), FONTSEL_KEY, mem);
	gtk_drawing_area_size(GTK_DRAWING_AREA(preview), 1, 1);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolledwindow), preview);
	gtk_signal_connect(GTK_OBJECT(preview), "expose_event",
		GTK_SIGNAL_FUNC(preview_expose_event), NULL);
	/* Set background color */
#if GTK_MAJOR_VERSION == 1
	rc = gtk_rc_style_new();
#else /* if GTK_MAJOR_VERSION == 2 */
	rc = gtk_widget_get_modifier_style(preview);
#endif
	c = &rc->bg[GTK_STATE_NORMAL];
	c->pixel = 0; c->red = c->green = c->blue = mem_background * 257;
	rc->color_flags[GTK_STATE_NORMAL] |= GTK_RC_BG;
	gtk_widget_modify_style(preview, rc);
#if GTK_MAJOR_VERSION == 1
	gtk_rc_style_unref(rc);
#endif

//	TOGGLES

	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));

	mem->toggle[TX_TOGG_ANTIALIAS] = add_a_toggle( _("Antialias"), hbox,
			inifile_get_gboolean( "fontAntialias0", TRUE ) );

	if (mem_channel != CHN_IMAGE)
	{
		mem->toggle[TX_TOGG_BACKGROUND] = add_a_toggle( _("Invert"), hbox,
			inifile_get_gboolean( "fontAntialias1", FALSE ) );
	}
	else
	{
		mem->toggle[TX_TOGG_BACKGROUND] = add_a_toggle( _("Background colour ="), hbox,
			inifile_get_gboolean( "fontAntialias1", FALSE ) );

		mem->spin[TX_SPIN_BACKGROUND] = pack5(hbox, add_a_spin(
			inifile_get_gint32("fontBackground", 0)	% mem_cols,
			0, mem_cols - 1));
	}

	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));

	mem->toggle[TX_TOGG_OBLIQUE] = add_a_toggle( _("Oblique"), hbox,
		inifile_get_gboolean( "fontAntialias3", FALSE ) );

	mem->toggle[TX_TOGG_ANGLE] = add_a_toggle( _("Angle of rotation ="), hbox, FALSE );

	mem->spin[TX_SPIN_ANGLE] = pack5(hbox, add_float_spin(
		inifile_get_gint32("fontAngle", 0) * 0.01, -360, 360));
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(mem->toggle[TX_TOGG_ANGLE]), 
		inifile_get_gboolean( "fontAntialias2", FALSE ) );

	for ( i=0; i<TX_TOGGS; i++ )
		gtk_signal_connect(GTK_OBJECT(mem->toggle[i]), "toggled",
			GTK_SIGNAL_FUNC(font_entry_changed), (gpointer)mem);
	for ( i=0; i<TX_SPINS; i++ )
		if (mem->spin[i]) spin_connect(mem->spin[i],
			GTK_SIGNAL_FUNC(font_entry_changed), (gpointer)mem);

	add_hseparator( vbox, 200, 10 );

	hbox = pack5(vbox, OK_box(0, mem->window,
		_("Paste Text"), GTK_SIGNAL_FUNC(paste_text_ok),
		_("Close"), GTK_SIGNAL_FUNC(delete_text)));


//	TAB 2 - DIRECTORIES

	page = add_new_page(notebook, _("Font Directories"));
	vbox = add_vbox(page);

	xpack5(vbox, make_font_clist(CLIST_DIRECTORIES, mem));

	mem->entry[TX_ENTRY_DIRECTORY] = mt_path_box(_("New Directory"), vbox,
						_("Select Directory"), FS_SELECT_DIR);

	add_hseparator( vbox, 200, 10 );

	hbox = pack5(vbox, gtk_hbox_new(FALSE, 0));

	button = add_a_button(_("Close"), 5, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(delete_text), mem);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button = add_a_button(_("Add"), 5, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_add_font_dir), mem);

	button = add_a_button(_("Remove"), 5, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_remove_font_dir), mem);

	button = add_a_button(_("Create Index"), 5, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_create_font_index), mem);

	populate_font_clist(mem, CLIST_FONTNAME);
	populate_font_clist(mem, CLIST_DIRECTORIES);
	font_clist_adjust_cols(mem, CLIST_FONTNAME);		// Needed to ensure all fonts visible

	font_clist_centralise(mem);				// Ensure each list is shown properly

	if (mem_img_bpp == 1) gtk_widget_hide(mem->toggle[TX_TOGG_ANTIALIAS]);
	gtk_window_set_transient_for( GTK_WINDOW(mem->window), GTK_WINDOW(main_window) );

	gtk_widget_show_all(mem->window);
	gtk_window_add_accel_group(GTK_WINDOW (mem->window), ag);

	gtk_widget_grab_focus( mem->entry[TX_ENTRY_TEXT] );
//	font_index_display(mem->head_node);
}

#endif	/* U_FREETYPE */
