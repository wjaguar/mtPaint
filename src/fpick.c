/*	fpick.c
	Copyright (C) 2007 Mark Tyler and Dmitry Groshev

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

#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <gtk/gtk.h>

#include <dirent.h>
#include <sys/stat.h>

#include "global.h"
#include "mygtk.h"
#include "inifile.h"
#include "memory.h"
#include "png.h"		// Needed by canvas.h
#include "canvas.h"
#include "toolbar.h"
#include "mainwindow.h"
#include "icons.h"
#include "fpick.h"

#define FP_DATA_KEY "mtPaint.fp_data"

#define FPICK_ICON_UP 0
#define FPICK_ICON_HOME 1
#define FPICK_ICON_DIR 2
#define FPICK_ICON_HIDDEN 3
#define FPICK_ICON_CASE 4
#define FPICK_ICON_TOT 5

#define FPICK_COMBO_ITEMS 16
#define FPICK_FILENAME_MAX_LEN 512

#define FPICK_CLIST_COLS 4
#define FPICK_CLIST_COLS_HIDDEN 2			// Used for sorting file/directory names

#define FPICK_CLIST_NAME 0
#define FPICK_CLIST_TYPE 1
#define FPICK_CLIST_SIZE 2
#define FPICK_CLIST_DATE 3

// ------ Main Data Structure ------

typedef struct
{
	int		allow_files,			// Allow the user to select files/directories
			allow_dirs,
			sort_column,			// Which column is being sorted in clist
			case_insensitive,		// For sorting
			show_hidden
			;

	char		combo_items[FPICK_COMBO_ITEMS][FPICK_FILENAME_MAX_LEN],
							// Stored as UTF8 in GTK+2
			txt_directory[FPICK_FILENAME_MAX_LEN],	// Current directory - Normal C string
			txt_file[FPICK_FILENAME_MAX_LEN]	// Full filename - Normal C string
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


/*
	Add thousand separator(s) to a string containing a number, e.g.
	str2thousands( dest, "1234567", 32, ',', '-', '.', 3, 0 );
	Creates:

	1234567		->	1,234,567
	-123456		->	-123,456
	-123456.7891	->	-123,456.7891


	Can also be used to separate any text between two delimeters, e.g.
	str2thousands( dest, "aaa>270907<bbb", 32, '-', '>', '<', 2, 0 );
	Creates:

	aaa>27-09-07<bbb

*/

static int str2thousands(	char *dest,		// Output buffer
			char *src,		// Input string (can be same as destination)
			int  dest_size,		// Size of output buffer
			char separator,		// Separator character to use, typically ','
			char minus,		// Minus character, typically '-'
			char dpoint,		// Decimal point character, typically '.'
			int  sep_num,		// Numbers to contain between separators, typically 3
			int  right_justify	// Should output string be right justified?
		)
{
	int	i, j, k, start, end, oldlen, newlen;
	char	*ch;


	if ( sep_num < 1 ) return -1;		// Sanity check

	oldlen = strlen(src);
	start  = 0;				// First character to be subjected to separating
	end    = oldlen - 1;			// Last character to be subjected to separating

	if ( (ch = strrchr(src, dpoint)) )	// Decimal point detected, adjust end accordingly
		end = ch - src - 1;
	if ( (ch = strchr(src, minus)) )	// Minus sign detected, adjust start accordingly
		start = ch - src + 1;
	if ( (end+1)<start ) return -1;		// Decimal point is before minus sign so bail out

	newlen = oldlen + (end - start) / sep_num;
	if ( newlen+1 > dest_size ) return -1;
				// Output buffer is not large enough to hold the result, so bail out

	i = oldlen - 1;				// Input buffer pointer
	k = dest_size - 1;			// Output buffer pointer
	dest[k--] = 0;				// Output string terminator

	while ( i>end ) dest[k--] = src[i--];	// Copy characters from the end to the '.'

	for ( j=0; i>=start; j++ )
	{
		if ( j && ((j % sep_num) == 0) )
			dest[k--] = separator;	// Add separator

		dest[k--] = src[i--];		// Copy char
	}

	while (i>=0) dest[k--] = src[i--];	// Copy characters from the '-' to the beginning

	if ( right_justify )
	{		 	// Right align so pad beginning of output with spaces
		 while ( k>=0 ) dest[k--] = ' ';
	}
	else
	{			// Left align so shift string flush to beginning
		k++;
		j = 0;
		do	dest[j++] = dest[k++];
		while	( dest[j-1] != 0 );
	}

	return 0;
}

