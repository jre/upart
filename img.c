#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "crc32.h"
#include "disk.h"
#include "img.h"
#include "util.h"

/* #define IMG_DEBUG */
//#define BIGMAJOR
//#define SMALLMAJOR
//#define HDRJUNK
//#define PREJUNK
//#define POSTJUNK

#ifdef HDRJUNK
#define HDRJUNKLEN              37
#endif
#ifdef PREJUNK
#define PREJUNKLEN              1125
#endif
#ifdef POSTJUNK
#define POSTJUNKLEN             5563
#endif

#define IMG_MAGIC               UINT64_C(0x5550415254eaf2e5)
#ifdef BIGMAJOR
#define IMG_MAJOR               0xffff
#elif defined(SMALLMAJOR)
#define IMG_MAJOR               0
#else
#define IMG_MAJOR               1
#endif
#ifdef HDRJUNK
#define IMG_MINOR               0xffff
#define IMG_HDR_LEN             (328 + HDRJUNKLEN)
#else
#define IMG_MINOR               0
#define IMG_HDR_LEN             328
#endif
#define IMG_SECT_LEN            16

struct up_imghdr_p
{
    uint64_t            magic;
    uint16_t            major;
    uint16_t            minor;
    uint32_t            hdrlen;
    uint32_t            hdrcrc;
    uint32_t            datastart;
    uint32_t            datasize;
    uint32_t            datacrc;
    uint32_t            sectsize;
    uint32_t            pad;
    uint64_t            size;
    uint64_t            cyls;
    uint64_t            heads;
    uint64_t            sects;
    char                label[256];
#ifdef HDRJUNK
    char                junk[HDRJUNKLEN];
#endif
} __attribute__((packed));

struct up_imgsect_p
{
    uint64_t            off;
    uint64_t            size;
} __attribute__((packed));

struct up_img
{
    struct up_imghdr_p  hdr;
    uint8_t            *data;
};

static uint32_t img_checkcrc(struct up_imghdr_p *hdr, int fd, const char *name);

static void
img_save_iter(const struct up_disk *disk, const struct up_disk_sectnode *node,
              void *arg)
{
    struct up_imgsect_p    *hdr;
    uint8_t               **data = (uint8_t**)arg;

    hdr = (struct up_imgsect_p *)(*data);
    *data += IMG_SECT_LEN;
#ifdef IMG_DEBUG
    fprintf(stderr, "saving %"PRId64" sectors at offset %"PRId64"\n",
            node->last - node->first + 1, node->first);
#endif
    hdr->off  = UP_HTOBE64(node->first);
    hdr->size = UP_HTOBE64(node->last - node->first + 1);
    memcpy(*data, node->data, (node->last - node->first + 1) * disk->upd_sectsize);
    *data += (node->last - node->first + 1) * disk->upd_sectsize;
}

