#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
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

    if(0 <= type && sizeof(up_stdtypes) / sizeof(up_stdtypes[0]) > type)
        return up_stdtypes[type];

    for(ii = 0; sizeof(up_exttypes) / sizeof(up_exttypes[0]) > ii; ii++)
        if(up_exttypes[ii].type == type)
            return up_exttypes[ii].label;

    return NULL;
}

int
up_sunlabel_fmt(char *buf, int size, unsigned int type, unsigned int flags)
{
    const char *typestr = up_sunlabel_parttype(type);
    char        flagstr[5];

    if(~PFLAG_KNOWN & flags)
        snprintf(flagstr, sizeof flagstr, "%04x", flags);
    else
        snprintf(flagstr, sizeof flagstr, "%c%c",
                 PFLAG_GETCHR(flags, RONLY), PFLAG_GETCHR(flags, UNMNT));

    if(typestr)
        return snprintf(buf, size, "%-5s %s", flagstr, typestr);
    else
        return snprintf(buf, size, "%-5s %u", flagstr, type);
}
