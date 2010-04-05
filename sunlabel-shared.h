#ifndef HDR_UPART_SUNLABEL_SHARED
#define HDR_UPART_SUNLABEL_SHARED

#define UP_SUNLABEL_FMT_HDR     "Flags Type"

const char *up_sunlabel_parttype(unsigned int type);
int	up_sunlabel_fmt(FILE *, unsigned int, unsigned int);

#endif /* HDR_UPART_SUNLABEL_SHARED */
