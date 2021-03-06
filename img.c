/* 
 * Copyright (c) 2008-2011 Joshua R. Elsasser.
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "disk.h"
#include "img.h"
#include "os.h"
#include "util.h"

/* #define IMG_DEBUG */

#define IMG_MAGIC		(UINT64_C(0x5550415254eaf2e5))
#define IMG_MAJOR		(1)
#define IMG_MINOR		(0)
#define IMG_HDR_LEN		(328)
#define IMG_SECT_LEN		(16)

struct imghdr {
	uint64_t magic;
	uint16_t major;
	uint16_t minor;
	uint32_t hdrlen;
	uint32_t hdrcrc;
	uint32_t datastart;
	uint32_t datasize;
	uint32_t datacrc;
	uint32_t sectsize;
	uint32_t pad;
	uint64_t size;
	uint64_t cyls;
	uint64_t heads;
	uint64_t sects;
	char label[256];
};

struct imgsect {
	uint64_t off;
	uint64_t size;
};

struct img {
	struct imghdr hdr;
	uint8_t *data;
};

static int	img_read(FILE *, const char *, void *, size_t, int64_t);
static int	img_checkcrc(struct imghdr *, FILE *, const char *, uint32_t *);

static int
img_save_iter(const struct disk *disk, const struct disk_sect *node,
    void *arg)
{
	struct imgsect hdr;
	uint8_t **data;

#ifdef IMG_DEBUG
	fprintf(stderr, "saving %"PRId64" sectors at offset %"PRId64"\n",
	    UP_SECT_COUNT(node), UP_SECT_OFF(node));
#endif

	data = (uint8_t **)arg;
	memcpy(&hdr, *data, sizeof(hdr));
	hdr.off = UP_HTOBE64(UP_SECT_OFF(node));
	hdr.size = UP_HTOBE64(UP_SECT_COUNT(node));
	memcpy(*data, &hdr, sizeof(hdr));
	*data += IMG_SECT_LEN;
	memcpy(*data, UP_SECT_DATA(node),
	    UP_SECT_COUNT(node) * UP_DISK_1SECT(disk));
	*data += UP_SECT_COUNT(node) * UP_DISK_1SECT(disk);

	return (1);
}

