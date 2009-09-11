/*	font.c
	Copyright (C) 2007-2008 Mark Tyler and Dmitry Groshev

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
#include "viewer.h"
#include "canvas.h"
#include "inifile.h"

#include <errno.h>

#ifdef U_FREETYPE
#include <iconv.h>
#include <ft2build.h>
#include FT_FREETYPE_H


#include "font.h"

#if GTK_MAJOR_VERSION == 1
#ifdef U_NLS
#include <langinfo.h>
#endif
#endif





/*
	-----------------------------------------------------------------
	|			Definitions & Structs			|
	-----------------------------------------------------------------
*/

#ifdef U_FREETYPE

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


struct filenameNODE
{
	char			*filename;		// Filename of font
	int			face_index;		// Face index within font file
	struct filenameNODE	*next;			// Pointer to next filename, NULL=no more
};

struct sizeNODE
{
	int			size;			// Font size.  0=Scalable
	struct filenameNODE	*filename;		// Pointer to first filename
	struct sizeNODE		*next;			// Pointer to next size, NULL=no more
};

struct styleNODE
{
	char			*style_name;		// Style name
	struct sizeNODE		*size;			// Pointer to first size of this font
	struct styleNODE	*next;			// Pointer to next style, NULL=no more
};

struct fontNODE
{
	char			*font_name;		// Font name
	int			directory;		// Which directory is this font in?
	struct styleNODE	*style;			// Pointer to first style of this font
	struct fontNODE		*next;			// Pointer to next font family, NULL=no more
};


typedef struct
{
	int		sort_column,			// Which column is being sorted in Font clist
			preview_w, preview_h		// Preview area geometry
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

	struct fontNODE		*head_node;			// Pointer to head node
	struct styleNODE	*current_style_node;		// Current style node head
	struct sizeNODE		*current_size_node;		// Current size node head
	struct filenameNODE	*current_filename_node;		// Current filename node head
} mtfontsel;


static struct fontNODE *global_font_node;
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

static void abcd_calc(
		FT_Vector	pen,
		int		Y1,
		int		Y2,
		double		angle_r,
		int		X1,
		int		*minx,
		int		*miny,
		int		*maxx,
		int		*maxy
		)
{
	int Ax, Ay;

	// Point A/C
	Ax = pen.x - Y1*sin(angle_r) + X1*cos(angle_r);
	Ay = pen.y + Y1*cos(angle_r) + X1*sin(angle_r);
	if ( Ax!=0 )
	{
		if ( Ax<0 ) Ax = (Ax-63)/64;
		else Ax = (Ax+63)/64;
	}
	if (Ax<*minx) *minx = Ax;
	if (Ax>*maxx) *maxx = Ax;
	if ( Ay!=0 )
	{
		if ( Ay<0 ) Ay = -(Ay-63)/64;
		else Ay = -(Ay+63)/64;
	}
	if (Ay<*miny) *miny = Ay;
	if (Ay>*maxy) *maxy = Ay;

	// Point B/D
	Ax = pen.x - Y2*sin(angle_r) + X1*cos(angle_r);
	Ay = pen.y + Y2*cos(angle_r) + X1*sin(angle_r);
	if ( Ax!=0 )
	{
		if ( Ax<0 ) Ax = (Ax-63)/64;
		else Ax = (Ax+63)/64;
	}
	if (Ax<*minx) *minx = Ax;
	if (Ax>*maxx) *maxx = Ax;
	if ( Ay!=0 )
	{
		if ( Ay<0 ) Ay = -(Ay-63)/64;
		else Ay = -(Ay+63)/64;
	}
	if (Ay<*miny) *miny = Ay;
	if (Ay>*maxy) *maxy = Ay;
}

#endif

#endif	// U_FREETYPE


