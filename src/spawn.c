/*	spawn.c
	Copyright (C) 2007-2013 Mark Tyler and Dmitry Groshev

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

#include <fcntl.h>

#include "global.h"
#undef _
#define _(X) X

#include "mygtk.h"
#include "inifile.h"
#include "memory.h"
#include "png.h"
#include "canvas.h"
#include "mainwindow.h"
#include "spawn.h"
#include "vcode.h"

static char *mt_temp_dir;

static char *get_tempdir()
{
	char *env;

	env = getenv("TMPDIR");
	if (!env || !*env) env = getenv("TMP");
	if (!env || !*env) env = getenv("TEMP");
#ifdef P_tmpdir
	if (!env || !*env) env = P_tmpdir;
#endif
	if (!env || !*env || (strlen(env) >= PATHBUF)) // Bad if too long
#ifdef WIN32
		env = "\\";
#else
		env = "/tmp";
#endif
	return (env);
}

static char *new_temp_dir()
{
	char *buf, *base = get_tempdir();

#ifdef HAVE_MKDTEMP
	buf = file_in_dir(NULL, base, "mtpaintXXXXXX", PATHBUF);
	if (!buf) return (NULL);
	if (mkdtemp(buf))
	{
		chmod(buf, 0755);
		return (buf);
	}
#else
	buf = tempnam(base, "mttmp");
	if (!buf) return (NULL);
#ifdef WIN32 /* No mkdtemp() in MinGW */
	/* tempnam() may use Unix path separator */
	reseparate(buf);
	if (!mkdir(buf)) return (buf);
#else
	if (!mkdir(buf, 0755)) return (buf);
#endif
#endif
	free(buf);
	return (NULL);
}

/* Store index for name, or fetch it (when idx < 0) */
static int last_temp_index(char *name, int idx)
{
// !!! For now, use simplest model - same index regardless of name
	static int index;
	if (idx >= 0) index = idx;
	return (index);
}

typedef struct {
	void *next, *id;
	int type, rgb;
	char name[1];
} tempfile;

static void *tempchain;

static char *remember_temp_file(char *name, int type, int rgb)
{
	static wjmem *tempstore;
	tempfile *tmp, *tm0;

	if ((tempstore || (tempstore = wjmemnew(0, 0))) &&
		(tmp = wjmalloc(tempstore, offsetof(tempfile, name) +
		strlen(name) + 1, ALIGNOF(tempfile))))
	{
		tmp->type = type;
		tmp->rgb = rgb;
		strcpy(tmp->name, name);
		if (mem_tempfiles) /* Have anchor - insert after it */
		{
			tm0 = tmp->id = mem_tempfiles;
			tmp->next = tm0->next;
			tm0->next = (void *)tmp;
		}
		else /* No anchor - insert before all */
		{
			tmp->next = tempchain;
			mem_tempfiles = tempchain = tmp->id = (void *)tmp;
		}
		return (tmp->name);
	}
	return (NULL); // Failed to allocate
}

void spawn_quit()
{
	tempfile *tmp;

	for (tmp = tempchain; tmp; tmp = tmp->next) unlink(tmp->name);
	if (mt_temp_dir) rmdir(mt_temp_dir);
}

