/*	main.c
	Copyright (C) 2004-2020 Mark Tyler and Dmitry Groshev

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
#include "memory.h"
#include "vcode.h"
#include "ani.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "viewer.h"
#include "inifile.h"
#include "canvas.h"
#include "layer.h"
#include "prefs.h"
#include "csel.h"
#include "spawn.h"

static int compare_names(const void *s1, const void *s2)
{
	return (s1 == s2 ? 0 : strcoll(*(const char **)s1, *(const char **)s2));
}

#ifndef WIN32
#include <glob.h>
#define PATHC_TYPE size_t
#else

/* This is Windows only, as POSIX systems have glob() implemented */

/* Error returns from glob() */
#define	GLOB_NOSPACE 1 /* No memory */
#define	GLOB_NOMATCH 3 /* No files */

#define	GLOB_APPEND  0x020 /* Append to existing array */
#define GLOB_MAGCHAR 0x100 /* Set if any wildcards in pattern */

typedef struct {
	int gl_pathc;
	char **gl_pathv;
	int gl_flags;
} glob_t;

#define PATHC_TYPE int

static void globfree(glob_t *pglob)
{
	int i;

	if (!pglob->gl_pathv) return;
	for (i = 0; i < pglob->gl_pathc; i++)
		free(pglob->gl_pathv[i]);
	free(pglob->gl_pathv);
}

typedef struct {
	DIR *dir;
	char *path, *mask; // Split up the string for them
	int lpath;
} glob_dir_level;

#define MAXDEPTH (PATHBUF / 2) /* A pattern with more cannot match anything */

static int split_pattern(glob_dir_level *dirs, char *pat)
{
	char *tm2, *tmp, *lastpart;
	int ch, bracket = 0, cnt = 0;


	dirs[0].path = tmp = lastpart = pat;
	while (tmp)
	{
		if (cnt >= MAXDEPTH) return (0);
		tmp += strcspn(tmp, "?*[]");
		ch = *tmp++;
		if (!ch)
		{
			dirs[cnt].path = lastpart;
			dirs[cnt++].mask = NULL;
			break;
		}
		if (ch == '[') bracket = TRUE;
		else if ((ch != ']') || bracket)
		{
			tmp = strchr(tmp, DIR_SEP);
			if (tmp) *tmp++ = '\0';
			tm2 = strrchr(lastpart, DIR_SEP);
			/* 0th slot is special - path string is counted,
			 * not terminated, and includes path separator */
			if (!cnt)
			{
				if (!tm2) tm2 = strchr(lastpart, ':');
				if (tm2) dirs[0].lpath = tm2 - lastpart + 1;
			}
			else if (tm2) dirs[cnt].path = lastpart;

			if (tm2) *tm2++ = '\0';
			else tm2 = lastpart;
			dirs[cnt++].mask = tm2;
			lastpart = tmp;
			bracket = FALSE;
		}
	}
	return (cnt);
}

static int glob_add_file(glob_t *pglob, char *buf)
{
	void *tmp;
	int l = pglob->gl_pathc;

	/* Use doubling array technique */
	if (!pglob->gl_pathv || (((l + 1) & ~l) > l))
	{
		tmp = realloc(pglob->gl_pathv, (l + 1) * 2 * sizeof(char *));
		if (!tmp) return (-1);
		pglob->gl_pathv = tmp;
	}
	/* Add the name to array */
	if (!(pglob->gl_pathv[l++] = strdup(buf))) return (-1);
	pglob->gl_pathv[pglob->gl_pathc = l] = NULL;
	return (0);
}

/* This implementation is strictly limited to mtPaint's needs, and tuned for
 * Win32 peculiarities; the only flag handled by it is GLOB_APPEND - WJ */
