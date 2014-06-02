/*	fpick.c
	Copyright (C) 2007-2014 Mark Tyler and Dmitry Groshev

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
#undef _
#define _(X) X

#include "mygtk.h"
#include "fpick.h"
#include "vcode.h"

#ifdef U_FPICK_MTPAINT		/* mtPaint fpicker */

#include "inifile.h"
#include "memory.h"
#include "png.h"		// Needed by canvas.h
#include "canvas.h"
#include "toolbar.h"
#include "mainwindow.h"
#include "icons.h"

#define FP_KEY "mtPaint.fpick"
#define FP_DATA_KEY "mtPaint.fp_data"

#define FPICK_ICON_UP 0
#define FPICK_ICON_HOME 1
#define FPICK_ICON_DIR 2
#define FPICK_ICON_HIDDEN 3
#define FPICK_ICON_CASE 4
#define FPICK_ICON_TOT 5

#define FPICK_COMBO_ITEMS 16

enum {
	COL_FILE = 0,
	COL_NAME,
	COL_SIZE,
	COL_TYPE,
	COL_TIME,
	COL_NOCASE,
	COL_CASE,

	COL_MAX
};

#define RELREF(X) ((char *)&X + X)

// ------ Main Data Structure ------

typedef struct
{
	int		allow_files,			// Allow the user to select files/directories
			allow_dirs,
			show_hidden
			;

	char		combo_items[FPICK_COMBO_ITEMS][PATHTXT],	// UTF8 in GTK+2
			/* Must be DIR_SEP terminated at all times */
			txt_directory[PATHBUF],	// Current directory - Normal C string
			txt_mask[PATHTXT]	// Filter mask - UTF8 in GTK+2
			;

	GtkWidget	*toolbar,			// Toolbar
			*icons[FPICK_ICON_TOT],		// Icons
			*combo,				// List at top holding recent directories
			*combo_entry			// Directory entry area in combo
			;

	GList		*combo_list;			// List of combo items
} fpicker;

typedef struct {
	char *title;
	int flags;
	int entry_f;
	void **list;
	void **hbox, **entry;
	void **ok, **cancel;
	int cnt, cntx, idx;
	int fsort;		// Sort column/direction of list
	int *fcols, *fmap;
	memx2 files;
	fpicker f;
	char fname[PATHBUF];
} fpick_dd;

static int case_insensitive;

static void fpick_btn(fpick_dd *dt, void **wdata, int what, void **where);

#if GTK_MAJOR_VERSION == 1

#define _GNU_SOURCE
#include <fnmatch.h>
#undef _GNU_SOURCE
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0
#endif

#define fpick_fnmatch(mask, str) !fnmatch(mask, str, FNM_PATHNAME | \
	(case_insensitive ? FNM_CASEFOLD : 0))

#elif (GTK_MAJOR_VERSION == 2) && (GTK2VERSION < 4) /* GTK+ 2.0/2.2 */
#define fpick_fnmatch(mask, str) wjfnmatch(mask, str, TRUE)
#endif

static void filter_dir(fpick_dd *dt, const char *pattern)
{
#if GTK2VERSION >= 4 /* GTK+ 2.4+ */
	GtkFileFilter *filt = gtk_file_filter_new();
	GtkFileFilterInfo info;
	gtk_file_filter_add_pattern(filt, pattern);
	info.contains = GTK_FILE_FILTER_DISPLAY_NAME;
#endif
	int *cols = dt->fcols, *map = dt->fmap;
	int i, cnt = dt->cnt;
	char *s;

	for (i = 0; i < cnt; i++ , cols += COL_MAX)
	{
		s = RELREF(cols[COL_NAME]);
		/* Filter files, let directories pass */
		if (pattern[0] && (s[0] == 'F'))
		{
#if GTK2VERSION >= 4 /* GTK+ 2.4+ */
			info.display_name = s + 1;
			if (!gtk_file_filter_filter(filt, &info)) continue;
#else
			if (!fpick_fnmatch(pattern, s + 1)) continue;
#endif
		}
		*map++ = i;
	}
	dt->cntx = map - dt->fmap;	

#if GTK2VERSION >= 4 /* GTK+ 2.4+ */
	gtk_object_sink(GTK_OBJECT(filt));
#endif
}

static fpick_dd *mdt;
static int cmp_rows(const void *f1, const void *f2);

