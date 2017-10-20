/*	inifile.c
	Copyright (C) 2007-2017 Dmitry Groshev

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

/* *** PREFACE ***
 *  This implementation has no comment preservation, and has nested sections -
 * because this is how we like our inifiles. :-)
 *  Allocations are done in slabs, because no slot is ever deallocated or
 * reordered; only modified string values are allocated singly, since it is
 * probable they will keep being modified.
 *  Implementation uses one 32-bit hash function as two 16-bit ones while
 * possible, but with 40000 keys or more, has to evaluate two 32-bit functions
 * instead. But such loads are expected to be rare. - WJ */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <gtk/gtk.h>

#include "global.h"
#include "memory.h"
#include "inifile.h"

/* Make code not compile where it cannot run */
typedef char Integers_Do_Not_Fit_Into_Pointers[2 * (sizeof(int) <= sizeof(char *)) - 1];

#define SLAB_INCREMENT 16384
#define SLAB_RESERVED  64 /* Reserved for allocator overhead */

#define INI_LIMIT 0x40000000 /* Limit imposed by hashing scheme */
#define HASHFILL(X) ((X) * 3) /* Hash occupancy no more than 1/3 */
#define HASHSEED 0x811C9DC5
#define HASH_RND(X) ((X) * 0x10450405 + 1)
#define HASH_MIN 256 /* Minimum hash capacity, so that it won't be too tiny */

/* Slot types */
enum {
	INI_NONE = 0,
	INI_UNDEF,
	INI_STR,
	INI_INT,
	INI_BOOL,
	INI_REF
};
// Negative types are section nesting levels

/* Slot flags */
#define INI_SYSTEM  0x0001 /* Systemwide inifile */
#define INI_MALLOC  0x0002 /* Value is allocated singly (not in block) */
#define INI_DEFAULT 0x0004 /* Default value is defined */
#define INI_TEMP    0x0008 /* Transient - not written out */

/* Escaping is needed for leading tabs and spaces, for inline CR and LF, and
 * possibly for inline backslash */
static char *escape_string(char *src, int bs)
{
	static const char *escaped = "\t \\\r\n";
	const char *e = escaped;
	char c, *cp, *tmp, *t2;

	if ((src[0] != '\t') && (src[0] != ' ') &&
		!src[strcspn(src, "\\\r\n" + !bs)]) return (NULL);
	t2 = tmp = calloc(1, strlen(src) * 2 + 1);
	if (tmp)
	{
		while ((c = *src++))
		{
			if ((cp = strchr(e, c)))
			{
				*t2++ = '\\';
				c = "t \\rn"[cp - escaped];
			}
			*t2++ = c;
			e = escaped + 2;
		}
		*t2 = '\0';
	}
	return (tmp);
}

#if GTK_MAJOR_VERSION == 1

/* GLib 1.2 doesn't provide g_strcompress() */
static void unescape_string(char *buf)
{
#define NUM_ESCAPES 5
	static const char escapes[] = "bfnrt01234567";
	static const char escaped[] = { 7, 12, 10, 13, 8, 0, 1, 2, 3, 4, 5, 6, 7 };
	char c, cc, *tmp, *src = buf;
	int v;

	while ((c = *src++))
	{
		if (c == '\\')
		{
			c = *src++;
			if (!c) break;
			while ((tmp = strchr(escapes, c)))
			{
				v = tmp - escapes;
				c = escaped[v];
				if (v >= NUM_ESCAPES)
				{
					// Octal escape
					cc = *src - '0';
					if (cc & ~7) break;
					c = c * 8 + cc;
					cc = *(++src) - '0';
					if (cc & ~7) break;
					c = c * 8 + cc;
					++src;
				}
				break;
			}
		}
		*buf++ = c;
	}
	*buf = '\0';
#undef NUM_ESCAPES
}

#else

static void unescape_string(char *buf)
{
	char *tmp = g_strcompress(buf);
	strcpy(buf, tmp);
	g_free(tmp);
}

#endif

/* Thomas Wang's and "One at a time" hash functions */
static guint32 hashf(guint32 seed, int section, char *key)
{
	seed += section;
	seed = (seed << 15) + ~seed;
	seed ^= (seed >> 12);
	seed += seed << 2;
	seed ^= seed >> 4;
	seed *= 2057;
	seed ^= seed >> 16;
	for (; *key; key++)
	{
		seed += *key;
		seed += seed << 10;
		seed ^= seed >> 6;
	}
	seed += seed << 3;
	seed ^= seed >> 11;
	seed += seed << 15;
	return (seed);
} 

