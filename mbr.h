#ifndef HDR_UPART_MBR
#define HDR_UPART_MBR

struct up_disk;
struct up_map;
struct up_opts;

/* Test for an MBR. Return -1 for error, 0 for no MBR, 1 for MBR found */
int up_mbr_test(struct up_disk *disk, int64_t start, int64_t size);

/* Load MBR and return private data, or NULL on error. */
struct up_map *up_mbr_load(struct up_disk *disk, int64_t start, int64_t size);

/* up_mbr_test and up_mbr_load combined */
/* XXX this return value and pointer pointer sucks, some errno-like
   error codes would be nice */
int up_mbr_testload(struct up_disk *disk, int64_t start, int64_t size,
                    struct up_map **mbr);

/* dump private data to stream */
void up_mbr_dump(const struct up_disk *disk, const struct up_map *mbr,
                 void *stream, const struct up_opts *opts);

/* return the name for a partition ID */
const char *up_mbr_name(uint8_t type);

#endif /* HDR_UPART_MBR */