static void fpick_sort(fpick_dd *dt)
{
	if (dt->cntx <= 0) return; // Nothing to do
	/* Sort row map */
	mdt = dt;
	qsort(dt->fmap, dt->cntx, sizeof(dt->fmap[0]), cmp_rows);
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
	char *src, *dest;

#ifdef WIN32
	// Unify path separators
	reseparate(txt);
#endif
	// Expand home directory
	if ((txt[0] == '~') && (txt[1] == DIR_SEP))
	{
		src = file_in_homedir(NULL, txt + 2, PATHBUF);
		strncpy0(txt, src, PATHBUF - 1);
		free(src);
	}
	// Remove multiple consecutive occurences of DIR_SEP
	if ((dest = src = strstr(txt, DIR_SEP_STR DIR_SEP_STR)))
	{
		while (*src)
		{
			if (*src == DIR_SEP) while (src[1] == DIR_SEP) src++;
			*dest++ = *src++;
		}
		*dest++ = '\0';
	}
}


static int cmp_rows(const void *f1, const void *f2)
{
	static const signed char sort_order[] =
		{ COL_NAME, COL_TIME, COL_SIZE, -1 };
	int *r1, *r2;
	char *s1, *s2;
	int d, c, bits, lvl;

	r1 = mdt->fcols + *(int *)f1 * COL_MAX;
	r2 = mdt->fcols + *(int *)f2 * COL_MAX;

	/* "/ .." Directory always goes first, other dirs next, files last;
	 * and their type IDs are ordered to reflect that */
	s1 = RELREF(r1[COL_NAME]);
	s2 = RELREF(r2[COL_NAME]);
	if ((d = s1[0] - s2[0])) return (d);

	bits = lvl = 0;
	c = abs(mdt->fsort) - 1 + COL_NAME;

	while (c >= 0)
	{
		if (bits & (1 << c))
		{
			c = sort_order[lvl++];
			continue;
		}
		bits |= 1 << c;
		s1 = RELREF(r1[c]);
		s2 = RELREF(r2[c]);
		switch (c)
		{
		case COL_TYPE:
			if ((d = strcollcmp(s1, s2))) break;
			continue;
		case COL_SIZE:
			if ((d = strcmp(s1, s2))) break;
			continue;
		case COL_TIME:
			if ((d = strcmp(s2, s1))) break; // Newest first
			continue;
		default:
		case COL_NAME:
			c = case_insensitive ? COL_NOCASE : COL_CASE;
			continue;
		case COL_NOCASE:
		case COL_CASE:
			if ((d = strkeycmp(s1, s2))) break;
			c = COL_CASE;
			continue;
		}
		break;
	}
	return (mdt->fsort < 0 ? -d : d);
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

static void scan_drives(fpick_dd *dt, int cdrive)
{
	static const unsigned char dmap[COL_MAX] = { 1, 0, 4, 4, 4, 1, 1 };
	memx2 mem = dt->files;
	char *cp, *dest, buf[PATHBUF]; // More than enough for 26 4-char strings
	int i, j, *tc;

	mem.here = 0;
	getmemx2(&mem, 8000); // default size
	mem.here += getmemx2(&mem, 26 * ((COL_MAX + 1) * sizeof(int) + 5)); // minimum size

	/* Get the current drive letter */
	if (!cdrive)
	{
		if (GetCurrentDirectory(sizeof(buf), buf) && (buf[1] == ':'))
			cdrive = buf[0];
	}
	cdrive = toupper(cdrive);
	/* Get all drives */
	GetLogicalDriveStrings(sizeof(buf), buf);

	tc = dt->fcols = (void *)mem.buf;
	dest = (void *)(tc + 26 * (COL_MAX + 1));
	dt->idx = -1;
	for (i = 0 , cp = buf; *cp; i++ , cp += strlen(cp) + 1)
	{
		for (j = 0; j < COL_MAX; j++)
		{
			*tc = dest + dmap[j] - (char *)tc;
			tc++;
		}
		strcpy(dest, "DC:\\"); // 'D' is type flag
		if ((dest[1] = toupper(cp[0])) == cdrive) dt->idx = i;
		dest += 5;
	}
	dt->cnt = i;

	// Setup mapping array
	dt->fmap = tc;
	for (j = 0; j < i; j++) *tc++ = j;
	dt->cntx = i;

	dt->files = mem;
}

static void fpick_scan_drives(fpick_dd *dt)	// Scan drives, populate widgets
{
	fpicker *fp = &dt->f;
	int cdrive = 0;

	/* Get the current drive letter */
	if (fp->txt_directory[1] == ':') cdrive = fp->txt_directory[0];

	fp->txt_directory[0] = '\0';
	gtk_entry_set_text(GTK_ENTRY(fp->combo_entry), ""); // Just clear it

	scan_drives(dt, cdrive);
	cmd_reset(dt->list, dt);
}

#endif

static void scan_dir(fpick_dd *dt, DIR *dp, char *select)
{
	char full_name[PATHBUF], txt_size[64], txt_date[64], tmp_txt[64];
	char *nm, *src, *dest, *dir = dt->f.txt_directory;
	memx2 mem = dt->files;
	fpicker *win = &dt->f;
	struct dirent *ep;
	struct stat buf;
	int *tc;
	int subdir, tf, n, l = strlen(dir), cnt = 0;

	mem.here = 0;
	getmemx2(&mem, 8000); // default size

	dt->idx = -1;
	if (strcmp(dir, DIR_SEP_STR)) // Have a parent dir to move to?
	{
		// Field #0 - original name
		addstr(&mem, "..", 0);
		// Field #1 - type flag (space) + name in GUI encoding
		addstr(&mem, " " DIR_SEP_STR " ..", 0);
		// Fields #2-4 empty for all dirs
		// Fields #5-6 are empty strings, to keep this sorted first
		addchars(&mem, 0, 3 + 2);
		cnt++;
	}

	while ((ep = readdir(dp)))
	{
		wjstrcat(full_name, PATHBUF, dir, l, ep->d_name, NULL);

		// Error getting file details
		if (stat(full_name, &buf) < 0) continue;

		if (!win->show_hidden && (ep->d_name[0] == '.')) continue;

#ifdef WIN32
		subdir = S_ISDIR(buf.st_mode);
#else
		subdir = (ep->d_type == DT_DIR) || S_ISDIR(buf.st_mode);
#endif
		tf = 'F'; // Type flag: 'D' for dir, 'F' for file
		if (subdir)
		{
			if (!win->allow_dirs) continue;
			// Don't look at '.' or '..'
			if (!strcmp(ep->d_name, ".") || !strcmp(ep->d_name, ".."))
				continue;
			tf = 'D';
		}
		else if (!win->allow_files) continue;

		/* Remember which row has matching name */
		if (select && !strcmp(ep->d_name, select)) dt->idx = cnt;

		cnt++;

		// Field #0 - original name
		addstr(&mem, ep->d_name, 0);
		// Field #1 - type flag + name in GUI encoding
		addchars(&mem, tf, 1);
		nm = gtkuncpy(NULL, ep->d_name, 0);
		addstr(&mem, nm, 0);
		// Fields #2-4 empty for dirs
		if (subdir) addchars(&mem, 0, 3);
		else
		{
			// Field #2 - file size
#ifdef WIN32
			n = snprintf(tmp_txt, 64, "%I64u", (unsigned long long)buf.st_size);
#else
			n = snprintf(tmp_txt, 64, "%llu", (unsigned long long)buf.st_size);
#endif
			memset(txt_size, ' ', 20);
			dest = txt_size + 20; *dest-- = '\0';
			for (src = tmp_txt + n - 1; src - tmp_txt > 2; )
			{
				*dest-- = *src--;
				*dest-- = *src--;
				*dest-- = *src--;
				*dest-- = ',';
			}
			while (src - tmp_txt >= 0) *dest-- = *src--;
			addstr(&mem, txt_size, 0);
			// Field #3 - file type (extension)
			src = strrchr(nm, '.');
			if (src && (src != nm) && src[1])
			{
#if GTK_MAJOR_VERSION == 1
				g_strup(src = g_strdup(src + 1));
#else
				src = g_utf8_strup(src + 1, -1);
#endif
				addstr(&mem, src, 0);
				g_free(src);
			}
			else addchars(&mem, 0, 1);
			// Field #4 - file modification time
			strftime(txt_date, 60, "%Y-%m-%d   %H:%M.%S",
				localtime(&buf.st_mtime));
			addstr(&mem, txt_date, 0);
		}
		// Field #5 - case-insensitive sort key
		src = isort_key(nm);
		addstr(&mem, src, 0);
		g_free(src);
		// Field #6 - alphabetic (case-sensitive) sort key
#if GTK_MAJOR_VERSION == 1
		addstr(&mem, nm, 0);
#else /* if GTK_MAJOR_VERSION == 2 */
		src = g_utf8_collate_key(nm, -1);
		addstr(&mem, src, 0);
		g_free(src);
#endif
		g_free(nm);
	}
	dt->cnt = cnt;

	/* Now add index array and mapping arrays to all this */
	l = (~(unsigned)mem.here + 1) & (sizeof(int) - 1);
	n = cnt * COL_MAX;
	getmemx2(&mem, l + (n + cnt) * sizeof(int));
	// Fill index array
	tc = dt->fcols = (void *)(mem.buf + mem.here + l);
	src = mem.buf;
	while (n-- > 0)
	{
		*tc = src - (char *)tc;
		tc++;
		src += strlen(src) + 1;
	}
	// Setup mapping array
	dt->fmap = tc;

	dt->files = mem;
}

/* Scan directory, populate widgets; return 1 if success, 0 if total failure,
 * -1 if failed with original dir and scanned a different one */
static int fpick_scan_directory(fpick_dd *dt, char *name, char *select)
{
	fpicker *win = &dt->f;
	DIR	*dp;
	char	*cp, *parent = NULL;
	char	full_name[PATHBUF];
	int len, fail, res = 1;


	strncpy0(full_name, name, PATHBUF - 1);
	len = strlen(full_name);
	/* Ensure the invariant */
	if (!len || (full_name[len - 1] != DIR_SEP))
		full_name[len++] = DIR_SEP , full_name[len] = 0;
	/* Step up the path till a searchable dir is found */
	fail = 0;
	while (!(dp = opendir(full_name)))
	{
		res = -1; // Remember that original path was invalid
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
		else return (0);
		full_name[len] = 0;
	}

	/* If we're going up the path and want to show from where */
	if (!select)
	{
		if (!strncmp(win->txt_directory, full_name, len) &&
			win->txt_directory[len])
		{
			cp = strchr(win->txt_directory + len, DIR_SEP); // Guaranteed
			parent = win->txt_directory + len;
			select = parent = g_strndup(parent, cp - parent);
		}
	}
	/* If we've nothing to show */
	else if (!select[0]) select = NULL; 

	strncpy(win->txt_directory, full_name, PATHBUF);
	fpick_directory_new(win, full_name);		// Register directory in combo

	scan_dir(dt, dp, select);
	g_free(parent);
	closedir(dp);
	filter_dir(dt, win->txt_mask);

	cmd_reset(dt->list, dt);

	return (res);
}

static void fpick_enter_dir_via_list(fpick_dd *dt, char *name)
{
	char ndir[PATHBUF], *c;
	fpicker *fp = &dt->f;
	int l;

	strncpy(ndir, fp->txt_directory, PATHBUF);
	l = strlen(ndir);
	if (!strcmp(name, ".."))	// Go to parent directory
	{
		if (l && (ndir[l - 1] == DIR_SEP)) ndir[--l] = '\0';
		c = strrchr(ndir, DIR_SEP);
		if (c) *c = '\0';
		else /* Already in root directory */
		{
#ifdef WIN32
			fpick_scan_drives(dt);
#endif
			return;
		}
	}
	else strnncat(ndir, name, PATHBUF);
	fpick_cleanse_path(ndir);
	fpick_scan_directory(dt, ndir, NULL);	// Enter new directory
}

static void fpick_ok(fpick_dd *dt)
{
	int *rp = dt->fcols + dt->idx * COL_MAX;
	char *txt_name = RELREF(rp[COL_FILE]), *txt_size = RELREF(rp[COL_SIZE]);

	/* Directory selected */
	if (!txt_size[0]) fpick_enter_dir_via_list(dt, txt_name);
	/* File selected */
	else fpick_btn(dt, NULL, op_EVT_OK, NULL);
}

static void fpick_select(fpick_dd *dt, void **wdata, int what, void **where)
{
	int *rp = dt->fcols + dt->idx * COL_MAX;
	char *txt_name = RELREF(rp[COL_FILE]), *txt_size = RELREF(rp[COL_SIZE]);

	// File selected
	if (txt_size[0]) cmd_setv(dt->entry, txt_name, PATH_VALUE);
}

/* Return 1 if changed directory, 0 if directory was the same, -1 if tried
 * to change but failed */
static int fpick_enter_dirname(fpick_dd *dt, const char *name, int l)
{
	char txt[PATHBUF], *ctxt;
	fpicker *fp = &dt->f;
	int res = 0;

	name = name ? g_strndup(name, l) :
		g_strdup(gtk_entry_get_text(GTK_ENTRY(fp->combo_entry)));
	gtkncpy(txt, name, PATHBUF);

	fpick_cleanse_path(txt); // Path might have been entered manually

	if (strcmp(txt, fp->txt_directory) &&
		// Only do something if the directory is new
		((res = fpick_scan_directory(dt, txt, NULL)) <= 0))
	{	// Directory doesn't exist so tell user
		ctxt = g_strdup_printf(__("Could not access directory %s"), name);
		alert_box(_("Error"), ctxt, NULL);
		g_free(ctxt);
		res = res < 0 ? 1 : -1;
	}
	g_free((char *)name);
	return (res);
}

static void fpick_combo_changed(GtkWidget *widget, gpointer user_data)
{
	fpick_enter_dirname(user_data, NULL, 0);
}

typedef struct {
	char *title, *what;
	void **xw; // parent widget-map
	void **cancel, **delete, **rename, **create;
	void **res;
	int dir;
	char fname[PATHBUF];
} fdialog_dd;

#define WBbase fdialog_dd
static void *fdialog_code[] = {
	ONTOP(xw), DIALOGpm(title),
	WLABELp(what),
	XPENTRY(fname, PATHBUF), FOCUS,
	WDONE, // vbox
	BORDER(BUTTON, 2),
	REF(cancel), CANCELBTN(_("Cancel"), dialog_event),
	UNLESSx(dir, 1),
		REF(delete), BUTTON(_("Delete"), dialog_event),
		REF(rename), OKBTN(_("Rename"), dialog_event),
	ENDIF(1),
	IFx(dir, 1),
		REF(create), OKBTN(_("Create"), dialog_event),
	ENDIF(1),
	RAISED, WDIALOG(res)
};
#undef WBbase

static void fpick_file_dialog(fpick_dd *dt, void **wdata, int what, void **where,
	void *xdata)
{
	fdialog_dd tdata, *ddt;
	char fnm[PATHBUF], *tmp, *fname = NULL, *snm = NULL;
	void **dd;
	fpicker *fp = &dt->f;
	int uninit_(l), res, row = (int)xdata;


	memset(&tdata, 0, sizeof(tdata));
	tdata.xw = wdata;
	if (row >= 0) /* Doing things to selected file */
	{
		fname = RELREF(dt->fcols[COL_FILE + dt->idx * COL_MAX]);
		if (!strcmp(fname, "..")) return; // Up-dir
#ifdef WIN32
		if (fname[1] == ':') return; // Drive
#endif

		sprintf(tdata.title = fnm, "%s / %s", __("Delete"), __("Rename"));
		tdata.what = _("Enter the new filename");
		strncpy0(tdata.fname, fname, PATHBUF);
	}
	else
	{
		tdata.title = _("Create Directory");
		tdata.what = _("Enter the name of the new directory");
		tdata.dir = TRUE;
	}
	dd = run_create(fdialog_code, &tdata, sizeof(tdata)); // run dialog

	/* Retrieve results */
	run_query(dd);
	ddt = GET_DDATA(dd);
	where = origin_slot(ddt->res);
	res = where == ddt->delete ? 2 : where == ddt->rename ? 3 :
		where == ddt->create ? 4 : 1;

	if (res > 1)
	{
		l = strlen(fp->txt_directory);
		wjstrcat(fnm, PATHBUF, fp->txt_directory, l, ddt->fname, NULL);
		// The source name SHOULD NOT get truncated, ever
		if (fname) snm = g_strconcat(fp->txt_directory, fname, NULL);
	}
	run_destroy(dd);

	tmp = NULL;
	if (res == 2) // Delete file or directory
	{
		char *ts = g_strdup_printf(__("Do you really want to delete \"%s\" ?"),
			RELREF(dt->fcols[COL_NAME + dt->idx * COL_MAX]) + 1);
		int r = alert_box(_("Warning"), ts, _("No"), _("Yes"), NULL);
		g_free(ts);
		if (r == 2)
		{
			if (remove(snm)) tmp = _("Unable to delete");
		}
		else res = 1;
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

	if (tmp) alert_box(_("Error"), tmp, NULL);
	else if (res > 1)
	{
		if (row >= 0) /* Deleted/renamed a file - move down */
		{
			if (++row >= dt->cntx) row = dt->cntx - 2;
			row = dt->fmap[row];
			row = COL_FILE + row * COL_MAX;
			// !!! "fcols" will get overwritten
			strncpy0(tmp = fnm, RELREF(dt->fcols[row]), PATHBUF);
		}
		else tmp = fnm + l; /* Created a directory - move to it */

		fpick_scan_directory(dt, fp->txt_directory, tmp);
	}
}

static toolbar_item fpick_bar[] = {
	{ _("Up"),			-1, FPICK_ICON_UP, 0, XPM_ICON(up) },
	{ _("Home"),			-1, FPICK_ICON_HOME, 0, XPM_ICON(home) },
	{ _("Create New Directory"),	-1, FPICK_ICON_DIR, 0, XPM_ICON(newdir) },
	{ _("Show Hidden Files"),	 0, FPICK_ICON_HIDDEN, 0, XPM_ICON(hidden) },
	{ _("Case Insensitive Sort"),	 0, FPICK_ICON_CASE, 0, XPM_ICON(case) },
	{ NULL }};

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

static int fpick_wildcard(fpick_dd *dt, int button)
{
	char ctxt[PATHTXT];
	fpicker *fp = &dt->f;
	char *ds, *nm, *mask = fp->txt_mask;

	cmd_peekv(dt->entry, ctxt, PATHTXT, PATH_RAW);
	/* Presume filename if called by user pressing "OK", pattern otherwise */
	if (button)
	{
		/* If user had changed directory in the combo */
		if (fpick_enter_dirname(dt, NULL, 0)) return (FALSE);
		/* If file entry is hidden anyway */
		if (!fp->allow_files) return (TRUE);
		/* Filename must have some chars and no wildcards in it */
		if (ctxt[0] && !ctxt[strcspn(ctxt, "?*")]) return (TRUE);
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

	/* Have directory - enter it */
	if (ds && (fpick_enter_dirname(dt, ctxt, ds + 1 - ctxt) > 0))
	{	// Opened a new dir - skip redisplay
		cmd_setv(dt->entry, nm, PATH_RAW); // Cut dir off
	}
	else
	{	/* Redisplay only files that match pattern */
		filter_dir(dt, mask);
		cmd_reset(dt->list, dt);
	}

	/* Don't let pattern pass as filename */
	return (FALSE);
}

/* "Tab completion" for entry field, like in GtkFileSelection */
static int fpick_entry_key(fpick_dd *dt, void **wdata, int what, void **where,
	key_ext *keydata)
{
	if (keydata->key != GDK_Tab) return (FALSE);
	fpick_wildcard(dt, FALSE);
	return (TRUE);
}

// !!! May get NULLs in "wdata" and "where" when called from other handlers
static void fpick_btn(fpick_dd *dt, void **wdata, int what, void **where)
{
	if (what == op_EVT_CANCEL) do_evt_1_d(dt->cancel);
	else if (fpick_wildcard(dt, TRUE)) do_evt_1_d(dt->ok);
}

static void **make_fpick(void **r, GtkWidget ***wpp, void **wdata)
{
	char txt[64];
	GtkWidget *vbox1, *hbox1;
	fpick_dd *dt = GET_DDATA(wdata);
	fpicker *res = &dt->f;
	int i;


	vbox1 = **wpp;
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
		"hide", GTK_SIGNAL_FUNC(fpick_combo_changed), dt);
	gtk_signal_connect(GTK_OBJECT(res->combo_entry),
		"activate", GTK_SIGNAL_FUNC(fpick_combo_changed), dt);

	// !!! Show things now - toolbars in GTK+1 mishandle show_all
	gtk_widget_show_all(vbox1);

	// ------- Toolbar -------

	res->toolbar = pack5(hbox1, fpick_toolbar(res->icons));

	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(res->icons[FPICK_ICON_HIDDEN]),
			res->show_hidden );
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(res->icons[FPICK_ICON_CASE]),
			case_insensitive );
	gtk_object_set_data(GTK_OBJECT(res->toolbar), FP_DATA_KEY, wdata);
			// Set this after setting the toggles so any events are ignored

	return (r);
}