static void str2lower(char *str, int len)		// Convert string to lowercase
{
	int i;

	for (i=0; i<len; i++)
	{
		if ( str[i] == 0 ) break;
		str[i] = tolower( str[i] );
	}
}

static void fpick_sort_files(fpicker *win)
{
	int i = win->sort_column;

	gtk_clist_set_sort_type( GTK_CLIST(win->clist), win->sort_direction );

	if ( i==0 )			// Sort by name so we use a hidden column for this
	{
		i = FPICK_CLIST_COLS;
		if ( !win->case_insensitive ) i++;
	}
	gtk_clist_set_sort_column( GTK_CLIST(win->clist), i );

	gtk_clist_sort( GTK_CLIST(win->clist) );
}

static void fpick_column_button( GtkWidget *widget, gint col, gpointer *pointer)
{
	fpicker *fp = gtk_object_get_data(GTK_OBJECT(widget), FP_DATA_KEY);
	GtkSortType direction;

	if ( !fp || col<0 || col>=FPICK_CLIST_COLS ) return;

	// reverse the sorting direction if the list is already sorted by this col
	if ( fp->sort_column == col )
		direction = (fp->sort_direction == GTK_SORT_ASCENDING
				? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
	else
	{
		direction = GTK_SORT_ASCENDING;

		gtk_widget_hide( fp->sort_arrows[fp->sort_column] );	// Hide old arrow
		gtk_widget_show( fp->sort_arrows[col] );		// Show new arrow
		fp->sort_column = col;
	}

	gtk_arrow_set(GTK_ARROW( fp->sort_arrows[col] ),
		direction == GTK_SORT_ASCENDING ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING,
		GTK_SHADOW_IN);

	fp->sort_direction = direction;

	fpick_sort_files(fp);
}


static void fpick_directory_new(fpicker *win, char *name)	// Register directory in combo
{
	int i;
	char txt[FPICK_FILENAME_MAX_LEN];

	gtkuncpy( txt, name, FPICK_FILENAME_MAX_LEN);

	for ( i=0 ; i<(FPICK_COMBO_ITEMS-1); i++ )	// Does this text already exist in the list?
		if ( !strcmp(txt, win->combo_items[i]) ) break;

	for ( ; i>0; i-- )				// Shuffle items down as needed
		strncpy( win->combo_items[i], win->combo_items[i-1], FPICK_FILENAME_MAX_LEN );

	strncpy( win->combo_items[0], txt, FPICK_FILENAME_MAX_LEN );		// Add item to list

	gtk_combo_set_popdown_strings( GTK_COMBO(win->combo), win->combo_list );
	gtk_entry_set_text( GTK_ENTRY(win->combo_entry), txt );
}

static int fpick_scan_directory(fpicker *win, char *name)	// Scan directory, populate widgets
{
	DIR	*dp;
	struct	dirent *ep;
	struct	stat buf;
	char	full_name[FPICK_FILENAME_MAX_LEN], *row_txt[FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN],
		txt_name[FPICK_FILENAME_MAX_LEN], txt_size[64], txt_date[64],
		tmp_txt[FPICK_FILENAME_MAX_LEN],
		txt_lowercase[FPICK_FILENAME_MAX_LEN],
		txt_normalcase[FPICK_FILENAME_MAX_LEN],
		dummy[] = "/"
		;

	row_txt[FPICK_CLIST_NAME] = txt_name;
	row_txt[FPICK_CLIST_TYPE] = "";
	row_txt[FPICK_CLIST_SIZE] = txt_size;
	row_txt[FPICK_CLIST_DATE] = txt_date;

	row_txt[FPICK_CLIST_COLS] = txt_lowercase;
	row_txt[FPICK_CLIST_COLS+1] = txt_normalcase;

	dummy[0] = DIR_SEP;
	if ( strlen(name)==0 ) name = dummy;

#ifdef WIN32
// In Windows, the root directory may be C:, so always add an extra DIR_SEP here
	if ( !strrchr(name, DIR_SEP) )
	{
		snprintf(tmp_txt, FPICK_FILENAME_MAX_LEN, "%s%c", name, DIR_SEP);
		dp = opendir(tmp_txt);
	}
	else dp = opendir(name);
#else
	dp = opendir(name);
#endif

	if (!dp) return FALSE;				// Directory doesn't exist so fail

	if ( name == dummy )
	{
		fpick_directory_new(win, "");
		win->txt_directory[0] = '\0';
	}
	else
	{
		fpick_directory_new(win, name);			// Register directory in combo
		strncpy(win->txt_directory, name, FPICK_FILENAME_MAX_LEN);
	}

	gtk_clist_clear( GTK_CLIST(win->clist) );	// Empty the list
	gtk_clist_freeze( GTK_CLIST(win->clist) );

	if ( strchr(name, DIR_SEP) && name!=dummy )
	{		// Clist entry to move to parent directory
		snprintf(txt_name, FPICK_FILENAME_MAX_LEN, "%c..", DIR_SEP);
		row_txt[FPICK_CLIST_TYPE] = "";
		txt_size[0] = '\0';
		txt_date[0] = '\0';
		txt_normalcase[0] = '\0';
		txt_lowercase[0] = '\0';
		gtk_clist_append( GTK_CLIST(win->clist), row_txt );
	}

	while ( (ep = readdir(dp)) )
	{
		snprintf(full_name, FPICK_FILENAME_MAX_LEN, "%s%c%s", name, DIR_SEP, ep->d_name);

		if (stat(full_name, &buf)>=0)		// Get file details
		{
			strftime(txt_date, 60, "%Y-%m-%d   %H:%M.%S", localtime(&buf.st_mtime) );

#ifdef WIN32
			if ( S_ISDIR(buf.st_mode) )
#else
			if ( ep->d_type == DT_DIR || S_ISDIR(buf.st_mode) )
#endif
			{		// Subdirectory
				if ( strcmp(ep->d_name, ".") != 0 && strcmp(ep->d_name, "..") != 0
					 && win->allow_dirs && !(!win->show_hidden && ep->d_name[0]=='.') )
				{		// Don't look at '.' or '..'
					snprintf(tmp_txt, FPICK_FILENAME_MAX_LEN, "%c %s",
							DIR_SEP, ep->d_name);

					strncpy(txt_normalcase, tmp_txt, FPICK_FILENAME_MAX_LEN);
					txt_normalcase[0] = 'a';
					strncpy(txt_lowercase, txt_normalcase, FPICK_FILENAME_MAX_LEN);
					str2lower(txt_lowercase, FPICK_FILENAME_MAX_LEN);

					gtkuncpy(txt_name, tmp_txt, FPICK_FILENAME_MAX_LEN);
					txt_size[0] = '\0';
					row_txt[FPICK_CLIST_TYPE] = "";

					gtk_clist_append( GTK_CLIST(win->clist), row_txt );
				}
			}
			else
			{		// File
				if ( win->allow_files && !(!win->show_hidden && ep->d_name[0]=='.') )
				{
					snprintf(tmp_txt, FPICK_FILENAME_MAX_LEN, "  %s", ep->d_name);
					gtkuncpy(txt_name, tmp_txt, FPICK_FILENAME_MAX_LEN);

					strncpy(txt_normalcase, tmp_txt, FPICK_FILENAME_MAX_LEN);
					txt_normalcase[0] = 'b';
					strncpy(txt_lowercase, txt_normalcase, FPICK_FILENAME_MAX_LEN);
					str2lower(txt_lowercase, FPICK_FILENAME_MAX_LEN);

#ifdef WIN32
					snprintf(tmp_txt, 64, "%I64u", (unsigned long long)buf.st_size);
#else
					snprintf(tmp_txt, 64, "%llu", (unsigned long long)buf.st_size);
#endif
					str2thousands( tmp_txt, tmp_txt, 20, ',', '-', '.', 3, 1 );
//	str2thousands( tmp_txt, "-1234567.34567", 20, ',', '-', '.', 3, 1 );
//	str2thousands( tmp_txt, "aaa>270907<bbb", 32, '-', '>', '<', 1, 0 );

					gtkuncpy(txt_size, tmp_txt, 64);

					strncpy(tmp_txt, txt_name, FPICK_FILENAME_MAX_LEN);
					str2lower(tmp_txt, FPICK_FILENAME_MAX_LEN);
					row_txt[FPICK_CLIST_TYPE] = strrchr(tmp_txt, '.');
					if (!row_txt[FPICK_CLIST_TYPE]) row_txt[FPICK_CLIST_TYPE] = "";
					else row_txt[FPICK_CLIST_TYPE]++;

					gtk_clist_append( GTK_CLIST(win->clist), row_txt );
				}
			}
		}
	}
	fpick_sort_files(win);
	gtk_clist_select_row(GTK_CLIST(win->clist), 0, 0);
	gtk_clist_thaw( GTK_CLIST(win->clist) );
	closedir(dp);

	return TRUE;
}

static void fpick_enter_dir_via_list( fpicker *fp, char *row_tex[1], char txt[3][FPICK_FILENAME_MAX_LEN] )
{
	char *c;

	strncpy( txt[0], fp->txt_directory, FPICK_FILENAME_MAX_LEN);
	gtkncpy( txt[1], row_tex[0]+2, FPICK_FILENAME_MAX_LEN);

	if ( row_tex[0][1] == '.' )			// Go to parent directory
	{
		if ( (c = strrchr(txt[0], DIR_SEP)) ) c[0] = '\0';
		strncpy( txt[2], txt[0], FPICK_FILENAME_MAX_LEN);
	}
	else snprintf( txt[2], FPICK_FILENAME_MAX_LEN, "%s%c%s", txt[0], DIR_SEP, txt[1]);

	if ( fpick_scan_directory(fp, txt[2]) )			// Enter new directory
	{
		gtkncpy( txt[0], gtk_entry_get_text( GTK_ENTRY(fp->file_entry)), FPICK_FILENAME_MAX_LEN);
		snprintf( fp->txt_file, FPICK_FILENAME_MAX_LEN,
				"%s%c%s", fp->txt_directory, DIR_SEP, txt[0]);
	}
}

static gint fpick_select_row(GtkWidget *widget, gint row, gint col, GdkEvent *event, gpointer *pointer)
{
	char *row_tex[1], txt[3][FPICK_FILENAME_MAX_LEN];
	fpicker *fp = gtk_object_get_data(GTK_OBJECT(widget), FP_DATA_KEY);
//printf("Select row %i\n",row);

	if (!fp) return FALSE;

	gtk_clist_get_text( GTK_CLIST(widget), row, 0, row_tex);
	if ( !row_tex[0] ) return FALSE;

	if ( row_tex[0][0] == DIR_SEP )		// Directory selected
	{
		if ( event && event->button.type == GDK_2BUTTON_PRESS )
		{				// Double click on directory so try to enter it
			fpick_enter_dir_via_list( fp, row_tex, txt );
		}
	}
	else					// File selected
	{
		gtk_entry_set_text( GTK_ENTRY(fp->file_entry), row_tex[0]+2 );

		strncpy( txt[0], fp->txt_directory, FPICK_FILENAME_MAX_LEN);
		gtkncpy( txt[1], row_tex[0]+2, FPICK_FILENAME_MAX_LEN);
		snprintf( fp->txt_file, FPICK_FILENAME_MAX_LEN,
				"%s%c%s", txt[0], DIR_SEP, txt[1]);

		if  (event && event->button.type == GDK_2BUTTON_PRESS )
		{				// Double click on file, so press OK button
			gtk_button_clicked (GTK_BUTTON (fp->ok_button));
		}
	}

	return TRUE;
}

static void fpick_combo_check( fpicker *fp )
{
	char txt[FPICK_FILENAME_MAX_LEN];

	gtkncpy( txt, gtk_entry_get_text( GTK_ENTRY(fp->combo_entry) ), FPICK_FILENAME_MAX_LEN );

	if ( strcmp(txt, fp->txt_directory) != 0 )	// Only do something if the directory is new
	{
		while ( strrchr(txt, DIR_SEP) && txt[strlen(txt)-1] == DIR_SEP )
		{
			txt[strlen(txt)-1] = '\0';	// Remove trailing directory separators
		}
		if ( fpick_scan_directory(fp, txt) )
		{
			gtkncpy( txt, gtk_entry_get_text( GTK_ENTRY(fp->file_entry)),
				FPICK_FILENAME_MAX_LEN);
			snprintf( fp->txt_file, FPICK_FILENAME_MAX_LEN,
				"%s%c%s", fp->txt_directory, DIR_SEP, txt);
		}
		else	// Directory doesn't exist so ask user if they want to create it
		{
			char txt2[FPICK_FILENAME_MAX_LEN];

			snprintf(txt2, FPICK_FILENAME_MAX_LEN, _("Could not access directory %s"), txt);
			alert_box(_("Error"), txt2, _("OK"), NULL, NULL);
		}
	}
}

static void fpick_combo_changed(GtkWidget *widget, gpointer user_data)
{
	fpicker *fp = gtk_object_get_data(GTK_OBJECT(user_data), FP_DATA_KEY);

	if ( !fp ) return;
	fpick_combo_check( fp );
}

static void fpick_name_activate(GtkWidget *widget, gpointer user_data)
{
	fpicker *fp = gtk_object_get_data(GTK_OBJECT(user_data), FP_DATA_KEY);

	if ( !fp ) return;

	gtk_button_clicked (GTK_BUTTON (fp->ok_button));
}

static void fpick_name_changed(GtkWidget *widget, gpointer user_data)
{
	char txt[FPICK_FILENAME_MAX_LEN];
	fpicker *fp = gtk_object_get_data(GTK_OBJECT(user_data), FP_DATA_KEY);

	if ( !fp ) return;

	gtkncpy( txt, gtk_entry_get_text( GTK_ENTRY(fp->file_entry) ), FPICK_FILENAME_MAX_LEN );
	snprintf( fp->txt_file, FPICK_FILENAME_MAX_LEN, "%s%c%s", fp->txt_directory, DIR_SEP, txt);
}


static gint fpick_key_event(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	fpicker *fp = gtk_object_get_data(GTK_OBJECT(user_data), FP_DATA_KEY);

	if ( fp && (event->keyval == GDK_KP_Enter || event->keyval == GDK_Return) )
	{
		GList *list = GTK_CLIST(fp->clist)->selection;
		char *row_tex[1], txt[3][FPICK_FILENAME_MAX_LEN];
		int row;

		if ( list )
		{
			row = GPOINTER_TO_INT(list->data);
			gtk_clist_get_text( GTK_CLIST(widget), row, 0, row_tex);
//printf("Key event enter/return - row=%i\n", row );

			if ( row_tex[0][0] == DIR_SEP )		// Directory selected
			{
				fpick_enter_dir_via_list( fp, row_tex, txt );
			}
			else					// File selected
			{
				gtk_button_clicked (GTK_BUTTON (fp->ok_button));
			}

			return TRUE;
		}
	}

	return FALSE;
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

GtkWidget *fpick_create(char *title)			// Initialize file picker
{
	char txt[128], *col_titles[FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN] =
				{ _("Name"), _("Type"), _("Size"), _("Modified"), "hidden1", "hidden2" };
	int i, col_width[FPICK_CLIST_COLS] = {250, 64, 80, 150};
	GtkWidget *vbox1, *hbox1, *scrolledwindow1, *temp_hbox, *temp_label;
	GtkAccelGroup* ag = gtk_accel_group_new();
	fpicker *res = calloc(1, sizeof(fpicker));

	if (!res) return NULL;

	res->sort_direction = GTK_SORT_ASCENDING;
	res->sort_column = 0;
	res->allow_files = TRUE;
	res->allow_dirs = TRUE;
	res->case_insensitive = inifile_get_gboolean("fpick_case_insensitive", TRUE );
	res->show_hidden = inifile_get_gboolean("fpick_show_hidden", FALSE );
	res->txt_directory[0] = '\0';
	res->txt_file[0] = '\0';

	res->window = add_a_window( GTK_WINDOW_TOPLEVEL, title, GTK_WIN_POS_NONE, TRUE );
	gtk_object_set_data(GTK_OBJECT(res->window), FP_DATA_KEY, res);

	win_restore_pos(res->window, "fs_window", 0, 0, 550, 500);

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox1);
	gtk_container_add (GTK_CONTAINER (res->window), vbox1);

	hbox1 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox1);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox1, FALSE, FALSE, 5);

	// ------- Combo Box -------

	res->combo = gtk_combo_new ();
	gtk_combo_disable_activate(GTK_COMBO(res->combo));
	gtk_widget_show (res->combo);
	gtk_box_pack_start (GTK_BOX (hbox1), res->combo, TRUE, TRUE, 5);
	res->combo_entry = GTK_COMBO (res->combo)->entry;
	gtk_widget_show (res->combo_entry);

	for ( i=0; i<FPICK_COMBO_ITEMS; i++ )
	{
		sprintf( txt, "fpick_dir_%i", i );
		strncpy( res->combo_items[i], inifile_get( txt, "" ),
				FPICK_FILENAME_MAX_LEN );
		res->combo_list = g_list_append( res->combo_list, res->combo_items[i] );
	}
	gtk_combo_set_popdown_strings( GTK_COMBO(res->combo), res->combo_list );
	gtk_object_set_data( GTK_OBJECT(res->combo), FP_DATA_KEY, res );

	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(res->combo)->popwin),
		"hide", GTK_SIGNAL_FUNC(fpick_combo_changed), res->combo);
	gtk_signal_connect(GTK_OBJECT(res->combo_entry),
		"activate", GTK_SIGNAL_FUNC(fpick_combo_changed), res->combo);

	// ------- Toolbar -------

	res->toolbar = fpick_toolbar(res->icons);

	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(res->icons[FPICK_ICON_HIDDEN]),
			res->show_hidden );
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(res->icons[FPICK_ICON_CASE]),
			res->case_insensitive );

	gtk_box_pack_start (GTK_BOX (hbox1), res->toolbar, FALSE, FALSE, 5);