int
up_img_save(const struct up_disk *disk, void *_stream,
            const char *label, const char *file)
{
    FILE                       *stream = _stream;
    struct up_imghdr_p          hdr;
    uint8_t                    *data, *ptr;
    size_t                      datalen;
#if defined(PREJUNK) || defined(POSTJUNK)
    uint8_t *junkbuf;
#endif

    assert(IMG_HDR_LEN == sizeof(struct up_imghdr_p));
    assert(IMG_SECT_LEN == sizeof(struct up_imgsect_p));

    /* allocate the data buffer */
    datalen = disk->upd_sectsused_count *
        (disk->upd_sectsize + sizeof(struct up_imgsect_p));
    if(0 == disk->upd_sectsused_count)
        data = NULL;
    else
    {
        data = up_malloc(disk->upd_sectsused_count,
                         disk->upd_sectsize + sizeof(struct up_imgsect_p));
        if(!data)
        {
            perror("malloc");
            return -1;
        }

        /* write sectors with sector headers into data buffer */
        ptr = data;
        up_disk_sectsiter(disk, img_save_iter, &ptr);
        /* datalen can be too big since we assume one header per
           sector when calculating it, but there may be groups of
           multiple sectors */
        assert(ptr - data <= datalen);
        datalen = ptr - data;
    }

    /* fill out header */
    memset(&hdr, 0, sizeof hdr);
    hdr.magic     = UP_HTOBE64(IMG_MAGIC);
    hdr.major     = UP_HTOBE16(IMG_MAJOR);
    hdr.minor     = UP_HTOBE16(IMG_MINOR);
    hdr.hdrlen    = UP_HTOBE32(IMG_HDR_LEN);
    hdr.hdrcrc    = 0;
#ifdef PREJUNK
    hdr.datastart = UP_HTOBE32(IMG_HDR_LEN + PREJUNKLEN);
#else
    hdr.datastart = UP_HTOBE32(IMG_HDR_LEN);
#endif
    hdr.datasize  = UP_HTOBE32(datalen);
    hdr.datacrc   = UP_HTOBE32(up_crc32(data, datalen, 0));
    hdr.sectsize  = UP_HTOBE32(disk->upd_sectsize);
    hdr.pad       = 0;
    hdr.size      = UP_HTOBE64(disk->upd_size);
    hdr.cyls      = UP_HTOBE64(disk->upd_cyls);
    hdr.heads     = UP_HTOBE64(disk->upd_heads);
    hdr.sects     = UP_HTOBE64(disk->upd_sects);
    strlcpy(hdr.label, label, sizeof hdr.label);
#ifdef HDRJUNK
    arc4random_buf(hdr.junk, sizeof hdr.junk);
#endif
    /* this must go last, for reasons which should be obvious */
    hdr.hdrcrc    = UP_HTOBE32(up_crc32(&hdr, IMG_HDR_LEN, 0));

    /* write the header */
    if(1 != fwrite(&hdr, IMG_HDR_LEN, 1, stream))
    {
        fprintf(stderr, "error writing to %s: %s\n",
                file, strerror(errno));
        free(data);
        return -1;
    }

#ifdef PREJUNK
    junkbuf = malloc(PREJUNKLEN);
    if(!junkbuf)
    {
        perror("malloc");
        return -1;
    }
    arc4random_buf(junkbuf, PREJUNKLEN);
    if(PREJUNKLEN != fwrite(junkbuf, 1, PREJUNKLEN, stream))
    {
        fprintf(stderr, "error writing to %s: %s\n",
                file, strerror(errno));
        free(junkbuf);
        free(data);
        return -1;
    }
    free(junkbuf);
#endif

    /* write the data buffer */
    if(0 < datalen)
    {
        if(datalen != fwrite(data, 1, datalen, stream))
        {
            fprintf(stderr, "error writing to %s: %s\n",
                    file, strerror(errno));
            free(data);
            return -1;
        }
    }

    free(data);

#ifdef POSTJUNK
    junkbuf = malloc(POSTJUNKLEN);
    if(!junkbuf)
    {
        perror("malloc");
        return -1;
    }
    arc4random_buf(junkbuf, POSTJUNKLEN);
    if(POSTJUNKLEN != fwrite(junkbuf, 1, POSTJUNKLEN, stream))
    {
        fprintf(stderr, "error writing to %s: %s\n",
                file, strerror(errno));
        free(junkbuf);
        return -1;
    }
    free(junkbuf);
#endif

    return 0;
}

int
up_img_load(int fd, const char *name, const struct up_opts *opts,
            struct up_img **ret)
{
    struct up_imghdr_p  hdr;
    void               *data;
    ssize_t             res;

    *ret = NULL;
    if(!opts->plainfile)
        return 0;

    assert(IMG_HDR_LEN == sizeof(struct up_imghdr_p));
    assert(IMG_SECT_LEN == sizeof(struct up_imgsect_p));