static void set_fname(fpick_dd *dt, char *name, int raw)
{
	char txt[PATHTXT];

	if (!raw)
	{
		/* Ensure that path is absolute */
		resolve_path(txt, PATHBUF, name);
		/* Separate the filename */
		name = strrchr(txt, DIR_SEP);
		*name++ = '\0';
		// Scan directory, populate boxes if successful
		if (!fpick_scan_directory(dt, txt, "")) return;
	}
	cmd_setv(dt->entry, name, raw ? PATH_RAW : PATH_VALUE);
}

/* Store things to inifile */
static void fpick_destroy(fpick_dd *dt, void **wdata)
{
	fpicker *win = &dt->f;
	char txt[64], buf[PATHBUF];
	int i;

	for ( i=0; i<FPICK_COMBO_ITEMS; i++ )		// Remember recently used directories
	{
		gtkncpy(buf, win->combo_items[i], PATHBUF);
		sprintf(txt, "fpick_dir_%i", i);
		inifile_set(txt, buf);
	}

	inifile_set_gboolean("fpick_case_insensitive", case_insensitive);
	inifile_set_gboolean("fpick_show_hidden", win->show_hidden );
}

static void fpick_iconbar_click(GtkWidget *widget, gpointer user_data)
{
	toolbar_item *item = user_data;
	void **wdata = gtk_object_get_data(GTK_OBJECT(widget->parent), FP_DATA_KEY);
	fpick_dd *dt;
	fpicker *fp;
	char fnm[PATHBUF];

	if (!wdata) return;

	dt = GET_DDATA(wdata);
	fp = &dt->f;
	switch (item->ID)
	{
	case FPICK_ICON_UP:
		fpick_enter_dir_via_list(dt, "..");
		break;
	case FPICK_ICON_HOME:
		cmd_read(dt->entry, dt);
		file_in_homedir(fnm, dt->fname, PATHBUF);
		set_fname(dt, fnm, FALSE);
		break;
	case FPICK_ICON_DIR:
		fpick_file_dialog(dt, wdata, 0, NULL, (void *)(-1));
		break;
	case FPICK_ICON_HIDDEN:
		fp->show_hidden = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		fpick_scan_directory(dt, fp->txt_directory, "");
		break;
	case FPICK_ICON_CASE:
		case_insensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		cmd_setv(dt->list, (void *)dt->fsort, LISTC_SORT);
		break;
	}
}