//	pack(hbox1, res->toolbar);
	gtk_object_set_data( GTK_OBJECT(res->toolbar), FP_DATA_KEY, res );
			// Set this after setting the toggles so any events are ignored

	// ------- CLIST - File List -------

	hbox1 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox1);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox1, TRUE, TRUE, 5);

	scrolledwindow1 = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scrolledwindow1),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_show (scrolledwindow1);

	gtk_box_pack_start (GTK_BOX (hbox1), scrolledwindow1, TRUE, TRUE, 5);

	res->clist = gtk_clist_new(FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN);

	for (i = 0; i < (FPICK_CLIST_COLS + FPICK_CLIST_COLS_HIDDEN); i++)
	{
		if ( i>=FPICK_CLIST_COLS )		// Hide the extra sorting columns
			gtk_clist_set_column_visibility( GTK_CLIST( res->clist ), i, FALSE );

		temp_hbox = gtk_hbox_new( FALSE, 0 );
		temp_label = gtk_label_new( col_titles[i] );
		res->sort_arrows[i] = gtk_arrow_new( GTK_ARROW_DOWN, GTK_SHADOW_IN );

		gtk_box_pack_start( GTK_BOX(temp_hbox), temp_label, FALSE, TRUE, 0 );
		gtk_box_pack_end( GTK_BOX(temp_hbox), res->sort_arrows[i], FALSE, TRUE, 0 );

		if ( i==FPICK_CLIST_SIZE ) gtk_clist_set_column_justification(
					GTK_CLIST(res->clist), i, GTK_JUSTIFY_RIGHT );
		if ( i==FPICK_CLIST_DATE ) gtk_clist_set_column_justification(
					GTK_CLIST(res->clist), i, GTK_JUSTIFY_CENTER );

		gtk_widget_show( temp_label );
		gtk_widget_show( temp_hbox );
		gtk_clist_set_column_widget( GTK_CLIST( res->clist ), i, temp_hbox );
		GTK_WIDGET_UNSET_FLAGS( GTK_CLIST(res->clist)->column[i].button, GTK_CAN_FOCUS );
	}
	gtk_widget_show( res->sort_arrows[0] );		// Show first arrow
	gtk_arrow_set(GTK_ARROW(res->sort_arrows[0]),
		( res->sort_direction == GTK_SORT_ASCENDING ? GTK_ARROW_DOWN : GTK_ARROW_UP), GTK_SHADOW_IN);

	for ( i=0; i<FPICK_CLIST_COLS; i++ )
	{
		snprintf(txt, 32, "fpick_col%i", i+1);
		gtk_clist_set_column_width (GTK_CLIST(res->clist), i,
			inifile_get_gint32(txt, col_width[i] ) );
	}