static char *get_temp_file(int type, int rgb)
{
	ls_settings settings;
	tempfile *tmp;
	unsigned char *img = NULL;
	char buf[PATHBUF], nstub[NAMEBUF], ids[32], *c, *f = "tmp.png";
	int fd, cnt, idx, res;

	/* Use the original file if possible */
	if (!mem_changed && mem_filename && (!rgb ^ (mem_img_bpp == 3)) &&
		((type == FT_NONE) ||
		 (detect_file_format(mem_filename, FALSE) == type)))
		return (mem_filename);

	/* Prepare temp directory */
	if (!mt_temp_dir) mt_temp_dir = new_temp_dir();
	if (!mt_temp_dir) return (NULL); /* Temp dir creation failed */

	/* Analyze name */
	if ((type == FT_NONE) && mem_filename)
	{
		f = strrchr(mem_filename, DIR_SEP);
		if (!f) f = mem_filename;
		type = file_type_by_ext(f, FF_SAVE_MASK);
	}
	if (type == FT_NONE) type = FT_PNG;

	/* Use existing file if possible */
	for (tmp = mem_tempfiles; tmp; tmp = tmp->next)
	{
		if (tmp->id != mem_tempfiles) break;
		if ((tmp->type == type) && (tmp->rgb == rgb))
			return (tmp->name);
	}

	/* Stubify filename */
	strcpy(nstub, "tmp");
	c = strrchr(f, '.');
	if (c != f) /* Extension should not be alone */
		wjstrcat(nstub, NAMEBUF, f, c ? c - f : strlen(f), NULL);

	/* Create temp file */
	while (TRUE)
	{
		idx = last_temp_index(nstub, -1);
		ids[0] = 0;
		for (cnt = 0; cnt < 256; cnt++ , idx++)
		{
			if (idx) sprintf(ids, "%d", idx);
			snprintf(buf, PATHBUF, "%s" DIR_SEP_STR "%s%s.%s",
				mt_temp_dir, nstub, ids, file_formats[type].ext);
			fd = open(buf, O_WRONLY | O_CREAT | O_EXCL, 0644);
			if (fd >= 0) break;
		}
		last_temp_index(nstub, idx);
		if (fd >= 0) break;
		if (!strcmp(nstub, "tmp")) return (NULL); /* Utter failure */
		strcpy(nstub, "tmp"); /* Try again with "tmp" */
	}
	close(fd);

	/* Save image */
	init_ls_settings(&settings, NULL);
	memcpy(settings.img, mem_img, sizeof(chanlist));
	settings.pal = mem_pal;
	settings.width = mem_width;
	settings.height = mem_height;
	settings.bpp = mem_img_bpp;
	settings.colors = mem_cols;
	settings.ftype = type;
	if (rgb && (mem_img_bpp == 1)) /* Save indexed as RGB */
	{
		settings.img[CHN_IMAGE] = img =
			malloc(mem_width * mem_height * 3);
		if (!img) return (NULL); /* Failed to allocate RGB buffer */
		settings.bpp = 3;
		do_convert_rgb(0, 1, mem_width * mem_height, img,
			mem_img[CHN_IMAGE], mem_pal);
	}
	res = save_image(buf, &settings);
	free(img);
	if (res) return (NULL); /* Failed to save */

	return (remember_temp_file(buf, type, rgb));
}

static char *insert_temp_file(char *pattern, int where, int skip)
{
	char *fname, *pat = pattern;
	int i, l, rgb = mem_img_bpp == 3, fform = FT_NONE;

	while (TRUE)
	{
		/* Skip initial whitespace if any */
		pat += strspn(pat, " \t");
		/* Finish if not a transform request */
		if (*pat != '>') break;
		l = strcspn(++pat, "> \t");
		if (!strncasecmp("RGB", pat, l)) rgb = TRUE;
		else
		{
			for (i = FT_NONE + 1; i < NUM_FTYPES; i++)
			{
				fformat *ff = file_formats + i;
				if ((ff->flags & FF_IMAGE) &&
					!(ff->flags & FF_NOSAVE) &&
					(!strncasecmp(ff->name, pat, l) ||
					!strncasecmp(ff->ext, pat, l) ||
					(ff->ext2[0] &&
					!strncasecmp(ff->ext2, pat, l))))
					fform = i;
			}
		}
		pat += l;
	}
	where -= pat - pattern;
	if (where < 0) return (NULL); // Syntax error

	if (fform != FT_NONE)
	{
		unsigned int flags = file_formats[fform].flags;
		if (rgb && !(flags & FF_RGB)) fform = FT_NONE; // No way
		else if (flags & FF_SAVE_MASK); // Is OK
		else if (flags & FF_RGB) rgb = TRUE; // Fallback
		else fform = FT_NONE; // Give up
	}

	fname = get_temp_file(fform, rgb);
	if (!fname) return (NULL); /* Temp save failed */
	return (wjstrcat(NULL, 0, pat, where, "\"", fname, "\"",
		pat + where + skip, NULL));
}

int spawn_expansion(char *cline, char *directory)
	// Replace %f with "current filename", then run via shell
{
	int res = -1;
	char *s1;
#ifdef WIN32
	char *argv[4] = { getenv("COMSPEC"), "/C", cline, NULL };
#else
	char *argv[4] = { "sh", "-c", cline, NULL };
#endif

	s1 = strstr( cline, "%f" );	// Has user included the current filename?
	if (s1)
	{
		s1 = insert_temp_file(cline, s1 - cline, 2);
		if (s1)
		{
			argv[2] = s1;
			res = spawn_process(argv, directory);
			free(s1);
		}
		else return -1;
	}
	else
	{
		res = spawn_process(argv, directory);
	}

		// Note that on Linux systems, returning 0 means that the shell was
		// launched OK, but gives no info on the success of the program being run by the shell.
		// To find out what happened you will need to use the output window.
		// MT 18-1-2007

	return res;
}



