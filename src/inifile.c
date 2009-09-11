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

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef WIN32
	#include <pwd.h>
#endif

#include <errno.h>

#include "global.h"

#include "inifile.h"

static GList *settings = NULL;
static gchar *ininame;

static gboolean settings_changed = FALSE;


EFILE *e_fopen(char *filename, int mode)
{
     EFILE *e;
     FILE *f;
     char *c;
     switch (mode) {
     case EFILE_READ:   c = "r"; break;
     case EFILE_WRITE:  c = "w"; break;
     case EFILE_APPEND: c = "a"; break;	
     default: g_assert_not_reached(); return NULL;
     }
     f = fopen(filename,c);
     if (f == NULL) {
	  c = g_strdup_printf(_("Could not open %s: %s"),filename,strerror(errno));
//	  user_error(c);
	  g_free(c);
	  return NULL;
     }
     e = g_malloc(sizeof(*e));
     e->handle = f;
     e->filename = g_strdup(filename);
     return e;
}

gboolean e_fclose(EFILE *f)
{
     gchar *c;
     gboolean b=FALSE;
if ( f == NULL ) return FALSE;	// Stops segfault
     if (fclose(f->handle) != 0) {
	  c = g_strdup_printf(_("Error closing %s: %s"), f->filename, strerror(errno));
//	  user_error(c);
	  g_free(c);
	  b = TRUE;
     }
     g_free(f->filename);
     g_free(f);
     return b;
}

long int e_readline(gchar **line, size_t *size, EFILE *stream)
{
     size_t s = 0;
     int c;
     while (1) {
	  if (s == *size) {
	       *size = *size ? *size * 2 : 32;
	       *line = g_realloc(*line, *size);
	  }
	  c = fgetc(stream->handle);
	  if (c==EOF || (c=='\n' && s>0)) { (*line)[s]=0; return s; }
	  if (c=='\n') return e_readline(line,size,stream);
	  (*line)[s] = (gchar)c;
	  s++;
     }
}

gboolean e_fwrite(void *data, size_t size, EFILE *stream)
{
     char *c;
     if (!size) return FALSE;
if ( stream == NULL ) return FALSE;	// Stops segfault
     if (fwrite(data,size,1,stream->handle) != 1) {
	  c = g_strdup_printf(_("Could not write to %s: %s"),stream->filename,
			      strerror(errno));
//	  user_error(c);
	  g_free(c);
	  return TRUE;
     }
     return FALSE;
}

gboolean file_exists(char *filename)
{
     FILE *f;
     f = fopen(filename,"r");
     if (!f) return (errno != ENOENT);
     fclose(f);
     return TRUE;
}

gchar *get_home_directory(void)
{
	static gchar *homedir = NULL;

	if (homedir) return homedir;
#ifndef WIN32
	homedir = getenv("HOME");
	if (!homedir)
	{
		struct passwd *p;

		p = getpwuid(getuid());
		if (p) homedir = p->pw_dir;
	}
	if (!homedir)
	{
		g_warning(_("Could not find home directory. Using current directory as "
			"home directory."));
		homedir = ".";
	}
#else
	homedir = getenv("USERPROFILE");	// Gets the current users home directory in WinXP
	if (!homedir) homedir = "";		// And this, in Win9x :-)
#endif
	return homedir;
}


struct inifile_setting {
     gchar *name;
     gchar *value;
};

static gboolean isspace(gchar c)
{
     return (c==' ' || c=='\t');
}

static gchar *skipspace(gchar *c)
{
     while (isspace(*c)) c++;
     return c;
}

void inifile_init( char *ini_filename )
{
     EFILE *f;
     gchar *c,*d,*namestart,*valuestart;
     size_t s = 0;
     int x;
     struct inifile_setting *current_setting;

     ininame = g_strjoin(NULL,get_home_directory(), ini_filename , NULL);
     if (!file_exists(ininame)) return;
     f = e_fopen(ininame,EFILE_READ);
     if (!f) return;
     c = NULL;
     while (1) {
	  x = e_readline(&c,&s,f);
	  if (x <= 0) break;

	  d = strchr(c,'#');
	  if (!d) d=strchr(c,0);
	  while (d>c && isspace(*(d-1))) d--;
	  *d = 0;

	  d = skipspace(c);

	  if (*d == 0) continue;

	  namestart = d;
	  while (*d!=0 && *d!=' ' && *d!='\t' && *d!='=') d++;
	  if (*d == 0) {
	       g_printerr("%s: Expected '=': %s\n",ininame,c);
	       break;
	  }
	  *d = 0;
	  d++;
	  d = skipspace(d);
	  if (*d!='=') {
	       g_printerr("%s: Expected '=': %s\n",ininame,c);
	       break;
	  }
	  d++;
	  d = skipspace(d);
#if 0 /* Empty values are legal too - WJ */
	  if (*d==0) {
	       g_printerr("%s: Expected value: %s\n",ininame,c);
	       break;
	  }
#endif
	  valuestart = d;
	  current_setting = g_malloc(sizeof(struct inifile_setting));
	  current_setting->name = g_strdup(namestart);
	  current_setting->value = g_strdup(valuestart);
	  settings = g_list_append(settings,current_setting);
     }
     e_fclose(f);
}

