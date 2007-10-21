#ifndef HDR_UPART_UTIL
#define HDR_UPART_UTIL

/* read a 16, 32, or 64 bit little-endian integer from a buffer */

#define UP_GETBUF16LE(buf) \
    ((uint16_t)(((const uint8_t *)(buf))[1] <<  8) | \
     (uint16_t)(((const uint8_t *)(buf))[0]))

#define UP_GETBUF32LE(buf) \
    ((uint32_t)(((const uint8_t *)(buf))[3] << 24) | \
     (uint32_t)(((const uint8_t *)(buf))[2] << 16) | \
     (uint32_t)(((const uint8_t *)(buf))[1] <<  8) | \
     (uint32_t)(((const uint8_t *)(buf))[0]))

#define UP_GETBUF64LE(buf) \
    ((uint64_t)(((const uint8_t *)(buf))[7] << 56) | \
     (uint64_t)(((const uint8_t *)(buf))[6] << 48) | \
     (uint64_t)(((const uint8_t *)(buf))[5] << 40) | \
     (uint64_t)(((const uint8_t *)(buf))[4] << 32) | \
     (uint64_t)(((const uint8_t *)(buf))[3] << 24) | \
     (uint64_t)(((const uint8_t *)(buf))[2] << 16) | \
     (uint64_t)(((const uint8_t *)(buf))[1] <<  8) | \
     (uint64_t)(((const uint8_t *)(buf))[0]))

/* read a 16, 32, or 64 bit big-endian integer from a buffer */

#define UP_GETBUF16BE(buf) \
    ((uint16_t)(((const uint8_t *)(buf))[0] <<  8) | \
     (uint16_t)(((const uint8_t *)(buf))[1]))

#define UP_GETBUF32BE(buf) \
    ((uint32_t)(((const uint8_t *)(buf))[0] << 24) | \
     (uint32_t)(((const uint8_t *)(buf))[1] << 16) | \
     (uint32_t)(((const uint8_t *)(buf))[2] <<  8) | \
     (uint32_t)(((const uint8_t *)(buf))[3]))

#define UP_GETBUF64BE(buf) \
    ((uint64_t)(((const uint8_t *)(buf))[0] << 56) | \
     (uint64_t)(((const uint8_t *)(buf))[1] << 48) | \
     (uint64_t)(((const uint8_t *)(buf))[2] << 40) | \
     (uint64_t)(((const uint8_t *)(buf))[3] << 32) | \
     (uint64_t)(((const uint8_t *)(buf))[4] << 24) | \
     (uint64_t)(((const uint8_t *)(buf))[5] << 16) | \
     (uint64_t)(((const uint8_t *)(buf))[6] <<  8) | \
     (uint64_t)(((const uint8_t *)(buf))[7]))

/*
  Dump SIZE bytes of data in BUF to STREAM to stdout, in
    a format similar to /usr/bin/hexdump -C
  If STREAM is NULL then stderr will be used.
  DISPOFF is added to offsets when displayed.
*/
void up_hexdump(const void *buf, size_t size, size_t dispoff, void *stream);

#endif /* HDR_UPART_UTIL */