// Front End stuff

static char *faction_ini[3] = { "fact%dName", "fact%dCommand", "fact%dDir" };

#define MAXNAMELEN 2048
typedef struct {
	char name[MAXNAMELEN];
	char cmd[PATHTXT]; // UTF-8
	char dir[PATHBUF]; // system encoding
} spawn_row;

typedef struct {
	spawn_row *strs;
	char *name, *cmd;
	void **list, **group;
	int idx, nidx, cnt;
	int lock;
	char dir[PATHBUF];
} spawn_dd;


void pressed_file_action(int item)
{
	char *comm, *dir, txt[64];

	sprintf(txt, faction_ini[1], item);
	comm = inifile_get(txt,"");
	sprintf(txt, faction_ini[2], item);
	dir = inifile_get(txt,"");

	spawn_expansion(comm, dir);
}

static void faction_changed(spawn_dd *dt, void **wdata, int what, void **where)
{
	void *cause;
	spawn_row *rp;

	if (dt->lock) return;
	cause = cmd_read(where, dt);

	rp = dt->strs + dt->idx;
	if (cause == dt->dir) strncpy(rp->dir, dt->dir, sizeof(rp->dir));
	else
	{
		strncpy0(rp->name, dt->name, sizeof(rp->name));
		strncpy0(rp->cmd, dt->cmd, sizeof(rp->cmd));
		cmd_setv(dt->list, (void *)dt->idx, LISTC_RESET_ROW);
	}
}

static void faction_select_row(spawn_dd *dt, void **wdata, int what, void **where)
{
	spawn_row *rp;

	cmd_read(where, dt);

	if (dt->nidx == dt->idx) return; // no change
	dt->lock = TRUE;
	
	/* Get strings from array, and reset entries */
	rp = dt->strs + (dt->idx = dt->nidx);
	dt->name = rp->name;
	dt->cmd = rp->cmd;
	strncpy(dt->dir, rp->dir, PATHBUF);
	cmd_reset(dt->group, dt);

	dt->lock = FALSE;
}

static void update_faction_menu()	// Populate menu
{
	int i, items = 0;
	char txt[64], *nm, *cm;
	GtkWidget *item;

	/* Show valid slots in menu */
	for (i = 1; i <= FACTION_PRESETS_TOTAL; i++)
	{
		item = menu_widgets[MENU_FACTION1 - 1 + i];
		gtk_widget_hide(item);		/* Hide by default */
		sprintf(txt, faction_ini[0], i);
		nm = inifile_get(txt, "");

		if (!nm || !nm[0] || (nm[0] == '#')) continue;
		sprintf(txt, faction_ini[1], i);
		cm = inifile_get(txt, "");

		if (!cm || !cm[0] || (cm[0] == '#')) continue;
		gtk_label_set_text(
			GTK_LABEL(GTK_MENU_ITEM(item)->item.bin.child), nm);
		gtk_widget_show(item);
		items++;
	}

	/* Hide separator if no valid slots */
	widget_showhide(menu_widgets[MENU_FACTION_S], items);
}	

