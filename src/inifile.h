/*	inifile.h
	Copyright (C) 2007 Dmitry Groshev

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

typedef struct {
	int sec, key, defv;
	char *value;
	short type, flags;
} inislot;

typedef struct {
	char *sblock[2];
	inislot *slots;
	short *hash;
	int hmask, maxloop;
	int count, slen;
	guint32 seed;
} inifile;

/* Core functions */

int new_ini(inifile *inip);
void forget_ini(inifile *inip);
int read_ini(inifile *inip, char *fname);
int write_ini(inifile *inip, char *fname, char *header);

int ini_setstr(inifile *inip, int section, char *key, char *value);
int ini_setint(inifile *inip, int section, char *key, int value);
int ini_setbool(inifile *inip, int section, char *key, int value);

char *ini_getstr(inifile *inip, int section, char *key, char *defv);
int ini_getint(inifile *inip, int section, char *key, int defv);
int ini_getbool(inifile *inip, int section, char *key, int defv);

int ini_setsection(inifile *inip, int section, char *key);
int ini_getsection(inifile *inip, int section, char *key);

/* File function */

char *get_home_directory(void);

/* Compatibility functions */

void inifile_init(char *ini_filename);
void inifile_quit();

char *inifile_get(char *setting, char *defaultValue);
int inifile_get_gint32(char *setting, int defaultValue);
int inifile_get_gboolean(char *setting, int defaultValue);

int inifile_set(char *setting, char *value);
int inifile_set_gint32(char *setting, int value);
int inifile_set_gboolean(char *setting, int value);
