/*	font.c
	Copyright (C) 2007-2013 Mark Tyler and Dmitry Groshev

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
#undef _
#define _(X) X

#include "mygtk.h"
#include "memory.h"
#include "vcode.h"
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

#define FONT_INDEX_FILENAME ".mtpaint_fonts"

#define TX_MAX_DIRS 100

/* Persistent settings */
int font_obl, font_bmsize, font_size;
int font_dirs;


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


typedef struct {
	fontNODE *fn;
	int dir, bm, name;
} fontname_cc;

typedef struct {
	styleNODE *sn;
	int name;
} fontstyle_cc;

typedef struct {
	sizeNODE *zn;
	int what;
	int n;
} fontsize_cc;

typedef struct {
	filenameNODE *fn;
	int name, face;
} fontfile_cc;

typedef struct {
	int idx, dir;
} dir_cc;

typedef struct {
	char *text; // !!! widget-owned memory
	int img, idx;
	int fontname, nfontnames, fnsort;
	int fontstyle, nfontstyles;
	int lfontsize, nlfontsizes;
	int fontfile, nfontfiles;
	int dir, ndirs;
	int fontsize;
	int bkg[3];
	int lock;
	int preview_w, preview_h;
	GtkWidget *preview_area; // !!! for now
	unsigned char *preview_rgb;
	memx2 fnmmem, fstmem, fszmem, ffnmem, dirmem;
	fontname_cc *fontnames;
	fontstyle_cc *fontstyles;
	fontsize_cc *fontsizes;
	fontfile_cc *fontfiles;
	dir_cc *dirs;
	void **obl_c, **size_spin, **add_b;
	void **fname_l, **fstyle_l, **fsize_l, **ffile_l, **dir_l;
	char dirp[PATHBUF];
} font_dd;

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

typedef struct statchain statchain;
struct statchain {
	statchain *p;
	struct stat buf;
};