static int cuckoo_insert(inifile *inip, inislot *slotp)
{
	gint32 idx, tmp;
	guint32 key;
	int i, j, d;

	/* Update section's chain */
	i = slotp->sec;
	idx = slotp - inip->slots;
	if ((j = (int)inip->slots[i].value) < idx)
	{
		inip->slots[i].value = (char *)idx;
		if (j) inip->slots[j].chain = idx;
		else inip->slots[i].defv = (char *)idx;
	}

	/* Decide if using one-key mode */
	d = inip->seed[0] == inip->seed[1];

	/* Normal cuckoo process */
	for (i = 0; i < inip->maxloop; i++)
	{
		key = hashf(inip->seed[i & 1], slotp->sec, slotp->key);
		key >>= (i & d) << 4; // Shift by 16 or don't
		j = (key & inip->hmask) * 2 + (i & 1);
		tmp = inip->hash[j];
		inip->hash[j] = idx;
		idx = tmp;
		if (!idx) return (TRUE);
		slotp = inip->slots + idx;
	}
	return (FALSE);
}

static int resize_hash(inifile *inip, int cnt)
{
	int len;

	if (!cnt) return (0); /* Degenerate case */
	len = nextpow2(HASHFILL(cnt) - 1);
	if (len <= inip->hmask * 2 + 2) return (0); /* Large enough */
	free(inip->hash);
	inip->hash = calloc(1, len * sizeof(gint32));
	if (!inip->hash) return (-1); /* Failure */
	inip->hmask = (len >> 1) - 1;
	inip->maxloop = ceil(3.0 * log(len / 2) /
		log((double)(len / 2) / cnt));
	return (1); /* Resized */
}

static int rehash(inifile *inip)
{
	int i, flag;

	flag = resize_hash(inip, inip->count);
	if (flag < 0) return (FALSE); /* Failed */

	while (TRUE) /* Until done */
	{
		if (!flag) /* No size change */
		{
			inip->seed[0] = inip->seed[1] = HASH_RND(inip->seed[0]);
			memset(inip->hash, 0, (inip->hmask + 1) * 2 * sizeof(gint32));
		}
		/* Enter two-key mode */
		if (inip->hmask > 0xFFFF) inip->seed[1] = HASH_RND(inip->seed[0]);
		/* Re-insert items */
		for (i = 1; (i <= inip->count) &&
			(flag = cuckoo_insert(inip, inip->slots + i)); i++);
		if (flag) return (TRUE);
	}
}

static inislot *cuckoo_find(inifile *inip, int section, char *name)
{
	inislot *slotp;
	guint32 key;
	gint32 i;
	int j = 0;

	key = hashf(inip->seed[0], section, name);
	while (TRUE)
	{
		i = inip->hash[(key & inip->hmask) * 2 + j];
		if (i)
		{
			slotp = inip->slots + i;
			if ((slotp->sec == section) && !strcmp(name, slotp->key))
				return (slotp);
		}
		if (j++) return (NULL);
		if (inip->seed[0] == inip->seed[1]) key >>= 16;
		else key = hashf(inip->seed[1], section, name);
	}
}

static inislot *add_slot(inifile *inip)
{
	inislot *ra;
	int i, j;

	if (inip->count >= INI_LIMIT) return (NULL); /* Too many entries */
	/* Extend the slab if needed */
	i = inip->count * sizeof(inislot) + sizeof(inislot) +
		SLAB_RESERVED + SLAB_INCREMENT - 1;
	j = (i + sizeof(inislot)) / SLAB_INCREMENT;
	if (i / SLAB_INCREMENT < j)
	{
		ra = realloc(inip->slots, j * SLAB_INCREMENT - SLAB_RESERVED);
		if (!ra) return (NULL);
		inip->slots = ra;
	}
	ra = inip->slots + ++inip->count;
	memset(ra, 0, sizeof(inislot));
	return (ra);
}