//	gtk_clist_set_column_width (GTK_CLIST(res->clist), FPICK_CLIST_COLS, 300 );
//	gtk_clist_set_column_width (GTK_CLIST(res->clist), FPICK_CLIST_COLS+1, 300 );

	gtk_clist_column_titles_show( GTK_CLIST(res->clist) );
	gtk_clist_set_selection_mode( GTK_CLIST(res->clist), GTK_SELECTION_BROWSE );

	gtk_container_add(GTK_CONTAINER( scrolledwindow1 ), res->clist);
	gtk_widget_show( res->clist );

	gtk_object_set_data( GTK_OBJECT(res->clist), FP_DATA_KEY, res );
	gtk_signal_connect(GTK_OBJECT(res->clist), "click_column",
		GTK_SIGNAL_FUNC(fpick_column_button), res->clist);
	gtk_signal_connect(GTK_OBJECT(res->clist), "select_row",
		GTK_SIGNAL_FUNC(fpick_select_row), NULL);
	gtk_signal_connect(GTK_OBJECT(res->clist), "key_press_event",
		GTK_SIGNAL_FUNC(fpick_key_event), res->clist);


	// ------- Extra widget section -------

	res->main_vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (res->main_vbox);
	gtk_box_pack_start (GTK_BOX (vbox1), res->main_vbox, FALSE, FALSE, 5);

	// ------- Entry Box -------

	hbox1 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox1);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox1, FALSE, FALSE, 5);

	res->file_entry = gtk_entry_new_with_max_length(100);
	gtk_widget_show (res->file_entry);
	gtk_box_pack_start (GTK_BOX (hbox1), res->file_entry, TRUE, TRUE, 5);
	gtk_signal_connect( GTK_OBJECT(res->file_entry),
			"changed", GTK_SIGNAL_FUNC(fpick_name_changed), res->clist);
	gtk_signal_connect(GTK_OBJECT(res->file_entry),
		"activate", GTK_SIGNAL_FUNC(fpick_name_activate), res->clist);

	// ------- Buttons -------

	hbox1 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox1);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox1, FALSE, FALSE, 5);

	res->ok_button = gtk_button_new_with_label(_("OK"));
	gtk_widget_set_usize(res->ok_button, 100, -1);
	gtk_widget_show (res->ok_button);
	gtk_box_pack_end (GTK_BOX (hbox1), res->ok_button, FALSE, FALSE, 5);