    /* try to read header and check magic */
    memset(&hdr, 0, sizeof hdr);
    res = pread(fd, &hdr, IMG_HDR_LEN, 0);
    if(0 > res)
    {
        fprintf(stderr, "failed to read from %s: %s\n",
                name, strerror(errno));
        return -1;
    }
    if(IMG_MAGIC != UP_BETOH64(hdr.magic))
        return 0;
    if(res != IMG_HDR_LEN)
    {
        fprintf(stderr, "truncated upart image file\n");
        return -1;
    }

#ifdef IMG_DEBUG
    fprintf(stderr, "upart image file %s:\n"
            "magic           %016"PRIx64"\n"
            "major           %u\n"
            "minor           %u\n"
            "header size     %u\n"
            "header crc      0x%08x\n"
            "data offset     %u\n"
            "data size       %u\n"
            "data crc        0x%08x\n"
            "sector size     %u\n"
            "sector count    %"PRId64"\n"
            "cyls            %"PRId64"\n"
            "heads           %"PRId64"\n"
            "sects           %"PRId64"\n"
            "label           %s\n"
            "\n",
            name,
            UP_BETOH64(hdr.magic),
            UP_BETOH16(hdr.major),
            UP_BETOH16(hdr.minor),
            UP_BETOH32(hdr.hdrlen),
            UP_BETOH32(hdr.hdrcrc),
            UP_BETOH32(hdr.datastart),
            UP_BETOH32(hdr.datasize),
            UP_BETOH32(hdr.datacrc),
            UP_BETOH32(hdr.sectsize),
            UP_BETOH64(hdr.size),
            UP_BETOH64(hdr.cyls),
            UP_BETOH64(hdr.heads),
            UP_BETOH64(hdr.sects),
            hdr.label);
#endif /* IMG_DEBUG */

    /* check version */
    if(IMG_MAJOR != UP_BETOH16(hdr.major))
    {
        fprintf(stderr, "error: upart image version %d.x is too new\n",
                UP_BETOH16(hdr.major));
        return -1;
    }
    if(IMG_MINOR < UP_BETOH16(hdr.minor))
        fprintf(stderr, "warning: treating version %d.%d upart image as "
                "%d.%d\n", UP_BETOH16(hdr.major), UP_BETOH16(hdr.minor),
                IMG_MAJOR, IMG_MINOR);

    /* validate header crc */
    if(UP_BETOH32(hdr.hdrlen) < IMG_HDR_LEN ||
       (UP_BETOH32(hdr.hdrlen) != IMG_HDR_LEN &&
        IMG_MINOR == UP_BETOH16(hdr.minor)))
    {
        fprintf(stderr, "corrupt upart image header: invalid version %d.%d "
                "header length: %d\n", UP_BETOH16(hdr.major),
                UP_BETOH16(hdr.minor), UP_BETOH32(hdr.hdrlen));
        return -1;
    }
    if(UP_BETOH32(hdr.hdrcrc) != img_checkcrc(&hdr, fd, name))
    {
        fprintf(stderr, "corrupt upart image header: "
                "header crc check failed\n");
        return -1;
    }

    /* allocate data buffer and read data */
    data = malloc(UP_BETOH32(hdr.datasize));
    if(!data)
    {
        perror("malloc");
        return -1;
    }
    res = pread(fd, data, UP_BETOH32(hdr.datasize), UP_BETOH32(hdr.datastart));
    if(UP_BETOH32(hdr.datasize) != res)
    {
        fprintf(stderr, "failed to read from %s: %s\n",
                name, (0 > res ? strerror(errno) : "short read count"));
        free(data);
        return -1;
    }

    /* validate the data crc */
    if(UP_BETOH32(hdr.datacrc) != up_crc32(data, UP_BETOH32(hdr.datasize), 0))
    {
        fprintf(stderr, "corrupt upart image: data crc check failed\n");
        free(data);
        return -1;
    }

    /* wrap everything up in a struct and return it */
    *ret = calloc(1, sizeof **ret);
    if(!*ret)
    {
        perror("malloc");
        free(data);
        return -1;
    }
    (*ret)->hdr  = hdr;
    (*ret)->data = data;

    return 1;
}