void fpick_get_filename(GtkWidget *fp, char *buf, int len, int raw)
{
	fpick_dd *dt = GET_DDATA(get_wdata(fp, FP_KEY));
	if (raw) cmd_peekv(dt->entry, buf, len, PATH_RAW);
	else
	{
		cmd_read(dt->entry, dt);
		snprintf(buf, len, "%s%s", dt->f.txt_directory, dt->fname);
	}
}

void fpick_set_filename(GtkWidget *fp, char *name, int raw)
{
	set_fname(GET_DDATA(get_wdata(fp, FP_KEY)), name, raw);
}

#define WBbase fpick_dd
static void *fpick_code[] = {
	IDENT(FP_KEY),
	WPWHEREVER, WINDOWpm(title), EVENT(DESTROY, fpick_destroy),
	EXEC(make_fpick),
	// ------- File List -------
	XHBOXbp(0, 0, 5),
	XSCROLL(1, 2), // auto/always
	WLIST,
	NRFILECOLUMNDax(_("Name"), COL_NAME, 250, 0, "fpick_col1"),
	NRTXTCOLUMNDaxx(_("Size"), COL_SIZE, 64, 2, "fpick_col2", "8,888,888,888"),
	NRTXTCOLUMNDax(_("Type"), COL_TYPE, 80, 2, "fpick_col3"),
	NRTXTCOLUMNDax(_("Modified"), COL_TIME, 150, 1, "fpick_col4"),
	COLUMNDATA(fcols, COL_MAX * sizeof(int)),
	REF(list), LISTCX(idx, cntx, fsort, fmap, fpick_select, fpick_file_dialog),
	EVENT(OK, fpick_ok), EVENT(CHANGE, fpick_sort),
	UNLESS(entry_f), FOCUS,
	CLEANUP(files.buf),
	WDONE,
	// ------- Extra widget section -------
	REF(hbox), HBOXPr, WDONE,
	// ------- Entry Box -------
	HBOXbp(0, 0, 5),
	REF(entry), XPENTRY(fname, PATHBUF),
	EVENT(KEY, fpick_entry_key), EVENT(OK, fpick_btn),
	UNLESS(f.allow_files), HIDDEN, IF(entry_f), FOCUS,
	WDONE,
	// ------- Buttons -------
	UOKBOXp0,
	MINWIDTH(100), EBUTTON(_("OK"), fpick_btn),
	MINWIDTH(100), ECANCELBTN(_("Cancel"), fpick_btn),
	WEND
};
#undef WBbase

