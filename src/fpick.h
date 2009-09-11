/*	fpick.h
	Copyright (C) 2007 Mark Tyler

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


// ------ Functions ------

fpicker *fpick_create( char *title );			// Initialize file picker
void fpick_set_filename( fpicker *win, char *name );	// Set the directory & filename
void fpick_destroy( fpicker *win );			// Destroy structures and release memory
void fpick_iconbar_click(GtkWidget *widget, gpointer data);

