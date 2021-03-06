/* 
 * Copyright (c) 2007-2014 Joshua R. Elsasser.
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

#ifndef HDR_UPART_UTIL
#define HDR_UPART_UTIL

#ifndef MIN
#define MIN(aa, bb)             ((aa) < (bb) ? (aa) : (bb))
#endif

#ifndef MAX
#define MAX(aa, bb)             ((aa) > (bb) ? (aa) : (bb))
#endif

#ifndef NITEMS
#define NITEMS(ary)		(sizeof(ary) / sizeof((ary)[0]))
#endif

#ifdef __GNUC__
#define ATTR_PRINTF(f,a)		__attribute__((format (printf, f, a)))
#define ATTR_PACKED			__attribute__((packed))
#define ATTR_UNUSED			__attribute__((unused))
#define ATTR_SENTINEL(p)		__attribute__((sentinel(p)))
#else
#define ATTR_PRINTF(f,a)
#define ATTR_PACKED
#define ATTR_UNUSED
#define ATTR_SENTINEL(p)
#endif

#define UP_ENDIAN_BIG           (4321)
#define UP_ENDIAN_LITTLE        (1234)
extern int up_endian;

/* convert NUM from big endian to host byte order */
#define UP_BETOH16(num) \
    (UP_ENDIAN_BIG    == up_endian ? (const uint16_t)(num) : UP_SWAP16(num))
#define UP_BETOH32(num) \
    (UP_ENDIAN_BIG    == up_endian ? (const uint32_t)(num) : UP_SWAP32(num))
#define UP_BETOH64(num) \
    (UP_ENDIAN_BIG    == up_endian ? (const uint64_t)(num) : UP_SWAP64(num))
/* convert NUM from little endian to host byte order */
#define UP_LETOH16(num) \
    (UP_ENDIAN_LITTLE == up_endian ? (const uint16_t)(num) : UP_SWAP16(num))
#define UP_LETOH32(num) \
    (UP_ENDIAN_LITTLE == up_endian ? (const uint32_t)(num) : UP_SWAP32(num))
#define UP_LETOH64(num) \
    (UP_ENDIAN_LITTLE == up_endian ? (const uint64_t)(num) : UP_SWAP64(num))
/* convert NUM from host byte order to big endian */
#define UP_HTOBE16(num) \
    (UP_ENDIAN_BIG    == up_endian ? (const uint16_t)(num) : UP_SWAP16(num))
#define UP_HTOBE32(num) \
    (UP_ENDIAN_BIG    == up_endian ? (const uint32_t)(num) : UP_SWAP32(num))
#define UP_HTOBE64(num) \
    (UP_ENDIAN_BIG    == up_endian ? (const uint64_t)(num) : UP_SWAP64(num))
/* convert NUM from host byte order to little endian */
#define UP_HTOLE16(num) \
    (UP_ENDIAN_LITTLE == up_endian ? (const uint16_t)(num) : UP_SWAP16(num))
#define UP_HTOLE32(num) \
    (UP_ENDIAN_LITTLE == up_endian ? (const uint32_t)(num) : UP_SWAP32(num))
#define UP_HTOLE64(num) \
    (UP_ENDIAN_LITTLE == up_endian ? (const uint64_t)(num) : UP_SWAP64(num))

/* convert NUM from byte order ORD to host order */
#define UP_ETOH16(num, ord) \
    ((ord) == up_endian ? (const uint16_t)(num) : UP_SWAP16(num))
#define UP_ETOH32(num, ord) \
    ((ord) == up_endian ? (const uint32_t)(num) : UP_SWAP32(num))
#define UP_ETOH64(num, ord) \
    ((ord) == up_endian ? (const uint64_t)(num) : UP_SWAP64(num))
/* convert NUM from host byte order to order ORD */
#define UP_HTOE16(num, ord) \
    ((ord) == up_endian ? (const uint16_t)(num) : UP_SWAP16(num))
#define UP_HTOE32(num, ord) \
    ((ord) == up_endian ? (const uint32_t)(num) : UP_SWAP32(num))
#define UP_HTOE64(num, ord) \
    ((ord) == up_endian ? (const uint64_t)(num) : UP_SWAP64(num))

/* byte swapping macros used by the above macros */
#define UP_SWAP16(num) \
    (((((const uint16_t)(num)) & UINT16_C(0x00ff)) << 8) | \
     ((((const uint16_t)(num)) & UINT16_C(0xff00)) >> 8))

