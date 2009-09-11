/*	fpick.c
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
#include "fpick.h"

#ifdef U_FPICK_MTPAINT		/* mtPaint fpicker */

#include <dirent.h>
#include <sys/stat.h>

#include "inifile.h"
#include "memory.h"
#include "png.h"		// Needed by canvas.h
#include "canvas.h"
#include "toolbar.h"
#include "mainwindow.h"
#include "icons.h"

#define FP_DATA_KEY "mtPaint.fp_data"
#define FP_BACKUP_KEY "mtPaint.fp_backup"

#define FPICK_ICON_UP 0
#define FPICK_ICON_HOME 1
#define FPICK_ICON_DIR 2
#define FPICK_ICON_HIDDEN 3
#define FPICK_ICON_CASE 4
#define FPICK_ICON_TOT 5

#define FPICK_COMBO_ITEMS 16

#define FPICK_CLIST_COLS 4
#define FPICK_CLIST_COLS_HIDDEN 2			// Used for sorting file/directory names

#define FPICK_CLIST_NAME 0
#define FPICK_CLIST_SIZE 1
#define FPICK_CLIST_TYPE 2
#define FPICK_CLIST_DATE 3

#define FPICK_CLIST_H_UC 4
#define FPICK_CLIST_H_C  5

// ------ Main Data Structure ------

typedef struct
{
	int		allow_files,			// Allow the user to select files/directories
			allow_dirs,
			sort_column,			// Which column is being sorted in clist
			show_hidden
			;

	char		combo_items[FPICK_COMBO_ITEMS][PATHTXT],	// UTF8 in GTK+2
			/* Must be DIR_SEP terminated at all times */
			txt_directory[PATHBUF],	// Current directory - Normal C string
			txt_file[PATHTXT],	// Full filename - Normal C string except on Windows
			txt_mask[PATHTXT]	// Filter mask - UTF8 in GTK+2
			;

	GtkWidget	*window,			// Main window
			*ok_button,			// OK button
			*cancel_button,			// Cancel button
			*main_vbox,			// For extra widgets
			*toolbar,			// Toolbar
			*icons[FPICK_ICON_TOT],		// Icons
			*combo,				// List at top holding recent directories
			*combo_entry,			// Directory entry area in combo
			*clist,				// Containing list of files/directories
			*sort_arrows[FPICK_CLIST_COLS+FPICK_CLIST_COLS_HIDDEN],	// Column sort arrows
			*file_entry			// Text entry box for filename
			;
	GtkSortType	sort_direction;			// Sort direction of clist

	GList		*combo_list;			// List of combo items
} fpicker;

static int case_insensitive;

#if GTK_MAJOR_VERSION == 1

#define _GNU_SOURCE
#include <fnmatch.h>
#undef _GNU_SOURCE
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0
#endif

#define fpick_fnmatch(mask, str) !fnmatch(mask, str, FNM_PATHNAME | \
	(case_insensitive ? FNM_CASEFOLD : 0))

#elif (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION < 4) /* GTK+ 2.0/2.2 */

/*
 * fpick_fnmatch(), get_char() and get_unescaped_char() are based on code
 * from GTK+ 2.6.7
 * Copyright (C) 1991, 1992, 1993 Free Software Foundation, Inc.
 * Modified by the GTK+ Team and others 1997-2000.
 * Stripped down, converted to UTF-8 and test cases added by Owen Taylor, 2002.
 *
 * Adapted for mtPaint by Dmitry Groshev, November 2008.
 */

#ifdef WIN32

static gunichar get_char(const char **str)
{
	gunichar c = g_utf8_get_char(*str);
	*str = g_utf8_next_char(*str);
	return (g_unichar_tolower(c));
}

#define get_unescaped_char(str, was_escaped) get_char(str)

#else

static gunichar get_char(const char **str)
{
	gunichar c = g_utf8_get_char(*str);
	*str = g_utf8_next_char(*str);
	return (c);
}

static gunichar get_unescaped_char(const char **str, int *was_escaped)
{
	gunichar c = get_char (str);
	if ((*was_escaped = c == '\\')) c = get_char(str);
	return (c);
}

#endif /* !WIN32 */

static int fpick_fnmatch(const char *pattern, const char *string)
{
	const char *p = pattern, *n = string;

	while (*p)
	{
		const char *last_n = n;
      		gunichar c = get_char(&p);
		gunichar nc = get_char(&n);

		switch (c)
		{
		case '?': if ((nc == '\0') || (nc == DIR_SEP)) return (FALSE);
			break;
		case '*':
		{
			const char *last_p;

			while (TRUE)
			{
				last_p = p;
				c = get_char(&p);
				if (c == '?')
				{
					if ((nc == '\0') || (nc == DIR_SEP))
						return (FALSE);
					last_n = n;
					nc = get_char(&n);
				}
				else if (c != '*') break;
	      		}

			/* If the pattern ends with wildcards, we have a
			 * guaranteed match unless there is a dir separator
			 * in the remainder of the string. */
			if (c == '\0') return (!strchr(last_n, DIR_SEP));
#ifndef WIN32
			if (c == '\\') c = get_char(&p);
#endif
			for (p = last_p; nc != '\0';)
			{
/* !!! Can get exponential behaviour here, but I'm too lazy to fix this corner
 * case just right now - WJ */
				if (((c == '[') || (c == nc)) &&
					fpick_fnmatch(p, last_n)) return (TRUE);
				last_n = n;
				nc = get_char(&n);
			}
			return (FALSE);
		}
		case '[':
		{
			int not; // Inverted class flag
			int was_escaped = FALSE;

			if ((nc == '\0') || (nc == DIR_SEP)) return (FALSE);
			if ((not = ((*p == '!') || (*p == '^')))) p++;

			c = get_unescaped_char(&p, &was_escaped);
			while (TRUE)
			{
				gunichar cstart = c, cend = c;

				/* [ (unterminated) loses.  */
				if (c == '\0') return (FALSE);
				c = get_unescaped_char(&p, &was_escaped);
				if (!was_escaped && (c == '-') && (*p != ']'))
				{
					cend = get_unescaped_char(&p, &was_escaped);
					if (cend == '\0') return (FALSE);
					c = get_unescaped_char(&p, &was_escaped);
				}
				if ((nc >= cstart) && (nc <= cend)) goto matched;
				if (!was_escaped && (c == ']')) break;
			}
			if (!not) return (FALSE);
			break;
matched:		if (not) return (FALSE);
			/* Skip the rest of the [...] that already matched.  */
			while (was_escaped || (c != ']'))
			{
				/* [ (unterminated) loses.  */
				if (c == '\0') return (FALSE);
				c = get_unescaped_char(&p, &was_escaped);
			}
			break;
	  	}
#ifndef WIN32
		case '\\': c = get_char(&p);
		/* Fallthrough */
#endif
		default: if (c != nc) return (FALSE);
		}
	}
	return (*n == '\0');
}

#endif

/* !!! The code below manipulates undocumented internals of GtkCList, to work
 * around its lack of row hiding functionality; thankfully, internals of this
 * "deprecated and unmaintained" widget are very unlikely to change - WJ */

