#ifndef HDR_UPART_IMG
#define HDR_UPART_IMG

struct up_disk;
struct up_img;
struct up_opts;
struct disk_params;

/* serialize disk metainfo and partition sectors to a file */
int up_img_save(const struct up_disk *disk, void *_stream, const char *label,
                const char *file, const struct up_opts *opts);

int up_img_load(int fd, const char *name, const struct up_opts *opts,
                struct up_img **ret);
int		 up_img_getparams(struct up_img *, struct disk_params *);
const char * up_img_getlabel(struct up_img *img, size_t *len);
int64_t up_img_read(struct up_img *img, int64_t start, int64_t sects, void *buf,
                    int verbose);
void up_img_free(struct up_img *img);

#endif