static char *store_string(inifile *inip, char *str)
{
	int i, l;
	char *ra;

	/* First byte of first block for zero length */
	if (!str || !*str) return (*(char **)inip->sblocks + sizeof(char *));

	/* Add new block if needed */
	l = strlen(str) + 1;
	i = inip->slen + SLAB_RESERVED + SLAB_INCREMENT - 1;
	if (i / SLAB_INCREMENT < (i + l) / SLAB_INCREMENT)
	{
		i = l + sizeof(char *) + SLAB_RESERVED + SLAB_INCREMENT - 1;
		i -= i % SLAB_INCREMENT + SLAB_RESERVED;
		ra = calloc(1, i);
		if (!ra) return (NULL);
		/* Insert at tail of ring */
		*(char **)ra = *(char **)inip->sblocks;
		*(char **)inip->sblocks = ra;
		inip->sblocks = ra;
		inip->slen = sizeof(char *);
	}
	ra = inip->sblocks + inip->slen;
	memcpy(ra, str, l);
	inip->slen += l;
	return (ra);
}

static inislot *key_slot(inifile *inip, int section, char *key, int type)
{
	inislot *slot;

	if (type < 0) type = inip->slots[section].type - 1;
	slot = cuckoo_find(inip, section, key);
	if (slot)
	{
		if (type == INI_NONE) return (slot);
		if (slot->flags & INI_MALLOC)
		{
			free(slot->value);
			slot->flags ^= INI_MALLOC;
		}
#if VALIDATE_TYPE
		if ((slot->type != type) && (slot->type > INI_UNDEF))
			g_warning("INI key '%s' changed type\n", key);
#endif
	}
	else
	{
		key = store_string(inip, key);
		if (!key) return (NULL);
		slot = add_slot(inip);
		if (!slot) return (NULL);
		slot->sec = section;
		slot->key = key;
		if (!cuckoo_insert(inip, slot) && !rehash(inip))
			return (NULL);
	}
	slot->type = type;
	return (slot);
}

int new_ini(inifile *inip)
{
	memset(inip, 0, sizeof(inifile));
	inip->sblocks = calloc(1, SLAB_INCREMENT - SLAB_RESERVED);
	if (!inip->sblocks) return (FALSE);
	*(char **)inip->sblocks = inip->sblocks;
	inip->slen = sizeof(char *) + 1;
	inip->slots = calloc(1, SLAB_INCREMENT - SLAB_RESERVED);
	inip->seed[0] = inip->seed[1] = HASHSEED;
	return (inip->slots && (resize_hash(inip, HASH_MIN) > 0));
}

void forget_ini(inifile *inip)
{
	char *this, *next, *mem = inip->sblocks;
	int i;

	if (mem) for (this = *(char **)mem; this != mem; this = next)
	{
		next = *(char **)this;
		free(this);
	}
	free(mem);
	for (i = 1; i <= inip->count; i++)
	{
		if (inip->slots[i].flags & INI_MALLOC)
			free(inip->slots[i].value);
	}
	free(inip->slots);
	free(inip->hash);
	memset(inip, 0, sizeof(inifile));
}

/* Load file whole into memory, with N zero bytes before and two after */
char *slurp_file(char *fname, int before)
{
	FILE *fp;
	char *buf;
	int i, l;

	if (!fname || !(fp = fopen(fname, "rb"))) return (NULL);
	fseek(fp, 0, SEEK_END);
	l = ftell(fp);
	buf = calloc(1, l + before + 2);
	if (buf)
	{
		fseek(fp, 0, SEEK_SET);
		i = fread(buf + before, 1, l, fp);
		if (i != l)
		{
			free(buf);
			buf = NULL;
		}
	}
	fclose(fp);
	return (buf);
}