static void fpick_clist_drop_backup(GtkCList *clist)
{
	GtkObject *obj = GTK_OBJECT(clist);
	GList *backup = gtk_object_get_data(obj, FP_BACKUP_KEY);
	if (!backup) return;
	clist->row_list = g_list_concat(clist->row_list, backup);
	clist->row_list_end = g_list_last(clist->row_list);
	gtk_object_set_data(obj, FP_BACKUP_KEY, NULL);
}

static void fpick_clist_clear(GtkCList *clist)
{
	fpick_clist_drop_backup(clist);
	gtk_clist_clear(clist);
}

static void fpick_clist_select_row(GtkCList *clist, int n)
{
	if (n < 0) return;
#if GTK_MAJOR_VERSION == 1
	GTK_CLIST_CLASS(((GtkObject *)clist)->klass)->select_row(clist, n, -1, NULL);
#else /* if GTK_MAJOR_VERSION == 2 */
	GTK_CLIST_GET_CLASS(clist)->select_row(clist, n, -1, NULL);
#endif
	/* !!! Focus fails to follow selection in browse mode - have to move
	 * it here; but it means a full redraw is necessary afterwards */
	clist->focus_row = n;
}

static void fpick_clist_scroll(GtkCList *clist) // Scroll to selected row
{
	gtk_clist_moveto(clist, clist->focus_row, -1, 0.5, 0.5);
}

static void fpick_clist_repattern(GtkCList *clist, const char *pattern)
{
	GtkObject *obj = GTK_OBJECT(clist);
	GList *tmp, *backup, *cur, *res, *pos = NULL;
	int n = 0;
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION >= 4) /* GTK+ 2.4+ */
	GtkFileFilter *filt = gtk_file_filter_new();
	GtkFileFilterInfo info;
	gtk_file_filter_add_pattern(filt, pattern);
	info.contains = GTK_FILE_FILTER_DISPLAY_NAME;
#endif

	/* Stop updates & redraws */
	gtk_clist_freeze(clist);
	/* Get backed-up contents */
	backup = gtk_object_get_data(obj, FP_BACKUP_KEY);
	/* Clean selection if any */
	if (clist->selection)
	{
		int n = GPOINTER_TO_INT(clist->selection->data);
		pos = g_list_nth(clist->row_list, n);
		gtk_clist_unselect_row(clist, n, -1);
	}
	/* Clear list but save its contents */
	cur = g_list_concat(clist->row_list, backup);
	clist->row_list = NULL;
	gtk_clist_clear(clist);
	/* Filter the contents */
	backup = res = NULL;
	while (cur)
	{
		GtkCListRow *row = cur->data;
		char *name = GTK_CELL_TEXT(row->cell[FPICK_CLIST_NAME])->text;
		GList **lst = &res; /* Add node to output list */

		tmp = cur;
		cur = cur->next;
		tmp->prev = NULL;
		/* Filter files, let directories pass */
		if (GTK_CELL_TEXT(row->cell[FPICK_CLIST_SIZE])->text[0] && pattern[0] &&
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION >= 4) /* GTK+ 2.4+ */
			(info.display_name = name , !gtk_file_filter_filter(filt, &info)))
#else
			!fpick_fnmatch(pattern, name))
#endif
			lst = &backup; /* Add node to backup list */
		else n++;
		if ((tmp->next = *lst)) tmp->next->prev = tmp;
		*lst = tmp;
	}
	/* Re-add filtered contents */
	clist->row_list = res;
	clist->row_list_end = g_list_last(res);
	clist->rows = n;
	gtk_object_set_data(obj, FP_BACKUP_KEY, backup);
	/* Sort the list */
	gtk_clist_sort(clist);
	/* Reselect the previously selected row if possible */
	n = g_list_position(clist->row_list, pos);
	if (n < 0) n = 0;
	fpick_clist_select_row(clist, n); // Avoid "select_row" signal emission
	/* Let it be redrawn */
	gtk_clist_thaw(clist);
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION >= 4) /* GTK+ 2.4+ */
	gtk_object_sink(GTK_OBJECT(filt));
#endif
}

static void fpick_sort_files(fpicker *win)
{
	GtkCList *clist = GTK_CLIST(win->clist);

	gtk_clist_set_sort_type(clist, win->sort_direction);
	gtk_clist_set_sort_column(clist, win->sort_column);
	gtk_clist_sort(clist);
	/* No selection yet */
	if (!clist->selection) fpick_clist_select_row(clist, 0);
	else
	{
	/* !!! Evil hack using undocumented widget internals: widget implementor
	 * forgot to move focus along with selection, so we do it here - WJ */
		clist->focus_row = GPOINTER_TO_INT(clist->selection->data);
		if (GTK_WIDGET_HAS_FOCUS(win->clist))
			gtk_widget_queue_draw(win->clist);
	}
}

/* *** A WORD OF WARNING ***
 * Collating string comparison functions consider letter case only *AFTER*
 * letter itself for any (usable) value of LC_COLLATE except LC_COLLATE=C, and
 * in that latter case, order by character codepoint which frequently is
 * unrelated to alphabetical ordering. And on GTK+1 with multibyte charset,
 * casefolding will break down horribly, too.
 * What this means in practice: don't expect anything sane out of alphabetical
 * sorting outside of strict ASCII, and don't expect anything sane out of
 * case-sensitive sorting at all. - WJ */

#if GTK_MAJOR_VERSION == 1

/* Returns a string which can be used as key for case-insensitive sort;
 * input string is in locale encoding in GTK+1, in UTF-8 in GTK+2 */
static char *isort_key(char *src)
{
	char *s;
	s = g_strdup(src);
	g_strdown(s);
// !!! Consider replicating g_utf8_collate_key(), based on strxfrm()
	return (s);
}

/* "strkeycmp" is for sort keys, "strcollcmp" for displayable strings */
#define strkeycmp strcoll
#define strcollcmp strcoll

#else /* if GTK_MAJOR_VERSION == 2 */

static char *isort_key(char *src)
{
	char *s;
	src = g_utf8_casefold(src, -1);
	s = g_utf8_collate_key(src, -1);
	g_free(src);
	return (s);
}

#define strkeycmp strcmp
#define strcollcmp g_utf8_collate

#endif

/* !!! Expects that "txt" points to PATHBUF-sized buffer */
static void fpick_cleanse_path(char *txt)	// Clean up null terminated path
{
	static const char dds[] = { DIR_SEP, DIR_SEP, 0 };
	char *src, *dest;

#ifdef WIN32
	// Unify path separators
	src = txt;
	while ((src = strchr(src, '/'))) *src = DIR_SEP;
#endif
	// Expand home directory
	if ((txt[0] == '~') && (txt[1] == DIR_SEP))
	{
		src = g_strconcat(get_home_directory(), txt + 1, NULL);
		strncpy0(txt, src, PATHBUF - 1);
		g_free(src);
	}
	// Remove multiple consecutive occurences of DIR_SEP
	if ((dest = src = strstr(txt, dds)))
	{
		while (*src)
		{
			if (*src == DIR_SEP) while (src[1] == DIR_SEP) src++;
			*dest++ = *src++;
		}
		*dest++ = '\0';
	}
}