/*
Render text to a new chunk of memory. NULL return = failure, otherwise points to memory.
int characters required to print unicode strings correctly.
*/
unsigned char *mt_text_render(
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
#if U_FREETYPE
	unsigned char	*mem=NULL;
#ifdef WIN32
	const char	*txtp1[1];
#else
	char		*txtp1[1];
#endif
	char		*txtp2[1];
	double		angle_r = angle / 180 * M_PI;
	int		n, minx, miny, maxx, maxy, bx, by, bw=0, bh=0,
			pass, ppb=1,
			fix_w=0, fix_h=0, scalable, Y1=0, Y2=0;
	size_t		ssize1 = characters, ssize2 = ssize1*4+5, s;
	iconv_t		cd;
	FT_Library	library;
	FT_Face		face;
	FT_Matrix	matrix;
	FT_Vector	pen;
	FT_Error	error;
	FT_UInt		glyph_index;
	FT_Int32	load_flags = FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT, *txt2;

//printf("\n%s %i %s %s %f %i %f %i\n", text, characters, filename, encoding, size, face_index, angle, flags);

	if ( flags & MT_TEXT_MONO ) load_flags |= FT_LOAD_TARGET_MONO;

	if ( ssize1<1 ) return NULL;

	error = FT_Init_FreeType( &library );
	if (error) return NULL;

	error = FT_New_Face( library, filename, face_index, &face );
	if (error) goto fail2;

	scalable = FT_IS_SCALABLE(face);

	if ( !scalable )
	{
		fix_w = face->available_sizes[0].width;
		fix_h = face->available_sizes[0].height;

		error = FT_Set_Pixel_Sizes(face, fix_w, fix_h);
	}
	else
	{
		if ( flags & MT_TEXT_OBLIQUE )
		{
			matrix.xx = (FT_Fixed)( (cos( angle_r )) * 0x10000L );
			matrix.xy = (FT_Fixed)( (0.25*cos(angle_r) - sin( angle_r )) * 0x10000L );
			matrix.yx = (FT_Fixed)( (sin( angle_r )) * 0x10000L );
			matrix.yy = (FT_Fixed)( (0.25*sin(angle_r) + cos( angle_r )) * 0x10000L );
		}
		else
		{
			matrix.xx = (FT_Fixed)( cos( angle_r ) * 0x10000L );
			matrix.xy = (FT_Fixed)(-sin( angle_r ) * 0x10000L );
			matrix.yx = (FT_Fixed)( sin( angle_r ) * 0x10000L );
			matrix.yy = (FT_Fixed)( cos( angle_r ) * 0x10000L );
		}

		error = FT_Set_Char_Size( face, size*64, 0, 0, 0 );
	}

	if (error) goto fail1;

	txt2 = calloc(1, ssize2);
	if (!txt2) goto fail1;

	txtp1[0] = text;
	txtp2[0] = (char *)txt2;

//	FT_Select_Charmap( face, FT_ENCODING_UNICODE );
	FT_Set_Charmap( face, face->charmaps[0] );	// Needed to make dingbat fonts work

	/* Convert input string to UTF-32, using native byte order */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	cd = iconv_open("UTF-32LE", encoding);
#else /* G_BYTE_ORDER == G_BIG_ENDIAN */
	cd = iconv_open("UTF-32BE", encoding);
#endif

	if ( cd == (iconv_t)(-1) ) goto fail0;

	s = iconv(cd, &txtp1[0], &ssize1, &txtp2[0], &ssize2);
	iconv_close(cd);
	if (s) goto fail0;

	minx=miny=2<<24; maxx=maxy=-(2<<24);

	for ( pass = 0; pass<2; pass++ )
	{
		pen.x = 0;
		pen.y = 0;
		ssize1 = characters;

		for ( n=0; ssize1>0 ; n+=1, ssize1-- )
		{
			if (txt2[n] == 0) break;
			glyph_index = FT_Get_Char_Index( face, txt2[n] );

			if ( scalable )		// Cannot rotate fixed fonts
			{
				FT_Set_Transform( face, &matrix, &pen );
			}

			error = FT_Load_Glyph( face, glyph_index, load_flags );
			if ( error ) continue;

			switch ( face->glyph->bitmap.pixel_mode )
			{
				case FT_PIXEL_MODE_GRAY2:	ppb=2; break;
				case FT_PIXEL_MODE_GRAY4:	ppb=4; break;
				case FT_PIXEL_MODE_MONO:	ppb=8; break;
			}

			if ( scalable )			// Scalable fonts
			{
				bx = face->glyph->bitmap_left;
				by = -face->glyph->bitmap_top;
				bw = face->glyph->bitmap.width;
				bh = face->glyph->bitmap.rows;

				if ( pass == 0 && bw>0 && bh>0)	// Only do this if there is a bitmap
				{
					if ( bx < minx ) minx = bx;
					if ( by < miny ) miny = by;
					if ( bx + bw - 1 > maxx ) maxx = bx + bw - 1;
					if ( by + bh - 1 > maxy ) maxy = by + bh - 1;
				}
				if ( pass == 0 && !(flags & MT_TEXT_SHRINK) && n == 0 )
				{
/*
Note: This is all my own trig work, so I can't be sure than FreeType will be using
the same methods.  Therefore we must always use the shrinked values as a minimum
safety net to avoid any segfaults due to chopping boundaries too harshly.
MT 19-8-2007
*/
					Y1 = FT_MulFix(face->ascender, face->size->metrics.y_scale);
					Y2 = FT_MulFix(face->descender, face->size->metrics.y_scale);

					abcd_calc( pen, Y1, Y2, angle_r,
						face->glyph->metrics.horiBearingX,
						&minx, &miny, &maxx, &maxy );
				}

				pen.x += face->glyph->advance.x;
				pen.y += face->glyph->advance.y;
			}
			else					// Bitmap fonts
			{
				bx = pen.x + face->glyph->bitmap_left;
				by = pen.y - face->glyph->bitmap_top;
				bw = face->glyph->bitmap.width;
				bh = face->glyph->bitmap.rows;

				if ( pass==0 && (!(flags & MT_TEXT_SHRINK) || (bw>0 && bh>0)) )
				{
					if ( bx < minx ) minx = bx;
					if ( by < miny ) miny = by;
					if ( bx + bw - 1 > maxx ) maxx = bx + bw - 1;
					if ( by + bh - 1 > maxy ) maxy = by + bh - 1;
				}

				pen.x += (face->glyph->advance.x >> 6);
				pen.y += (face->glyph->advance.y >> 6);
			}

			if (pass==1)		// Draw bitmap onto clipboard memory in pass 1
			{
				ft_draw_bitmap( mem, *width, &face->glyph->bitmap,
							bx-minx, by-miny, ppb );
			}
		}

		if ( pass==0 )		// Set up new clipboard
		{
			if ( scalable )
			{
				if ( !(flags & MT_TEXT_SHRINK) && face->glyph )
				{
					pen.x -= face->glyph->advance.x;
					pen.y -= face->glyph->advance.y;

					abcd_calc( pen, Y1, Y2, angle_r,
						face->glyph->metrics.horiBearingX +
						face->glyph->metrics.width-64,
						&minx, &miny, &maxx, &maxy );
				}
			}
			else
			{
				if (!(flags & MT_TEXT_SHRINK) && !FT_IS_FIXED_WIDTH(face))
				{
/*
Note: Some bozo decided that bitmap fonts would not have ascent/descent values
in the face structure, so therefore we have a major headache dealing with these
fonts when we want a full vertical ascent/descent render.  Fixed width fonts seem
OK as they are rendered in their entirity, but variable width bitmap fonts cannot
accurately be rendering with ascent/descent.  My solution here is to multiply the
vertical size by 1.2 and assume that the descent is 25% of the ascent.
MT 19-8-2007
*/
					by = -(1.2 * fix_h) * 0.8 - 0.5;	// Ascent
					if (by<miny) miny=by;
					by = (1.2 * fix_h) * 0.2 + 0.5;		// Descent
					if (by>maxy) maxy=by;
				}
			}

			*width = maxx - minx + 1;
			*height = maxy - miny + 1;
			if ( (mem = calloc(1, (*width)*(*height)) ) )
			{
						// Memory prepared OK
			}
			else
			{
				pass=10;	// Allocation failed so bail out
			}
		}
	}

	if ( face && !scalable && (angle!=0 || size>=2) )	// Rotate/Scale the bitmap font
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
#else
	return NULL;
#endif		// U_FREEPTYPE
}