//	gtk_widget_add_accelerator (res->ok_button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
//	gtk_widget_add_accelerator (res->ok_button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);

	res->cancel_button = gtk_button_new_with_label(_("Cancel"));
	gtk_widget_set_usize(res->cancel_button, 100, -1);
	gtk_widget_show (res->cancel_button);
	gtk_box_pack_end (GTK_BOX (hbox1), res->cancel_button, FALSE, FALSE, 5);
	gtk_widget_add_accelerator (res->cancel_button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_window_add_accel_group(GTK_WINDOW (res->window), ag);

	gtk_widget_grab_focus(res->file_entry);

	return (res->window);
}

void fpick_set_filename(GtkWidget *fp, char *name, int raw)
{
	fpicker *win = gtk_object_get_data(GTK_OBJECT(fp), FP_DATA_KEY);
	int i;
	char *c, d, txt[FPICK_FILENAME_MAX_LEN];

	if (raw)
	{
		gtk_entry_set_text(GTK_ENTRY(win->file_entry), name);
		return;
	}

// !!! BUG !!! - string might be constant & thus read-only
	c = strrchr( name, DIR_SEP );
	if (c)						// Extract filename & directory
	{
		d = *c;
		*c = '\0';				// Strip off filename
		i = fpick_scan_directory(win, name);	// Scan directory, populate boxes if successful
		*c = d;
		c++;
	}
	else
	{
		i = fpick_scan_directory(win, name);	// Scan directory, populate boxes if successful
		c = "";
	}

	if ( i )
	{
		strncpy(win->txt_file, name, FPICK_FILENAME_MAX_LEN);
		gtkuncpy(txt, name, FPICK_FILENAME_MAX_LEN);
		gtk_entry_set_text( GTK_ENTRY(win->file_entry), c );
	}
}