int read_ini(inifile *inip, char *fname, int itype)
{
	inifile ini;
	inislot *slot;
	char *tmp, *wrk, *w2, *str;
	int i, j, l, q, sec = 0;


	/* Read the file */
	tmp = slurp_file(fname, sizeof(char *) + 1);
	if (!tmp) return (0);
	ini = *inip;
	/* Insert at head of ring */
	*(char **)tmp = *(char **)inip->sblocks;
	*(char **)inip->sblocks = tmp;

	/* Parse the contents */
	for (tmp += sizeof(char *) + 1; ; tmp = str)
	{
		tmp += strspn(tmp, "\r\n\t ");
		if (!*tmp) break;
		str = tmp + strcspn(tmp, "\r\n");
		if (*str) *str++ = '\0';
		if ((*tmp == ';') || (*tmp == '#')) continue; /* Comment */
		if (*tmp == '[') /* Section */
		{
			if (!(w2 = strchr(tmp + 1, ']'))) goto error;
			for (i = 0; tmp[i + 1] == '>'; i++); // Nesting level
			j = i + ini.slots[sec].type;
			if (j > 0) goto error;
			l = sec;
			while (j++) l = ini.slots[l].sec;
			*w2 = '\0';
			/* Hash this */
			tmp += ++i;
			slot = l || *tmp ? cuckoo_find(&ini, l, tmp) : ini.slots;
			if (!slot) /* New section */
			{
				slot = add_slot(&ini);
				if (!slot) goto fail;
				slot->type = -i;
				slot->sec = l;
				slot->key = tmp;
				slot->flags = itype;
				if (!cuckoo_insert(&ini, slot) && !rehash(&ini))
					goto fail;
			}
			sec = slot - ini.slots; /* Activate */
			continue;
		}
		/* Variable (spaces in name allowed) */
		w2 = strchr(tmp, '=');
		if (!w2)
		{
error:			g_printerr("Wrong INI line: '%s'\n", tmp);
			continue;
		}
		for (wrk = w2 - 1; wrk - tmp >= 0; wrk--)
			if ((*wrk != ' ') && (*wrk != '\t')) break;
		wrk[1] = '\0';
		q = *(++w2) == '='; /* "==" means quoted value */
		w2 += q + strspn(w2 + q, "\t ");
//		for (wrk = str - 1; *wrk && ((*wrk == '\t') || (*wrk == ' '); *wrk-- = '\0');
		/* Hash this pair */
		slot = cuckoo_find(&ini, sec, tmp);
		if (!slot) /* New key */
		{
			slot = add_slot(&ini);
			if (!slot) goto fail;
			slot->sec = sec;
			slot->key = tmp;
			slot->flags = itype;
			if (!cuckoo_insert(&ini, slot) && !rehash(&ini))
				goto fail;
		}
		if (q) unescape_string(w2);
		slot->type = INI_UNDEF;
		slot->value = w2;
	}

	/* Return the result */
	*inip = ini;
	return (1);

	/* Catastrophic failure - unable to add key */
fail:	forget_ini(&ini);
	*inip = ini;
	return (-1);
}

int write_ini(inifile *inip, char *fname, char *header)
{
	FILE *fp;
	inislot *slotp, *secp = NULL;
	char *name, *sv, *xv;
	int i, j, sec, var, up, down, written = 0;

	if (!(fp = fopen(fname, "w"))) return (FALSE);
	if (header) fprintf(fp, "%s\n", header);

	sec = 0; var = -1;
	while (TRUE)
	{
		/* (Re)start scanning a new section */
		if (var <= 0) i = (int)inip->slots[sec].defv , var = !var;
		else if (!sec) break; /* All done */
		/* Return to scanning for parent's subsections */
		else
		{
			i = inip->slots[sec].chain;
			sec = inip->slots[sec].sec;
			if (written > sec) written = sec;
		}
		for (; i; i = inip->slots[i].chain)
		{
			slotp = inip->slots + i;

			/* Transients are skipped with whole subtrees */
			if (slotp->flags & INI_TEMP) continue;

			/* Variables first, subsections second */
			if ((slotp->type < 0) ^ var) continue;

			if (var) /* Section */
			{
				sec = i; var = -1;
				break;
			}

			sv = slotp->value ? slotp->value : "";

			/* Keys from system inifile ignore defaults, because
			 * they exist to override the defaults - WJ */
			if (!(slotp->flags & INI_SYSTEM) &&
				(slotp->flags & INI_DEFAULT) &&
				(slotp->type == INI_STR ?
				!strcmp(slotp->defv, sv) :
				(slotp->type == INI_INT) || (slotp->type == INI_REF) ?
				(int)slotp->value == (int)slotp->defv :
				slotp->type == INI_BOOL ?
				!!slotp->value == !!slotp->defv :
				FALSE))	continue;

			/* Write out sections */
			up = 0;
			while (written < sec) // Reverse chain to written level
			{
				down = inip->slots[sec].sec;
				inip->slots[sec].sec = up;
				up = sec;
				sec = down;
			}
			while (up) // Write out reversing back
			{
				down = sec;
				written = sec = up;
				secp = inip->slots + sec;
				up = secp->sec;
				secp->sec = down;
				fputc('[', fp);
				for (j = -1; j > secp->type; j--) fputc('>', fp);
				fprintf(fp, "%s]\n", secp->key);
			}

			/* Write out variable */
			name = slotp->key;
			switch (slotp->type)
			{
			case INI_REF:
				fprintf(fp, "%s == ", name);
				j = (int)slotp->value;
				up = 0;
				while (j > 0) // Reverse chain to level 0
				{
					down = inip->slots[j].sec;
					inip->slots[j].sec = up;
					up = j;
					j = down;
				}
				while (up) // Write out reversing back
				{
					down = j;
					j = up;
					secp = inip->slots + j;
					up = secp->sec;
					secp->sec = down;
					xv = escape_string(secp->key, TRUE);
					fprintf(fp, "%s%s", xv ? xv : secp->key,
						up ? "\\n" : "");
					free(xv);
				}
				fputc('\n', fp);
				break;
			case INI_INT:
				fprintf(fp, "%s = %d\n", name, (int)slotp->value);
				break;
			case INI_BOOL:
				fprintf(fp, "%s = %s\n", name, slotp->value ?
					"true" : "false");
				break;
			default:
				/* Escape value if needed */
				if ((xv = escape_string(sv, FALSE)))
				{
					fprintf(fp, "%s == %s\n", name, xv);
					free(xv);
				}
				else fprintf(fp, "%s = %s\n", name, sv);
				break;
			}
		}
	}
	/* Return to main section if have sections */
	if (secp) fputs("[]\n", fp);

	fclose(fp);
	return (TRUE);
}

