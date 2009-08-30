#ifndef HDR_UPART_BSDLABEL
#define HDR_UPART_BSDLABEL

struct up_disk;
struct up_part;

/* register BSD disklabel partition map type */
void up_bsdlabel_register(void);

#define UP_BSDLABEL_FSTYPE_UNUSED       (0)
#define UP_BSDLABEL_FSTYPE_42BSD        (7)

#define OBSDLABEL_BF_BSIZE(bf)	((bf) ? 1 << (((bf) >> 3) + 12) : 0)
#define OBSDLABEL_BF_FRAG(bf)	((bf) ? 1 << (((bf) & 7) - 1) : 0)

#define UP_BSDLABEL_FMT_HDR() \
    (UP_NOISY(EXTRA) ? "Type    fsize bsize   cpg" : \
     (UP_NOISY(NORMAL) ? "Type" :  NULL))

const char *up_bsdlabel_fstype(int type);
int up_bsdlabel_fmt(const struct up_part *part, char *buf, int size,
                    int type, uint32_t fsize, int frags, int cpg);

#endif /* HDR_UPART_BSDLABEL */
