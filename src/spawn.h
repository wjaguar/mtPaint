/*	spawn.h
	Copyright (C) 2007 Mark Tyler

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



int spawn_process(char *argv[], char *directory);	// argv must be NULL terminated!
int spawn_expansion(char *cline, char *directory);
		// Replace %f with "current filename", then run via shell

void pressed_file_configure();
void pressed_file_action(int item);
void init_factions();					// Initialize file action menu

void spawn_quit();	// Delete temp files

// Default action codes
#define DA_GIF_CREATE  0
#define DA_GIF_EXPLODE 1
#define DA_GIF_PLAY    2
#define DA_GIF_EDIT    3

//	Run some default action
int run_def_action(int action, char *sname, char *dname, int delay);
