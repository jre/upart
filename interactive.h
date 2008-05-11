#ifndef HDR_UPART_INTERACTIVE
#define HDR_UPART_INTERACTIVE

struct up_disk;
struct up_opts;

int interactive_image(struct up_disk *src, const char *dest,
                      const struct up_opts *opts);
int interactive_restore(struct up_disk *src, struct up_disk *dest,
                        const struct up_opts *opts);

#endif /* HDR_UPART_INTERACTIVE */