gchar *inifile_get(gchar *setting, gchar *defaultValue)
{
     GList *l = settings;
     struct inifile_setting *s;
     while (l!=NULL && g_strcasecmp((s=l->data)->name,setting)) l=l->next;
     if (l==NULL) {
	  if (defaultValue) inifile_set(setting,defaultValue);
	  return defaultValue;
     }
     s = l->data;
     return s->value;
}

gint32 inifile_get_gint32(gchar *setting, gint32 defaultValue)
{
     gchar *c,*d;
     gint32 ui;
     c = inifile_get(setting, NULL);
     if (c) {
	  ui = strtoul(c,&d,10);
	  if (*d==0) return ui;
     }
     inifile_set_gint32(setting,defaultValue);
     return defaultValue;
}

gfloat inifile_get_gfloat(gchar *setting, gfloat defaultValue)
{
     gchar *c,*d;
     double x;
     c = inifile_get(setting,NULL);
     if (c) {
//	  set_c_locale();
	  x = strtod(c,&d);
//	  restore_locale();
	  if (*d==0) return (gfloat)x;
     }
     inifile_set_gfloat(setting,defaultValue);
     return defaultValue;
}

gboolean inifile_get_gboolean(gchar *setting, gboolean defaultValue)
{
     gchar *c;
     c = inifile_get(setting, NULL);
     if (c) {
	  if ( !g_strcasecmp(c,"y") || !strcmp(c,"1") ||
	       !g_strcasecmp(c,"yes") || !g_strcasecmp(c,"on") || 
	       !g_strcasecmp(c,"true") || !g_strcasecmp(c,"enabled")) 
	       return TRUE;
	  if (!g_strcasecmp(c,"n") || !strcmp(c,"0") ||
	      !g_strcasecmp(c,"no") || !g_strcasecmp(c,"off") ||
	      !g_strcasecmp(c,"false") || !g_strcasecmp(c,"disabled"))
	       return FALSE;
     }
     inifile_set_gboolean(setting,defaultValue);
     return defaultValue;
}

gboolean inifile_set(gchar *setting, gchar *value)
{
     GList *l = settings;
     struct inifile_setting *s;
     while (l!=NULL && g_strcasecmp((s=l->data)->name,setting)) l=l->next;
     if (l==NULL) {
	  s = g_malloc(sizeof(struct inifile_setting));
	  s->name = g_strdup(setting);
	  s->value = g_strdup(value);
	  settings = g_list_append(settings, s);
     } else {
	  s = l->data;
          if (!strcmp(value,s->value)) return FALSE;
	  g_free(s->value);
	  s->value = g_strdup(value);
     }
     settings_changed = TRUE;
     return TRUE;
}

gboolean inifile_set_gint32(gchar *setting, gint32 value)
{
     gchar c[32];
     g_snprintf(c,sizeof(c),"%li",(long int)value);
     return inifile_set(setting,c);
}

gboolean inifile_set_gboolean(gchar *setting, gboolean value)
{
     return inifile_set(setting,value?"true":"false");
}

gboolean inifile_set_gfloat(gchar *setting, gfloat value)
{
     gchar c[128];
//     set_c_locale();
     g_snprintf(c,sizeof(c),"%f",(float)value);
//     restore_locale();
     return inifile_set(setting,c);
}

static void do_save_setting(struct inifile_setting *s, EFILE *f)
{
     e_fwrite(s->name,strlen(s->name),f);
     e_fwrite(" = ",3,f);
     e_fwrite(s->value,strlen(s->value),f);
     e_fwrite("\n",1,f);
}

void inifile_save(void)
{
     EFILE *f = e_fopen(ininame,EFILE_WRITE);
     char *c = 
	  "# Remove this file to restore default settings.\n"
	  "\n";
     if (e_fwrite(c,strlen(c),f)) return;
     g_list_foreach(settings,(GFunc)do_save_setting,f);
     e_fclose(f);
     settings_changed = FALSE;
}

static void do_quit(struct inifile_setting *i)
{
     g_free(i->name);
     g_free(i->value);
     g_free(i);
}

void inifile_quit(void)
{
     if (settings_changed) inifile_save();
     g_list_foreach(settings,(GFunc)do_quit,NULL);
     g_list_free(settings);
}