#ifdef U_FREETYPE
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

#include <dirent.h>
#include <sys/stat.h>



static void trim_tab( char *buf, char *txt)
{
	char *st;

	buf[0] = 0;
	if (txt) strncpy0(buf, txt, MAXLEN);
	for ( st=buf; st[0]!=0; st++ ) if ( st[0]=='\t' ) st[0]=' ';
	if ( buf[0] == 0 ) snprintf(buf, MAXLEN, "_None");
}

static int font_dir_search( FT_Library *ft_lib, int dirnum, FILE *fp, char *dir )
{	// Search given directory for font files - recursively traverse directories
	FT_Face		face;
	FT_Error	error;
	DIR		*dp;
	struct dirent	*ep;
	struct stat	buf;
	char		full_name[PATHBUF], size_type[MAXLEN], tmp[2][MAXLEN];
	int		face_index;


	dp = opendir(dir);
	if (!dp) return 0;

	while ( (ep = readdir(dp)) )
	{
		snprintf(full_name, PATHBUF, "%s%c%s", dir, DIR_SEP, ep->d_name);

		if ( stat(full_name, &buf)<0 ) continue;	// Get file details

#ifdef WIN32
		if ( S_ISDIR(buf.st_mode) )
#else
		if ( ep->d_type == DT_DIR || S_ISDIR(buf.st_mode) )
#endif
		{				// Subdirectory so recurse
			if ( strcmp(ep->d_name, ".") != 0 && strcmp(ep->d_name, "..") != 0 )
			{
				font_dir_search( ft_lib, dirnum, fp, full_name );
			}
		}
		else		// File so see if its a font
		for (	face_index = 0;
			!(error = FT_New_Face( *ft_lib, full_name, face_index, &face ));
			face_index++ )
		{
			if ( FT_IS_SCALABLE( face ) )
				snprintf(size_type, MAXLEN, "0");
			else
				snprintf(size_type, MAXLEN, "%i",
					face->available_sizes[0].height +
					(face->available_sizes[0].width << SIZE_SHIFT) +
					(face_index << (SIZE_SHIFT*2))
					);

// I use a tab character as a field delimeter, so replace any in the strings with a space

			trim_tab( tmp[0], face->family_name );
			trim_tab( tmp[1], face->style_name );

			fprintf(fp, "%s\t%i\t%s\t%s\t%s\n",
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

	return 0;		// No error
}

static int font_index_create(char *filename, char **dir_in)
{	// dir_in points to NULL terminated sequence of directories to search for fonts
	FT_Library	library;
	FT_Error	error;
	int		i, res=-1;
	FILE		*fp;


	error = FT_Init_FreeType( &library );
	if (error) return -1;

	if ((fp = fopen(filename, "w")) == NULL) goto fail;

	for ( i=0, res=0; dir_in[i] && !res; i++ )
	{
		res = font_dir_search( &library, i, fp, dir_in[i] );
	}

	fclose(fp);
fail:
	FT_Done_FreeType(library);

	return res;
}


static void font_mem_clear()		// Empty whole structure from memory
{
	struct fontNODE		*fo, *fo2;
	struct styleNODE	*sn, *sn2;
	struct sizeNODE		*zn, *zn2;
	struct filenameNODE	*fl, *fl2;

	free(font_text); font_text = NULL;
	for (fo = global_font_node; fo; fo = fo2)
	{
		for (sn = fo->style; sn; sn = sn2)
		{
			for (zn = sn->size; zn; zn = zn2)
			{
				for (fl = zn->filename; fl; fl = fl2)
				{
					fl2 = fl->next;
					free( fl );
				}
				zn2 = zn->next;
				free( zn );
			}
			sn2 = sn->next;
			free( sn );
		}
		fo2 = fo->next;
		free( fo );
	}
	global_font_node = NULL;
}

static int font_mem_add(char *font, int dirn, char *style, int fsize,
	char *filename)
{// Add new font data to memory structure. Returns TRUE if successful.
	int			bm_index, res = FALSE;
	struct fontNODE		*fo;
	struct styleNODE	*st;
	struct sizeNODE		*ze;
	struct filenameNODE	*fl;


	bm_index = fsize >> SIZE_SHIFT * 2;
	fsize &= (1 << SIZE_SHIFT * 2) - 1;

	for (fo = global_font_node; fo; fo = fo->next)
	{
		if (!strcmp(fo->font_name, font) && (fo->directory == dirn))
			break;	// Font family+directory already exists
	}

	if (!fo)		// Set up new structure as no match currently exists
	{
		fo = calloc(1, sizeof(*fo) );	// calloc empties all records
		if (fo == NULL) goto fail;	// Memory failure
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
		st = calloc(1, sizeof(*st) );	// calloc empties all records
		if (st == NULL) goto fail;	// Memory failure
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
		ze = calloc(1, sizeof(*ze) );	// calloc empties all records
		if (ze == NULL) goto fail;	// Memory failure
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
	fl = calloc(1, sizeof(*fl));
	if (!fl) goto fail;

	fl->next = ze->filename;		// Old first filename (maybe NULL)
	ze->filename = fl;			// This is the new first filename

	fl->filename = filename;
	fl->face_index = bm_index;

	res = TRUE;
fail:	return (res);
}

static void font_index_load(char *filename)
{
	char *buf, *tmp, *tail, *slots[SLOT_TOT];
	int i, dir, size;


	font_text = slurp_file(filename);
	if (!font_text) return;

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
			break;
		}
	}
}

#if 0
static void font_index_display(struct fontNODE	*head)
{
	int			tot=0, families=0, styles=0, sizes=0, filenames=0;
	struct fontNODE		*fo = head;
	struct styleNODE	*st;
	struct sizeNODE		*ze;
	struct filenameNODE	*fl;


	while (fo)
	{
		printf("%s (%i)\n", fo->font_name, fo->directory);
		tot += strlen(fo->font_name) + 1;
		tot += sizeof(*fo);
		families ++;
		st = fo->style;
		while (st)
		{
			printf("\t%s\n", st->style_name);
			tot += strlen(st->style_name) + 1;
			tot += sizeof(*st);
			styles ++;
			ze = st->size;
			while (ze)
			{
				printf("\t\t%i x %i\n", ze->size % (1<<SIZE_SHIFT),
						(ze->size >> SIZE_SHIFT) % (1<<SIZE_SHIFT)
						);
				tot += sizeof(*ze);
				sizes ++;
				fl = ze->filename;
				while (fl)
				{
					printf("\t\t\t%3i %s\n", fl->face_index, fl->filename);
					tot += strlen(fl->filename) + 1;
					tot += sizeof(*fl);
					filenames++;
					fl = fl->next;
				}
				ze = ze->next;
			}
			st = st->next;
		}
		fo = fo->next;
	}
	printf("\nMemory Used\t%'i (%.1fK)\nFont Families\t%i\nFont Styles\t%i\nFont Sizes\t%i\nFont Filenames\t%i\n\n", tot, ((double)tot)/1024, families, styles, sizes, filenames);
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
#endif

#if GTK_MAJOR_VERSION == 2
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


static gint delete_text( GtkWidget *widget, gpointer data )
{
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(widget), "mtfontsel");


	if ( !fp ) fp = data;
	if ( !fp ) return FALSE;

	win_store_pos(fp->window, "paste_text_window");
	gtk_widget_destroy( fp->window );

	if (fp->preview_rgb) free(fp->preview_rgb);
	free(fp);

	return FALSE;
}

#endif		// U_FREETYPE

void ft_render_text()		// FreeType equivalent of render_text()
{
#ifdef U_FREETYPE
	unsigned char *text_1bpp;
	int w=1, h=1;


	text_1bpp = render_to_1bpp(&w, &h);

	if (text_1bpp && make_text_clipboard(text_1bpp, w, h, 1))
		text_paste = TEXT_PASTE_FT;
	else text_paste = TEXT_PASTE_NONE;
#endif		// U_FREETYPE
}

#ifdef U_FREETYPE
static void font_clist_centralise(mtfontsel *mem)
{
	int i;

	for ( i=0; i<FONTSEL_CLISTS; i++ )	// Ensure selections are visible and central
	{
		if ( GTK_CLIST(mem->clist[i])->selection )
			gtk_clist_moveto( GTK_CLIST(mem->clist[i]),
				(int)(GTK_CLIST(mem->clist[i])->selection->data), 0, 0.5, 0);
	}
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
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(widget), "mtfontsel");


	if ( !fp ) return FALSE;

	read_font_controls(fp);
	ft_render_text();
	if (mem_clipboard) pressed_paste(TRUE);

	delete_text( widget, data );

	return FALSE;
}

static void font_clist_adjust_cols(mtfontsel *mem, int cl)
{
	int i;

 	for ( i=0; i<FONTSEL_CLISTS_MAXCOL; i++ )		// Adjust column widths for new data
	{
		gtk_clist_set_column_width( GTK_CLIST(mem->clist[cl]), i,
			5 + gtk_clist_optimal_column_width( GTK_CLIST(mem->clist[cl]), i ) );
	}
}

static void populate_font_clist( mtfontsel *mem, int cl )
{
	int i, j, row, select_row = -1, real_size = 0;
	char txt[32], buf[128], buf2[256];
	gchar *row_text[FONTSEL_CLISTS_MAXCOL] = {NULL, NULL, NULL};


	gtk_clist_freeze( GTK_CLIST(mem->clist[cl]) );
	gtk_clist_clear( GTK_CLIST(mem->clist[cl]) );

	if ( cl == CLIST_FONTNAME )
	{
		struct fontNODE *fn = mem->head_node;
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
			row = gtk_clist_append( GTK_CLIST(mem->clist[cl]), row_text );
			gtk_clist_set_row_data( GTK_CLIST(mem->clist[cl]), row, (gpointer)fn );

			if ( !strcmp(fn->font_name, last_font_name) &&
				last_font_name_dir == fn->directory &&
				last_font_name_bitmap == bitmap_font
				)
					select_row = row;

			fn = fn->next;
		}
	}
	if ( cl == CLIST_FONTSTYLE )
	{
		static const char *default_styles[] =
			{ "Regular", "Medium", "Book", "Roman", NULL };
		char *last_font_style = inifile_get( "lastFontStyle", "" );
		struct styleNODE *sn = mem->current_style_node;
		int default_row = -1;

		while (sn)
		{
			gtkuncpy(buf2, sn->style_name, 256);	// Transfer to UTF-8 in GTK+2
			row_text[0] = buf2;
			row = gtk_clist_append( GTK_CLIST(mem->clist[cl]), row_text );
			gtk_clist_set_row_data( GTK_CLIST(mem->clist[cl]), row, (gpointer)sn );

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
	if ( cl == CLIST_FONTSIZE )
	{
		struct sizeNODE *zn = mem->current_size_node;
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
				row = gtk_clist_append( GTK_CLIST(mem->clist[cl]), row_text );
				gtk_clist_set_row_data( GTK_CLIST(mem->clist[cl]), row, (gpointer)zn );
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
			row = gtk_clist_append( GTK_CLIST(mem->clist[cl]), row_text );
			gtk_clist_set_row_data( GTK_CLIST(mem->clist[cl]), row, (gpointer)zn );

			if ( old_bitmap_geometry == zn->size ) select_row = row;

			zn = zn->next;
		}
	}
	if ( cl == CLIST_FONTFILE )
	{
		struct filenameNODE *fn = mem->current_filename_node;
		char *s, *last_filename = inifile_get( "lastTextFilename", "" );

		while (fn)
		{
			s = strrchr(fn->filename, DIR_SEP);
			if (!s) s = fn->filename; else s++;
			gtkuncpy(buf2, s, 256);			// Transfer to UTF-8 in GTK+2
			snprintf(txt, 32, "%3i", fn->face_index);
			row_text[0] = buf2;
			row_text[1] = txt;
			row = gtk_clist_append( GTK_CLIST(mem->clist[cl]), row_text );
			gtk_clist_set_row_data( GTK_CLIST(mem->clist[cl]), row, (gpointer)fn );

			if ( !strcmp(fn->filename, last_filename) ) select_row = row;

			fn = fn->next;
		}
	}
	if ( cl == CLIST_DIRECTORIES )
	{
		j = inifile_get_gint32("font_dirs", 0 );
		for ( i=0; i<j; i++ )
		{
			snprintf(buf, 128, "font_dir%i", i);
			snprintf(txt, 32, "%3i", i+1);
			gtkuncpy(buf2, inifile_get( buf, "" ), 256);	// Transfer to UTF-8 in GTK+2
			row_text[0] = txt;
			row_text[1] = buf2;
			gtk_clist_append( GTK_CLIST(mem->clist[cl]), row_text );
//printf("%s %s\n", row_text[0], row_text[1]);
		}
	}

	font_clist_adjust_cols(mem, cl);

	if ( select_row>=0 )	// Select chosen item _before_ the sort
	{
		gtk_clist_select_row( GTK_CLIST(mem->clist[cl]), select_row, 0 );
		gtk_clist_sort( GTK_CLIST(mem->clist[cl]) );
//printf("current selection = %i\n", (int) (GTK_CLIST(mem->clist[cl])->selection->data) );
	}
	else		// Select 1st item _after_ the sort
	{
		gtk_clist_sort( GTK_CLIST(mem->clist[cl]) );
		gtk_clist_select_row( GTK_CLIST(mem->clist[cl]), 0, 0 );
	}

	gtk_clist_thaw( GTK_CLIST(mem->clist[cl]) );

	if ( GTK_CLIST(mem->clist[cl])->selection )
		gtk_clist_moveto( GTK_CLIST(mem->clist[cl]),
			(int)(GTK_CLIST(mem->clist[cl])->selection->data), 0, 0.5, 0);
				// Needs to be after thaw

	if ( real_size )
		gtk_spin_button_set_value( GTK_SPIN_BUTTON( mem->spin[TX_SPIN_SIZE] ), real_size );
// This hack is needed to ensure any scalable size that is not in clist is correctly chosen
}


static gint click_add_font_dir( GtkWidget *widget, gpointer user )
{
	int i = inifile_get_gint32("font_dirs", 0 );
	char txt[PATHBUF], buf[32];
	gchar *row_text[FONTSEL_CLISTS_MAXCOL] = {NULL, NULL, NULL};
	mtfontsel *fp = user;


	if ( !fp ) return FALSE;

	row_text[1] = (gchar *)gtk_entry_get_text( GTK_ENTRY(fp->entry[TX_ENTRY_DIRECTORY]) );
	gtkncpy( txt, row_text[1], PATHBUF);

	if ( strlen(txt)>0 && i<TX_MAX_DIRS )
	{
		snprintf(buf, 128, "font_dir%i", i);
		inifile_set( buf, txt );

		snprintf(buf, 32, "%3i", i+1);
		row_text[0] = buf;
		gtk_clist_append( GTK_CLIST(fp->clist[CLIST_DIRECTORIES]), row_text );

		i++;
		inifile_set_gint32("font_dirs", i );
	}

	return FALSE;
}

static gint click_remove_font_dir( GtkWidget *widget, gpointer user )
{
	char txt[32], txt2[32];
	int i, delete_row = -1, row_tot = inifile_get_gint32("font_dirs", 0 );
	mtfontsel *fp = user;


	if ( !fp ) return FALSE;

	if ( GTK_CLIST(fp->clist[CLIST_DIRECTORIES])->selection )
		delete_row = (int)(GTK_CLIST(fp->clist[CLIST_DIRECTORIES])->selection->data);

	if ( delete_row<0 || delete_row>=row_tot ) return FALSE;

	gtk_clist_remove( GTK_CLIST(fp->clist[CLIST_DIRECTORIES]), delete_row );
		// Delete current row in clist

	for ( i=delete_row; i<(row_tot-1); i++)
	{
			// Re-number clist numbers in first column
		snprintf(txt, 32, "%3i", i+1);
		gtk_clist_set_text( GTK_CLIST(fp->clist[CLIST_DIRECTORIES]), i, 0, txt );

			// Re-work inifile items
		snprintf(txt, 32, "font_dir%i", i);
		snprintf(txt2, 32, "font_dir%i", i+1);
		inifile_set( txt, inifile_get(txt2, "") );
	}

	snprintf(txt, 32, "font_dir%i", row_tot-1);	// Flush last (unwanted) item inifile
	inifile_set( txt, "" );
	inifile_get( txt, "" );

	inifile_set_gint32("font_dirs", row_tot-1 );

	return FALSE;
}

static gint click_create_font_index( GtkWidget *widget, gpointer user )
{
	mtfontsel *fp = user;


	if ( !fp ) return FALSE;

	if ( inifile_get_gint32("font_dirs", 0 ) > 0 )
	{
		int i;
		char txt[PATHBUF];

		snprintf(txt, PATHBUF, "%s%c%s", get_home_directory(),
			DIR_SEP, FONT_INDEX_FILENAME);
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
	else
	{
		alert_box(_("Error"), _("You must select at least one directory to search for fonts."), NULL);
	}

	return FALSE;
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

static void font_clist_select_row( GtkWidget *clist, gint row, gint column, GdkEvent *event, gpointer user)
{
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(clist), "mtfontsel");
	int cl = (int) user;


//printf("fp = %i row = %i user = %i\n", (int) fp, row, (int) user);
	if ( !fp || cl<0 || cl>=FONTSEL_CLISTS ) return;

	if ( cl == CLIST_FONTNAME )
	{
		struct fontNODE *fn = gtk_clist_get_row_data( GTK_CLIST(clist), row );

		if ( fn )
		{
			int bitmap_font = !!fn->style->size->size;

			inifile_set_gboolean( "fontTypeBitmap", bitmap_font );
			gtk_widget_set_sensitive( fp->toggle[TX_TOGG_OBLIQUE],
				!bitmap_font );

			silent_update_size_spin( fp );			// Update size spin

			fp->current_style_node = fn->style;		// New style head node
//			fp->current_size_node = NULL;			// Now invalid
//			fp->current_filename_node = NULL;		// Now invalid
			populate_font_clist( fp, CLIST_FONTSTYLE );	// Update style list
			inifile_set( "lastFontName", fn->font_name );
			inifile_set_gint32( "lastFontNameDir", fn->directory );
			inifile_set_gint32( "lastFontNameBitmap", bitmap_font );
		}
	}
	if ( cl == CLIST_FONTSTYLE )
	{
		struct styleNODE *sn = gtk_clist_get_row_data( GTK_CLIST(clist), row );

		if ( sn )
		{
			fp->current_size_node = sn->size;		// New size head node
//			fp->current_filename_node = NULL;		// Now invalid

			populate_font_clist( fp, CLIST_FONTSIZE );
			inifile_set( "lastFontStyle", sn->style_name );
		}
	}
	if ( cl == CLIST_FONTSIZE )
	{
		int i, j;
		struct sizeNODE *zn = gtk_clist_get_row_data( GTK_CLIST(clist), row );
		gchar *celltext;

		if ( zn )
		{
			i = gtk_clist_get_text(GTK_CLIST(clist), row, 0, &celltext);
			if ( zn->size == 0 )		// Scalable so remember size
			{
				if (i)
				{

					sscanf(celltext, "%i", &j);
					inifile_set_gint32( "fontSize", j );

					silent_update_size_spin( fp );		// Update size spin
				}
			}
			if ( zn->size != 0 )		// Non-Scalable so remember index
			{
				if (i)
				{
					inifile_set_gint32( "lastfontBitmapGeometry", zn->size );
				}
			}
			fp->current_filename_node = zn->filename;	// New filename head node
			populate_font_clist( fp, CLIST_FONTFILE );	// Update filename list
		}
	}
	if ( cl == CLIST_FONTFILE )
	{
		struct filenameNODE *fn = gtk_clist_get_row_data( GTK_CLIST(clist), row );

		if ( fn )
		{
			inifile_set( "lastTextFilename", fn->filename );
			inifile_set_gint32( "lastTextFace", fn->face_index );
			font_preview_update( fp );		// Update the font preview area
		}
	}
}


static void font_clist_column_button( GtkWidget *widget, gint col, gpointer user)
{
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(widget), "mtfontsel");
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

static void add_font_clist(int i, GtkWidget *hbox, mtfontsel *mem, int padding)
{
	int	j, clist_text_cols[FONTSEL_CLISTS] = { 3, 1, 1, 2, 2 };
	char	*clist_text_titles[FONTSEL_CLISTS][FONTSEL_CLISTS_MAXCOL] = {
			{ "", "", _("Font") },
			{ _("Style"), "", "" },
			{ _("Size"), "", "" },
			{ _("Filename"), _("Face"), "" },
			{ "", _("Directory"), "" }
			};
	GtkWidget	*scrolledwindow, *temp_hbox, *temp_label;


	scrolledwindow = gtk_scrolled_window_new (NULL, NULL);

	if ( i == CLIST_FONTNAME ) j = GTK_POLICY_NEVER;
	else j= GTK_POLICY_AUTOMATIC;

	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scrolledwindow),
		j, GTK_POLICY_AUTOMATIC);

	mem->clist[i] = gtk_clist_new( clist_text_cols[i] );
	gtk_object_set_data( GTK_OBJECT(mem->clist[i]), "mtfontsel", mem );

#if 0
	if ( i == CLIST_FONTSIZE || i == CLIST_FONTSTYLE )
		pack5(hbox, scrolledwindow);
	else
#endif
		gtk_box_pack_start (GTK_BOX (hbox), scrolledwindow, TRUE, TRUE, padding);

	if ( i == CLIST_FONTSTYLE )
		gtk_widget_set_usize(mem->clist[i], 100, -2);

	for (j = 0; j < clist_text_cols[i]; j++)
	{
		temp_hbox = gtk_hbox_new( FALSE, 0 );
		temp_label = pack(temp_hbox, gtk_label_new(clist_text_titles[i][j]));

		if (i == CLIST_FONTNAME) mem->sort_arrows[j] =
			pack_end(temp_hbox, gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_IN));

		gtk_widget_show( temp_label );
		gtk_widget_show( temp_hbox );
		gtk_clist_set_column_widget( GTK_CLIST( mem->clist[i] ), j, temp_hbox );
		gtk_clist_set_column_resizeable( GTK_CLIST(mem->clist[i]), j, FALSE );
		if (i == CLIST_FONTSIZE)
			gtk_clist_set_column_justification(
				GTK_CLIST(mem->clist[i]), j, GTK_JUSTIFY_CENTER);
	}

	if ( i == CLIST_FONTNAME )
	{
		mem->sort_column = 2;
		gtk_widget_show( mem->sort_arrows[mem->sort_column] );	// Show sort arrow
		gtk_clist_set_sort_column( GTK_CLIST(mem->clist[i]), mem->sort_column );
		gtk_arrow_set(GTK_ARROW( mem->sort_arrows[mem->sort_column] ),
			( mem->sort_direction == GTK_SORT_ASCENDING ?
				GTK_ARROW_DOWN : GTK_ARROW_UP), GTK_SHADOW_IN);
		gtk_signal_connect(GTK_OBJECT(mem->clist[i]), "click_column",
				GTK_SIGNAL_FUNC(font_clist_column_button), (gpointer)i);
	}
	else
	{
#if GTK_MAJOR_VERSION == 2
		gtk_clist_column_titles_passive(GTK_CLIST(mem->clist[i]));
#endif
#if GTK_MAJOR_VERSION == 1
		for (j = 0; j < clist_text_cols[i]; j++)
			gtk_clist_column_title_passive( GTK_CLIST(mem->clist[i]), j );
#endif
	}

	gtk_clist_column_titles_show( GTK_CLIST(mem->clist[i]) );
	gtk_clist_set_selection_mode( GTK_CLIST(mem->clist[i]), GTK_SELECTION_BROWSE );

	gtk_container_add(GTK_CONTAINER( scrolledwindow ), mem->clist[i]);

	gtk_signal_connect(GTK_OBJECT(mem->clist[i]), "select_row",
		GTK_SIGNAL_FUNC(font_clist_select_row), (gpointer)i);
}