void init_factions()
{
#ifndef WIN32
	int i, j;
	static char *row_def[][3] = {
		{"View EXIF data (leafpad)", "exif %f | leafpad"},
		{"View filesystem data (xterm)", "xterm -hold -e ls -l %f"},
		{"Edit in Gimp", "gimp %f"},
		{"View in GQview", "gqview %f"},
		{"Print image", "kprinter %f"},
		{"Email image", "seamonkey -compose attachment=file://%f"},
		{"Send image to Firefox", "firefox %f"},
		{"Send image to OpenOffice", "soffice %f"},
		{"Edit Clipboards", "mtpaint ~/.clip*"},
		{"Time delayed screenshot", "sleep 10; mtpaint -s &"},
		{"View image information", "xterm -hold -sb -rightbar -geometry 100x100 -e identify -verbose %f"},
		{"#Create temp directory", "mkdir ~/images"},
		{"#Remove temp directory", "rm -rf ~/images"},
		{"#GIF to PNG conversion (in situ)", "mogrify -format png *.gif"},
		{"#ICO to PNG conversion (temp directory)", "ls --file-type *.ico | xargs -I FILE convert FILE ~/images/FILE.png"},
		{"Convert image to ICO file", "mogrify -format ico %f"},
		{"Create thumbnails in temp directory", "ls --file-type * | xargs -I FILE convert FILE -thumbnail 120x120 -sharpen 1 -quality 95 ~/images/th_FILE.jpg"},
		{"Create thumbnails (in situ)", "ls --file-type * | xargs -I FILE convert FILE -thumbnail 120x120 -sharpen 1 -quality 95 th_FILE.jpg"},
		{"Peruse temp images", "mtpaint ~/images/*"},
		{"Rename *.jpeg to *.jpg", "rename .jpeg .jpg *.jpeg"},
		{"Remove spaces from filenames", "for file in *\" \"*; do mv \"$file\" `echo $file | sed -e 's/ /_/g'`; done"},
		{"Remove extra .jpg. from filename", "rename .jpg. . *.jpg.jpg"},
//		{"", ""},
		{NULL, NULL, NULL}
		},
		txt[64];

	for (i = 0; row_def[i][0]; i++)		// Needed for first time usage - populate inifile list
	{
		for (j = 0; j < 3; j++)
		{
			sprintf(txt, faction_ini[j], i + 1);
			inifile_get(txt, row_def[i][j]);
		}
	}
#endif

	update_faction_menu();			// Prepare menu
}

static void faction_btn(spawn_dd *dt, void **wdata, int what, void **where)
{
	spawn_row *rp = dt->strs;
	char *s, txt[64], buf[PATHBUF];
	int i, j, idx[FACTION_ROWS_TOTAL];

	if (what == op_EVT_CLICK) /* Execute */
	{
		gtkncpy(buf, dt->cmd, PATHBUF);
		spawn_expansion(buf, dt->dir);
		return;
	}

	cmd_peekv(dt->list, idx, sizeof(idx), LISTC_ORDER);
	for (i = 0; i < FACTION_ROWS_TOTAL; i++ , rp++)
	{
		for (j = 0; j < 3; j++)
		{
			sprintf(txt, faction_ini[j], idx[i] + 1);
			if (!j) s = rp->name;
			else if (j == 1) gtkncpy(s = buf, rp->cmd, sizeof(buf));
			else s = rp->dir;
			inifile_set(txt, s);
		}
	}
	update_faction_menu();
	run_destroy(wdata);
}

#define WBbase spawn_dd
static void *spawn_code[] = {
	WINDOWm(_("Configure File Actions")),
	DEFSIZE(500, 400),
	XVBOXb(0, 5), // !!! Originally the main vbox was that way
	XSCROLL(1, 1), // auto/auto
	WLIST,
	NTXTCOLUMND(_("Action"), spawn_row, name, 200, 0),
	NTXTCOLUMND(_("Command"), spawn_row, cmd, 0 /* 200 */, 0),
	REF(list), LISTCd(nidx, cnt, strs, sizeof(spawn_row), faction_select_row),
	TRIGGER, CLEANUP(strs),
	REF(group), GROUP(1),
	FHBOX(_("Action")), XENTRY(name), EVENT(CHANGE, faction_changed), WDONE,
	FHBOX(_("Command")), XENTRY(cmd), EVENT(CHANGE, faction_changed), WDONE,
	PATH(_("Directory"), _("Select Directory"), FS_SELECT_DIR, dir),
	EVENT(CHANGE, faction_changed),
	BORDER(OKBOX, 0),
	OKBOX(_("OK"), faction_btn, _("Cancel"), NULL),
	OKADD(_("Execute"), faction_btn),
	WSHOW
};
#undef WBbase

void pressed_file_configure()
{
	spawn_dd tdata;
	spawn_row *rp;
	char txt[64], *s;
	int i, j;

	rp = tdata.strs = calloc(FACTION_ROWS_TOTAL, sizeof(spawn_row));
	if (!rp) return; // Not enough memory

	for (i = 1; i <= FACTION_ROWS_TOTAL; i++ , rp++)
	{
		for (j = 0; j < 3; j++)
		{
			sprintf(txt, faction_ini[j], i);
			s = inifile_get(txt, "");
			if (!j) strncpy0(rp->name, s, sizeof(rp->name));
			else if (j == 1) gtkuncpy(rp->cmd, s, sizeof(rp->cmd));
			else strncpy0(rp->dir, s, sizeof(rp->dir));
		}
	}

	tdata.name = tdata.cmd = "";
	tdata.dir[0] = '\0';
	tdata.idx = -1;
	tdata.nidx = 0;
	tdata.cnt = FACTION_ROWS_TOTAL;
	tdata.lock = FALSE;

	run_create(spawn_code, &tdata, sizeof(tdata));
}


