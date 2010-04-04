/*	help.c
	Copyright (C) 2004-2010 Mark Tyler and Dmitry Groshev

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

#undef _
#define _(X) X

#define HELP_PAGE_COUNT 4
static char *help_titles[HELP_PAGE_COUNT] = {
_("General"),
_("Keyboard shortcuts"),
_("Mouse shortcuts"),
_("Credits"),
};

static char *help_page0[] = {
_("mtPaint 3.40 - Copyright (C) 2004-2010 The Authors\n"),
_("See 'Credits' section for a list of the authors.\n"),
_("mtPaint is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.\n"),
_("mtPaint is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.\n"),
_("mtPaint is a simple GTK+1/2 painting program designed for creating icons and pixel based artwork. It can edit indexed palette or 24 bit RGB images and offers basic painting and palette manipulation tools. It also has several other more powerful features such as channels, layers and animation. Due to its simplicity and lack of dependencies it runs well on GNU/Linux, Windows and older PC hardware.\n"),
_("There is full documentation of mtPaint's features contained in a handbook.  If you don't already have this, you can download it from the mtPaint website.\n"),
_("If you like mtPaint and you want to keep up to date with new releases, or you want to give some feedback, then the mailing lists may be of interest to you:\n"),
_("http://sourceforge.net/mail/?group_id=155874"),
NULL };
static char *help_page1[] = {
_("  Ctrl-N            Create new image"),
_("  Ctrl-O            Open Image"),
_("  Ctrl-S            Save Image"),
_("  Ctrl-Shift-S      Save layers file"),
_("  Ctrl-Q            Quit program\n"),
_("  Ctrl-A            Select whole image"),
_("  Escape            Select nothing, cancel paste box"),
_("  Ctrl-C            Copy selection to clipboard"),
_("  Ctrl-X            Copy selection to clipboard, and then paint current pattern to selection area"),
_("  Ctrl-V            Paste clipboard to centre of current view"),
_("  Ctrl-K            Paste clipboard to location it was copied from"),
_("  Ctrl-Shift-V      Paste clipboard to new layer"),
_("  Enter/Return      Commit paste to canvas\n"),
_("  Arrow keys        Paint Mode - Move the mouse pointer"),
_("  Arrow keys        Selection Mode - Nudge selection box or paste box by one pixel"),
_("  Shift+Arrow keys  Nudge mouse pointer, selection box or paste box by x pixels - x is defined by the Preferences window"),
_("  Ctrl+Arrows       Move layer or resize selection box\n"),
_("  Enter/Return      Paint Mode - Simulate left click"),
_("  Backspace         Paint Mode - Simulate right click\n"),
_("  [ or ]            Change colour A to the next or previous palette item"),
_("  Shift+[ or ]      Change colour B to the next or previous palette item\n"),
_("  Delete            Crop image to selection"),
_("  Insert            Transform colours - i.e. Brightness, Contrast, Saturation, Posterize, Gamma"),
_("  Ctrl-G            Greyscale the image"),
_("  Shift-Ctrl-G      Greyscale the image (Gamma corrected)"),
_("  Ctrl+M            Mirror the image"),
_("  Shift-Ctrl-I      Invert the image\n"),
_("  Ctrl-T            Draw a rectangle around the selection area with the current fill"),
_("  Ctrl-Shift-T      Fill in the selection area with the current fill"),
_("  Ctrl-L            Draw an ellipse spanning the selection area"),
_("  Ctrl-Shift-L      Draw a filled ellipse spanning the selection area\n"),
_("  Ctrl-E            Edit the RGB values for colours A & B"),
_("  Ctrl-W            Edit all palette colours\n"),
_("  Ctrl-P            Preferences"),
_("  Ctrl-I            Information\n"),
_("  Ctrl-Z            Undo last action"),
_("  Ctrl-R            Redo an undone action\n"),
_("  Shift-T           Text Tool (GTK+)"),
_("  T                 Text Tool (FreeType)\n"),
_("  V                 View Window"),
_("  L                 Layers Window\n"),
_("  X                 Swap Colours A & B"),
_("  E                 Choose Colour\n"),
_("  A                 Draw open arrow head when using the line tool (size set by flow setting)"),
_("  S                 Draw closed arrow head when using the line tool (size set by flow setting)\n"),
_("  +,=               Main edit window - Zoom in"),
_("  -                 Main edit window - Zoom out"),
_("  Shift +,=         View window - Zoom in"),
_("  Shift -           View window - Zoom out\n"),
_("  1                 10% zoom"),
_("  2                 25% zoom"),
_("  3                 50% zoom"),
_("  4                 100% zoom"),
_("  5                 400% zoom"),
_("  6                 800% zoom"),
_("  7                 1200% zoom"),
_("  8                 1600% zoom"),
_("  9                 2000% zoom\n"),
_("  Shift + 1         Edit image channel"),
_("  Shift + 2         Edit alpha channel"),
_("  Shift + 3         Edit selection channel"),
_("  Shift + 4         Edit mask channel\n"),
_("  F1                Help"),
_("  F2                Choose Pattern"),
_("  F3                Choose Brush"),
_("  F4                Paint Tool"),
_("  F5                Toggle Main Toolbar"),
_("  F6                Toggle Tools Toolbar"),
_("  F7                Toggle Settings Toolbar"),
_("  F8                Toggle Palette"),
_("  F9                Selection Tool"),
_("  F12               Toggle Dock Area\n"),
_("  Ctrl + F1 - F12   Save current clipboard to file 1-12"),
_("  Shift + F1 - F12  Load clipboard from file 1-12\n"),
_("  Ctrl + 1, 2, ... , 0  Set opacity to 10%, 20%, ... , 100% (main or keypad numbers)"),
_("  Ctrl + + or =     Increase opacity by 1"),
_("  Ctrl + -          Decrease opacity by 1\n"),
_("  Home              Show or hide main window menu/toolbar/status bar/palette"),
_("  Page Up           Scale Image"),
_("  Page Down         Resize Image canvas"),
_("  End               Pan Window"),
NULL };
static char *help_page2[] = {
_("  Left button          Paint to canvas using the current tool"),
_("  Middle button        Selects the point which will be the centre of the image after the next zoom"),
_("  Right button         Commit paste to canvas / Stop drawing current line / Cancel selection\n"),
_("  Scroll Wheel         In GTK+2 the user can have the scroll wheel zoom in or out via the Preferences window\n"),
_("  Ctrl+Left button     Choose colour A from under mouse pointer"),
_("  Ctrl+Middle button   Create colour A/B and pattern based on the RGB colour in A (RGB images only)"),
_("  Ctrl+Right button    Choose colour B from under mouse pointer"),
_("  Ctrl+Scroll Wheel    Scroll the main edit window left or right\n"),
_("  Ctrl+Double click    Set colour A or B to average colour under brush square or selection marquee (RGB only)\n"),
_("  Shift+Right button   Selects the point which will be the centre of the image after the next zoom\n\n"),
_("You can fixate the X/Y co-ordinates while moving the mouse:\n"),
_("  Shift                Constrain mouse movements to vertical line"),
_("  Shift+Ctrl           Constrain mouse movements to horizontal line"),
NULL };
static char *help_page3[] = {
_("mtPaint is maintained by Dmitry Groshev.\n"),
_("wjaguar@users.sourceforge.net"),
_("http://mtpaint.sourceforge.net/\n"),
_("The following people (in alphabetical order) have contributed directly to the project, and are therefore worthy of gracious thanks for their generosity and hard work:\n\n"),
_("Authors\n"),
_("Dmitry Groshev - Contributing developer for version 2.30. Lead developer and maintainer from version 3.00 to the present."),
_("Mark Tyler - Original author and maintainer up to version 3.00, occasional contributor thereafter."),
_("Xiaolin Wu - Wrote the Wu quantizing method - see wu.c for more information.\n\n"),
_("General Contributions (Feedback and Ideas for improvements unless otherwise stated)\n"),
_("Abdulla Al Muhairi - Website redesign April 2005"),
_("Alan Horkan"),
_("Alexandre Prokoudine"),
_("Antonio Andrea Bianco"),
_("Dennis Lee"),
_("Ed Jason"),
_("Eddie Kohler - Created Gifsicle which is needed for the creation and viewing of animated GIF files http://www.lcdf.org/gifsicle/"),
_("Guadalinex Team (Junta de Andalucia) - man page, Launchpad/Rosetta registration"),
_("Lou Afonso"),
_("Magnus Hjorth"),
_("Martin Zelaia"),
_("Pavel Ruzicka"),
_("Puppy Linux (Barry Kauler)"),
_("Vlastimil Krejcir"),
_("William Kern"),
_("Pasi Kallinen\n\n"),
_("Translations\n"),
_("Brazilian Portuguese - Paulo Trevizan"),
_("Czech - Pavel Ruzicka, Martin Petricek, Roman Hornik"),
_("Dutch - Hans Strijards"),
_("French - Nicolas Velin, Pascal Billard, Sylvain Cresto, Johan Serre, Philippe Etienne"),
_("Galician - Miguel Anxo Bouzada"),
_("German - Oliver Frommel, B. Clausius"),
_("Italian - Angelo Gemmi"),
_("Japanese - Norihiro YONEDA"),
_("Polish - Bartosz Kaszubowski, LucaS"),
_("Portuguese - Israel G. Lugo, Tiago Silva"),
_("Russian - Sergey Irupin, Dmitry Groshev"),
_("Simplified Chinese - Cecc"),
_("Slovak - Jozef Riha"),
_("Spanish - Guadalinex Team (Junta de Andalucia), Antonio Sanchez Leon, Miguel Anxo Bouzada, Francisco Jose Rey"),
_("Swedish - Daniel Nylander, Daniel Eriksson"),
_("Tagalog - Anjelo delCarmen"),
_("Taiwanese Chinese - Wei-Lun Chao"),
_("Turkish - Muhammet Kara, Tutku Dalmaz"),
NULL };
#define HELP_PAGE_MAX 82

static char **help_pages[HELP_PAGE_COUNT] = {
	help_page0, help_page1, help_page2, help_page3
};

#undef _
#define _(X) __(X)
