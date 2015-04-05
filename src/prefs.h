/*	prefs.h
	Copyright (C) 2005-2015 Mark Tyler and Dmitry Groshev

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

#define HANDBOOK_BROWSER_INI "docsBrowser"
#define HANDBOOK_LOCATION_INI "docsLocation"
#define DEFAULT_PAL_INI "defaultPalette"
#define DEFAULT_PAT_INI "defaultPatterns"
#define DEFAULT_THEME_INI "defaultTheme"

#define MAX_TF 100 /* Tablet tool factor scale */

int tablet_working, tablet_tool_use[3];		// Size, flow, opacity
int tablet_tool_factor[3];			// Size, flow, opacity

void pressed_preferences();
void init_tablet();				// Set up variables
