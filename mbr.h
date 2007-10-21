#ifndef HDR_UPART_MBR
#define HDR_UPART_MBR

struct up_disk;

/* Test for an MBR. Return -1 for error, 0 for no MBR, 1 for MBR found */
int up_mbr_test(const struct up_disk *disk, int fd, off_t start, off_t end);

/* Load MBR and return private data, or NULL on error. */
void *up_mbr_load(const struct up_disk *disk, int fd, off_t start, off_t end);

/* up_mbr_test and up_mbr_load combined */
/* XXX this return value and pointer pointer sucks, some errno-like
   error codes would be nice */
int up_mbr_testload(const struct up_disk *disk, int fd, off_t start, off_t end,
                    void **mbr);

/* free private data */
void up_mbr_free(void *mbr);

/* dump private data to stream */
void up_mbr_dump(void *mbr, void *stream);

#endif /* HDR_UPART_MBR */
