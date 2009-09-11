/*	main.c
	Copyright (C) 2004-2008 Mark Tyler and Dmitry Groshev

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

#include <stdlib.h>
#include <gtk/gtk.h>

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "viewer.h"
#include "inifile.h"
#include "canvas.h"
#include "layer.h"
#include "csel.h"
#include "spawn.h"


int main( int argc, char *argv[] )
{
	gboolean new_empty = TRUE, get_screenshot = FALSE;

	if (argc > 1)
	{
		if ( strcmp(argv[1], "--version") == 0 )
		{
			printf("%s\n\n", VERSION);
			exit(0);
		}
		if ( strcmp(argv[1], "--help") == 0 )
		{
			printf("%s\n\n"
				"Usage: mtpaint [option] [imagefile ... ]\n\n"
				"Options:\n"
				"  --help          Output this help\n"
				"  --version       Output version information\n"
				"  -s              Grab screenshot\n"
				"  -v              Start in viewer mode\n\n"
			, VERSION);
			exit(0);
		}
	}

	global_argv = argv;
	putenv( "G_BROKEN_FILENAMES=1" );	// Needed to read non ASCII filenames in GTK+2
	inifile_init("/.mtpaint");

#ifdef U_NLS
#if GTK_MAJOR_VERSION == 1
	/* !!! GTK+1 needs locale set up before gtk_init(); GTK+2, *QUITE*
	 * the opposite - WJ */
	setup_language();
#endif
#endif

	gtk_init( &argc, &argv );
	gtk_init_bugfixes();

#ifdef U_NLS
#if GTK_MAJOR_VERSION == 2
	/* !!! GTK+2 starts acting up if this is before gtk_init() - WJ */
	setup_language();
#endif
	bindtextdomain("mtpaint", MT_LANG_DEST);
	textdomain("mtpaint");
#if GTK_MAJOR_VERSION == 2
	bind_textdomain_codeset("mtpaint", "UTF-8");
#endif
#endif

	file_arg_start = 1;
	if (argc > 1)		// Argument received, so assume user is trying to load a file
	{
		if ( strcmp(argv[1], "-g") == 0 )	// Loading GIF animation frames
		{
			file_arg_start+=2;
			sscanf(argv[2], "%i", &preserved_gif_delay);
		}
		if ( strcmp(argv[1], "-v") == 0 )	// Viewer mode
		{
			file_arg_start++;
			viewer_mode = TRUE;
		}
		if ( strcmp(argv[1], "-s") == 0 )	// Screenshot
		{
			file_arg_start++;
			get_screenshot = TRUE;
		}
		if ( strstr(argv[0], "mtv") != NULL ) viewer_mode = TRUE;
	}
	files_passed = argc - file_arg_start;

	string_init();				// Translate static strings
	var_init();				// Load INI variables
	mem_init();				// Set up memory & back end
	layers_init();
	init_cols();

	if ( get_screenshot )
	{
		/* !!! Use 0th layer load being non-undoable */
		if (load_image(NULL, FS_LAYER_LOAD, FT_PIXMAP) == 1)
			new_empty = FALSE;	// Successfully grabbed so no new empty
		else get_screenshot = FALSE;	// Screenshot failed
	}
	main_init();					// Create main window

	if ( get_screenshot )
	{
		do_new_chores(FALSE);
		notify_changed();
	}
	else
	{
		if ((files_passed > 0) && !do_a_load(argv[file_arg_start]))
			new_empty = FALSE;
	}

	if ( new_empty )		// If no file was loaded, start with a blank canvas
	{
		create_default_image();
	}
	update_menus();

	gtk_main();
	spawn_quit();
	inifile_quit();

	return 0;
}
