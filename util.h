#ifndef HDR_UPART_UTIL
#define HDR_UPART_UTIL

#ifndef MIN
#define MIN(aa, bb)             ((aa) < (bb) ? (aa) : (bb))
#endif

#ifndef MAX
#define MAX(aa, bb)             ((aa) > (bb) ? (aa) : (bb))
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
   char * to store the unit label in. Use the UP_BESTDECIMAL() macro
   on the return value to get the best decimal precision when printing. */
float up_fmtsize(int64_t num, const char **units);
#define UP_BESTDECIMAL(d)       (10.0 > (d) ? 2 : (100.0 > (d) ? 1 : 0))

/* Like snprintf(), except it appends to the end of a string. */
int up_scatprintf(char *str, size_t size, const char *format, ...);

/* Like calloc(), except doesn't zero the memory */
void *up_malloc(size_t nmemb, size_t size);

/* save and retrieve the program name */
int up_savename(const char *argv0);
const char *up_getname(void);

void up_err(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
void up_warn(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
#define UP_MSG_FWARN            (1 << 0)
#define UP_MSG_FERR             (1 << 1)
#define UP_MSG_FBARE            (1 << 2)
void up_msg(unsigned int flags, const char *fmt, ...)
    __attribute__((format (printf, 2, 3)));

#define UP_VERBOSITY_SILENT     -2
#define UP_VERBOSITY_QUIET      -1
#define UP_VERBOSITY_NORMAL     0
#define UP_VERBOSITY_EXTRA      1
#define UP_VERBOSITY_SPAM       2
#define UP_NOISY(got, need) (UP_VERBOSITY_ ## need <= (got))

/* see strlcpy(3) manpage */
#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

/* see strlcat(3) manpage */
#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t siz);
#endif

struct up_opts
{
    const char         *serialize;
    const char         *label;
    int                 verbosity;
    unsigned int        plainfile     : 1;
    unsigned int        relaxed       : 1;
    unsigned int        sloppyio      : 1;
};

#endif /* HDR_UPART_UTIL */