int
up_img_getparams(struct up_disk *disk, struct up_img *img)
{
    disk->upd_sectsize = UP_BETOH32(img->hdr.sectsize);
    disk->upd_size     = UP_BETOH64(img->hdr.size);
    disk->upd_cyls     = UP_BETOH64(img->hdr.cyls);
    disk->upd_heads    = UP_BETOH64(img->hdr.heads);
    disk->upd_sects    = UP_BETOH64(img->hdr.sects);
    
    return 0;
}

const char *
up_img_getlabel(struct up_img *img, size_t *len)
{
    if(memchr(img->hdr.label, 0, sizeof img->hdr.label))
        *len = strlen(img->hdr.label);
    else
        *len = sizeof img->hdr.label;

    return img->hdr.label;
}

int64_t
up_img_read(struct up_img *img, int64_t start, int64_t sects, void *buf)
{
    size_t              imgoff, sectsize;
    struct up_imgsect_p sect;

#ifdef IMG_DEBUG
    fprintf(stderr, "searching for %"PRId64" sectors at offset %"PRId64"\n",
            sects, start);
#endif

    imgoff = 0;
    sectsize = UP_BETOH32(img->hdr.sectsize);
    while(UP_BETOH32(img->hdr.datasize) - imgoff > IMG_SECT_LEN)
    {
        memcpy(&sect, img->data + imgoff, IMG_SECT_LEN);
        sect.off  = UP_BETOH64(sect.off);
        sect.size = UP_BETOH64(sect.size);
        imgoff += IMG_SECT_LEN;
#ifdef IMG_DEBUG
        fprintf(stderr, "found %"PRId64" sectors at offset %"PRId64"\n",
                sect.size, sect.off);
#endif
        if(UP_BETOH32(img->hdr.datasize) - imgoff < sect.size * sectsize)
        {
            fprintf(stderr, "error: truncated image file data\n");
            return -1;
        }
        if(start >= sect.off && start < sect.off + sect.size)
        {
            if(start > sect.off)
            {
                imgoff += (start - sect.off) * sectsize;
                sect.size -= start - sect.off;
                sect.off = start;
            }
            if(sects > sect.size)
            {
                memcpy(buf, img->data + imgoff, sect.size * sectsize);
                start += sect.size;
                sects -= sect.size;
                buf   += sect.size * sectsize;
                return sect.size + up_img_read(img, start, sects, buf);
            }
            else
            {
                memcpy(buf, img->data + imgoff, sects * sectsize);
                return sects;
            }
        }
        imgoff += sect.size * sectsize;
    }

#ifdef IMG_DEBUG
    fprintf(stderr, "warning: sector %"PRId64" not found in image file\n", start);
#endif
    memset(buf, 0, sects * sectsize);
    return sects;
}

void
up_img_free(struct up_img *img)
{
    if(img)
    {
        free(img->data);
        free(img);
    }
}

static uint32_t
img_checkcrc(struct up_imghdr_p *hdr, int fd, const char *name)
{
    uint32_t    old, crc;
    void       *extra;
    ssize_t     len, res;

    /* get the crc of the main header first */
    old         = hdr->hdrcrc;
    hdr->hdrcrc = 0;
    crc         = up_crc32(hdr, sizeof *hdr, 0);
    hdr->hdrcrc = old;

    /* if there's extra header data, read it in and get it's crc too */
    len = UP_BETOH32(hdr->hdrlen) - sizeof *hdr;
    if(0 < len)
    {
        extra = malloc(len);
        if(!extra)
        {
            perror("malloc");
            /* XXX real error value would be nice */
            return ~0;
        }
        res = pread(fd, extra, len, IMG_HDR_LEN);
        if(len != res)
        {
            fprintf(stderr, "failed to read from %s: %s\n",
                    name, (0 > res ? strerror(errno) : "short read count"));
            /* XXX real error value would be nice */
            return -1;
        }
        crc = up_crc32(extra, len, crc);
        free(extra);
    }

    return crc;
}