static gint fpick_compare(GtkCList *clist, gconstpointer ptr1, gconstpointer ptr2)
{
	static const signed char sort_order[] = { FPICK_CLIST_NAME,
		FPICK_CLIST_DATE, FPICK_CLIST_SIZE, -1 };
	GtkCListRow *r1 = (GtkCListRow *)ptr1, *r2 = (GtkCListRow *)ptr2;
	unsigned char *s1, *s2;
	int c = clist->sort_column, bits = 0, lvl = 0, d = 0;

	/* "/ .." Directory always goes first, and conveniently it is also the
	 * one and only GTK_CELL_TEXT in an entire column */
	d = r1->cell[FPICK_CLIST_NAME].type - r2->cell[FPICK_CLIST_NAME].type;
	if (GTK_CELL_TEXT > GTK_CELL_PIXTEXT) d = -d;
	if (d) return (clist->sort_type == GTK_SORT_DESCENDING ? -d : d);

	/* Directories have empty size column, and always go before files */
	s1 = GTK_CELL_TEXT(r1->cell[FPICK_CLIST_SIZE])->text;
	s2 = GTK_CELL_TEXT(r2->cell[FPICK_CLIST_SIZE])->text;

	if (!s1[0] ^ !s2[0])
	{
		d = (int)s1[0] - s2[0];
		return (clist->sort_type == GTK_SORT_DESCENDING ? -d : d);
	}

	while (c >= 0)
	{
		if (bits & (1 << c))
		{
			c = sort_order[lvl++];
			continue;
		}
		bits |= 1 << c;
		s1 = GTK_CELL_TEXT(r1->cell[c])->text;
		s2 = GTK_CELL_TEXT(r2->cell[c])->text;
		switch (c)
		{
		case FPICK_CLIST_TYPE:
			if ((d = strcollcmp(s1, s2))) break;
			continue;
		case FPICK_CLIST_SIZE:
			if ((d = strcmp(s1, s2))) break;
			continue;
		case FPICK_CLIST_DATE:
			if ((d = strcmp(s2, s1))) break; // Newest first
			continue;
		default:
		case FPICK_CLIST_NAME:
			c = case_insensitive ? FPICK_CLIST_H_UC : FPICK_CLIST_H_C;
			continue;
		case FPICK_CLIST_H_UC:
		case FPICK_CLIST_H_C:
			if ((d = strkeycmp(s1, s2))) break;
			c = FPICK_CLIST_H_C;
			continue;
		}
		break;
	}
	return (d);
}