#define UP_SWAP32(num) \
    (((((const uint32_t)(num)) & UINT32_C(0x000000ff)) << 24) | \
     ((((const uint32_t)(num)) & UINT32_C(0x0000ff00)) <<  8) | \
     ((((const uint32_t)(num)) & UINT32_C(0x00ff0000)) >>  8) | \
     ((((const uint32_t)(num)) & UINT32_C(0xff000000)) >> 24))

#define UP_SWAP64(num) \
    (((((const uint64_t)(num)) & UINT64_C(0x00000000000000ff)) << 56) | \
     ((((const uint64_t)(num)) & UINT64_C(0x000000000000ff00)) << 40) | \
     ((((const uint64_t)(num)) & UINT64_C(0x0000000000ff0000)) << 24) | \
     ((((const uint64_t)(num)) & UINT64_C(0x00000000ff000000)) <<  8) | \
     ((((const uint64_t)(num)) & UINT64_C(0x000000ff00000000)) >>  8) | \
     ((((const uint64_t)(num)) & UINT64_C(0x0000ff0000000000)) >> 24) | \
     ((((const uint64_t)(num)) & UINT64_C(0x00ff000000000000)) >> 40) | \
     ((((const uint64_t)(num)) & UINT64_C(0xff00000000000000)) >> 56))

/* determine the current machine byte order */
int up_getendian(void);

/*

  Dump SIZE bytes of data in BUF to STREAM, in a format similar to hexdump -C
  DISPOFF is added to offsets when displayed.
*/
void up_hexdump(const void *buf, size_t size, uint64_t dispoff, void *stream);

/* Print something somewhat similar to hexdump -C piped through diff -u */
void up_hexdiff(const void *old, size_t osize, uint64_t ooff, const char *oname,
                const void *new, size_t nsize, uint64_t noff, const char *nname,
                void *stream);

/* Format NUM as a human readable size. UNITS should point to a const
   char * to store the unit label in. The UP_BESTDECIMAL() macro may
   be used on the return value for a 4-column precision. */
float up_fmtsize(int64_t num, const char **units);
#define UP_BESTDECIMAL(d)       (9.99 > (d) ? 2 : (99.9 > (d) ? 1 : 0))

/* Like snprintf(), except it appends to the end of a string. */
int up_scatprintf(char *str, size_t size, const char *format, ...);

/* Allocate memory with malloc() or calloc(), printing errors on failure */
#define XA_ZERO		(1 << 0) /* Zero the allocated memory */
#define XA_QUIET	(1 << 1) /* Don't print an error message on failure */
#define XA_FATAL	(1 << 2) /* Exit on failure */
void	*xalloc(size_t, size_t, unsigned int);
char	*xstrdup(const char *, unsigned int);

/* save and retrieve the program name */
int up_savename(const char *argv0);
const char *up_getname(void);

void up_err(const char *fmt, ...) ATTR_PRINTF(1, 2);
void up_warn(const char *fmt, ...) ATTR_PRINTF(1, 2);
#define UP_MSG_FWARN            (1 << 0)
#define UP_MSG_FERR             (1 << 1)
#define UP_MSG_FBARE            (1 << 2)
void up_msg(unsigned int flags, const char *fmt, ...) ATTR_PRINTF(2, 3);

#define UP_VERBOSITY_SILENT     -2
#define UP_VERBOSITY_QUIET      -1
#define UP_VERBOSITY_NORMAL     0
#define UP_VERBOSITY_EXTRA      1
#define UP_VERBOSITY_SPAM       2
#define UP_NOISY(need)		(opts->verbosity >= UP_VERBOSITY_ ## need)

int	printsect(uint64_t, FILE *);
int	printsect_pad(uint64_t, int, FILE *);
int	printsect_verbose(uint64_t, FILE *);

/* see strlcpy(3) manpage */
#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

/* see strlcat(3) manpage */
#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t siz);
#endif

/* see getopt(3) manpage */
#ifndef HAVE_GETOPT
extern	char *optarg;
extern	int opterr;
extern	int optind;
extern	int optopt;
extern	int optreset;
int	getopt(int, char * const *, const char *);
#endif

struct opts
{
	const char *serialize;
	const char *label;
	int verbosity;
	unsigned int plainfile : 1;
	unsigned int relaxed : 1;
	unsigned int sloppyio : 1;
	unsigned int printhex : 1;
	unsigned int humansize : 1;
	unsigned int swapcols : 1;
};

/* Pointer to the global program options, initially NULL. */
extern const struct opts *opts;

/* Initialize an option struct with sane default values. */
void	 init_options(struct opts *);

/* Set the global options. The passed-in struct is copied and need not
 * be retained. */
void	 set_options(const struct opts *);

#endif /* HDR_UPART_UTIL */