int ini_setstr(inifile *inip, int section, char *key, char *value)
{
	inislot *slot;

	/* NULLs are stored as empty strings, for less hazardous handling.
	 * Value duplicated at once, for key_slot() might free it
	 * (if it is the current one, being stored over itself) */
	value = value && *value ? strdup(value) : NULL;
	if (!(slot = key_slot(inip, section, key, INI_STR)))
	{
		free(value);
		return (0);
	}
	slot->value = "";
	if (value)
	{
		slot->value = value;
		slot->flags |= INI_MALLOC;
	}
	return (slot - inip->slots);
}

int ini_setint(inifile *inip, int section, char *key, int value)
{
	inislot *slot;

	if (!(slot = key_slot(inip, section, key, INI_INT))) return (0);
	slot->value = (char *)value;
	return (slot - inip->slots);
}

int ini_setbool(inifile *inip, int section, char *key, int value)
{
	inislot *slot;

	if (!(slot = key_slot(inip, section, key, INI_BOOL))) return (0);
	slot->value = (char *)!!value;
	return (slot - inip->slots);
}

int ini_setref(inifile *inip, int section, char *key, int value)
{
	inislot *slot;

	if (!(slot = key_slot(inip, section, key, INI_REF))) return (0);
	slot->value = (char *)value;
	return (slot - inip->slots);
}

char *ini_getstr(inifile *inip, int section, char *key, char *defv)
{
	inislot *slot;

	/* NULLs are stored as empty strings, for less hazardous handling */
	if (!defv) defv = "";

	/* Read existing */
	slot = key_slot(inip, section, key, INI_NONE);
	if (!slot) return (defv);
	if (slot->type == INI_STR)
	{
#if VALIDATE_DEF
		if ((slot->flags & INI_DEFAULT) &&
			strcmp(defv ? defv : "", slot->defv))
			g_warning("INI key '%s' new default\n", key);
#endif
		if (slot->flags & INI_DEFAULT) return (slot->value);
		slot->type = INI_UNDEF; /* Fall through to storing default */
	}

	/* Store default */
	slot->flags &= ~INI_DEFAULT;
	if ((slot->defv = store_string(inip, defv)))
		slot->flags |= INI_DEFAULT;

	if (slot->type != INI_UNDEF)
	{
#if VALIDATE_TYPE
		if (slot->type != INI_NONE)
			g_printerr("INI key '%s' wrong type\n", key);
#endif
		if (!defv) slot->value = NULL;
		else if (!*defv) slot->value = "";
		else
		{
			slot->value = strdup(defv);
			if (slot->value) slot->flags |= INI_MALLOC;
		}
	}
	slot->type = INI_STR;
	return (slot->value);
}