// Process spawning code

#ifdef WIN32

/* With Glib's g_spawn*() smart-ass tricks making it utterly unusable in
 * Windows, the only way is to reimplement the functionality - WJ */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int spawn_process(char *argv[], char *directory)
{
	PROCESS_INFORMATION pi;
	STARTUPINFO st;
	gchar *cmdline, *c;
	int res;

	memset(&pi, 0, sizeof(pi));
	memset(&st, 0, sizeof(st));
	st.cb = sizeof(st);
	cmdline = g_strjoinv(" ", argv);
	if (directory && !directory[0]) directory = NULL;

	c = g_locale_from_utf8(cmdline, -1, NULL, NULL, NULL);
	if (c)
	{
		g_free(cmdline);
		cmdline = c;
		c = NULL;
	}
	if (directory)
	{
		c = g_locale_from_utf8(directory, -1, NULL, NULL, NULL);
		if (c) directory = c;
	}

	res = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
		/* CREATE_NEW_CONSOLE | */
		CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
		NULL, directory, &st, &pi);
	g_free(cmdline);
	g_free(c);
	if (!res) return (1);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return (0);
}

#else

#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

int spawn_process(char *argv[], char *directory)
{
	pid_t child, grandchild;
	int res = -1, err, fd_flags, fds[2], status;

	child = fork();
	if ( child < 0 ) return 1;

	if (child == 0)				// Child
	{
		if (pipe(fds) == -1) _exit(1);		// Set up the pipe

		grandchild = fork();

		if (grandchild == 0)		// Grandchild
		{
			if (directory) chdir(directory);

			if (close(fds[0]) != -1)	// Close the read pipe
				/* Make the write end close-on-exec */
			if ((fd_flags = fcntl(fds[1], F_GETFD)) != -1)
				if (fcntl(fds[1], F_SETFD, fd_flags | FD_CLOEXEC) != -1)
					execvp(argv[0], &argv[0]); /* Run program */

			/* If we are here, an error occurred */
			/* Send the error code to the parent */
			err = errno;
			write(fds[1], &err, sizeof(err));
			_exit(1);
		}
		/* Close the write pipe - has to be done BEFORE read */
		close(fds[1]);

		/* Get the error code from the grandchild */
		if (grandchild > 0) res = read(fds[0], &err, sizeof(err));

		close(fds[0]);			// Close the read pipe

		_exit(res);
	}

	waitpid(child, &status, 0);		// Wait for child to die
	res = WEXITSTATUS(status);		// Find out the childs exit value

	return (res);
}

#endif

// Copy string quoting space chars

static char *quote_spaces(const char *src)
{
	char *tmp, *dest;
	int n = 0;

	for (*(const char **)&tmp = src; *tmp; tmp++)
	{
		if (*tmp == ' ')
#ifdef WIN32
			n += 2;
#else
			n++;
#endif
		n++;
	}

	dest = malloc(n + 1);
	if (!dest) return ("");

	for (tmp = dest; *src; src++)
	{
		if (*src == ' ')
		{
#ifdef WIN32
			*tmp++ = '"';
			*tmp++ = ' ';
			*tmp++ = '"';
#else
			*tmp++ = '\\';
			*tmp++ = ' ';
#endif
		}
		else *tmp++ = *src;
	}
	*tmp = 0;
	return (dest);
}

// Wrapper for animated GIF handling, or other builtin actions

#ifdef U_ANIM_IMAGICK
	/* Use Image Magick for animated GIF processing; not really recommended,
	 * because it is many times slower than Gifsicle, and less efficient */
	/* !!! Image Magick version prior to 6.2.6-3 won't work properly */
#define CMD_GIF_CREATE \
	"convert %2$s -layers optimize -set delay %1$d -loop 0 \"%3$s\""
#define CMD_GIF_PLAY "animate \"%s\" &"

#else	/* Use Gifsicle; the default choice for animated GIFs */

/*	global colourmaps, suppress warning, high optimizations, background
 *	removal method, infinite loops, ensure result works with Java & MS IE */
#define CMD_GIF_CREATE \
	"gifsicle --colors 256 -w -O2 -D 2 -l0 --careful -d %d %s -o \"%s\""