static int glob(const char *pattern, int flags, void *nothing, glob_t *pglob)
{
	glob_dir_level dirs[MAXDEPTH + 1], *dp;
	struct dirent *ep;
	struct stat sbuf;
	char *pat, buf[PATHBUF];
	int l, lv, maxdepth, prevcnt, memfail = 0;


	pglob->gl_flags = flags;
	if (!(flags & GLOB_APPEND))
	{
		pglob->gl_pathc = 0;
		pglob->gl_pathv = NULL;
	}
	prevcnt = pglob->gl_pathc;

	/* Prepare the pattern */
	if (!pattern[0]) return (GLOB_NOMATCH);
	pat = strdup(pattern);
	if (!pat) goto mfail;
	reseparate(pat);

	/* Split up the pattern */
	memset(dirs, 0, sizeof(dirs));
	if (!(maxdepth = split_pattern(dirs, pat)))
	{
		free(pat);
		return (GLOB_NOMATCH);
	}

	/* Scan through dir(s) */
	maxdepth--;
	for (lv = 0; lv >= 0; )
	{
		dp = dirs + lv--; // Step back a level in advance
		/* Start scanning directory */
		if (!dp->dir)
		{
			l = dp->lpath;
			buf[l] = '\0';
			if (lv < 0) memcpy(buf, dp->path, l); // Level 0
			else if (!dp->path); // No extra path part
			else if (l + 1 + strlen(dp->path) >= PATHBUF) // Too long
				continue;
			else // Add path part
			{
				strcpy(buf + l + 1, dp->path);
				buf[l] = DIR_SEP;
			}
			dp->lpath = strlen(buf);
			if (!dp->mask)
			{
				if (!stat(buf, &sbuf))
					memfail |= glob_add_file(pglob, buf);
				continue;
			}
			dp->dir = opendir(buf[0] ? buf : ".");
			if (!dp->dir) continue;
		}
		/* Finish scanning directory */
		if (memfail || !(ep = readdir(dp->dir)))
		{
			closedir(dp->dir);
			dp->dir = NULL;
			continue;
		}
		lv++; // Undo step back
		/* Skip "." and ".." */
		if (!strcmp(ep->d_name, ".") || !strcmp(ep->d_name, ".."))
			continue;
		/* Filter through mask */
		if (!wjfnmatch(dp->mask, ep->d_name, FALSE)) continue;
		/* Combine names */
		l = dp->lpath + !!lv;
		if (l + strlen(ep->d_name) >= PATHBUF) // Too long
			continue; // Should not happen, but let's make sure
		strcpy(buf + l, ep->d_name);
		if (lv) buf[l - 1] = DIR_SEP; // No forced separator on level 0
		/* Filter files on lower levels */
		if (stat(buf, &sbuf) || ((lv < maxdepth) && !S_ISDIR(sbuf.st_mode)))
			continue;
		/* Add to result set */
		if (lv == maxdepth) memfail |= glob_add_file(pglob, buf);
		/* Enter into directory */
		else
		{
			dp[1].lpath = strlen(buf);
			lv++;
		}
	}
	free(pat);

	/* Report the results */
	if (memfail)
	{
mfail:		globfree(pglob);
		return (GLOB_NOSPACE);
	}
	if (pglob->gl_pathc == prevcnt) return (GLOB_NOMATCH);
	if (maxdepth) pglob->gl_flags |= GLOB_MAGCHAR;

	/* Sort the names */
	qsort(pglob->gl_pathv + prevcnt, pglob->gl_pathc - prevcnt,
		sizeof(char *), compare_names);

	return (0);
}

#endif

static char **flist;
int flist_len, flist_top;
int warnmax;

static int extend_flist(int n, int ntoo)
{
	char **tmp;

	/* Limit the sum & guard against overflow */
	n = n > FILES_MAX - ntoo ? FILES_MAX : n + ntoo;
	n = n > FILES_MAX - flist_top ? FILES_MAX : n + flist_top;

	if (n > flist_len)
	{
		n = (n + 1023) & ~1023; // Align
		if ((tmp = realloc(flist, n * sizeof(char *))))
		{
			flist = tmp;
			flist_len = n;
		}
	}
	return (flist_len - flist_top);
}

#ifdef WIN32
#define LSEP "\r\n"
#else
#define LSEP "\n"
#endif

static void add_filelist(char *name, int nf)
{
	char *what, *t, *err;
	int i, l, lf = 0, a0 = 0;

	if (warnmax |= flist_top >= FILES_MAX) return;
	err = "Could not load list";
	t = what = slurp_file_l(name, 0, &l); // Reads less than INT_MAX
	while (what)
	{
		/* Let file be LF or NUL separated */
		for (i = 0; i < l; i++)
		{
			lf += what[i] == '\n';
			a0 += !what[i];
		}
		if (a0) lf = a0; // NUL has precedence
		if (!lf) a0 = 1; // Take all file as single name

		err = "Empty list";
		if (lf >= l) break;
		err = "Ignored too long list";
		if (lf > FILES_MAX) break;

		/* One extra as last name may be unterminated */
		extend_flist(lf + 1, nf);

		if (!a0) /* LF separated */
		{
			while (t - what < l)
			{
				t += strspn(t, LSEP);
				if (t - what >= l) break;
				if (warnmax |= flist_top >= flist_len) break;
				flist[flist_top++] = t;
				t += strcspn(t, LSEP);
				*t++ = '\0';
			}
		}
		else /* NUL separated */
		{
			while (t - what < l)
			{
				while (!*t && (++t - what < l));
				if (t - what >= l) break;
				if (warnmax |= flist_top >= flist_len) break;
				flist[flist_top++] = t;
				t += strlen(t) + 1;
			}
		}
		return;
	}
	free(what);
	printf("%s: %s\n", err, name);
}

