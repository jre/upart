#ifndef HDR_UPART_IMG
#define HDR_UPART_IMG

struct up_disk;
struct up_img;
struct disk_params;

/* serialize disk metainfo and partition sectors to a file */
int		 up_img_save(const struct up_disk *, void *, const char *,
    const char *);

int		 up_img_load(int, const char *, struct up_img **);
int		 up_img_getparams(struct up_img *, struct disk_params *);
const char	*up_img_getlabel(struct up_img *, size_t *);
int64_t		 up_img_read(struct up_img *, int64_t, int64_t, void *);
void		 up_img_free(struct up_img *);

#endif