GtkWidget *fpick(GtkWidget ***wpp, char *ddata, void **pp, void **r)
{
	fpick_dd tdata, *dt;
	void **res;

	memset(&tdata, 0, sizeof(tdata));
	tdata.title = *(char **)(ddata + (int)pp[2]);
	tdata.flags = *(int *)(ddata + (int)pp[3]);
	tdata.ok = NEXT_SLOT(r);
	tdata.cancel = SLOT_N(r, 2);

	tdata.entry_f = tdata.flags & FPICK_ENTRY;

	tdata.fsort = 1; // By name column, ascending

	case_insensitive = inifile_get_gboolean("fpick_case_insensitive", TRUE );

	tdata.f.show_hidden = inifile_get_gboolean("fpick_show_hidden", FALSE );
	tdata.f.allow_files = !(tdata.flags & FPICK_DIRS_ONLY);
	tdata.f.allow_dirs = TRUE;

	res = run_create(fpick_code, &tdata, sizeof(tdata));
	dt = GET_DDATA(res);
	*--*wpp = dt->hbox[0];
	return (GET_REAL_WINDOW(res));
}

#endif				/* mtPaint fpicker */




#ifdef U_FPICK_GTKFILESEL		/* GtkFileSelection based dialog */

GtkWidget *fpick(GtkWidget ***wpp, char *ddata, void **pp, void **r)
{
#if GTK_MAJOR_VERSION == 1
	GtkAccelGroup* ag = gtk_accel_group_new();
#endif
	GtkWidget *box, *fp;
	int flags = *(int *)(ddata + (int)pp[3]);

	fp = gtk_file_selection_new(__(*(char **)(ddata + (int)pp[2])));
	gtk_window_set_modal(GTK_WINDOW(fp), TRUE);
	if ( flags & FPICK_DIRS_ONLY )
	{
		gtk_widget_hide(GTK_WIDGET(GTK_FILE_SELECTION(fp)->selection_entry));
		gtk_widget_set_sensitive(GTK_WIDGET(GTK_FILE_SELECTION(fp)->file_list),
			FALSE);		// Don't let the user select files
	}

	add_click(NEXT_SLOT(r), GTK_FILE_SELECTION(fp)->ok_button);
	add_click(SLOT_N(r, 2), GTK_FILE_SELECTION(fp)->cancel_button);
	add_del(SLOT_N(r, 2), fp);

	*--*wpp = box = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(box);
	pack(GTK_FILE_SELECTION(fp)->main_vbox, box);

#if GTK_MAJOR_VERSION == 1 /* No builtin accelerators - add our own */
	gtk_widget_add_accelerator(GTK_FILE_SELECTION(fp)->cancel_button,
		"clicked", ag, GDK_Escape, 0, (GtkAccelFlags)0);
	gtk_window_add_accel_group(GTK_WINDOW(fp), ag);
#endif
	return (fp);
}