static void fpick_column_button(GtkCList *clist, gint column, gpointer user_data)
{
	fpicker *fp = user_data;
	GtkSortType direction;

	if ((column < 0) || (column >= FPICK_CLIST_COLS)) return;

	// reverse the sorting direction if the list is already sorted by this col
	if ( fp->sort_column == column )
		direction = (fp->sort_direction == GTK_SORT_ASCENDING ?
			GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
	else	// Different column clicked so use default value for that column
	{
		direction = GTK_SORT_ASCENDING;

		gtk_widget_hide( fp->sort_arrows[fp->sort_column] );	// Hide old arrow
		gtk_widget_show( fp->sort_arrows[column] );		// Show new arrow
		fp->sort_column = column;
	}

	gtk_arrow_set(GTK_ARROW(fp->sort_arrows[column]),
		direction == GTK_SORT_ASCENDING ? GTK_ARROW_DOWN : GTK_ARROW_UP,
		GTK_SHADOW_IN);

	fp->sort_direction = direction;

	fpick_sort_files(fp);
	fpick_clist_scroll(GTK_CLIST(fp->clist)); // Scroll to selected row
}


static void fpick_directory_new(fpicker *win, char *name)	// Register directory in combo
{
	int i;
	char txt[PATHTXT];

	gtkuncpy(txt, name, PATHTXT);

// !!! Shuffle list items, not strings !!!
	for ( i=0 ; i<(FPICK_COMBO_ITEMS-1); i++ )	// Does this text already exist in the list?
		if ( !strcmp(txt, win->combo_items[i]) ) break;

	for ( ; i>0; i-- )				// Shuffle items down as needed
		strncpy(win->combo_items[i], win->combo_items[i-1], PATHTXT);

	strncpy(win->combo_items[0], txt, PATHTXT);		// Add item to list

	gtk_combo_set_popdown_strings( GTK_COMBO(win->combo), win->combo_list );
	gtk_entry_set_text( GTK_ENTRY(win->combo_entry), txt );
}

#ifdef WIN32

#include <ctype.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int fpick_scan_drives(fpicker *fp)	// Scan drives, populate widgets
{
	static char *empty_row[FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN] =
		{ "", "", "", "", "", "" }, ws[4] = "C:\\";
	char *cp, buf[128]; // More than enough for 26 4-char strings
	GtkCList *clist = GTK_CLIST(fp->clist);
	GdkPixmap *icon;
	GdkBitmap *mask;
	int row;

	fp->txt_directory[0] = '\0';
	gtk_entry_set_text(GTK_ENTRY(fp->combo_entry), ""); // Just clear it
	GetLogicalDriveStrings(sizeof(buf), buf);
	icon = gdk_pixmap_create_from_xpm_d(main_window->window, &mask,
		NULL, xpm_open_xpm);
	gtk_clist_freeze(clist);
	fpick_clist_clear(clist);
	for (cp = buf; *cp; cp += strlen(cp) + 1)
	{
		ws[0] = toupper(cp[0]);
		row = gtk_clist_append(clist, empty_row);
		gtk_clist_set_pixtext(clist, row, FPICK_CLIST_NAME, ws, 4, icon, mask);
	}
	fpick_clist_select_row(clist, 0);
	fpick_sort_files(fp);
	gtk_clist_thaw(clist);
//	fpick_clist_scroll(clist); // Scroll to selected row
	gdk_pixmap_unref(icon);
	gdk_pixmap_unref(mask);

	return (TRUE);
}

#endif

static const char root_dir[] = { DIR_SEP, 0 };

/* Scan directory, populate widgets */
static int fpick_scan_directory(fpicker *win, char *name, int select)
{
	static char updir[] = { DIR_SEP, ' ', '.', '.', 0 };
	static char nothing[] = "";
	DIR	*dp;
	struct	dirent *ep;
	struct	stat buf;
	char	*cp, *src, *dest, *parent = NULL;
	char	full_name[PATHBUF],
		*row_txt[FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN],
		txt_name[PATHTXT], txt_size[64], txt_date[64], tmp_txt[64];
	GtkCList  *clist = GTK_CLIST(win->clist);
	GdkPixmap *icons[2];
	GdkBitmap *masks[2];
	int i, l, len, row, fail;

	icons[1] = gdk_pixmap_create_from_xpm_d(main_window->window, &masks[1],
		NULL, xpm_open_xpm);
	icons[0] = gdk_pixmap_create_from_xpm_d(main_window->window, &masks[0],
		NULL, xpm_new_xpm);

	row_txt[FPICK_CLIST_SIZE] = txt_size;
	row_txt[FPICK_CLIST_DATE] = txt_date;

	strncpy0(full_name, name, PATHBUF - 1);
	len = strlen(full_name);
	/* Ensure the invariant */
	if (!len || (full_name[len - 1] != DIR_SEP))
		full_name[len++] = DIR_SEP , full_name[len] = 0;
	/* Step up the path till a searchable dir is found */
	fail = 0;
	while (!(dp = opendir(full_name)))
	{
		full_name[len - 1] = 0;
		cp = strrchr(full_name, DIR_SEP);
		// Try to go one level up
		if (cp) len = cp - full_name + 1;
		// No luck - restart with current dir
		else if (!fail++)
                {
			getcwd(full_name, PATHBUF - 1);
			len = strlen(full_name);
			full_name[len++] = DIR_SEP;
                }
		// If current dir hasn't helped either, give up
		else return (FALSE);
		full_name[len] = 0;
	}

	/* If we're going up the path and want to show from where */
	if (select && !strncmp(win->txt_directory, full_name, len) &&
		win->txt_directory[len])
	{
		cp = strchr(win->txt_directory + len, DIR_SEP); // Guaranteed
		parent = win->txt_directory + len;
		parent = g_strndup(parent, cp - parent);
	}
	select = -1;

	strncpy(win->txt_directory, full_name, PATHBUF);
	fpick_directory_new(win, full_name);		// Register directory in combo

	gtk_clist_freeze(clist);
	fpick_clist_clear(clist);	// Empty the list

	if (strcmp(full_name, root_dir)) // Have a parent dir to move to?
	{
		row_txt[FPICK_CLIST_NAME] = updir;
		row_txt[FPICK_CLIST_TYPE] = row_txt[FPICK_CLIST_H_UC] =
			row_txt[FPICK_CLIST_H_C] = "";
		txt_size[0] = txt_date[0] = '\0';
		gtk_clist_append(clist, row_txt );
	}

	while ( (ep = readdir(dp)) )
	{
		full_name[len] = 0;
		strnncat(full_name, ep->d_name, PATHBUF);

		// Error getting file details
		if (stat(full_name, &buf) < 0) continue;

		if (!win->show_hidden && (ep->d_name[0] == '.')) continue;

		strftime(txt_date, 60, "%Y-%m-%d   %H:%M.%S", localtime(&buf.st_mtime) );
		row_txt[FPICK_CLIST_TYPE] = nothing;

#ifdef WIN32
		if ( S_ISDIR(buf.st_mode) )
#else
		if ( ep->d_type == DT_DIR || S_ISDIR(buf.st_mode) )
#endif
		{		// Subdirectory
			if (!win->allow_dirs) continue;
			// Don't look at '.' or '..'
			if (!strcmp(ep->d_name, ".") || !strcmp(ep->d_name, ".."))
				continue;

			gtkuncpy(txt_name, ep->d_name, PATHTXT);
			txt_size[0] = '\0';
		}
		else
		{		// File
			if (!win->allow_files) continue;

			gtkuncpy(txt_name, ep->d_name, PATHTXT);
#ifdef WIN32
			l = snprintf(tmp_txt, 64, "%I64u", (unsigned long long)buf.st_size);
#else
			l = snprintf(tmp_txt, 64, "%llu", (unsigned long long)buf.st_size);
#endif
			memset(txt_size, ' ', 20);
			dest = txt_size + 20; *dest-- = '\0';
			for (src = tmp_txt + l - 1; src - tmp_txt > 2; )
			{
				*dest-- = *src--;
				*dest-- = *src--;
				*dest-- = *src--;
				*dest-- = ',';
			}
			while (src - tmp_txt >= 0) *dest-- = *src--;

			cp = strrchr(txt_name, '.');
			if (cp && (cp != txt_name))
			{
#if GTK_MAJOR_VERSION == 1
				g_strup(row_txt[FPICK_CLIST_TYPE] = g_strdup(cp + 1));
#else
				row_txt[FPICK_CLIST_TYPE] =
					g_utf8_strup(cp + 1, -1);
#endif
			}
		}
#if GTK_MAJOR_VERSION == 1
		row_txt[FPICK_CLIST_H_C] = txt_name;
#else /* if GTK_MAJOR_VERSION == 2 */
		row_txt[FPICK_CLIST_H_C] = g_utf8_collate_key(txt_name, -1);
#endif
		row_txt[FPICK_CLIST_H_UC] = isort_key(txt_name);
		row_txt[FPICK_CLIST_NAME] = ""; // No use to set name just to reset again
		row = gtk_clist_append(clist, row_txt );
		g_free(row_txt[FPICK_CLIST_H_UC]);

		i = !txt_size[0];
		gtk_clist_set_pixtext(clist, row, FPICK_CLIST_NAME, txt_name, 4,
			icons[i], masks[i]);
		/* Remember which row has matching directory name */
		if (parent && i && !strcmp(ep->d_name, parent)) select = row;

		if (row_txt[FPICK_CLIST_TYPE] != nothing)
			g_free(row_txt[FPICK_CLIST_TYPE]);
#if GTK_MAJOR_VERSION == 2
		g_free(row_txt[FPICK_CLIST_H_C]);
#endif
	}
	g_free(parent);
	fpick_clist_select_row(clist, select);
	/* Apply file mask if present, just sort otherwise */
	if (!win->txt_mask[0]) fpick_sort_files(win);
	else fpick_clist_repattern(clist, win->txt_mask);
	gtk_clist_thaw(clist);
	fpick_clist_scroll(clist); // Scroll to selected row

	closedir(dp);
	gdk_pixmap_unref(icons[0]);
	gdk_pixmap_unref(icons[1]);
	gdk_pixmap_unref(masks[0]);
	gdk_pixmap_unref(masks[1]);

	return TRUE;
}

static void fpick_enter_dir_via_list(fpicker *fp, char *name)
{
	char ndir[PATHBUF], *c;
	int l;

	strncpy(ndir, fp->txt_directory, PATHBUF);
	l = strlen(ndir);
	if (!strcmp(name, ".."))	// Go to parent directory
	{
		if (l && (ndir[l - 1] == DIR_SEP)) ndir[--l] = '\0';
		c = strrchr(ndir, DIR_SEP);
		if (c) *c = '\0';
		else
#ifndef WIN32
			 strcpy(ndir, root_dir);
#else
		{
			fpick_scan_drives(fp);
			return;
		}
#endif
	}
	else gtkncpy(ndir + l, name, PATHBUF - l);
	fpick_cleanse_path(ndir);
	fpick_scan_directory(fp, ndir, TRUE);	// Enter new directory
}

static char *get_fname(GtkCList *clist, int row)
{
	char *txt = NULL;

	if (gtk_clist_get_cell_type(clist, row, FPICK_CLIST_NAME) == GTK_CELL_TEXT)
		return ("..");
	gtk_clist_get_pixtext(clist, row, FPICK_CLIST_NAME, &txt, NULL, NULL, NULL);
	return (txt);
}

static void fpick_select_row(GtkCList *clist, gint row, gint col,
	GdkEventButton *event, gpointer user_data)
{
	fpicker *fp = user_data;
	char *txt_name, *txt_size;
	int dclick = event && (event->type == GDK_2BUTTON_PRESS);

	txt_name = get_fname(clist, row);
	
	gtk_clist_get_text(clist, row, FPICK_CLIST_SIZE, &txt_size);
	if ( !txt_name ) return;

	if (!txt_size[0])		// Directory selected
	{
		// Double click on directory so try to enter it
		if (dclick) fpick_enter_dir_via_list(fp, txt_name);
	}
	else					// File selected
	{
		gtk_entry_set_text(GTK_ENTRY(fp->file_entry), txt_name);

		// Double click on file, so press OK button
		if (dclick) gtk_button_clicked(GTK_BUTTON(fp->ok_button));
	}
}

static int fpick_enter_dirname(fpicker *fp, const char *name)
{
	char txt[PATHBUF], *ctxt;

	gtkncpy(txt, name, PATHBUF);

	fpick_cleanse_path(txt); // Path might have been entered manually

	// Only do something if the directory is new
	if (!strcmp(txt, fp->txt_directory)) return (0);

	if (!fpick_scan_directory(fp, txt, TRUE))
	{	// Directory doesn't exist so ask user if they want to create it
		ctxt = g_strdup_printf(_("Could not access directory %s"), name);
		alert_box(_("Error"), ctxt, _("OK"), NULL, NULL);
		g_free(ctxt);
		return (-1);
	}
	return (1);
}

static void fpick_combo_changed(GtkWidget *widget, gpointer user_data)
{
	fpicker *fp = user_data;
	fpick_enter_dirname(fp, gtk_entry_get_text(GTK_ENTRY(fp->combo_entry)));
}

static void fpick_dialog_fn(char *key)
{
	*(key - *key) = *key;
}

static void fpick_file_dialog(fpicker *fp, char *fname)
{
	char fnm[PATHBUF], keys[] = { 0, 1, 2, 3, 4 }, *snm = NULL, *tmp;
	GtkWidget *win, *button, *label, *entry;
	GtkAccelGroup *ag = gtk_accel_group_new();
	int l, res;


	win = gtk_dialog_new();
	if (fname) sprintf(tmp = fnm, "%s / %s", _("Delete"), _("Rename"));
	else tmp = _("Create Directory");
	gtk_window_set_title(GTK_WINDOW(win), tmp);
	gtk_window_set_modal(GTK_WINDOW(win), TRUE);
	gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
	gtk_container_set_border_width(GTK_CONTAINER(win), 6);
	gtk_signal_connect_object(GTK_OBJECT(win), "destroy",
		GTK_SIGNAL_FUNC(fpick_dialog_fn), (gpointer)(keys + 1));
	label = gtk_label_new(fname ? _("Enter the new filename") :
		_("Enter the name of the new directory"));
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(win)->vbox), label, TRUE, FALSE, 8);

	entry = xpack5(GTK_DIALOG(win)->vbox, gtk_entry_new_with_max_length(PATHBUF));
	if (fname) gtk_entry_set_text(GTK_ENTRY(entry), fname);

	button = add_a_button(_("Cancel"), 2, GTK_DIALOG(win)->action_area, TRUE);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(win));
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags)0);

	if (fname)
	{
		button = add_a_button(_("Delete"), 2, GTK_DIALOG(win)->action_area, TRUE);
		gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(fpick_dialog_fn), (gpointer)(keys + 2));
	}

	button = add_a_button(fname ? _("Rename") : _("Create"), 2,
		GTK_DIALOG(win)->action_area, TRUE );
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(fpick_dialog_fn), (gpointer)(keys + (fname ? 3 : 4)));
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags)0);
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags)0);

	gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(main_window));
	gtk_widget_show_all(win);
	gdk_window_raise(win->window);
	gtk_widget_grab_focus(entry);

	gtk_window_add_accel_group(GTK_WINDOW(win), ag);

	while (!keys[0]) gtk_main_iteration();

	res = keys[0];
	if (res > 1)
	{
		strncpy(fnm, fp->txt_directory, PATHBUF);
		l = strlen(fnm);
		gtkncpy(fnm + l, gtk_entry_get_text(GTK_ENTRY(entry)), PATHBUF - l);
		gtk_widget_destroy(win);
		if (fname)
		{
			// The source name SHOULD NOT get truncated, ever
			char *ts = gtkncpy(NULL, fname, 0);
			snm = g_strconcat(fp->txt_directory, ts, NULL);
			g_free(ts);
		}
	}

	tmp = NULL;
	if (res == 2) // Delete file or directory
	{
		char *ts = g_strdup_printf(_("Do you really want to delete \"%s\" ?"), fname);
		l = alert_box(_("Warning"), ts, _("No"), _("Yes"), NULL);
		g_free(ts);
		if (l == 2)
		{
			if (remove(snm)) tmp = _("Unable to delete");
		}
	}
	else if (res == 3) // Rename file or directory
	{
		if (rename(snm, fnm)) tmp = _("Unable to rename");
	}
	else if (res == 4) // Create directory
	{
#ifdef WIN32
		if (mkdir(fnm))
#else
		if (mkdir(fnm, 0777))
#endif
			tmp = _("Unable to create directory");
	}
	g_free(snm);
	if (tmp) alert_box(_("Error"), tmp, _("OK"), NULL, NULL);
	else if (res > 1) fpick_scan_directory(fp, fp->txt_directory, FALSE);
}