int
up_img_save(const struct disk *disk, FILE *stream, const char *label,
    const char *file)
{
	struct imghdr hdr;
	uint8_t *data, *ptr;
	size_t datalen;

	assert(sizeof(struct imghdr) == IMG_HDR_LEN);
	assert(sizeof(struct imgsect) == IMG_SECT_LEN);

	/* label defaults to the device description */
	if (label == NULL)
		label = UP_DISK_DESC(disk);

	/* allocate the data buffer */
	datalen = disk->sectsused_count *
	    (UP_DISK_1SECT(disk) + IMG_SECT_LEN);
	if (disk->sectsused_count == 0)
		data = NULL;
	else {
		if ((data = xalloc(disk->sectsused_count,
			    UP_DISK_1SECT(disk) + IMG_SECT_LEN, 0)) == NULL)
			return (-1);

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
	memset(&hdr, 0, sizeof(hdr));
	hdr.magic = UP_HTOBE64(IMG_MAGIC);
	hdr.major = UP_HTOBE16(IMG_MAJOR);
	hdr.minor = UP_HTOBE16(IMG_MINOR);
	hdr.hdrlen = UP_HTOBE32(IMG_HDR_LEN);
	hdr.hdrcrc = 0;
	hdr.datastart = UP_HTOBE32(IMG_HDR_LEN);
	hdr.datasize = UP_HTOBE32(datalen);
	hdr.datacrc = UP_HTOBE32(up_crc32(data, datalen, 0));
	hdr.sectsize = UP_HTOBE32(UP_DISK_1SECT(disk));
	hdr.pad = 0;
	hdr.size = UP_HTOBE64(UP_DISK_SIZESECTS(disk));
	hdr.cyls = UP_HTOBE64(UP_DISK_CYLS(disk));
	hdr.heads = UP_HTOBE64(UP_DISK_HEADS(disk));
	hdr.sects = UP_HTOBE64(UP_DISK_SPT(disk));
	strlcpy(hdr.label, label, sizeof(hdr.label));
	/* this must go last, for reasons which should be obvious */
	hdr.hdrcrc = UP_HTOBE32(up_crc32(&hdr, IMG_HDR_LEN, 0));

	/* write the header */
	if (fwrite(&hdr, IMG_HDR_LEN, 1, stream) != 1) {
		if (UP_NOISY(QUIET))
			up_err("error writing to %s: %s",
			    file, os_lasterrstr());
		free(data);
		return (-1);
	}

	/* write the data buffer */
	if (datalen > 0) {
		if (fwrite(data, 1, datalen, stream) != datalen ) {
			if (UP_NOISY(QUIET))
				up_err("error writing to %s: %s",
				    file, os_lasterrstr());
			free(data);
			return (-1);
		}
	}

	free(data);
	return (0);
}

int
up_img_load(FILE *stream, const char *name, struct img **ret)
{
	struct imghdr hdr;
	void *data;
	uint32_t crc;

	assert(sizeof(struct imghdr) == IMG_HDR_LEN);
	assert(sizeof(struct imgsect) == IMG_SECT_LEN);
	*ret = NULL;

	/* try to read header and check magic */
	memset(&hdr, 0, sizeof hdr);
	if (img_read(stream, name, &hdr, IMG_HDR_LEN, 0) != 0)
		return (-1);
	if (UP_BETOH64(hdr.magic) != IMG_MAGIC)
		return (0);

	/* make sure label string is nul-terminated */
	hdr.label[sizeof(hdr.label)-1] = '\0';

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
	if (UP_BETOH16(hdr.major) != IMG_MAJOR) {
		if (UP_NOISY(QUIET))
			up_err("upart image version %d.x is too %s",
			    UP_BETOH16(hdr.major),
			    (UP_BETOH16(hdr.major) > IMG_MAJOR ?
			"new" : "old"));
		return (-1);
	}
	if(UP_BETOH16(hdr.minor) > IMG_MINOR && UP_NOISY(QUIET))
		up_warn("treating version %d.%d upart image as %d.%d",
		    UP_BETOH16(hdr.major), UP_BETOH16(hdr.minor),
		    IMG_MAJOR, IMG_MINOR);

	/* validate header crc */
	if (UP_BETOH32(hdr.hdrlen) < IMG_HDR_LEN ||
	    (UP_BETOH32(hdr.hdrlen) != IMG_HDR_LEN &&
	     UP_BETOH16(hdr.minor) == IMG_MINOR)) {
		 if (UP_NOISY(QUIET))
		 	up_err("corrupt upart image header: invalid version "
			    "%d.%d header length: %d",
			    UP_BETOH16(hdr.major), UP_BETOH16(hdr.minor),
			    UP_BETOH32(hdr.hdrlen));
		return (-1);
	}
	if (img_checkcrc(&hdr, stream, name, &crc) < 0)
		return (-1);
	if (UP_BETOH32(hdr.hdrcrc) != crc) {
		if (UP_NOISY(QUIET))
			up_err("corrupt upart image header: "
			    "header crc check failed");
		return (-1);
	}

	/* allocate data buffer and read data */
	if ((data = xalloc(1, UP_BETOH32(hdr.datasize), 0)) == NULL)
		return (-1);
	if (img_read(stream, name, data, UP_BETOH32(hdr.datasize),
		UP_BETOH32(hdr.datastart)) != 0) {
		free(data);
		return (-1);
	}

	/* validate the data crc */
	if (up_crc32(data, UP_BETOH32(hdr.datasize), 0) !=
	    UP_BETOH32(hdr.datacrc)) {
		if (UP_NOISY(QUIET))
			up_err("corrupt upart image: data crc check failed");
		free(data);
		return (-1);
	}

	/* wrap everything up in a struct and return it */
	if ((*ret = xalloc(1, sizeof(**ret), XA_ZERO)) == NULL) {
		free(data);
		return (-1);
	}
	(*ret)->hdr = hdr;
	(*ret)->data = data;

	return (1);
}

void
up_img_getparams(struct img *img, struct disk_params *params)
{
	params->sectsize = UP_BETOH32(img->hdr.sectsize);
	params->size = UP_BETOH64(img->hdr.size);
	params->cyls = UP_BETOH64(img->hdr.cyls);
	params->heads = UP_BETOH64(img->hdr.heads);
	params->sects = UP_BETOH64(img->hdr.sects);
}

const char *
up_img_getlabel(struct img *img)
{
	assert(memchr(img->hdr.label, 0, sizeof(img->hdr.label)));
	return (img->hdr.label);
}

int64_t
up_img_read(struct img *img, int64_t start, int64_t sects, void *_buf)
{
	uint8_t *buf;
	size_t imgoff, sectsize;
	struct imgsect sect;

#ifdef IMG_DEBUG
	fprintf(stderr, "searching for %"PRId64" sectors at offset %"
	    PRId64"\n", sects, start);
#endif

	buf = _buf;
	imgoff = 0;
	sectsize = UP_BETOH32(img->hdr.sectsize);
	while (UP_BETOH32(img->hdr.datasize) - imgoff > IMG_SECT_LEN) {
		memcpy(&sect, img->data + imgoff, IMG_SECT_LEN);
		sect.off = UP_BETOH64(sect.off);
		sect.size = UP_BETOH64(sect.size);
		imgoff += IMG_SECT_LEN;
#ifdef IMG_DEBUG
		fprintf(stderr, "found %"PRId64" sectors at offset %"
		    PRId64"\n", sect.size, sect.off);
#endif
		/* XXX should sanity-check data when image is first read */
		if (UP_BETOH32(img->hdr.datasize) - imgoff <
		    sect.size * sectsize) {
			if (UP_NOISY(QUIET))
				up_err("truncated image file data");
			return (-1);
		}
		if (start >= sect.off && start < sect.off + sect.size) {
			if (start > sect.off) {
				imgoff += (start - sect.off) * sectsize;
				sect.size -= start - sect.off;
				sect.off = start;
			}
			if (sects > sect.size) {
				memcpy(buf, img->data + imgoff,
				    sect.size * sectsize);
				start += sect.size;
				sects -= sect.size;
				buf += sect.size * sectsize;
				return (up_img_read(img, start, sects, buf) +
				    sect.size);
			} else {
				memcpy(buf, img->data + imgoff,
				    sects * sectsize);
				return (sects);
			}
		}
		imgoff += sect.size * sectsize;
	}

#ifdef IMG_DEBUG
	fprintf(stderr, "warning: sector %"PRId64" not found in image file\n",
	    start);
#endif
	memset(buf, 0, sects * sectsize);
	return sects;
}

void
up_img_free(struct img *img)
{
	if(img != NULL) {
		free(img->data);
		free(img);
	}
}

static int
img_read(FILE *stream, const char *name, void *buf, size_t size, int64_t off)
{
	if (fseeko(stream, off, SEEK_SET) != 0) {
		if (UP_NOISY(QUIET))
			up_err("failed to seek image file %s: %s",
			    name, os_lasterrstr());
		return (-1);
	}

	if (fread(buf, 1, size, stream) != size) {
		if (UP_NOISY(QUIET))
			up_err("failed to read from image file %s: %s", name,
			    (ferror(stream) ? os_lasterrstr() :
				"unexpected end of file"));
		return (-1);
	}

	return (0);
}

static int
img_checkcrc(struct imghdr *hdr, FILE *stream, const char *name, uint32_t *ret)
{
	uint32_t old, crc;
	void *extra;
	ssize_t len;

	/* get the crc of the main header first */
	old = hdr->hdrcrc;
	hdr->hdrcrc = 0;
	crc = up_crc32(hdr, sizeof *hdr, 0);
	hdr->hdrcrc = old;

	/* if there's extra header data, read it in and get it's crc too */
	len = UP_BETOH32(hdr->hdrlen) - sizeof *hdr;
	if (len > 0) {
		if ((extra = xalloc(len, 1, 0)) == NULL)
			return (-1);
		if (img_read(stream, name, extra, len, IMG_HDR_LEN) != 0) {
			free(extra);
			return (-1);
		}
		crc = up_crc32(extra, len, crc);
		free(extra);
	}

	*ret = crc;	
	return (0);
}
