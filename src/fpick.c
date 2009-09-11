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

	char		combo_items[FPICK_COMBO_ITEMS][PATHTXT],
							// Stored as UTF8 in GTK+2
			txt_directory[PATHBUF],	// Current directory - Normal C string
			txt_file[PATHTXT]	// Full filename - Normal C string except on Windows
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

/* *** A WORD OF WARNING ***
 * Collating string comparison functions consider letter case only *AFTER*
 * letter itself for any (usable) value of LC_COLLATE except LC_COLLATE=C, and
 * in that latter case, order by character codepoint which frequently is
 * unrelated to alphabetical ordering. And on GTK+1 with multibyte charset,
 * casefolding will break down horribly, too.
 * What this means in practice: don't expect anything sane out of alphabetical
 * sorting outside of strict ASCII, and don't expect anything sane out of
 * case-sensitive sorting at all. - WJ */

#if GTK_MAJOR_VERSION == 1 // !!! Untested !!!

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

static int case_insensitive;



static void fpick_cleanse_path(char *txt)		// Clean up null terminated path
{
	char dds[] = { DIR_SEP, DIR_SEP, 0 }, *c;
	int l;

	// Remove multiple consecutive occurences of DIR_SEP
	while ( (c = strstr(txt, dds)) )
	{
		do
		{
			c[0] = c[1];
			c++;
		} while ( c[0] );	// strcpy disallows overlapping so here is my replacement
	}

	// Remove trailing directory separator if at least 2 exist:
	if ( strchr(txt, DIR_SEP) != strrchr(txt, DIR_SEP) )
	{
		l = strlen(txt) - 1;
		if ( txt[l] == DIR_SEP ) txt[l] = 0;
	}
}