static gboolean fpick_key_event(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	fpicker *fp = user_data;
	GtkCList *clist = GTK_CLIST(fp->clist);
	GList *list;
	char *txt_name, *txt_size;
	int row = 0;

	switch (event->keyval)
	{
	case GDK_End: case GDK_KP_End:
		row = clist->rows - 1;
	case GDK_Home: case GDK_KP_Home:
		clist->focus_row = row;
		gtk_clist_select_row(clist, row, 0);
		gtk_clist_moveto(clist, row, 0, 0.5, 0.5);
		return (TRUE);
	case GDK_Return: case GDK_KP_Enter:
		break;
	default: return (FALSE);
	}

	if (!(list = clist->selection)) return (FALSE);

	row = GPOINTER_TO_INT(list->data);

	txt_name = get_fname(clist, row);
	if (!txt_name) return (TRUE);
	
	gtk_clist_get_text(clist, row, FPICK_CLIST_SIZE, &txt_size);

	/* Directory selected */
	if (!txt_size[0]) fpick_enter_dir_via_list(fp, txt_name);
	/* File selected */
	else gtk_button_clicked(GTK_BUTTON(fp->ok_button));

	return (TRUE);
}

static gboolean fpick_click_event(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	fpicker *fp = user_data;
	char *fname;

	if ((event->button != 3) || (event->type != GDK_BUTTON_PRESS))
		return (FALSE);
	fname = get_fname(GTK_CLIST(fp->clist), GTK_CLIST(fp->clist)->focus_row);
	if (!strcmp(fname, "..")) return (TRUE); // Up-dir
#ifdef WIN32
	if (fname[1] == ':') return (TRUE); // Drive
#endif

	fpick_file_dialog(fp, fname);

	return (TRUE);
}