int main( int argc, char *argv[] )
{
	glob_t globdata;
	int file_arg_start = argc, new_empty = TRUE, get_screenshot = FALSE;
	int i, j, l, nf, nw, nl, w0, pass, fmode, dosort = FALSE;

	if (argc > 1)
	{
		if ( strcmp(argv[1], "--version") == 0 )
		{
			printf("%s\n\n", MT_VERSION);
			exit(0);
		}
		if ( strcmp(argv[1], "--help") == 0 )
		{
			printf("%s\n\n"
				"Usage: mtpaint [option] [imagefile ... ]\n\n"
				"Options:\n"
				"  --help          Output this help\n"
				"  --version       Output version information\n"
				"  --flist         Read a list of files\n"
				"  --sort          Sort files passed as arguments\n"
				"  --cmd           Commandline scripting mode, no GUI\n"
				"  -s              Grab screenshot\n"
				"  -v              Start in viewer mode\n"
				"  --              End of options\n\n"
			, MT_VERSION);
			exit(0);
		}
		if (!strcmp(argv[1], "--cmd"))
		{
			cmd_mode = TRUE;
			script_cmds = argv + 2;
		}
	}

	putenv( "G_BROKEN_FILENAMES=1" );	// Needed to read non ASCII filenames in GTK+2

	/* Disable bug-ridden eyecandy module that breaks sizing */
	putenv("LIBOVERLAY_SCROLLBAR=0");

#if GTK2VERSION >= 4
	/* Tablet handling in GTK+ 2.18+ is broken beyond repair if this mode
	 * is set; so unset it, if g_unsetenv() is present */
	g_unsetenv("GDK_NATIVE_WINDOWS");
#endif

#ifdef U_THREADS
	/* Enable threading for GLib, but NOT for GTK+ (at least, not yet) */
	g_thread_init(NULL);
#endif
	inifile_init("/etc/mtpaint/mtpaintrc", "~/.mtpaint");

#ifdef U_NLS
#if GTK_MAJOR_VERSION == 1
	/* !!! GTK+1 needs locale set up before gtk_init(); GTK+2, *QUITE*
	 * the opposite - WJ */
	setup_language();
#endif
#endif

#ifdef U_THREADS
	/* !!! Uncomment to allow GTK+ calls from other threads */
	/* gdk_threads_init(); */
#endif
	if (!cmd_mode)
	{
		gtk_init(&argc, &argv);
		gtk_init_bugfixes();
	}
#if GTK_MAJOR_VERSION == 2
	if (!cmd_mode)
	{
		char *theme = inifile_get(DEFAULT_THEME_INI, "");
		if (theme[0]) gtk_rc_parse(theme);
	}
	else g_type_init();
#endif

#ifdef U_NLS
	{
		char *locdir = extend_path(MT_LANG_DEST);
#if GTK_MAJOR_VERSION >= 2
		/* !!! GTK+2 starts acting up if this is before gtk_init() - WJ */
		setup_language();
#endif
		bindtextdomain("mtpaint", locdir);
		g_free(locdir);
		textdomain("mtpaint");
#if GTK_MAJOR_VERSION >= 2
		bind_textdomain_codeset("mtpaint", "UTF-8");
#endif
	}
#endif

	nf = nw = nl = w0 = pass = fmode = 0;
	memset(&globdata, 0, sizeof(globdata));
	for (i = 1 + cmd_mode; ; i++)
	{
		char *arg;

		if (i >= argc) // Pass is done
		{
			if (pass++) break; // Totally done

			file_args = argv + file_arg_start;
			files_passed = (warnmax = nf > FILES_MAX) ? FILES_MAX : nf;
			if (!(nw | nl)) break; // Regular filenames are good to go

			warnmax = 0; // Need to know WHEN limit gets hit

			if (nl | nf) // List not needed for wildcards alone
				extend_flist(nf, 1024); // Filenames and then some

			/* Go forth and stuff filenames into the list */
			i = 1 + cmd_mode;
			fmode = 0;
		}

		arg = argv[i];
		if (fmode);	// Expect filename or wildcard
		else if (!strcmp(arg, "--"))	// End of options
		{
			fmode |= 2;	// Files only, no wildcards
			continue;
		}
		else if (cmd_mode) continue;	// One more script command
		else if (!strcmp(arg, "-g"))	// Loading GIF animation frames
		{
			if (++i >= argc) continue;
			sscanf(argv[i], "%i", &preserved_gif_delay);
			continue;
		}
		else if (!strcmp(arg, "-v"))	// Viewer mode
		{
			viewer_mode = TRUE;
			continue;
		}
		else if (!strcmp(arg, "-s"))	// Screenshot
		{
			get_screenshot = TRUE;
			continue;
		}
		else if (!strcmp(arg, "--flist"))	// Filelist
		{
			if (++i >= argc) continue;
			nl++;
			if (!pass) continue;

			add_filelist(argv[i], nf);
			if (warnmax) break;
			continue;
		}
		else if (!strcmp(arg, "--sort"))	// Sort names
		{
			dosort = TRUE;
			continue;
		}

/* !!! I avoid GLOB_DOOFFS here, because glibc before version 2.2 mishandled it,
 * and quite a few copycats had cloned those buggy versions, some libc
 * implementors among them. So it is possible to encounter a broken function
 * in the wild, and playing it safe doesn't cost all that much - WJ */
		if ((fmode < 2) && !strcmp(arg, "-w"))	// Wildcard
		{
			if (++i >= argc) continue;
			nw++;
			if (!pass) continue;

			if (warnmax |= globdata.gl_pathc >= FILES_MAX) break;
			// Ignore errors - be glad for whatever gets returned
			glob(argv[i], (globdata.gl_pathc ? GLOB_APPEND : 0),
				NULL, &globdata);
			if (!flist) continue; // Use gl_pathv

			/* Add newfound filenames to flist */
			if (globdata.gl_pathc <= (PATHC_TYPE)w0) continue; // Nothing new
			l = (globdata.gl_pathc > FILES_MAX ? FILES_MAX :
				(int)globdata.gl_pathc) - w0;
			j = extend_flist(l, nf);
			if (l > j) l = j;
			memcpy(flist + flist_top, globdata.gl_pathv + w0,
				l * sizeof(char *));
			flist_top += l;
			w0 += l;
			if (warnmax |= globdata.gl_pathc > (PATHC_TYPE)w0) break;
			continue;
		}

		// Believe this is a filename
		fmode |= 1; // Only names and maybe wildcards past this point
		if (file_arg_start > i) file_arg_start = i;
		if (pass)
		{
			if (warnmax |= flist_top >= flist_len) break;
			flist[flist_top++] = argv[i];
		}
		nf += 1 - pass * 2; // Counting back on second pass
	}

	/* Flist if exists, globs otherwise, commandline as default */
	if (flist) file_args = flist , files_passed = flist_top;
	else if (globdata.gl_pathc) file_args = globdata.gl_pathv ,
		files_passed = globdata.gl_pathc > FILES_MAX ? FILES_MAX :
			(int)globdata.gl_pathc;
	if (warnmax) printf("Too many files, limiting to %d\n", files_passed);

	/* Sort the list of names */
	if (dosort && files_passed) qsort(file_args, files_passed,
		sizeof(char *), compare_names);

	if (strstr(argv[0], "mtv")) viewer_mode = TRUE;

	string_init();				// Translate static strings
	var_init();				// Load INI variables
	mem_init();				// Set up memory & back end
	layers_init();
	init_cols();

	if ( get_screenshot )
	{
		if (load_image(NULL, FS_PNG_LOAD, FT_PIXMAP) == 1)
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
		if ((files_passed > 0) && !do_a_load(file_args[0], FALSE))
			new_empty = FALSE;
	}

	if ( new_empty )		// If no file was loaded, start with a blank canvas
	{
		create_default_image();
	}

	update_menus();

	if (cmd_mode) // Console
		run_script(script_cmds);
	else // GUI
	{
		THREADS_ENTER();
		gtk_main();
		THREADS_LEAVE();

		inifile_quit();
	}
	spawn_quit();

	return (cmd_mode && user_break);
}
