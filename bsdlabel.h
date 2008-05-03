#ifndef HDR_UPART_BSDLABEL
#define HDR_UPART_BSDLABEL

struct up_disk;
struct up_opts;
struct up_part;

/* register BSD disklabel partition map type */
void up_bsdlabel_register(void);

#define UP_BSDLABEL_FSTYPE_UNUSED       (0)
#define UP_BSDLABEL_FSTYPE_42BSD        (7)

#define UP_BSDLABEL_FMT_HDR(verbose) \
    (UP_NOISY((verbose), EXTRA) ? "Type    fsize bsize   cpg" : \
     (UP_NOISY((verbose), NORMAL) ? "Type" :  NULL))

const char *up_bsdlabel_fstype(int type);
int up_bsdlabel_fmt(const struct up_part *part, int verbose, char *buf, int size,
                    int type, uint32_t fsize, int frags, int cpg, int v1);

#endif /* HDR_UPART_BSDLABEL */