#undef _
#define _(X) X

static toolbar_item fpick_bar[] = {
	{ _("Up"),			-1, FPICK_ICON_UP, 0, xpm_up_xpm },
	{ _("Home"),			-1, FPICK_ICON_HOME, 0, xpm_home_xpm },
	{ _("Create New Directory"),	-1, FPICK_ICON_DIR, 0, xpm_newdir_xpm },
	{ _("Show Hidden Files"),	 0, FPICK_ICON_HIDDEN, 0, xpm_hidden_xpm },
	{ _("Case Insensitive Sort"),	 0, FPICK_ICON_CASE, 0, xpm_case_xpm },
	{ NULL }};

#undef _
#define _(X) __(X)

static void fpick_iconbar_click(GtkWidget *widget, gpointer user_data);

static GtkWidget *fpick_toolbar(GtkWidget **wlist)
{		
	GtkWidget *toolbar;

#if GTK_MAJOR_VERSION == 1
	toolbar = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);
#endif
#if GTK_MAJOR_VERSION == 2
	toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
#endif
	fill_toolbar(GTK_TOOLBAR(toolbar), fpick_bar, wlist,
		GTK_SIGNAL_FUNC(fpick_iconbar_click), NULL);
	gtk_widget_show(toolbar);

	return toolbar;
}

static void fpick_wildcard(GtkButton *button, gpointer user_data)
{
	fpicker *fp = user_data;
	char *ctxt, *ds, *nm, *mask = fp->txt_mask;

	if (!fp->allow_files) return; // File entry is hidden anyway
	ctxt = (char *)gtk_entry_get_text(GTK_ENTRY(fp->file_entry));
	/* Presume filename if called by user pressing "OK", pattern otherwise */
	if (button)
	{
		/* Filename must have some chars and no wildcards in it */
		if (ctxt[0] && !ctxt[strcspn(ctxt, "?*")]) return;
		/* Don't let pattern pass as filename */
		gtk_signal_emit_stop_by_name(GTK_OBJECT(button), "clicked");
	}

	/* Do we have directory in here? */
	ds = strrchr(ctxt, DIR_SEP);
#ifdef WIN32 /* Allow '/' separator too */
	if ((nm = strrchr(ds ? ds : ctxt, '/'))) ds = nm;
#endif

	/* Store filename pattern */
	nm = ds ? ds + 1 : ctxt;
	strncpy0(mask, nm, PATHTXT - 1);
	if (mask[0] && !strchr(mask, '*'))
	{
		/* Add a '*' at end if one isn't present in string */
		int l = strlen(mask);
		mask[l++] = '*';
		mask[l] = '\0';
	}

	while (ds) /* Have directory - enter it */
	{
		int res = fpick_enter_dirname(fp,
			ctxt = g_strndup(ctxt, ds + 1 - ctxt));
		g_free(ctxt);
		if (res <= 0) break; // If failed to open a new dir
		gtk_entry_set_text(GTK_ENTRY(fp->file_entry), nm); // Cut dir off
		return; // Opened a new dir - skip redisplay
	}

	/* Torture GtkCList into displaying only files that match pattern */
	fpick_clist_repattern(GTK_CLIST(fp->clist), mask);
	fpick_clist_scroll(GTK_CLIST(fp->clist)); // Scroll to selected row
}

/* "Tab completion" for entry field, like in GtkFileSelection */
static gboolean fpick_entry_key(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	if (event->keyval != GDK_Tab) return (FALSE);
#if GTK_MAJOR_VERSION == 1
	/* Return value alone doesn't stop GTK1 from running other handlers */
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif
	fpick_wildcard(NULL, user_data);
	return (TRUE);
}