void fpick_destroy(GtkWidget *fp)			// Destroy structures and release memory
{
	fpicker *win = gtk_object_get_data(GTK_OBJECT(fp), FP_DATA_KEY);
	char txt[128];
	int i;
	GtkCList *clist = GTK_CLIST(win->clist);
	GtkCListColumn *col;

	for ( i=0; i<FPICK_COMBO_ITEMS; i++ )		// Remember recently used directories
	{
		sprintf( txt, "fpick_dir_%i", i );
		inifile_set( txt, win->combo_items[i] );
	}

	col = clist->column;
	for ( i=0; i<FPICK_CLIST_COLS; i++ )		// Remember column widths
	{
		snprintf(txt, 32, "fpick_col%i", i+1);
		inifile_set_gint32(txt, col->width );
		col++;
	}

	inifile_set_gboolean("fpick_case_insensitive", win->case_insensitive );
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
	int res=0;
	char txt[2][FPICK_FILENAME_MAX_LEN+1];
	GtkWidget *win, *button, *label, *entry;
	GtkAccelGroup* ag = gtk_accel_group_new();

	win = gtk_dialog_new();
	gtk_window_set_title( GTK_WINDOW(win), _("Create Directory") );
	gtk_window_set_modal( GTK_WINDOW(win), TRUE );
	gtk_window_set_position( GTK_WINDOW(win), GTK_WIN_POS_CENTER );
	gtk_container_set_border_width( GTK_CONTAINER(win), 6 );
	gtk_signal_connect( GTK_OBJECT(win), "destroy",
			GTK_SIGNAL_FUNC(fpick_newdir_destroy), (gpointer) &res );

	label = gtk_label_new( _("Enter the name of the new directory") );
	gtk_label_set_line_wrap( GTK_LABEL(label), TRUE );
	gtk_box_pack_start( GTK_BOX(GTK_DIALOG(win)->vbox), label, TRUE, FALSE, 8 );
	gtk_widget_show( label );

	entry = gtk_entry_new_with_max_length(100);
	gtk_widget_show(entry);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(win)->vbox), entry, TRUE, TRUE, 5);

	button = add_a_button( _("Cancel"), 2, GTK_DIALOG(win)->action_area, TRUE );
	gtk_signal_connect( GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(fpick_newdir_cancel), (gpointer) &res );
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button = add_a_button( _("OK"), 2, GTK_DIALOG(win)->action_area, TRUE );
	gtk_signal_connect( GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(fpick_newdir_ok), (gpointer) &res );
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);

	gtk_window_set_transient_for( GTK_WINDOW(win), GTK_WINDOW(main_window) );
	gtk_widget_show(win);
	gdk_window_raise(win->window);
	gtk_widget_grab_focus(entry);

	gtk_window_add_accel_group(GTK_WINDOW(win), ag);

	while ( res == 0 ) gtk_main_iteration();

	if ( res==2 ) gtk_widget_destroy( win );
	if ( res==1 )
	{
		gtkncpy( txt[0], gtk_entry_get_text( GTK_ENTRY(entry) ), FPICK_FILENAME_MAX_LEN );
		snprintf(txt[1], FPICK_FILENAME_MAX_LEN, "%s%c%s", fp->txt_directory, DIR_SEP, txt[0]);

		gtk_widget_destroy( win );

//printf("create directory %s\n", txt[1]);
#ifdef WIN32
		if ( mkdir(txt[1]) != 0 )
#else
		if ( mkdir(txt[1], 0755) != 0 )
#endif
		{
			snprintf(txt[0], FPICK_FILENAME_MAX_LEN, "%s %s",
					_("Unable to create directory"), txt[1]);
			alert_box(_("Error"), txt[0], _("OK"), NULL, NULL);
		}
		else fpick_scan_directory(fp, fp->txt_directory);
	}
}