void fpick_get_filename(GtkWidget *fp, char *buf, int len, int raw)
{
	char *fname = (char *)gtk_entry_get_text(GTK_ENTRY(
		GTK_FILE_SELECTION(fp)->selection_entry));
	if (raw) strncpy0(buf, fname, len);
	else
	{
#ifdef WIN32 /* Widget returns filename in UTF8 */
		gtkncpy(buf, gtk_file_selection_get_filename(GTK_FILE_SELECTION(fp)), len);
#else
		strncpy0(buf, gtk_file_selection_get_filename(GTK_FILE_SELECTION(fp)), len);
#endif
		/* Make sure directory paths end with DIR_SEP */
		if (fname[0]) return;
		raw = strlen(buf);
		if (!raw || (buf[raw - 1] != DIR_SEP))
		{
			if (raw > len - 2) raw = len - 2;
			buf[raw] = DIR_SEP;
			buf[raw + 1] = '\0';
		}
	}
}

void fpick_set_filename(GtkWidget *fp, char *name, int raw)
{
	if (raw) gtk_entry_set_text(GTK_ENTRY(
		GTK_FILE_SELECTION(fp)->selection_entry), name);
#ifdef WIN32 /* Widget wants filename in UTF8 */
	else
	{
		name = gtkuncpy(NULL, name, 0);
		gtk_file_selection_set_filename(GTK_FILE_SELECTION(fp), name);
		g_free(name);
	}
#else
	else gtk_file_selection_set_filename(GTK_FILE_SELECTION(fp), name);
#endif
}

#endif		 /* GtkFileSelection based dialog */