static gint fpick_compare(GtkCList *clist, gconstpointer ptr1, gconstpointer ptr2)
{
	static const signed char sort_order[] = { FPICK_CLIST_NAME,
		FPICK_CLIST_DATE, FPICK_CLIST_SIZE, -1 }, updir[] = { DIR_SEP, ' ', '.', '.', 0 };
	GtkCListRow *r1 = (GtkCListRow *)ptr1, *r2 = (GtkCListRow *)ptr2;
	unsigned char *s1, *s2;
	int c = clist->sort_column, bits = 0, lvl = 0, d = 0;

	/* "/ .." Directory always goes first */
	s1 = GTK_CELL_TEXT(r1->cell[FPICK_CLIST_NAME])->text;
	s2 = GTK_CELL_TEXT(r2->cell[FPICK_CLIST_NAME])->text;

	d = (clist->sort_type == GTK_SORT_DESCENDING ? 1 : -1);
	if ( !strcmp(s1, updir) ) return d;
	if ( !strcmp(s2, updir) ) return -d;

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
		case FPICK_CLIST_DATE:
			if ((d = strcmp(s1, s2))) break;
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

static void fpick_sort_files(fpicker *win)
{
	gtk_clist_set_sort_type(GTK_CLIST(win->clist), win->sort_direction);
	gtk_clist_set_sort_column(GTK_CLIST(win->clist), win->sort_column);
	gtk_clist_sort(GTK_CLIST(win->clist));
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
	else
	{		// Different column clicked so use default value for that column
		GtkSortType cols[FPICK_CLIST_COLS];

		cols[FPICK_CLIST_NAME] = GTK_SORT_ASCENDING;
		cols[FPICK_CLIST_SIZE] = GTK_SORT_ASCENDING;
		cols[FPICK_CLIST_TYPE] = GTK_SORT_ASCENDING;
		cols[FPICK_CLIST_DATE] = GTK_SORT_DESCENDING;

		direction = cols[column];

		gtk_widget_hide( fp->sort_arrows[fp->sort_column] );	// Hide old arrow
		gtk_widget_show( fp->sort_arrows[column] );		// Show new arrow
		fp->sort_column = column;
	}

	gtk_arrow_set(GTK_ARROW(fp->sort_arrows[column]),
		direction == GTK_SORT_ASCENDING ? GTK_ARROW_DOWN : GTK_ARROW_UP,
		GTK_SHADOW_IN);

	fp->sort_direction = direction;

	fpick_sort_files(fp);
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

static const char root_dir[] = { DIR_SEP, 0 };

static int fpick_scan_directory(fpicker *win, char *name)	// Scan directory, populate widgets
{
	static const char updir[] = { DIR_SEP, ' ', '.', '.', 0 };
	static char nothing[] = "";
	DIR	*dp;
	struct	dirent *ep;
	struct	stat buf;
	char	*cp, *src, *dest, full_name[PATHBUF],
		*row_txt[FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN],
		txt_name[PATHTXT + 2], txt_size[64], txt_date[64],
		tmp_txt[PATHBUF + 2]
#if GTK_MAJOR_VERSION == 1
		, tmp_txt2[PATHBUF + 2]
#endif
		;
	int l, row;
	GdkPixmap *icon_dir, *icon_file, *icon;
	GdkBitmap *mask_dir, *mask_file, *mask;

	icon_dir = gdk_pixmap_create_from_xpm_d(main_window->window, &mask_dir, NULL, xpm_open_xpm);
	icon_file = gdk_pixmap_create_from_xpm_d(main_window->window, &mask_file, NULL, xpm_new_xpm);

	row_txt[FPICK_CLIST_NAME] = txt_name;
	row_txt[FPICK_CLIST_SIZE] = txt_size;
	row_txt[FPICK_CLIST_DATE] = txt_date;

// In Windows, the root directory may be C:, but opendir() can handle that
	dp = opendir(name[0] ? name : root_dir);
	if (!dp) return FALSE;				// Directory doesn't exist so fail

	fpick_directory_new(win, name);			// Register directory in combo
	strncpy0(win->txt_directory, name, PATHBUF);

	gtk_clist_clear( GTK_CLIST(win->clist) );	// Empty the list
	gtk_clist_freeze( GTK_CLIST(win->clist) );

	cp = strchr(name, DIR_SEP);
	if ( cp && cp[1]!=0 )
	{		// Clist entry to move to parent directory
		strcpy(txt_name, updir);
		row_txt[FPICK_CLIST_TYPE] = row_txt[FPICK_CLIST_H_UC] = "";
		row_txt[FPICK_CLIST_H_C] = txt_name;
		txt_size[0] = txt_date[0] = '\0';
		gtk_clist_append( GTK_CLIST(win->clist), row_txt );
	}

	while ( (ep = readdir(dp)) )
	{
		snprintf(full_name, PATHBUF, "%s%c%s", name, DIR_SEP, ep->d_name);

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

			snprintf(tmp_txt, PATHBUF + 2, "%c %s", DIR_SEP, ep->d_name);
			gtkuncpy(txt_name, tmp_txt, PATHTXT + 2);
			txt_size[0] = '\0';
		}
		else
		{		// File
			if (!win->allow_files) continue;

			snprintf(tmp_txt, PATHBUF + 2, "  %s", ep->d_name);
			gtkuncpy(txt_name, tmp_txt, PATHTXT + 2);
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

			if ((cp = strrchr(txt_name, '.')))
			{
#if GTK_MAJOR_VERSION == 1
				strcpy(tmp_txt2, cp + 1);
				row_txt[FPICK_CLIST_TYPE] = tmp_txt2;
				g_strup(tmp_txt2);
#else
				row_txt[FPICK_CLIST_TYPE] =
					g_utf8_strup(cp + 1, -1);
#endif
			}
		}
#if GTK_MAJOR_VERSION == 1
		row_txt[FPICK_CLIST_H_C] = tmp_txt;
#else /* if GTK_MAJOR_VERSION == 2 */
		row_txt[FPICK_CLIST_H_C] = g_utf8_collate_key(txt_name, -1);
#endif
		row_txt[FPICK_CLIST_H_UC] = isort_key(txt_name);
		row = gtk_clist_append( GTK_CLIST(win->clist), row_txt );
		g_free(row_txt[FPICK_CLIST_H_UC]);

		if ( txt_name[0] == DIR_SEP )
		{
			icon = icon_dir;
			mask = mask_dir;
		}
		else
		{
			icon = icon_file;
			mask = mask_file;
		}
		gtk_clist_set_pixtext( GTK_CLIST(win->clist), row, FPICK_CLIST_NAME,
			row_txt[FPICK_CLIST_NAME] + 2, 4, icon, mask );

#if GTK_MAJOR_VERSION == 2
		if (row_txt[FPICK_CLIST_TYPE] != nothing)
			g_free(row_txt[FPICK_CLIST_TYPE]);
		g_free(row_txt[FPICK_CLIST_H_C]);
#endif
	}
	fpick_sort_files(win);
	gtk_clist_select_row(GTK_CLIST(win->clist), 0, 0);
	gtk_clist_thaw( GTK_CLIST(win->clist) );
	closedir(dp);
	gdk_pixmap_unref(icon_dir);
	gdk_pixmap_unref(icon_file);

	return TRUE;
}

static void fpick_enter_dir_via_list(fpicker *fp, char *name)
{
	char ndir[PATHBUF], *dir = fp->txt_directory, *c;

	if (!strcmp(name, ".."))	// Go to parent directory
	{
		c = strrchr(dir, DIR_SEP);
		if (!c || (c == dir)) strcpy(ndir, root_dir);
		else strncpy0(ndir, dir, c - dir+1);
	}
	else
	{
		c = gtkncpy(NULL, name, PATHBUF);
		snprintf(ndir, PATHBUF, "%s%c%s", dir, DIR_SEP, c);
		g_free(c);
	}
	fpick_cleanse_path(ndir);
	fpick_scan_directory(fp, ndir);	// Enter new directory
}

static void fpick_select_row(GtkCList *clist, gint row, gint col,
	GdkEventButton *event, gpointer user_data)
{
	fpicker *fp = user_data;
	char *txt_name = NULL, *txt_dir;
	int dclick = event && (event->type == GDK_2BUTTON_PRESS);
	GtkCellType cell_type;

	cell_type = gtk_clist_get_cell_type(clist, row, FPICK_CLIST_NAME);

	if ( cell_type == GTK_CELL_PIXTEXT )
		gtk_clist_get_pixtext(clist, row, FPICK_CLIST_NAME, &txt_name, NULL, NULL, NULL);
	if ( cell_type == GTK_CELL_TEXT )
	{
		gtk_clist_get_text(clist, row, FPICK_CLIST_NAME, &txt_name);
		txt_name += 2;	// This is the non icon '/ ..' so add 2 to get real name
	}
	
	gtk_clist_get_text(clist, row, FPICK_CLIST_H_C, &txt_dir);
	if ( !txt_name ) return;

	if ( txt_dir[0] == DIR_SEP )		// Directory selected
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

static void fpick_combo_changed(GtkWidget *widget, gpointer user_data)
{
	fpicker *fp = user_data;
	char txt[PATHBUF], *ctxt;

	ctxt = (char *)gtk_entry_get_text(GTK_ENTRY(fp->combo_entry));
	gtkncpy(txt, ctxt, PATHBUF);

	fpick_cleanse_path(txt);

	// Only do something if the directory is new
	if (!strcmp(txt, fp->txt_directory)) return;

	if (!fpick_scan_directory(fp, txt))
	{	// Directory doesn't exist so ask user if they want to create it
		ctxt = g_strdup_printf(_("Could not access directory %s"), ctxt);
		alert_box(_("Error"), ctxt, _("OK"), NULL, NULL);
		g_free(ctxt);
	}
}

static gboolean fpick_key_event(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	fpicker *fp = user_data;
	GList *list;
	char *txt_name = NULL, *txt_dir;
	int row;
	GtkCellType cell_type;

	if ( event->keyval == GDK_Home )
	{
		GTK_CLIST(fp->clist)->focus_row = 0;
		gtk_clist_select_row(GTK_CLIST(fp->clist), 0, 0);
		gtk_clist_moveto(GTK_CLIST(fp->clist), 0, 0, 0.5, 0.5);
		return TRUE;
	}

	if ( event->keyval == GDK_End )
	{
		int last_row = GTK_CLIST(fp->clist)->rows - 1;

		GTK_CLIST(fp->clist)->focus_row = last_row;
		gtk_clist_select_row(GTK_CLIST(fp->clist), last_row, 0);
		gtk_clist_moveto(GTK_CLIST(fp->clist), last_row, 0, 0.5, 0.5);
		return TRUE;
	}

	if ((event->keyval != GDK_KP_Enter) && (event->keyval != GDK_Return))
		return (FALSE);
	if (!(list = GTK_CLIST(fp->clist)->selection)) return (FALSE);

	row = GPOINTER_TO_INT(list->data);

	cell_type = gtk_clist_get_cell_type(GTK_CLIST(widget), row, FPICK_CLIST_NAME);

	if ( cell_type == GTK_CELL_PIXTEXT )
		gtk_clist_get_pixtext(GTK_CLIST(widget), row, FPICK_CLIST_NAME, &txt_name,
				NULL, NULL, NULL);
	if ( cell_type == GTK_CELL_TEXT )
	{
		gtk_clist_get_text(GTK_CLIST(widget), row, FPICK_CLIST_NAME, &txt_name);
		txt_name += 2;	// This is the non icon '/ ..' so add 2 to get real name
	}
	
	gtk_clist_get_text(GTK_CLIST(widget), row, FPICK_CLIST_H_C, &txt_dir);
	if ( !txt_name ) return FALSE;

	/* Directory selected */
	if (txt_dir[0] == DIR_SEP) fpick_enter_dir_via_list(fp, txt_name);
	/* File selected */
	else gtk_button_clicked (GTK_BUTTON (fp->ok_button));

	return (TRUE);
}

#undef _
#define _(X) X

static toolbar_item fpick_bar[] = {
	{ FPICK_ICON_UP,	-1, 0, 0, 0, _("Up"), xpm_up_xpm },
	{ FPICK_ICON_HOME,	-1, 0, 0, 0, _("Home"), xpm_home_xpm },
	{ FPICK_ICON_DIR,	-1, 0, 0, 0, _("Create New Directory"), xpm_newdir_xpm },
	{ FPICK_ICON_HIDDEN,	 0, 0, 0, 0, _("Show Hidden Files"), xpm_hidden_xpm },
	{ FPICK_ICON_CASE,	 0, 0, 0, 0, _("Case Insensitive Sort"), xpm_case_xpm },
	{ 0, 0, 0, 0, 0, NULL, NULL }};

#undef _
#define _(X) __(X)

static void fpick_iconbar_click(GtkWidget *widget, gpointer data);

static GtkWidget *fpick_toolbar(GtkWidget **wlist)
{		
	int i;
	GtkWidget *toolbar;

#if GTK_MAJOR_VERSION == 1
	toolbar = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);
#endif
#if GTK_MAJOR_VERSION == 2
	toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
#endif
	fill_toolbar(GTK_TOOLBAR(toolbar), fpick_bar,
		GTK_SIGNAL_FUNC(fpick_iconbar_click), 0, NULL, 0);
	gtk_widget_show(toolbar);

	for (i = 0; i < FPICK_ICON_TOT; i++)
		wlist[i] = fpick_bar[i].widget;

	return toolbar;
}

GtkWidget *fpick_create(char *title, int flags)			// Initialize file picker
{
	char txt[64], *col_titles[FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN] =
		{ "", "", "", "", "", "" };
	int i, col_width[FPICK_CLIST_COLS] = {250, 64, 80, 150};
	GtkWidget *vbox1, *hbox1, *scrolledwindow1, *temp_hbox;
	GtkAccelGroup* ag = gtk_accel_group_new();
	fpicker *res = calloc(1, sizeof(fpicker));

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
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_show_all(hbox1);

	res->clist = gtk_clist_new(FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN);
	gtk_clist_set_compare_func(GTK_CLIST(res->clist), fpick_compare);

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

		if ( i==FPICK_CLIST_SIZE ) gtk_clist_set_column_justification(
					GTK_CLIST(res->clist), i, GTK_JUSTIFY_RIGHT );
		if ( i==FPICK_CLIST_TYPE ) gtk_clist_set_column_justification(
					GTK_CLIST(res->clist), i, GTK_JUSTIFY_RIGHT );
		if ( i==FPICK_CLIST_DATE ) gtk_clist_set_column_justification(
					GTK_CLIST(res->clist), i, GTK_JUSTIFY_CENTER );

		gtk_clist_set_column_widget(GTK_CLIST(res->clist), i, temp_hbox);
		GTK_WIDGET_UNSET_FLAGS(GTK_CLIST(res->clist)->column[i].button,
			GTK_CAN_FOCUS);
	}
	gtk_widget_show( res->sort_arrows[0] );		// Show first arrow

	for ( i=0; i<FPICK_CLIST_COLS; i++ )
	{
		sprintf(txt, "fpick_col%i", i+1);
		gtk_clist_set_column_width (GTK_CLIST(res->clist), i,
			inifile_get_gint32(txt, col_width[i] ) );
	}

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


	// ------- Extra widget section -------

	gtk_widget_show(res->main_vbox = pack5(vbox1, gtk_vbox_new(FALSE, 0)));

	// ------- Entry Box -------

// !!! What the box is for?
	hbox1 = pack5(vbox1, gtk_hbox_new(FALSE, 0));
	res->file_entry = xpack5(hbox1, gtk_entry_new_with_max_length(100));
	gtk_widget_show_all(hbox1);

	// ------- Buttons -------

	hbox1 = pack5(vbox1, gtk_hbox_new(FALSE, 0));

	res->ok_button = pack_end5(hbox1, gtk_button_new_with_label(_("OK")));
	gtk_widget_set_usize(res->ok_button, 100, -1);
//	gtk_widget_add_accelerator (res->ok_button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
//	gtk_widget_add_accelerator (res->ok_button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);

	res->cancel_button = pack_end5(hbox1, gtk_button_new_with_label(_("Cancel")));
	gtk_widget_set_usize(res->cancel_button, 100, -1);
	gtk_widget_add_accelerator (res->cancel_button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_widget_show_all(hbox1);

	gtk_window_add_accel_group(GTK_WINDOW (res->window), ag);

	gtk_signal_connect_object(GTK_OBJECT(res->file_entry), "activate",
		GTK_SIGNAL_FUNC(gtk_button_clicked), GTK_BUTTON_0(res->ok_button));

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
		if (!fpick_scan_directory(win, txt)) return;

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

static void fpick_newdir_destroy( GtkWidget *widget, int *data )
{
	*data = 10;
	gtk_widget_destroy(widget);
}

static void fpick_newdir_cancel( GtkWidget *widget, int *data )
{
	*data = 2;
}

static void fpick_newdir_ok( GtkWidget *widget, int *data )
{
	*data = 1;
}

static void fpick_create_newdir(fpicker *fp)
{
	char nm[PATHBUF], fnm[PATHBUF];
	GtkWidget *win, *button, *label, *entry;
	GtkAccelGroup *ag = gtk_accel_group_new();
	int res=0;

	win = gtk_dialog_new();
	gtk_window_set_title( GTK_WINDOW(win), _("Create Directory") );
	gtk_window_set_modal( GTK_WINDOW(win), TRUE );
	gtk_window_set_position( GTK_WINDOW(win), GTK_WIN_POS_CENTER );
	gtk_container_set_border_width( GTK_CONTAINER(win), 6 );
	gtk_signal_connect(GTK_OBJECT(win), "destroy",
		GTK_SIGNAL_FUNC(fpick_newdir_destroy), &res);

	label = gtk_label_new( _("Enter the name of the new directory") );
	gtk_label_set_line_wrap( GTK_LABEL(label), TRUE );
	gtk_box_pack_start( GTK_BOX(GTK_DIALOG(win)->vbox), label, TRUE, FALSE, 8 );

	entry = xpack5(GTK_DIALOG(win)->vbox, gtk_entry_new_with_max_length(100));

	button = add_a_button( _("Cancel"), 2, GTK_DIALOG(win)->action_area, TRUE );
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(fpick_newdir_cancel), &res);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button = add_a_button( _("OK"), 2, GTK_DIALOG(win)->action_area, TRUE );
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(fpick_newdir_ok), &res);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);

	gtk_window_set_transient_for( GTK_WINDOW(win), GTK_WINDOW(main_window) );
	gtk_widget_show_all(win);
	gdk_window_raise(win->window);
	gtk_widget_grab_focus(entry);

	gtk_window_add_accel_group(GTK_WINDOW(win), ag);

	while (!res) gtk_main_iteration();

	if (res == 2) gtk_widget_destroy(win);
	else if (res == 1)
	{
		gtkncpy(nm, gtk_entry_get_text(GTK_ENTRY(entry)), PATHBUF);
		snprintf(fnm, PATHBUF, "%s%c%s", fp->txt_directory, DIR_SEP, nm);

		gtk_widget_destroy( win );

#ifdef WIN32
		if (mkdir(fnm))
#else
		if (mkdir(fnm, 0777))
#endif
		{
			alert_box(_("Error"), _("Unable to create directory"),
				_("OK"), NULL, NULL);
		}
		else fpick_scan_directory(fp, fp->txt_directory);
	}
}

