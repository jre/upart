#ifndef HDR_UPART_MBR
#define HDR_UPART_MBR

struct up_disk;
struct up_opts;

/* Test for an MBR. Return -1 for error, 0 for no MBR, 1 for MBR found */
int up_mbr_test(struct up_disk *disk, int64_t start, int64_t size);

/* Load MBR and return private data, or NULL on error. */
void *up_mbr_load(struct up_disk *disk, int64_t start, int64_t size);

/* up_mbr_test and up_mbr_load combined */
/* XXX this return value and pointer pointer sucks, some errno-like
   error codes would be nice */
int up_mbr_testload(struct up_disk *disk, int64_t start, int64_t size,
                    void **mbr);

/* free private data */
void up_mbr_free(void *mbr);

/* dump private data to stream */
void up_mbr_dump(const struct up_disk *disk, const void *mbr, void *stream,
                 const struct up_opts *opts);

/* return the name for a partition ID */
const char *up_mbr_name(uint8_t type);

int up_mbr_iter(struct up_disk *disk, const void *mbr,
                int (*func)(struct up_disk *, int64_t, int64_t, const char *, void *),
                void *arg);

#endif /* HDR_UPART_MBR */
