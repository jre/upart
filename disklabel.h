#ifndef HDR_UPART_DISKLABEL
#define HDR_UPART_DISKLABEL

struct up_disk;
struct up_opts;

/* Test for a BSD disklabel. Return -1 for error, 0 for no label, 1 if found */
int up_disklabel_test(struct up_disk *disk, int64_t start, int64_t size);

/* Load a BSD disklabel and return private data, or NULL on error. */
void *up_disklabel_load(struct up_disk *disk, int64_t start, int64_t size);

/* up_disklabel_test and up_disklabel_load combined */
/* XXX this return value and pointer pointer sucks, some errno-like
   error codes would be nice */
int up_disklabel_testload(struct up_disk *disk, int64_t start, int64_t size,
                          void **label);

/* free private data */
void up_disklabel_free(void *label);

/* dump private data to stream */
void up_disklabel_dump(const struct up_disk *disk, const void *label,
                       void *stream, const struct up_opts *opt);

#endif /* HDR_UPART_DISKLABEL */