int ini_getint(inifile *inip, int section, char *key, int defv)
{
	inislot *slot;
	char *tail;
	long l;

	/* Read existing */
	slot = key_slot(inip, section, key, INI_NONE);
	if (!slot) return (defv);
	if (slot->type == INI_INT)
	{
#if VALIDATE_DEF
		if ((slot->flags & INI_DEFAULT) && ((char *)defv != slot->defv))
			g_warning("INI key '%s' new default\n", key);
#endif
		if (slot->flags & INI_DEFAULT) return ((int)(slot->value));
		/* Fall through to storing default */
	}

	/* Store default */
	slot->defv = (char *)defv;
	slot->flags |= INI_DEFAULT;

	while (slot->type != INI_INT)
	{
		if (slot->type == INI_UNDEF)
		{
			l = strtol(slot->value, &tail, 10);
			slot->value = (char *)l;
			if (!*tail) break;
		}
		else if (slot->flags & INI_MALLOC)
		{
			free(slot->value);
			slot->flags ^= INI_MALLOC;
		}
#if VALIDATE_TYPE
		if (slot->type != INI_NONE)
			g_printerr("INI key '%s' wrong type\n", key);
#endif
		slot->value = (char *)defv;
		break;
	}
	slot->type = INI_INT;
	return ((int)(slot->value));
}

int str2bool(const char *s)
{
	static const char *YN[] = { "n", "y", "0", "1", "no", "yes",
		"off", "on", "false", "true", "disabled", "enabled", NULL };
	int i;

	for (i = 0; YN[i]; i++) if (!strcasecmp(YN[i], s)) return (i & 1);
	return (-1);
}

int ini_getbool(inifile *inip, int section, char *key, int defv)
{
	inislot *slot;
	int i;

	defv = !!defv;

	/* Read existing */
	slot = key_slot(inip, section, key, INI_NONE);
	if (!slot) return (defv);
	if (slot->type == INI_BOOL)
	{
#if VALIDATE_DEF
		if ((slot->flags & INI_DEFAULT) && ((char *)defv != slot->defv))
			g_warning("INI key '%s' new default\n", key);
#endif
		if (slot->flags & INI_DEFAULT) return ((int)(slot->value));
		/* Fall through to storing default */
	}

	/* Store default */
	slot->defv = (char *)defv;
	slot->flags |= INI_DEFAULT;

	while (slot->type != INI_BOOL)
	{
		if (slot->type == INI_UNDEF)
		{
			slot->value = (char *)(i = str2bool(slot->value));
			if (i >= 0) break;
		}
		else if (slot->flags & INI_MALLOC)
		{
			free(slot->value);
			slot->flags ^= INI_MALLOC;
		}
#if VALIDATE_TYPE
		if (slot->type != INI_NONE)
			g_printerr("INI key '%s' wrong type\n", key);
#endif
		slot->value = (char *)defv;
		break;
	}
	slot->type = INI_BOOL;
	return ((int)(slot->value));
}

int ini_getref(inifile *inip, int section, char *key, int defv)
{
	inislot *slot;
	char *w, *tmp;

	/* Read existing */
	slot = key_slot(inip, section, key, INI_NONE);
	if (!slot) return (defv);
	if (slot->type == INI_REF)
	{
#if VALIDATE_DEF
		if ((slot->flags & INI_DEFAULT) && ((char *)defv != slot->defv))
			g_warning("INI key '%s' new default\n", key);
#endif
		if (slot->flags & INI_DEFAULT) return ((int)(slot->value));
		/* Fall through to storing default */
	}

	/* Store default */
	slot->defv = (char *)defv;
	slot->flags |= INI_DEFAULT;

	while (slot->type != INI_REF)
	{
		if (slot->type == INI_UNDEF)
		{
			/* Parse a LF-separated path through sections */
			section = 0;
			tmp = slot->value;
			while (tmp)
			{
				tmp = strchr(w = tmp, '\n');
				if (tmp)
				{
					*tmp++ = '\0';
					section = ini_getsection(inip, section, w);
					if (section <= 0) break;
				}
				else if (*w) 
				{
					inislot *s = cuckoo_find(inip, section, w);
					section = s ? s - inip->slots : 0;
				}
			}
			/* Empty stays empty, broken goes to default */
			if ((section <= 0) && slot->value[0]) section = defv;
			slot->value = (char *)section;
			break;
		}
		else if (slot->flags & INI_MALLOC)
		{
			free(slot->value);
			slot->flags ^= INI_MALLOC;
		}
#if VALIDATE_TYPE
		if (slot->type != INI_NONE)
			g_printerr("INI key '%s' wrong type\n", key);
#endif
		slot->value = (char *)defv;
		break;
	}
	slot->type = INI_REF;
	return ((int)(slot->value));
}

