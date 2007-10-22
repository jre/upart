#ifndef HDR_UPART_UTIL
#define HDR_UPART_UTIL

/* read a 16, 32, or 64 bit little-endian unsigned integer from a buffer */

#define UP_GETBUF16LE(buf) \
    (((uint16_t)(((const uint8_t *)(buf))[1]) <<  8) | \
     ((uint16_t)(((const uint8_t *)(buf))[0])))

#define UP_GETBUF32LE(buf) \
    (((uint32_t)(((const uint8_t *)(buf))[3]) << 24) | \
     ((uint32_t)(((const uint8_t *)(buf))[2]) << 16) | \
     ((uint32_t)(((const uint8_t *)(buf))[1]) <<  8) | \
     ((uint32_t)(((const uint8_t *)(buf))[0])))

#define UP_GETBUF64LE(buf) \
    (((uint64_t)(((const uint8_t *)(buf))[7]) << 56) | \
     ((uint64_t)(((const uint8_t *)(buf))[6]) << 48) | \
     ((uint64_t)(((const uint8_t *)(buf))[5]) << 40) | \
     ((uint64_t)(((const uint8_t *)(buf))[4]) << 32) | \
     ((uint64_t)(((const uint8_t *)(buf))[3]) << 24) | \
     ((uint64_t)(((const uint8_t *)(buf))[2]) << 16) | \
     ((uint64_t)(((const uint8_t *)(buf))[1]) <<  8) | \
     ((uint64_t)(((const uint8_t *)(buf))[0])))

/* read a 16, 32, or 64 bit big-endian unsigned integer from a buffer */

#define UP_GETBUF16BE(buf) \
    (((uint16_t)(((const uint8_t *)(buf))[0]) <<  8) | \
     ((uint16_t)(((const uint8_t *)(buf))[1])))

#define UP_GETBUF32BE(buf) \
    (((uint32_t)(((const uint8_t *)(buf))[0]) << 24) | \
     ((uint32_t)(((const uint8_t *)(buf))[1]) << 16) | \
     ((uint32_t)(((const uint8_t *)(buf))[2]) <<  8) | \
     ((uint32_t)(((const uint8_t *)(buf))[3])))

#define UP_GETBUF64BE(buf) \
    (((uint64_t)(((const uint8_t *)(buf))[0]) << 56) | \
     ((uint64_t)(((const uint8_t *)(buf))[1]) << 48) | \
     ((uint64_t)(((const uint8_t *)(buf))[2]) << 40) | \
     ((uint64_t)(((const uint8_t *)(buf))[3]) << 32) | \
     ((uint64_t)(((const uint8_t *)(buf))[4]) << 24) | \
     ((uint64_t)(((const uint8_t *)(buf))[5]) << 16) | \
     ((uint64_t)(((const uint8_t *)(buf))[6]) << 8) | \
     ((uint64_t)(((const uint8_t *)(buf))[7])))

/* write a 16, 32, or 64 bit little-endian unsigned integer to a buffer */

#define UP_SETBUF16LE(buf, val) \
    do { \
        uint8_t *up_setbuf16le_buf = (buf); \
        up_setbuf16le_buf[1] = ((uint16_t)(val) >>  8) & 0xff; \
        up_setbuf16le_buf[0] =  (uint16_t)(val)        & 0xff; \
    } while(0)

#define UP_SETBUF32LE(buf, val) \
    do { \
        uint8_t *up_setbuf32le_buf = (buf); \
        up_setbuf32le_buf[3] = ((uint32_t)(val) >> 24) & 0xff; \
        up_setbuf32le_buf[2] = ((uint32_t)(val) >> 16) & 0xff; \
        up_setbuf32le_buf[1] = ((uint32_t)(val) >>  8) & 0xff; \
        up_setbuf32le_buf[0] =  (uint32_t)(val)        & 0xff; \
    } while(0)

#define UP_SETBUF64LE(buf, val) \
    do { \
        uint8_t *up_setbuf64le_buf = (buf); \
        up_setbuf64le_buf[7] = ((uint64_t)(val) >> 56) & 0xff; \
        up_setbuf64le_buf[6] = ((uint64_t)(val) >> 48) & 0xff; \
        up_setbuf64le_buf[5] = ((uint64_t)(val) >> 40) & 0xff; \
        up_setbuf64le_buf[4] = ((uint64_t)(val) >> 32) & 0xff; \
        up_setbuf64le_buf[3] = ((uint64_t)(val) >> 24) & 0xff; \
        up_setbuf64le_buf[2] = ((uint64_t)(val) >> 16) & 0xff; \
        up_setbuf64le_buf[1] = ((uint64_t)(val) >>  8) & 0xff; \
        up_setbuf64le_buf[0] =  (uint64_t)(val)        & 0xff; \
    } while(0)

/* write a 16, 32, or 64 bit big-endian unsigned integer to a buffer */

#define UP_SETBUF16BE(buf, val) \
    do { \
        uint8_t *up_setbuf16be_buf = (buf); \
        up_setbuf16be_buf[0] = ((uint16_t)(val) >>  8) & 0xff; \
        up_setbuf16be_buf[1] =  (uint16_t)(val)        & 0xff; \
    } while(0)

#define UP_SETBUF32BE(buf, val) \
    do { \
        uint8_t *up_setbuf32be_buf = (buf); \
        up_setbuf32be_buf[0] = ((uint32_t)(val) >> 24) & 0xff; \
        up_setbuf32be_buf[1] = ((uint32_t)(val) >> 16) & 0xff; \
        up_setbuf32be_buf[2] = ((uint32_t)(val) >>  8) & 0xff; \
        up_setbuf32be_buf[3] =  (uint32_t)(val)        & 0xff; \
    } while(0)

#define UP_SETBUF64BE(buf, val) \
    do { \
        uint8_t *up_setbuf64be_buf = (buf); \
        up_setbuf64be_buf[0] = ((uint64_t)(val) >> 56) & 0xff; \
        up_setbuf64be_buf[1] = ((uint64_t)(val) >> 48) & 0xff; \
        up_setbuf64be_buf[2] = ((uint64_t)(val) >> 40) & 0xff; \
        up_setbuf64be_buf[3] = ((uint64_t)(val) >> 32) & 0xff; \
        up_setbuf64be_buf[4] = ((uint64_t)(val) >> 24) & 0xff; \
        up_setbuf64be_buf[5] = ((uint64_t)(val) >> 16) & 0xff; \
        up_setbuf64be_buf[6] = ((uint64_t)(val) >>  8) & 0xff; \
        up_setbuf64be_buf[7] =  (uint64_t)(val)        & 0xff; \
    } while(0)

/*
  Dump SIZE bytes of data in BUF to STREAM to stdout, in
    a format similar to /usr/bin/hexdump -C
  If STREAM is NULL then stderr will be used.
  DISPOFF is added to offsets when displayed.
*/
void up_hexdump(const void *buf, size_t size, size_t dispoff, void *stream);

/* Format NUM as a human readable size. UNITS should point to a const
   char * to store the unit label in. Use the UP_BESTDECIMAL() macro
   on the return value to get the best decimal precision when printing. */
float up_fmtsize(int64_t num, const char **units);
#define UP_BESTDECIMAL(d)       (10.0 > (d) ? 2 : (100.0 > (d) ? 1 : 0))

struct up_opts
{
    unsigned int        upo_verbose  : 1;
};

#endif /* HDR_UPART_UTIL */