static void init_font_lists()		//	LIST INITIALIZATION
{
	char *root = get_home_directory(), txt[PATHBUF];

	snprintf(txt, PATHBUF, "%s%c%s", root, DIR_SEP, FONT_INDEX_FILENAME);

	font_index_load(txt);	// Does a valid ~/.mtpaint_fonts index exist?

	if ( !global_font_node )		// Index file not loaded
	{
		if ( inifile_get_gint32("font_dirs", 0 ) > 0 )
			font_gui_create_index(txt);	// We have directories so create them
		else					// We don't have any directories so get them
		{
			int new_dirs = 0;
#ifdef WIN32
			static char *windir = NULL;
			char buf[PATHBUF];

			windir = getenv("WINDIR");
			if (!windir) windir = "C:\\WINDOWS";	// Fallback
			snprintf(buf, PATHBUF, "%s%c%s", windir, DIR_SEP, "Fonts");

			inifile_set( "font_dir0", buf );

			new_dirs = 1;
#else
			FILE *fp;
			char buf[4096], buf2[128], *s;

			if ( (fp = fopen("/etc/X11/xorg.conf", "r") ) == NULL)
				fp = fopen("/etc/X11/XF86Config", "r");

			// If these files are not found the user will have to manually enter directories

			if ( fp )
			{
				while ( new_dirs<TX_MAX_DIRS && fgets(buf, 4090, fp) )
				{
					buf[4090] = 0;

					s = strstr(buf, "FontPath");
					if (!s) continue;

					s = strrchr(buf, '"');
					if (!s) continue;

					s[0] = 0;
					s = strchr(buf, '"');
					if (!s) continue;

					snprintf(buf2, 128, "font_dir%i", new_dirs);
					inifile_set( buf2, s+1 );
					new_dirs++;
				}
				fclose(fp);
			}

			if (new_dirs == 0 && (fp = fopen("/etc/fonts/fonts.conf", "r")) )
			{
				char *s1, *s2;

				while ( new_dirs<TX_MAX_DIRS && fgets(buf, 4090, fp) )
				{
					buf[4090] = 0;
					s1 = buf;

					while ( (s=strstr(s1, "</dir>")) )
					{
						s[0] = 0;
						s2 = s1;
						s1 = s+5;

						s = strstr(s2, "<dir>");
						if (!s) continue;
						s += 5;

						snprintf(buf2, 128, "font_dir%i", new_dirs);
						inifile_set( buf2, s );
						new_dirs++;
					}
				}
				fclose(fp);
			}
#endif
			if (new_dirs > 0)
			{
				inifile_set_gint32("font_dirs", new_dirs );
				font_gui_create_index(txt);
			}
		}
		font_index_load(txt);	// Try for second and last time
	}
}


