#ifndef HDR_UPART_IMG
#define HDR_UPART_IMG

struct disk;
struct img;
struct disk_params;

/* serialize disk metainfo and partition sectors to a file */
int		 up_img_save(const struct disk *, FILE *, const char *,
    const char *);

int		 up_img_load(FILE *, const char *, struct img **);
void		 up_img_getparams(struct img *, struct disk_params *);
const char	*up_img_getlabel(struct img *, size_t *);
int64_t		 up_img_read(struct img *, int64_t, int64_t, void *);
void		 up_img_free(struct img *);

#endif