GtkWidget *fpick_create(char *title, int flags)		// Initialize file picker
{
	static const short col_width[FPICK_CLIST_COLS] = {250, 64, 80, 150};
	char txt[64], *col_titles[FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN] =
		{ "", "", "", "", "", "" };
	GtkWidget *vbox1, *hbox1, *scrolledwindow1, *temp_hbox;
	GtkAccelGroup* ag = gtk_accel_group_new();
	fpicker *res = calloc(1, sizeof(fpicker));
	int i, w, l;

	if (!res) return NULL;

	col_titles[FPICK_CLIST_NAME] = _("Name");
	col_titles[FPICK_CLIST_SIZE] = _("Size");
	col_titles[FPICK_CLIST_TYPE] = _("Type");
	col_titles[FPICK_CLIST_DATE] = _("Modified");

	case_insensitive = inifile_get_gboolean("fpick_case_insensitive", TRUE );

	res->show_hidden = inifile_get_gboolean("fpick_show_hidden", FALSE );
	res->sort_direction = GTK_SORT_ASCENDING;
	res->sort_column = 0;
	res->allow_files = res->allow_dirs = TRUE;
	res->txt_directory[0] = res->txt_file[0] = '\0';

	res->window = add_a_window( GTK_WINDOW_TOPLEVEL, title, GTK_WIN_POS_NONE, TRUE );
	gtk_object_set_data(GTK_OBJECT(res->window), FP_DATA_KEY, res);

	win_restore_pos(res->window, "fs_window", 0, 0, 550, 500);

	vbox1 = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(res->window), vbox1);
	hbox1 = pack5(vbox1, gtk_hbox_new(FALSE, 0));

	// ------- Combo Box -------

	res->combo = xpack5(hbox1, gtk_combo_new());
	gtk_combo_disable_activate(GTK_COMBO(res->combo));
	res->combo_entry = GTK_COMBO(res->combo)->entry;

	for ( i=0; i<FPICK_COMBO_ITEMS; i++ )
	{
		sprintf(txt, "fpick_dir_%i", i);
		gtkuncpy(res->combo_items[i], inifile_get(txt, ""), PATHTXT);
		res->combo_list = g_list_append( res->combo_list, res->combo_items[i] );
	}
	gtk_combo_set_popdown_strings( GTK_COMBO(res->combo), res->combo_list );

	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(res->combo)->popwin),
		"hide", GTK_SIGNAL_FUNC(fpick_combo_changed), res);
	gtk_signal_connect(GTK_OBJECT(res->combo_entry),
		"activate", GTK_SIGNAL_FUNC(fpick_combo_changed), res);

	// !!! Show things now - toolbars in GTK+1 mishandle show_all
	gtk_widget_show_all(vbox1);

	// ------- Toolbar -------

	res->toolbar = pack5(hbox1, fpick_toolbar(res->icons));

	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(res->icons[FPICK_ICON_HIDDEN]),
			res->show_hidden );
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(res->icons[FPICK_ICON_CASE]),
			case_insensitive );
	gtk_object_set_data( GTK_OBJECT(res->toolbar), FP_DATA_KEY, res );
			// Set this after setting the toggles so any events are ignored

	// ------- CLIST - File List -------

	hbox1 = xpack5(vbox1, gtk_hbox_new(FALSE, 0));
	scrolledwindow1 = xpack5(hbox1, gtk_scrolled_window_new(NULL, NULL));
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scrolledwindow1),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_widget_show_all(hbox1);

	res->clist = gtk_clist_new(FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN);
	gtk_clist_set_compare_func(GTK_CLIST(res->clist), fpick_compare);
	gtk_signal_connect(GTK_OBJECT(res->clist), "destroy",
		GTK_SIGNAL_FUNC(fpick_clist_drop_backup), NULL);


	for (i = 0; i < (FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN); i++)
	{
		if ( i>=FPICK_CLIST_COLS )		// Hide the extra sorting columns
			gtk_clist_set_column_visibility( GTK_CLIST( res->clist ), i, FALSE );

		temp_hbox = gtk_hbox_new( FALSE, 0 );
		if ( i == FPICK_CLIST_TYPE || i == FPICK_CLIST_SIZE )
			pack_end(temp_hbox, gtk_label_new(col_titles[i]));
		else if ( i == FPICK_CLIST_NAME ) pack(temp_hbox, gtk_label_new(col_titles[i]));
		else xpack(temp_hbox, gtk_label_new(col_titles[i]));

		gtk_widget_show_all(temp_hbox);
		res->sort_arrows[i] = pack_end(temp_hbox,
			gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_IN));

		if ((i == FPICK_CLIST_SIZE) || (i == FPICK_CLIST_TYPE))
			gtk_clist_set_column_justification(GTK_CLIST(res->clist),
				i, GTK_JUSTIFY_RIGHT );
		else if (i == FPICK_CLIST_DATE)
			gtk_clist_set_column_justification(GTK_CLIST(res->clist),
				i, GTK_JUSTIFY_CENTER );

		gtk_clist_set_column_widget(GTK_CLIST(res->clist), i, temp_hbox);
		GTK_WIDGET_UNSET_FLAGS(GTK_CLIST(res->clist)->column[i].button,
			GTK_CAN_FOCUS);
	}
	gtk_widget_show( res->sort_arrows[0] );		// Show first arrow

	gtk_clist_column_titles_show( GTK_CLIST(res->clist) );
	gtk_clist_set_selection_mode( GTK_CLIST(res->clist), GTK_SELECTION_BROWSE );

	gtk_container_add(GTK_CONTAINER( scrolledwindow1 ), res->clist);
	gtk_widget_show( res->clist );

	gtk_signal_connect(GTK_OBJECT(res->clist), "click_column",
		GTK_SIGNAL_FUNC(fpick_column_button), res);
	gtk_signal_connect(GTK_OBJECT(res->clist), "select_row",
		GTK_SIGNAL_FUNC(fpick_select_row), res);
	gtk_signal_connect(GTK_OBJECT(res->clist), "key_press_event",
		GTK_SIGNAL_FUNC(fpick_key_event), res);
	gtk_signal_connect(GTK_OBJECT(res->clist), "button_press_event",
		GTK_SIGNAL_FUNC(fpick_click_event), res);


	// ------- Extra widget section -------

	gtk_widget_show(res->main_vbox = pack5(vbox1, gtk_vbox_new(FALSE, 0)));

	// ------- Entry Box -------

	hbox1 = pack5(vbox1, gtk_hbox_new(FALSE, 0));
	res->file_entry = xpack5(hbox1, gtk_entry_new_with_max_length(100));
	gtk_widget_show_all(hbox1);
	gtk_signal_connect(GTK_OBJECT(res->file_entry), "key_press_event",
		GTK_SIGNAL_FUNC(fpick_entry_key), res);

	// ------- Buttons -------

	hbox1 = pack5(vbox1, gtk_hbox_new(FALSE, 0));

	res->ok_button = pack_end5(hbox1, gtk_button_new_with_label(_("OK")));
	gtk_widget_set_usize(res->ok_button, 100, -1);
