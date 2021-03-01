/*	spawn.h
	Copyright (C) 2007-2021 Mark Tyler and Dmitry Groshev

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


#define FACTION_ROWS_TOTAL 25
#define FACTION_PRESETS_TOTAL 15
		// This number must exactly match the number of menu items declared

char *interpolate_line(char *pattern, int cmd);		// Percent variable interpolation

int spawn_process(char *argv[], char *directory);	// argv must be NULL terminated!
int spawn_expansion(char *cline, char *directory);
		// Replace %f with "current filename", then run via shell

void pressed_file_configure();
void pressed_file_action(int item);
void init_factions();					// Initialize file action menu

int get_tempname(char *buf, char *f, int type);		// Create tempfile for name
void spawn_quit();	// Delete temp files

// Default action codes
enum {
	DA_GIF_CREATE = 0,
	DA_GIF_PLAY,
	DA_GIF_EDIT,
	DA_SVG_CONVERT,
	DA_WEBP_PLAY,

	DA_NCODES
};

//	Run a regular default action
int run_def_action(int action, char *sname, char *dname, int delay);

/* All-in-one transport container for default actions */
typedef struct {
	char *sname;
	char *dname;
	int delay; // in 1/100s of a second
	int width, height;
} da_settings;

//	Run any default action
int run_def_action_x(int action, da_settings *settings);

int show_html(char *browser, char *docs);