static void font_dir_search(FT_Library *ft_lib, int dirnum, FILE *fp, char *dir,
	statchain *cc)
{	// Search given directory for font files - recursively traverse directories
	statchain	sc = { cc };
	FT_Face		face;
	DIR		*dp;
	struct dirent	*ep;
	char		full_name[PATHBUF], tmp[2][MAXLEN];
	int		face_index;


	dp = opendir(dir);
	if (!dp) return;

	while ( (ep = readdir(dp)) )
	{
		file_in_dir(full_name, dir, ep->d_name, PATHBUF);

		if (stat(full_name, &sc.buf) < 0) continue;	// Get file details

#ifdef WIN32
		if (S_ISDIR(sc.buf.st_mode))
#else
		if ((ep->d_type == DT_DIR) || S_ISDIR(sc.buf.st_mode))
#endif
		{	// Subdirectory so recurse
			if (!strcmp(ep->d_name, ".") || !strcmp(ep->d_name, ".."))
				continue;
			/* If no inode number, assume it's Windows and just hope
			 * for the best: symlink loops do exist in Windows 7+, but
			 * I know of no simple approach for avoiding them - WJ */
			if (sc.buf.st_ino)
			{
				for (cc = sc.p; cc; cc = cc->p)
				if ((sc.buf.st_dev == cc->buf.st_dev) &&
					(sc.buf.st_ino == cc->buf.st_ino)) break;
				if (cc) continue; // Directory loop
			}
			font_dir_search( ft_lib, dirnum, fp, full_name, &sc );
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
	statchain	sc = { NULL };
	FT_Library	library;
	int		i;
	FILE		*fp;


	if (FT_Init_FreeType(&library)) return;

	if ((fp = fopen(filename, "w")))
	{
		for (i = 0; dir_in[i]; i++)
		{
			if (stat(dir_in[i], &sc.buf) < 0) continue;
			font_dir_search(&library, i, fp, dir_in[i], &sc);
		}
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


	size = inifile_get_gboolean("fontTypeBitmap", TRUE) ?
		font_bmsize : font_size;

	if ((mem_img_bpp == 1) || !font_aa)
	{
		flags |= MT_TEXT_MONO;
		flags |= MT_TEXT_ROTATE_NN;	// RGB image without AA = nearest neighbour rotation
	}
	if (font_obl) flags |= MT_TEXT_OBLIQUE;
	if (font_r) angle = font_angle / 100.0;

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

/* Re-render the preview text and update it */
static void font_preview_update(font_dd *dt)
{
	unsigned char *text_1bpp;
	int w=1, h=1;


	if (dt->preview_rgb)		// Remove old rendering
	{
		free(dt->preview_rgb);
		dt->preview_rgb = NULL;
	}

	text_1bpp = render_to_1bpp(&w, &h);

	if ( text_1bpp )
	{
		dt->preview_rgb = calloc( 1, 3*w*h );
		if (dt->preview_rgb)
		{
			int i, j = w*h;
			unsigned char *src = text_1bpp, *dest = dt->preview_rgb;

			for ( i=0; i<j; i++ )
			{
				dest[0] = dest[1] = dest[2] = *src++ ^ 255;
				dest += 3;
			}

			dt->preview_w = w;
			dt->preview_h = h;
		}
		free( text_1bpp );
//printf("font preview update %i x %i\n", w, h);
	}

	gtk_widget_set_usize(dt->preview_area, w, h);
	gtk_widget_queue_draw(dt->preview_area);
		// Show the world the fruits of my hard labour!  ;-)
// FIXME - GTK+1 has rendering problems when the area becomes smaller - old artifacts are left behind
}



static void font_gui_create_index(char *filename) // Create index file with settings from ~/.mtpaint
{
	char buf[128], *dirs[TX_MAX_DIRS + 1];
	int i;


	memset(dirs, 0, sizeof(dirs));
	for (i = 0; i < font_dirs; i++)
	{
		snprintf(buf, 128, "font_dir%i", i);
		dirs[i] = inifile_get( buf, "" );
	}

	progress_init( __("Creating Font Index"), 0 );
	font_index_create( filename, dirs );
	progress_end();
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

static void store_values(font_dd *dt)
{
	inifile_set("textString", dt->text);
	if (inifile_get_gboolean( "fontTypeBitmap", TRUE))
		font_bmsize = dt->fontsize;
	else font_size = dt->fontsize;
	if (mem_channel == CHN_IMAGE) font_bkg = dt->bkg[0];
}

static void paste_text_ok(font_dd *dt, void **wdata, int what, void **where)
{
	run_query(wdata);
	store_values(dt);
	ft_render_text();
	if (mem_clipboard) pressed_paste(TRUE);
	run_destroy(wdata);
}

static void collect_fontnames(font_dd *dt)
{
	memx2 mem = dt->fnmmem;
	char buf2[256];
	fontNODE *fn;
	fontname_cc *fc;
	char *last_font_name = inifile_get("lastFontName", "");
	int last_font_name_dir = inifile_get_gint32("lastFontNameDir", 0),
		last_font_name_bitmap = inifile_get_gint32("lastFontNameBitmap", 0);
	int ofs, dir, cnt, b, strs[TX_MAX_DIRS];

	/* Gather up nodes */
	for (fn = global_font_node , cnt = 0; fn; fn = fn->next) cnt++;
	dt->fontname = -1;
	memset(strs, 0, sizeof(strs));
	mem.here = 0;
	getmemx2(&mem, 8000); // default size
	b = mem.here += getmemx2(&mem, cnt * sizeof(fontname_cc)); // minimum size
	addstr(&mem, "B", 0);
	for (fn = global_font_node , cnt = 0; fn; fn = fn->next)
	{
		if ((fn->directory < 0) || (fn->directory >= TX_MAX_DIRS))
			continue;
		/* Trying to remember start row */
		if (!strcmp(fn->font_name, last_font_name) &&
			(last_font_name_dir == fn->directory) &&
			(last_font_name_bitmap == !!fn->style->size->size))
			dt->fontname = cnt;
		/* Prepare dir index */
		dir = fn->directory;
		if (!strs[dir])
		{
			strs[dir] = mem.here;
			sprintf(buf2, "%3d", dir + 1);
			addstr(&mem, buf2, 0);
		}
		dir = strs[dir];
		/* Convert name string (to UTF-8 in GTK+2) and store it */
		ofs = mem.here;
		gtkuncpy(buf2, fn->font_name, sizeof(buf2));
		addstr(&mem, buf2, 0);
		/* Add a row - with offsets for now */
		fc = (fontname_cc *)mem.buf + cnt;
		fc->fn = fn;
		fc->dir = dir - ((char *)&fc->dir - mem.buf);
		// "B" if has size (is bitmap), "" otherwise
		fc->bm = (fn->style->size->size ? b : b + 1) -
			((char *)&fc->bm - mem.buf);
		fc->name = ofs - ((char *)&fc->name - mem.buf);
		cnt++;
	}
	dt->nfontnames = cnt;

	/* Allocations done - now set up pointers */
	dt->fontnames = (void *)mem.buf; // won't change now

	/* Save allocator data */
	dt->fnmmem = mem;
}

static void collect_fontstyles(font_dd *dt)
{
	static const char *default_styles[] =
		{ "Regular", "Medium", "Book", "Roman", NULL };
	char *last_font_style = inifile_get("lastFontStyle", "");
	char buf2[256];
	styleNODE *sn;
	fontstyle_cc *fc;
	memx2 mem = dt->fstmem;
	int i, ofs, cnt, default_row = -1;

	/* Gather up nodes */
	sn = dt->fontnames[dt->fontname].fn->style;
	for (cnt = 0; sn; sn = sn->next) cnt++;
	dt->fontstyle = -1;
	mem.here = 0;
	getmemx2(&mem, 4000); // default size
	mem.here += getmemx2(&mem, cnt * sizeof(fontstyle_cc)); // minimum size
	sn = dt->fontnames[dt->fontname].fn->style;
	for (cnt = 0; sn; sn = sn->next)
	{
		/* Trying to remember start row */
		if (!strcmp(sn->style_name, last_font_style))
			dt->fontstyle = cnt;
		else for (i = 0; default_styles[i]; i++)
		{
			if (!strcmp(sn->style_name, default_styles[i]))
				default_row = cnt;
		}
		/* Convert name string (to UTF-8 in GTK+2) and store it */
		ofs = mem.here;
		gtkuncpy(buf2, sn->style_name, sizeof(buf2));
		addstr(&mem, buf2, 0);
		/* Add a row - with offset for now */
		fc = (fontstyle_cc *)mem.buf + cnt;
		fc->sn = sn;
		fc->name = ofs - ((char *)&fc->name - mem.buf);
		cnt++;
	}
	/* Use default if last style not found */
	if (dt->fontstyle < 0) dt->fontstyle = default_row;
	dt->nfontstyles = cnt;

	/* Allocations done - now set up pointers */
	dt->fontstyles = (void *)mem.buf; // won't change now

	/* Save allocator data */
	dt->fstmem = mem;
}

static void collect_fontsizes(font_dd *dt)
{
	static const unsigned char sizes[] = {
		8, 9, 10, 11, 12, 13, 14, 16, 18, 20,
		22, 24, 26, 28, 32, 36, 40, 48, 56, 64, 72 };
	char buf2[256];
	sizeNODE *zn;
	fontsize_cc *fc;
	memx2 mem = dt->fszmem;
	int i, cnt;

	/* Gather up nodes */
	zn = dt->fontstyles[dt->fontstyle].sn->size;
	dt->lfontsize = -1;
	mem.here = 0;
	getmemx2(&mem, sizeof(sizes) * sizeof(fontsize_cc) > 4000 ?
		sizeof(sizes) * sizeof(fontsize_cc) : 4000); // default size
	if (zn && !zn->size)
	{
		mem.here += sizeof(sizes) * sizeof(fontsize_cc);
		for (i = 0; i < sizeof(sizes); i++)
		{
			int ofs, n = sizes[i];
			/* Trying to remember start row */
			if (n == font_size) dt->lfontsize = i;
			/* Prepare text */
			ofs = mem.here;
			sprintf(buf2, "%2d", n);
			addstr(&mem, buf2, 0);
			/* Add a row - with offset for now */
			fc = (fontsize_cc *)mem.buf + i;
			fc->zn = zn;
			fc->n = n;
			fc->what = ofs - ((char *)&fc->what - mem.buf);
		}
		cnt = sizeof(sizes);
	}
	else
	{
		int geom = inifile_get_gint32("lastfontBitmapGeometry", 0);

		for (cnt = 0; zn; zn = zn->next) cnt++;
		mem.here += getmemx2(&mem, cnt * sizeof(fontsize_cc)); // minsize
		zn = dt->fontstyles[dt->fontstyle].sn->size;
		for (cnt = 0; zn; zn = zn->next)
		{
			int ofs, n = zn->size;
			/* Trying to remember start row */
			if (geom == n) dt->lfontsize = cnt;
			/* Prepare text */
			ofs = mem.here;
			snprintf(buf2, 32, "%2d x %2d",
				i = (n >> SIZE_SHIFT) & ((1 << SIZE_SHIFT) - 1),
				n & ((1 << SIZE_SHIFT) - 1));
			addstr(&mem, buf2, 0);
			/* Add a row - with offset for now */
			fc = (fontsize_cc *)mem.buf + cnt;
			fc->zn = zn;
			fc->n = 0; // !!! or maybe zn->size?
			fc->what = ofs - ((char *)&fc->what - mem.buf);
			cnt++;
		}
	}
	dt->nlfontsizes = cnt;

	/* Allocations done - now set up pointers */
	dt->fontsizes = (void *)mem.buf; // won't change now

	/* Save allocator data */
	dt->fszmem = mem;
}

static void collect_fontfiles(font_dd *dt)
{
	char *last_filename = inifile_get("lastTextFilename", "");
	char buf2[256];
	filenameNODE *fn;
	fontfile_cc *fc;
	memx2 mem = dt->ffnmem;
	int cnt;

	/* Gather up nodes */
	fn = dt->fontsizes[dt->lfontsize].zn->filename;
	for (cnt = 0; fn; fn = fn->next) cnt++;
	dt->fontfile = -1;
	mem.here = 0;
	getmemx2(&mem, 4000); // default size
	mem.here += getmemx2(&mem, cnt * sizeof(fontfile_cc)); // minimum size
	fn = dt->fontsizes[dt->lfontsize].zn->filename;
	for (cnt = 0; fn; fn = fn->next)
	{
		char *s, *nm = fn->filename;
		int ofs1, ofs2;

		/* Trying to remember start row */
		if (!strcmp(nm, last_filename)) dt->fontfile = cnt;
		/* Convert name string (to UTF-8 in GTK+2) and store it */
		s = strrchr(nm, DIR_SEP);
		s = s ? s + 1 : nm;
		ofs1 = mem.here;
		gtkuncpy(buf2, s, sizeof(buf2));
		addstr(&mem, buf2, 0);
		/* Store index string */
		sprintf(buf2, "%3d", fn->face_index);
		ofs2 = mem.here;
		addstr(&mem, buf2, 0);
		/* Add a row - with offsets for now */
		fc = (fontfile_cc *)mem.buf + cnt;
		fc->fn = fn;
		fc->name = ofs1 - ((char *)&fc->name - mem.buf);
		fc->face = ofs2 - ((char *)&fc->face - mem.buf);
		cnt++;
	}
	dt->nfontfiles = cnt;

	/* Allocations done - now set up pointers */
	dt->fontfiles = (void *)mem.buf; // won't change now

	/* Save allocator data */
	dt->ffnmem = mem;
}

static void collect_dirnames(font_dd *dt)
{
	memx2 mem = dt->dirmem;
	char buf2[256];
	dir_cc *fc;
	int i;

	/* Gather up nodes */
	mem.here = 0;
	getmemx2(&mem, 4000); // default size
	mem.here += getmemx2(&mem, font_dirs * sizeof(dir_cc)); // minimum size
	for (i = 0; i < font_dirs; i++)
	{
		int ofs1, ofs2;
		/* Prepare dir index */
		ofs1 = mem.here;
		sprintf(buf2, "%3d", i + 1);
		addstr(&mem, buf2, 0);
		/* Convert name string (to UTF-8 in GTK+2) and store it */
		ofs2 = mem.here;
		snprintf(buf2, 128, "font_dir%i", i);
		gtkuncpy(buf2, inifile_get(buf2, ""), sizeof(buf2));
		addstr(&mem, buf2, 0);
		/* Add a row - with offsets for now */
		fc = (dir_cc *)mem.buf + i;
		fc->idx = ofs1 - ((char *)&fc->idx - mem.buf);
		fc->dir = ofs2 - ((char *)&fc->dir - mem.buf);
	}
	dt->ndirs = font_dirs;

	/* Allocations done - now set up pointers */
	dt->dirs = (void *)mem.buf; // won't change now

	/* Save allocator data */
	dt->dirmem = mem;
}


static void click_font_dir_btn(font_dd *dt, void **wdata, int what, void **where)
{
	int i, rows = font_dirs;
	char buf[32], buf2[32];

	run_query(wdata);
	if (origin_slot(where) != dt->add_b) // Remove row
	{
		if (!rows) return; // Nothing to delete
		rows--;
		// Re-work inifile items
		for (i = dt->dir; i < rows; i++)
		{
			snprintf(buf, sizeof(buf), "font_dir%i", i);
			snprintf(buf2, sizeof(buf2), "font_dir%i", i + 1);
			inifile_set(buf, inifile_get(buf2, ""));
		}
		// !!! List is sorted by index, so selected row == selected index
		if (dt->dir >= rows) dt->dir--;
	}
	else // Add row
	{
		if (!dt->dirp[0] || (rows >= TX_MAX_DIRS))
			return; // Cannot add
		snprintf(buf, sizeof(buf), "font_dir%i", rows++);
		inifile_set(buf, dt->dirp);
	}
	font_dirs = rows;
	collect_dirnames(dt);
	cmd_reset(dt->dir_l, dt);
}

static void select_font(font_dd *dt, void **wdata, int what, void **where);

static void click_create_font_index(font_dd *dt, void **wdata)
{
	if (font_dirs > 0)
	{
		char txt[PATHBUF];

		file_in_homedir(txt, FONT_INDEX_FILENAME, PATHBUF);
		font_gui_create_index(txt);			// Create new index file

		dt->lock = TRUE; // Paranoia
		font_mem_clear();		// Empty memory of current nodes
		font_index_load(txt);
		collect_fontnames(dt);
		cmd_reset(dt->fname_l, dt);
		dt->lock = FALSE;
		select_font(dt, wdata, op_EVT_SELECT, dt->fname_l); // reselect
	}
	else alert_box(_("Error"),
		_("You must select at least one directory to search for fonts."), NULL);
}

static void select_font(font_dd *dt, void **wdata, int what, void **where)
{
	void *cause;
	fontNODE *fn;
	styleNODE *sn;
	fontsize_cc *fc;
	filenameNODE *nn;
	int bitmap_font, idx;


	cause = cmd_read(where, dt);
	if (dt->lock || !dt->nfontnames) return;
	dt->lock = TRUE;

	idx = cause == &dt->fontname ? 0 : cause == &dt->fontstyle ? 1 :
		cause == &dt->lfontsize ? 2 : 3; /* cause == &dt->fontfile */
	switch (idx)
	{
	case 0: // Name list
		fn = dt->fontnames[dt->fontname].fn;
		bitmap_font = !!fn->style->size->size;

		inifile_set_gboolean("fontTypeBitmap", bitmap_font);
		cmd_sensitive(dt->obl_c, !bitmap_font);

		/* Update style list */
		collect_fontstyles(dt);
		cmd_reset(dt->fstyle_l, dt);

		inifile_set("lastFontName", fn->font_name);
		inifile_set_gint32("lastFontNameDir", fn->directory);
		inifile_set_gint32("lastFontNameBitmap", bitmap_font);
		/* Fallthrough */
	case 1: // Style list
		sn = dt->fontstyles[dt->fontstyle].sn;

		/* Update size list */
		collect_fontsizes(dt);
		cmd_reset(dt->fsize_l, dt);

		inifile_set("lastFontStyle", sn->style_name);
		/* Fallthrough */
	case 2: // Size list
		fc = dt->fontsizes + dt->lfontsize;

		// Scalable so remember size
		if (!fc->zn->size) font_size = fc->n;
		// Non-Scalable so remember index
		else inifile_set_gint32("lastfontBitmapGeometry", fc->zn->size);

		/* Update file list */
		collect_fontfiles(dt);
		cmd_reset(dt->ffile_l, dt);
		/* Fallthrough */
	case 3: // File list
		nn = dt->fontfiles[dt->fontfile].fn;

		inifile_set("lastTextFilename", nn->filename);
		inifile_set_gint32("lastTextFace", nn->face_index);
		break;
	}

	/* Update size */
	dt->fontsize = dt->fontsizes[dt->lfontsize].zn->size ? // nonzero for bitmaps
		font_bmsize : font_size;
	cmd_reset(dt->size_spin, dt);

	dt->lock = FALSE;

	font_preview_update(dt);	// Update the font preview area
}


static void init_font_lists()		//	LIST INITIALIZATION
{
	char txt[PATHBUF];

	/* Get font directories if we don't have any */
	if (font_dirs <= 0)
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
		font_dirs = new_dirs;
	}

	file_in_homedir(txt, FONT_INDEX_FILENAME, PATHBUF);
	font_index_load(txt);	// Does a valid ~/.mtpaint_fonts index exist?
	if (!global_font_node)	// Index file not loaded
	{
		font_gui_create_index(txt);
		font_index_load(txt);	// Try for second and last time
	}
}


static gboolean preview_expose_event(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	int x = event->area.x, y = event->area.y;
	int w = event->area.width, h = event->area.height;
	int w2, h2;
	font_dd *dt = user_data;


	if (!dt->preview_rgb) return (FALSE);
#if GTK_MAJOR_VERSION == 1
	/* !!! GTK+2 clears background automatically */
	gdk_window_clear_area(widget->window, x, y, w, h);
#endif

	w2 = dt->preview_w - x; if (w > w2) w = w2;
	h2 = dt->preview_h - y; if (h > h2) h = h2;
	if ((w < 1) || (h < 1)) return (FALSE);

	gdk_draw_rgb_image(widget->window, widget->style->black_gc,
		x, y, w, h, GDK_RGB_DITHER_NONE,
		dt->preview_rgb + (y * dt->preview_w + x) * 3, dt->preview_w * 3);

	return (FALSE);
}

static void font_entry_changed(font_dd *dt, void **wdata, int what, void **where)
{
	cmd_read(where, dt);
	if (dt->lock) return;

	store_values(dt);
	font_preview_update(dt);	// Update the font preview area
}

static void **create_font_pad(void **r, GtkWidget ***wpp, void **wdata)
{
	GdkColor *c;
	GtkRcStyle *rc;
	GtkWidget *preview;
	font_dd *dt = GET_DDATA(wdata);
	
	dt->preview_area = preview = gtk_drawing_area_new();
	gtk_widget_show(preview);
	gtk_drawing_area_size(GTK_DRAWING_AREA(preview), 1, 1);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(*(*wpp)++),
		preview);
	gtk_signal_connect(GTK_OBJECT(preview), "expose_event",
		GTK_SIGNAL_FUNC(preview_expose_event), dt);
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
	return (r);
}

#define WBbase font_dd
static void *font_code[] = {
	WPWHEREVER, WINDOWm(_("Paste Text")),
	WXYWH("paste_text_window", 400, 450),
	BORDER(NBOOK, 0), NBOOK, // !!! Originally was in window directly
//	TAB 1 - TEXT
	PAGE(_("Text")),
	XHBOX, // !!! Originally the page was an hbox
	VBOXbp(0, 0, 5), // !!! what for?
	XSCROLL(0, 1), // never/auto
	WLIST,
	RTXTCOLUMND(fontname_cc, dir, 0, 0),
	RTXTCOLUMND(fontname_cc, bm, 0, 0),
	NRTXTCOLUMND(_("Font"), fontname_cc, name, 0, 0),
	COLUMNDATA(fontnames, sizeof(fontname_cc)), CLEANUP(fontnames),
	REF(fname_l), LISTCS(fontname, nfontnames, fnsort, select_font), TRIGGER,
	WDONE, // VBOXP
	XVBOXbp(0, 0, 5),
	XHBOXbp(0, 0, 5),
	XSCROLL(1, 1), // auto/auto
	WLIST,
	NRTXTCOLUMND(_("Style"), fontstyle_cc, name, 0, 0),
	COLUMNDATA(fontstyles, sizeof(fontstyle_cc)), CLEANUP(fontstyles),
	REF(fstyle_l), WIDTH(100), LISTC(fontstyle, nfontstyles, select_font),
	XVBOX,
	BORDER(XSCROLL, 0),
	XSCROLL(1, 1), // auto/auto
	DEFBORDER(XSCROLL),
	WLIST,
	NRTXTCOLUMND(_("Size"), fontsize_cc, what, 0, 1), // centered
	COLUMNDATA(fontsizes, sizeof(fontsize_cc)), CLEANUP(fontsizes),
	REF(fsize_l), LISTC(lfontsize, nlfontsizes, select_font),
	REF(size_spin), SPINc(fontsize, 1, 500), EVENT(CHANGE, font_entry_changed),
	WDONE, // XVBOX
	XSCROLL(1, 1), // auto/auto
	WLIST,
	NRTXTCOLUMND(_("Filename"), fontfile_cc, name, 0, 0),
	NRTXTCOLUMND(_("Face"), fontfile_cc, face, 0, 0),
	COLUMNDATA(fontfiles, sizeof(fontfile_cc)), CLEANUP(fontfiles),
	REF(ffile_l), LISTC(fontfile, nfontfiles, select_font),
	WDONE, // XHBOXbp
//	Text entry box
	FVBOX(_("Text")), // !!! Originally was hbox
	MLENTRY(text), EVENT(CHANGE, font_entry_changed), FOCUS,
	WDONE,
//	PREVIEW AREA
	FXVBOX(_("Preview")), // !!! Originally was hbox
	BORDER(XSCROLL, 0),
	XSCROLL(1, 1), // auto/auto
	DEFBORDER(XSCROLL),
	EXEC(create_font_pad), // !!! for later
	CLEANUP(preview_rgb),
	WDONE, // FXVBOX
//	TOGGLES
	HBOX,
	UNLESSx(idx, 1),
		CHECKv(_("Antialias"), font_aa), EVENT(CHANGE, font_entry_changed),
	ENDIF(1),
	UNLESSx(img, 1),
		CHECKv(_("Invert"), font_bk), EVENT(CHANGE, font_entry_changed),
	ENDIF(1),
	IFx(img, 1),
		CHECKv(_("Background colour ="), font_bk),
			EVENT(CHANGE, font_entry_changed),
		SPINa(bkg), EVENT(CHANGE, font_entry_changed),
	ENDIF(1),
	WDONE, // HBOX
	HBOX,
	REF(obl_c), CHECKv(_("Oblique"), font_obl), EVENT(CHANGE, font_entry_changed),
	CHECKv(_("Angle of rotation ="), font_r), EVENT(CHANGE, font_entry_changed),
	FSPINv(font_angle, -36000, 36000), EVENT(CHANGE, font_entry_changed),
	WDONE,
	HSEPl(200),
	OKBOXp(_("Paste Text"), paste_text_ok, _("Close"), NULL), WDONE,
	WDONE, WDONE, WDONE, // XVBOXbp, XHBOX, PAGE
//	TAB 2 - DIRECTORIES
	PAGE(_("Font Directories")),
//	VBOX, // !!! utterly useless
	XSCROLL(1, 1), // auto/auto
	WLIST,
	RTXTCOLUMND(dir_cc, idx, 0, 0),
	NRTXTCOLUMND(_("Directory"), dir_cc, dir, 0, 0),
	COLUMNDATA(dirs, sizeof(dir_cc)), CLEANUP(dirs),
	REF(dir_l), LISTC(dir, ndirs, NULL),
	PATH(_("New Directory"), _("Select Directory"), FS_SELECT_DIR, dirp),
	HSEPl(200),
	HBOXbp(0, 0, 5),
	/* !!! Keyboard shortcut doesn't work for invisible buttons in GTK+ */
// !!! Test whether the event tries to happen twice on window close, then
	CANCELBTN(_("Close"), NULL),
	REF(add_b), BUTTON(_("Add"), click_font_dir_btn),
	BUTTON(_("Remove"), click_font_dir_btn),
	BUTTON(_("Create Index"), click_create_font_index),
	WSHOW
};
#undef WBbase

void pressed_mt_text()
{
	font_dd tdata;

	if (!global_font_node) init_font_lists();

	memset(&tdata, 0, sizeof(tdata));
	tdata.fontsize = font_size; // !!! is reset
	tdata.text = inifile_get("textString", __("Enter Text Here"));
	tdata.bkg[0] = font_bkg % mem_cols;
	tdata.bkg[1] = 0;
	tdata.bkg[2] = mem_cols - 1;
	tdata.img = mem_channel == CHN_IMAGE;
	tdata.idx = tdata.img && (mem_img_bpp == 1);
	collect_fontnames(&tdata);
	tdata.fnsort = 3; // By name column, ascending
	collect_dirnames(&tdata);
	tdata.dir = -1; // Top sorted

	run_create(font_code, &tdata, sizeof(tdata));
}

#endif	/* U_FREETYPE */