static gint preview_expose_event( GtkWidget *widget, GdkEventExpose *event )
{
	unsigned char *rgb;
	int x = event->area.x, y = event->area.y;
	int w = event->area.width, h = event->area.height;
	mtfontsel *fp = gtk_object_get_data(GTK_OBJECT(widget), "mtfontsel");


	if ( !fp || w<1 || h<1 ) return FALSE;

	rgb = malloc( w*h*3 );
	if ( !rgb ) return FALSE;
	memset( rgb, inifile_get_gint32("backgroundGrey", 180), w*h*3 );

	if ( fp->preview_rgb )
	{
		unsigned char *src, *dest;
		int w2, h2, j;

		if ( x < fp->preview_w && y < fp->preview_h )
		{
			mtMIN( w2, w, fp->preview_w - x )
			mtMIN( h2, h, fp->preview_h - y )

			for ( j=0; j<h2; j++ )
			{
				src = fp->preview_rgb + 3*(x + fp->preview_w*(y+j));
				dest = rgb + 3*w*j;
				memcpy(dest, src, w2 * 3);
			}
		}
	}

	gdk_draw_rgb_image ( fp->preview_area->window,
			fp->preview_area->style->black_gc,
			x, y, w, h,
			GDK_RGB_DITHER_NONE, rgb, w*3
			);

	free(rgb);

	return FALSE;
}

