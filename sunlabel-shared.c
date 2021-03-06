/* 
 * Copyright (c) 2008-2010 Joshua R. Elsasser.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "sunlabel-shared.h"

#define PFLAG_UNMNT        (0x01)
#define PFLAG_UNMNT_CHRS   ("mu")
#define PFLAG_RONLY        (0x10)
#define PFLAG_RONLY_CHRS   ("wr")
#define PFLAG_KNOWN        (PFLAG_UNMNT | PFLAG_RONLY)
#define PFLAG_GETCHR(var, flag) \
    ((PFLAG_##flag##_CHRS)[(PFLAG_##flag & (var) ? 1 : 0)])

static const char *up_stdtypes[] =
{
    "unassigned",
    "boot",
    "root",
    "swap",
    "usr",
    "backup",
    "stand",
    "var",
    "home",
    "altsctr",
    "cache",
    "reserved",
};

static const struct { uint16_t type; const char *label; } up_exttypes[] =
{
    {0x82, "Linux Swap"},
    {0x83, "Linux Filesystem"},
};

const char *
up_sunlabel_parttype(unsigned int type)
{
    int ii;

    if(sizeof(up_stdtypes) / sizeof(up_stdtypes[0]) > type)
        return up_stdtypes[type];

    for(ii = 0; sizeof(up_exttypes) / sizeof(up_exttypes[0]) > ii; ii++)
        if(up_exttypes[ii].type == type)
            return up_exttypes[ii].label;

    return NULL;
}

int
up_sunlabel_fmt(FILE *stream, unsigned int type, unsigned int flags)
{
	const char *typestr;
	char flagstr[5];

	if (~PFLAG_KNOWN & flags)
		snprintf(flagstr, sizeof flagstr, "%04x", flags);
	else
		snprintf(flagstr, sizeof flagstr, "%c%c",
		    PFLAG_GETCHR(flags, RONLY), PFLAG_GETCHR(flags, UNMNT));

	typestr = up_sunlabel_parttype(type);
	if (typestr != NULL)
		return (fprintf(stream, " %-5s %s", flagstr, typestr));
	else
		return (fprintf(stream, " %-5s %u", flagstr, type));
}