static void fpick_up_directory(fpicker *fp)
{
	char *st = strrchr( fp->txt_directory, DIR_SEP );

// !!! Have to handle Windows drives too
	if ( st )
	{
		if ( strchr( fp->txt_directory, DIR_SEP ) == st )
		{
			st[1] = 0;		// Don't lose the final DIR_SEP on GNU systems
		}
		else
		{
			st[0] = 0;
		}
		fpick_scan_directory(fp, fp->txt_directory);
	}
}

static void fpick_iconbar_click(GtkWidget *widget, gpointer data)
{
	fpicker *fp = gtk_object_get_data(GTK_OBJECT(widget->parent), FP_DATA_KEY);
	char nm[PATHBUF], fnm[PATHBUF];
	int j = (int)data;

	if (!fp) return;

	switch (j)
	{
	case FPICK_ICON_UP:
		fpick_up_directory(fp);
		break;
	case FPICK_ICON_HOME:
		gtkncpy(nm, gtk_entry_get_text(GTK_ENTRY(fp->file_entry)), PATHBUF);
		snprintf(fnm, PATHBUF, "%s%c%s", get_home_directory(), DIR_SEP, nm);
		fpick_set_filename(fp->window, fnm, FALSE);
		break;
	case FPICK_ICON_DIR:
		fpick_create_newdir(fp);
		break;
	case FPICK_ICON_HIDDEN:
		fp->show_hidden = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		fpick_scan_directory(fp, fp->txt_directory);
		break;
	case FPICK_ICON_CASE:
		case_insensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		fpick_sort_files(fp);
		break;
	}
}

void fpick_setup(GtkWidget *fp, GtkWidget *xtra, GtkSignalFunc ok_fn, GtkSignalFunc cancel_fn)
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
	snprintf(fpick->txt_file, PATHTXT, "%s%c%s", dir, DIR_SEP, txt);
#elif defined WIN32 /* Upconvert dir to UTF8 */
	dir = gtkuncpy(NULL, dir, PATHTXT);
	snprintf(fpick->txt_file, PATHTXT, "%s%c%s", dir, DIR_SEP, txt);
	g_free(dir);
#else /* Convert filename to locale */
	txt = gtkncpy(NULL, txt, PATHBUF);
	snprintf(fpick->txt_file, PATHBUF, "%s%c%s", dir, DIR_SEP, txt);
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

void fpick_setup(GtkWidget *fp, GtkWidget *xtra, GtkSignalFunc ok_fn, GtkSignalFunc cancel_fn)
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
