/*
 * Copyright (C) 2002 2003, Magnus Hjorth
 *
 * This file came from mhWaveEdit.
 *
 * mhWaveEdit is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by        
 * the Free Software Foundation; either version 2 of the License, or  
 * (at your option) any later version.
 *
 * mhWaveEdit is distributed in the hope that it will be useful,   
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mhWaveEdit; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 */


/* This module handles the runtime configuration of the program. */

#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>


typedef struct {
     FILE *handle;
     char *filename;
} EFILE;

#define EFILE_READ 0
#define EFILE_WRITE 1
#define EFILE_APPEND 2

gchar *get_home_directory(void);
gboolean file_exists(char *filename);
EFILE *e_fopen(char *filename, int mode);
gboolean e_fclose(EFILE *stream);
gboolean e_fwrite(void *data, size_t size, EFILE *stream);
long int e_readline(gchar **line, size_t *size, EFILE *stream);


/* This function must be called before any other of these functions. It will
 * initialize the inifile system and load the settings file ~/.mhwaveedit/config
 * if available. 
 */

void inifile_init( char *ini_filename );
gchar *inifile_get(gchar *setting, gchar *defaultValue);
gint32 inifile_get_gint32(gchar *setting, gint32 defaultValue);

/* Same as inifile_get except requires that the value is a boolean value.
 * 'y','yes','1','true','enabled','on' are interpreted as TRUE.
 * 'n','no','0','false','disabled','off' are interpreted as FALSE.
 * Returns: The value converted into a boolean or defaultValue if setting 
 * doesn't exist or is not a valid boolean.
 */

gboolean inifile_get_gboolean(gchar *setting, gboolean defaultValue);
gfloat inifile_get_gfloat(gchar *setting, gfloat defaultValue);

/* Changes a setting.
 * setting - Name of the setting.
 * value - The new value.
 * Returns: TRUE if the setting was modified, FALSE if it already had that
 *          value.
 */

gboolean inifile_set(gchar *setting, gchar *value);
gboolean inifile_set_gint32(gchar *setting, gint32 value);
gboolean inifile_set_gboolean(gchar *setting, gboolean value);
gboolean inifile_set_gfloat(gchar *setting, gfloat value);


/* Save the settings into the config file. */
void inifile_save(void);

/* Free all resources and save the settings if they were changed. */
void inifile_quit(void);