static void font_entry_changed(GtkWidget *widget, gpointer user)	// A GUI entry has changed
{
	mtfontsel *fp = user;


	if (!fp) return;
	read_font_controls(fp);
	font_preview_update( fp );		// Update the font preview area
}
#endif		// U_FREETYPE


void pressed_mt_text()
{
#ifdef U_FREETYPE
	int		i;
	mtfontsel	*mem;
	GtkWidget	*vbox, *vbox2, *hbox, *notebook, *page, *scrolledwindow, *button;
	GtkAccelGroup* ag = gtk_accel_group_new();


	if ( !global_font_node ) init_font_lists();

	mem = calloc(1, sizeof(mtfontsel));
	mem->head_node = global_font_node;

	mem->window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Paste Text"), GTK_WIN_POS_NONE, TRUE );
//	gtk_window_set_default_size( GTK_WINDOW(mem->window), 400, 450 );
	win_restore_pos(mem->window, "paste_text_window", 0, 0, 400, 450);

	gtk_object_set_data( GTK_OBJECT(mem->window), "mtfontsel", mem );
//printf("mem = %i\n", (int)mem);

	notebook = gtk_notebook_new();
	gtk_container_add (GTK_CONTAINER (mem->window), notebook);
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);

//	TAB 1 - TEXT

	page = add_new_page(notebook, _("Text"));

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (page), hbox);

	vbox = pack5(hbox, gtk_vbox_new(FALSE, 0));

	add_font_clist(0, vbox, mem, 5);

	vbox = xpack5(hbox, gtk_vbox_new(FALSE, 0));
	hbox = xpack5(vbox, gtk_hbox_new(FALSE, 0));

	add_font_clist(CLIST_FONTSTYLE, hbox, mem, 5);

	vbox2 = xpack(hbox, gtk_vbox_new(FALSE, 0));
	add_font_clist(CLIST_FONTSIZE, vbox2, mem, 0);
	mem->spin[TX_SPIN_SIZE] = pack(vbox2,
		add_a_spin(inifile_get_gint32("fontSize", 12), 1, 500));