int ini_setsection(inifile *inip, int section, char *key)
{
	inislot *slot = key_slot(inip, section, key, -1);
	if (!slot) return (-1);
	return (slot - inip->slots);
}

int ini_getsection(inifile *inip, int section, char *key)
{
	inislot *slot = cuckoo_find(inip, section, key);
	if (!slot || (slot->type != inip->slots[section].type - 1)) return (-1);
	return (slot - inip->slots);
}

int ini_transient(inifile *inip, int section, char *key)
{
	inislot *slot = key ? cuckoo_find(inip, section, key) :
		section ? inip->slots + section : NULL;
	if (!slot) return (FALSE);
	slot->flags |= INI_TEMP;
	return (TRUE);
}

#ifdef WIN32

char *get_home_directory(void)
{
	static char *homedir = NULL;

	if (homedir) return homedir;
	homedir = getenv("USERPROFILE");	// Gets the current users home directory in WinXP
	if (!homedir) homedir = "";		// And this, in Win9x :-)
	return homedir;
}

char *extend_path(const char *path)
{
	char *dir, *name;

	if (path[0] == '~')
		return (g_strdup_printf("%s%s", get_home_directory(), path + 1));
	if (path[0] && (path[1] == ':')) // Path w/drive letter is absolute
		return (g_strdup(path));
	name = g_win32_get_package_installation_directory(NULL, NULL);
	dir = g_locale_from_utf8(name, -1, NULL, NULL, NULL);
	g_free(name);
	name = g_strdup_printf("%s%s", dir, path);
	g_free(dir);
	return (name);
}

#else

#include <unistd.h>
#include <pwd.h>

/*
 * This function came from mhWaveEdit
 * by Magnus Hjorth, 2003.
 */
gchar *get_home_directory(void)
{
	static char *homedir = NULL;
	struct passwd *p;

	if (homedir) return homedir;
	homedir = getenv("HOME");
	if (!homedir)
	{
		p = getpwuid(getuid());
		if (p) homedir = p->pw_dir;
	}
	if (!homedir)
	{
		g_warning(_("Could not find home directory. Using current directory as "
			"home directory."));
		homedir = ".";
	}
	return homedir;
}

char *extend_path(const char *path)
{
	if (path[0] == '~')
		return (g_strdup_printf("%s%s", get_home_directory(), path + 1));
	return (g_strdup(path));
}

#endif

/* Compatibility functions */

inifile main_ini;
static char *main_ininame;

void inifile_init(char *system_ini, char *user_ini)
{
	char *tmp, *ini = user_ini;
	int res, mask = 3;

	while (new_ini(&main_ini))
	{
		if ((mask & 1) && system_ini)
		{
			tmp = extend_path(system_ini);
			res = read_ini(&main_ini, tmp, INI_SYSTEM);
			g_free(tmp);
			if (res <= 0) mask ^= 1; // Don't try again if failed
			if (res < 0) continue; // Restart if struct got deleted
			/* !!! Allow system inifile to relocate user inifile */
			ini = inifile_get("userINI", user_ini);
			if (!ini[0]) ini = system_ini;
		}
		if ((mask & 2) && user_ini)
		{
			main_ininame = extend_path(ini);
			res = read_ini(&main_ini, main_ininame, 0);
			if (res <= 0) // Failed
			{
				mask ^= 2;
				if (res < 0) continue;
			}
		}
		break;
	}
}

void inifile_quit()
{
	write_ini(&main_ini, main_ininame,
	  "# Remove this file to restore default settings.\n");
	forget_ini(&main_ini);
	g_free(main_ininame);
}

char *inifile_get(char *setting, char *defaultValue)
{
	return (ini_getstr(&main_ini, 0, setting, defaultValue));
}

int inifile_get_gint32(char *setting, int defaultValue)
{
	return (ini_getint(&main_ini, 0, setting, defaultValue));
}

int inifile_get_gboolean(char *setting, int defaultValue)
{
	return (ini_getbool(&main_ini, 0, setting, defaultValue));
}

int inifile_set(char *setting, char *value)
{
	return (ini_setstr(&main_ini, 0, setting, value));
}

int inifile_set_gint32(char *setting, int value)
{
	return (ini_setint(&main_ini, 0, setting, value));
}

int inifile_set_gboolean(char *setting, int value)
{
	return (ini_setbool(&main_ini, 0, setting, value));
}