static void fpick_up_directory(fpicker *fp)
{
	char *st = strrchr( fp->txt_directory, DIR_SEP );

	if ( st )
	{
		st[0] = 0;
		fpick_scan_directory(fp, fp->txt_directory);
	}
}

static void fpick_iconbar_click(GtkWidget *widget, gpointer data)
{
	fpicker *fp = gtk_object_get_data(GTK_OBJECT(widget->parent), FP_DATA_KEY);
	char txt[2][FPICK_FILENAME_MAX_LEN];
	gint j = (gint) data;

	if (!fp) return;

	switch (j)
	{
		case FPICK_ICON_UP:	fpick_up_directory(fp);
					break;
		case FPICK_ICON_HOME:	gtkncpy( txt[0], gtk_entry_get_text( GTK_ENTRY(fp->file_entry) ),
						FPICK_FILENAME_MAX_LEN );
					snprintf( txt[1], FPICK_FILENAME_MAX_LEN, "%s%c%s",
						get_home_directory(), DIR_SEP, txt[0]);
					fpick_set_filename(fp->window, txt[1], FALSE);
					break;
		case FPICK_ICON_DIR:	fpick_create_newdir(fp);
					break;
		case FPICK_ICON_HIDDEN:	fp->show_hidden = gtk_toggle_button_get_active(
							GTK_TOGGLE_BUTTON(widget) );
					fpick_scan_directory(fp, fp->txt_directory);
					break;
		case FPICK_ICON_CASE:	fp->case_insensitive = gtk_toggle_button_get_active(
							GTK_TOGGLE_BUTTON(widget) );
					fpick_scan_directory(fp, fp->txt_directory);
					break;
	}
}

void fpick_setup(GtkWidget *fp, GtkWidget *xtra, GtkSignalFunc ok_fn,
	GtkSignalFunc cancel_fn, int dirs_only)
{
	fpicker *fpick = gtk_object_get_data(GTK_OBJECT(fp), FP_DATA_KEY);

	if (dirs_only)
	{
		fpick->allow_files = FALSE;
		gtk_widget_hide(fpick->file_entry);
	}
	gtk_signal_connect_object(GTK_OBJECT(fpick->ok_button),
		"clicked", ok_fn, GTK_OBJECT(fp));
	gtk_signal_connect_object(GTK_OBJECT(fpick->cancel_button),
		"clicked", cancel_fn, GTK_OBJECT(fp));
	gtk_signal_connect_object(GTK_OBJECT(fpick->window),
		"delete_event", cancel_fn, GTK_OBJECT(fp));
	pack(fpick->main_vbox, xtra);
}

// !!! Encoding???
const char *fpick_get_filename(GtkWidget *fp, int raw)
{
	fpicker *fpick = gtk_object_get_data(GTK_OBJECT(fp), FP_DATA_KEY);
	if (raw) return (gtk_entry_get_text(GTK_ENTRY(fpick->file_entry)));
	return (fpick->txt_file);
}