#define CMD_GIF_PLAY "gifview -a \"%s\" &"

#endif

/* Windows shell does not know "&", nor needs it to run mtPaint detached */
#ifndef WIN32

#define CMD_DETACH " &"

#else /* WIN32 */

#define CMD_DETACH
#ifndef WEXITSTATUS
#define WEXITSTATUS(A) ((A) & 0xFF)
#endif

#endif

int run_def_action(int action, char *sname, char *dname, int delay)
{
	char *msg, *c8, *command = NULL;
	int res, code;

	switch (action)
	{
	case DA_GIF_CREATE:
		c8 = quote_spaces(sname);
		command = g_strdup_printf(CMD_GIF_CREATE, delay, c8, dname);
		free(c8);
		break;
	case DA_GIF_PLAY:
#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
		/* 'gifviev' and 'animate' both are X-only */
		command = g_strdup_printf(CMD_GIF_PLAY, sname);
		break;
#else
		return (-1);
#endif
	case DA_GIF_EDIT:
		command = g_strdup_printf("mtpaint -g %d -w \"%s.???\" -w \"%s.????\""
			CMD_DETACH, delay, sname, sname);
		break;
	}

	res = system(command);
	if (res)
	{
		if (res > 0) code = WEXITSTATUS(res);
		else code = res;
		c8 = gtkuncpy(NULL, command, 0);
		msg = g_strdup_printf(__("Error %i reported when trying to run %s"),
			code, c8);
		alert_box(_("Error"), msg, NULL);
		g_free(msg);
		g_free(c8);
	}
	g_free(command);

	return (res);
}


#ifdef WIN32

#include <shellapi.h>
#define HANDBOOK_LOCATION_WIN "..\\docs\\index.html"

#else /* Linux */

#define HANDBOOK_BROWSER "firefox"
#define HANDBOOK_LOCATION "/usr/doc/mtpaint/index.html"
#define HANDBOOK_LOCATION2 "/usr/share/doc/mtpaint/index.html"

#endif

int show_html(char *browser, char *docs)
{
	char buf[PATHBUF + 2], buf2[PATHBUF];
	int i=-1;
#ifdef WIN32
	char *r;

	if (!docs || !docs[0])
	{
		/* Use default path relative to installdir */
		docs = buf + 1;
		i = GetModuleFileNameA(NULL, docs, PATHBUF);
		if (!i) return (-1);
		r = strrchr(docs, '\\');
		if (!r) return (-1);
		r[1] = 0;
		strnncat(docs, HANDBOOK_LOCATION_WIN, PATHBUF);
	}
#else /* Linux */
	char *argv[5];

	if (!docs || !docs[0])
	{
		docs = HANDBOOK_LOCATION;
		if (valid_file(docs) < 0) docs = HANDBOOK_LOCATION2;
	}
#endif
	else docs = gtkncpy(buf + 1, docs, PATHBUF);

	if ((valid_file(docs) < 0))
	{
		alert_box( _("Error"),
			_("I am unable to find the documentation.  Either you need to download the mtPaint Handbook from the web site and install it, or you need to set the correct location in the Preferences window."), NULL);
		return (-1);
	}

#ifdef WIN32
	if (browser && !browser[0]) browser = NULL;
	if (browser)
	{
		/* Quote the filename */
		i = strlen(docs);
		buf[0] = docs[i] = '"';
		docs[i + 1] = '\0';
		/* Rearrange parameters */
		docs = gtkncpy(buf2, browser, PATHBUF);
		browser = buf;
	}
	if ((unsigned int)ShellExecuteA(NULL, "open", docs, browser,
		NULL, SW_SHOW) <= 32) i = -1;
	else i = 0;
#else
	argv[1] = docs;
	argv[2] = NULL;
	/* Try to use default browser */
	if (!browser || !browser[0])
	{
		argv[0] = "xdg-open";
		i = spawn_process(argv, NULL);
		if (!i) return (0); // System has xdg-utils installed
		// No xdg-utils - try "BROWSER" variable then
		browser = getenv("BROWSER");
	}
	else browser = gtkncpy(buf2, browser, PATHBUF);

	if (!browser) browser = HANDBOOK_BROWSER;

	argv[0] = browser;
	i = spawn_process(argv, NULL);
#endif
	if (i) alert_box( _("Error"),
		_("There was a problem running the HTML browser.  You need to set the correct program name in the Preferences window."), NULL);
	return (i);
}
