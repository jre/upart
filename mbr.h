#ifndef HDR_UPART_MBR
#define HDR_UPART_MBR

struct up_disk;
struct up_map;
struct up_opts;

/* register MBR partition map type */
void up_mbr_register(void);

/* return the name for a partition ID */
const char *up_mbr_name(uint8_t type);

#endif /* HDR_UPART_MBR */