//	gtk_widget_add_accelerator (res->ok_button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
//	gtk_widget_add_accelerator (res->ok_button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_signal_connect(GTK_OBJECT(res->ok_button), "clicked",
		GTK_SIGNAL_FUNC(fpick_wildcard), res);

	res->cancel_button = pack_end5(hbox1, gtk_button_new_with_label(_("Cancel")));
	gtk_widget_set_usize(res->cancel_button, 100, -1);
	gtk_widget_add_accelerator (res->cancel_button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_widget_show_all(hbox1);

	gtk_window_add_accel_group(GTK_WINDOW (res->window), ag);

	gtk_signal_connect_object(GTK_OBJECT(res->file_entry), "activate",
		GTK_SIGNAL_FUNC(gtk_button_clicked), GTK_OBJECT(res->ok_button));

	if ( flags & FPICK_ENTRY ) gtk_widget_grab_focus(res->file_entry);
	else gtk_widget_grab_focus(res->clist);

	if ( flags & FPICK_DIRS_ONLY )
	{
		res->allow_files = FALSE;
		gtk_widget_hide(res->file_entry);
	}

	/* Ensure enough space for pixmaps */
	gtk_widget_realize(res->clist);
	gtk_clist_set_row_height(GTK_CLIST(res->clist), 0);
	if (GTK_CLIST(res->clist)->row_height < 16)
		gtk_clist_set_row_height(GTK_CLIST(res->clist), 16);

	/* Ensure width */
	for (i = 0; i < FPICK_CLIST_COLS; i++)
	{
		sprintf(txt, "fpick_col%i", i + 1);
		w = inifile_get_gint32(txt, col_width[i]);
//		if ((i == FPICK_CLIST_SIZE) || (i == FPICK_CLIST_DATE))
		if (i == FPICK_CLIST_SIZE)
		{
#if GTK_MAJOR_VERSION == 1
			l = gdk_string_width(res->clist->style->font,
#else /* if GTK_MAJOR_VERSION == 2 */
			l = gdk_string_width(gtk_style_get_font(res->clist->style),
#endif
				"8,888,888,888");
			if (w < l) w = l;
		}
		gtk_clist_set_column_width(GTK_CLIST(res->clist), i, w);
	}

	return (res->window);
}

void fpick_set_filename(GtkWidget *fp, char *name, int raw)
{
	fpicker *win = gtk_object_get_data(GTK_OBJECT(fp), FP_DATA_KEY);
	char txt[PATHTXT], *c;
	int i = 0;

	if (!raw)
	{
		/* Ensure that path is absolute */
		txt[0] = '\0';
		if ((name[0] != DIR_SEP)
#ifdef WIN32
			&& (name[0] != '/') && (name[1] != ':')
#endif
		)
		{
			getcwd(txt, PATHBUF - 1);
			i = strlen(txt);
			txt[i++] = DIR_SEP;
		}

		strnncat(txt, name, PATHTXT);
#ifdef WIN32 /* Separators can be of both types on Windows - unify */
		for (c = txt + i; (c = strchr(c, '/')); *c = DIR_SEP);
#endif

		/* Separate the filename */
		c = strrchr(txt, DIR_SEP);	// Guaranteed to be present now
		name += c - txt - i + 1;
		*c = '\0';
#ifdef WIN32 /* Name is UTF8 on input there */
		gtkncpy(txt + i, txt + i, PATHBUF - i);
#endif

		// Scan directory, populate boxes if successful
		if (!fpick_scan_directory(win, txt, FALSE)) return;

#ifndef WIN32 /* Name is in locale encoding on input */
		name = gtkuncpy(txt, name, PATHTXT);
#endif
	}
	gtk_entry_set_text(GTK_ENTRY(win->file_entry), name);
}

void fpick_destroy(GtkWidget *fp)			// Destroy structures and release memory
{
	fpicker *win = gtk_object_get_data(GTK_OBJECT(fp), FP_DATA_KEY);
	GtkCListColumn *col = GTK_CLIST(win->clist)->column;
	char txt[64], buf[PATHBUF];
	int i;

	for ( i=0; i<FPICK_COMBO_ITEMS; i++ )		// Remember recently used directories
	{
		gtkncpy(buf, win->combo_items[i], PATHBUF);
		sprintf(txt, "fpick_dir_%i", i);
		inifile_set(txt, buf);
	}

	for ( i=0; i<FPICK_CLIST_COLS; i++ )		// Remember column widths
	{
		sprintf(txt, "fpick_col%i", i + 1);
		inifile_set_gint32(txt, col[i].width);
	}

	inifile_set_gboolean("fpick_case_insensitive", case_insensitive);
	inifile_set_gboolean("fpick_show_hidden", win->show_hidden );

	free(win);
	destroy_dialog(fp);
}

static void fpick_iconbar_click(GtkWidget *widget, gpointer user_data)
{
	toolbar_item *item = user_data;
	fpicker *fp = gtk_object_get_data(GTK_OBJECT(widget->parent), FP_DATA_KEY);
	char nm[PATHBUF], fnm[PATHBUF];

	if (!fp) return;

	switch (item->ID)
	{
	case FPICK_ICON_UP:
		fpick_enter_dir_via_list(fp, "..");
		break;
	case FPICK_ICON_HOME:
		gtkncpy(nm, gtk_entry_get_text(GTK_ENTRY(fp->file_entry)), PATHBUF);
		snprintf(fnm, PATHBUF, "%s%c%s", get_home_directory(), DIR_SEP, nm);
		fpick_set_filename(fp->window, fnm, FALSE);
		break;
	case FPICK_ICON_DIR:
		fpick_file_dialog(fp, NULL);
		break;
	case FPICK_ICON_HIDDEN:
		fp->show_hidden = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		fpick_scan_directory(fp, fp->txt_directory, FALSE);
		break;
	case FPICK_ICON_CASE:
		case_insensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		fpick_sort_files(fp);
		fpick_clist_scroll(GTK_CLIST(fp->clist)); // Scroll to selected row
		break;
	}
}

void fpick_setup(GtkWidget *fp, GtkWidget *xtra, GtkSignalFunc ok_fn,
	GtkSignalFunc cancel_fn)
{
	fpicker *fpick = gtk_object_get_data(GTK_OBJECT(fp), FP_DATA_KEY);

	gtk_signal_connect_object(GTK_OBJECT(fpick->ok_button),
		"clicked", ok_fn, GTK_OBJECT(fp));
	gtk_signal_connect_object(GTK_OBJECT(fpick->cancel_button),
		"clicked", cancel_fn, GTK_OBJECT(fp));
	gtk_signal_connect_object(GTK_OBJECT(fpick->window),
		"delete_event", cancel_fn, GTK_OBJECT(fp));
	pack(fpick->main_vbox, xtra);
}

const char *fpick_get_filename(GtkWidget *fp, int raw)
{
	fpicker *fpick = gtk_object_get_data(GTK_OBJECT(fp), FP_DATA_KEY);
	char *txt = (char *)gtk_entry_get_text(GTK_ENTRY(fpick->file_entry));
	char *dir = fpick->txt_directory;

	if (raw) return (txt);
#if GTK_MAJOR_VERSION == 1 /* Same encoding everywhere */
	snprintf(fpick->txt_file, PATHTXT, "%s%s", dir, txt);
#elif defined WIN32 /* Upconvert dir to UTF8 */
	dir = gtkuncpy(NULL, dir, PATHTXT);
	snprintf(fpick->txt_file, PATHTXT, "%s%s", dir, txt);
	g_free(dir);
#else /* Convert filename to locale */
	txt = gtkncpy(NULL, txt, PATHBUF);
	snprintf(fpick->txt_file, PATHBUF, "%s%s", dir, txt);
	g_free(txt);
#endif
	return (fpick->txt_file);
}
#endif				/* mtPaint fpicker */




#ifdef U_FPICK_GTKFILESEL		/* GtkFileSelection based dialog */

GtkWidget *fpick_create(char *title, int flags)
{
	GtkWidget *fp = gtk_file_selection_new(title);

	if ( flags & FPICK_DIRS_ONLY )
	{
		gtk_widget_hide(GTK_WIDGET(GTK_FILE_SELECTION(fp)->selection_entry));
		gtk_widget_set_sensitive(GTK_WIDGET(GTK_FILE_SELECTION(fp)->file_list),
			FALSE);		// Don't let the user select files
	}

	return (fp);
}

void fpick_destroy(GtkWidget *fp)
{
	destroy_dialog(fp);
}

void fpick_setup(GtkWidget *fp, GtkWidget *xtra, GtkSignalFunc ok_fn,
	GtkSignalFunc cancel_fn)
{
#if GTK_MAJOR_VERSION == 1
	GtkAccelGroup* ag = gtk_accel_group_new();
#endif

	gtk_signal_connect_object(GTK_OBJECT(GTK_FILE_SELECTION(fp)->ok_button),
		"clicked", ok_fn, GTK_OBJECT(fp));

	gtk_signal_connect_object(GTK_OBJECT(GTK_FILE_SELECTION(fp)->cancel_button),
		"clicked", cancel_fn, GTK_OBJECT(fp));

	gtk_signal_connect_object(GTK_OBJECT(fp),
		"delete_event", cancel_fn, GTK_OBJECT(fp));

	pack(GTK_FILE_SELECTION(fp)->main_vbox, xtra);

#if GTK_MAJOR_VERSION == 1 /* No builtin accelerators - add our own */
	gtk_widget_add_accelerator(GTK_FILE_SELECTION(fp)->cancel_button,
		"clicked", ag, GDK_Escape, 0, (GtkAccelFlags)0);
	gtk_window_add_accel_group(GTK_WINDOW(fp), ag);
#endif
}

const char *fpick_get_filename(GtkWidget *fp, int raw)
{
	if (raw) return (gtk_entry_get_text(GTK_ENTRY(
		GTK_FILE_SELECTION(fp)->selection_entry)));
	else return (gtk_file_selection_get_filename(GTK_FILE_SELECTION(fp)));
}

void fpick_set_filename(GtkWidget *fp, char *name, int raw)
{
	if (raw) gtk_entry_set_text(GTK_ENTRY(
		GTK_FILE_SELECTION(fp)->selection_entry), name);
	else gtk_file_selection_set_filename(GTK_FILE_SELECTION(fp), name);
}

#endif		 /* GtkFileSelection based dialog */