#if GTK_CHECK_VERSION(2,4,0)
	gtk_entry_set_alignment( GTK_ENTRY(&(GTK_SPIN_BUTTON( mem->spin[TX_SPIN_SIZE] )->entry)), 0.5);
#endif

	add_font_clist(CLIST_FONTFILE, hbox, mem, 5);

//	Text entry box

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	add_with_frame(vbox, _("Text"), hbox, 5);
	mem->entry[TX_ENTRY_TEXT] = xpack(hbox, gtk_entry_new());
	gtk_entry_set_text (GTK_ENTRY (mem->entry[TX_ENTRY_TEXT]),
		inifile_get( "textString", _("Enter Text Here") ) );
	gtk_signal_connect(GTK_OBJECT(mem->entry[TX_ENTRY_TEXT]), "changed",
		GTK_SIGNAL_FUNC(font_entry_changed), (gpointer)mem);


//	PREVIEW AREA

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	add_with_frame_x(vbox, _("Preview"), hbox, 5, TRUE);

	scrolledwindow = xpack(hbox, gtk_scrolled_window_new(NULL, NULL));
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	mem->preview_area = gtk_drawing_area_new();
	gtk_object_set_data( GTK_OBJECT(mem->preview_area), "mtfontsel", mem );
	gtk_drawing_area_size(GTK_DRAWING_AREA (mem->preview_area), 1, 1);
	gtk_scrolled_window_add_with_viewport( GTK_SCROLLED_WINDOW(scrolledwindow), mem->preview_area);
	gtk_widget_show( mem->preview_area );

	gtk_signal_connect(GTK_OBJECT(mem->preview_area), "expose_event",
		GTK_SIGNAL_FUNC(preview_expose_event), NULL);

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

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (page), vbox);

	add_font_clist(CLIST_DIRECTORIES, vbox, mem, 5);

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

	gtk_window_add_accel_group(GTK_WINDOW (mem->window), ag);
	gtk_widget_show_all(mem->window);

	font_clist_centralise(mem);				// Ensure each list is shown properly

	if (mem_img_bpp == 1) gtk_widget_hide(mem->toggle[TX_TOGG_ANTIALIAS]);
	gtk_window_set_transient_for( GTK_WINDOW(mem->window), GTK_WINDOW(main_window) );

	gtk_widget_grab_focus( mem->entry[TX_ENTRY_TEXT] );
//font_index_display(mem->head_node);
#endif		// U_FREETYPE
}
