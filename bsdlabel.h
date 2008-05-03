#ifndef HDR_UPART_BSDLABEL
#define HDR_UPART_BSDLABEL

struct up_disk;
struct up_opts;

/* register BSD disklabel partition map type */
void up_bsdlabel_register(void);

#define UP_BSDLABEL_FSTYPE_UNUSED       (0)
#define UP_BSDLABEL_FSTYPE_42BSD        (7)

const char *up_bsdlabel_fstype(int type);

#endif /* HDR_UPART_BSDLABEL */
