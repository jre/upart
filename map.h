#ifndef HDR_UPART_MAP
#define HDR_UPART_MAP

#include "bsdqueue.h"

struct up_map;
struct up_part;
typedef void (*up_freepriv_map)(struct up_map *);
typedef void (*up_freepriv_part)(struct up_map *, struct up_part *);

#define UP_PART_EMPTY           (1<<0) /* empty or deleted */
#define UP_PART_OOB             (1<<1) /* out of bounds */

#define UP_PART_IS_BAD(flags)   ((UP_PART_EMPTY|UP_PART_OOB) & (flags))

SIMPLEQ_HEAD(up_part_list, up_part);

struct up_part
{
    int64_t             start;
    int64_t             size;
    int                 type;
    const char *        label;
    int                 flags;
    void *              priv;
    up_freepriv_part    freepriv;
    struct up_part_list children;
    SIMPLEQ_ENTRY(up_part) link;
};

enum up_map_type
{
    UP_MAP_NONE = 0,
    UP_MAP_MBR,
    UP_MAP_BSD,
};

struct up_map
{
    int64_t             start;
    int64_t             size;
    enum up_map_type    type;
    void *              priv;
    up_freepriv_map     freepriv;
    struct up_part_list list;
};

struct up_map *up_map_new(int64_t start, int64_t size, enum up_map_type type,
                          void *priv, up_freepriv_map freepriv);
struct up_part *up_map_add(struct up_map *map, struct up_part *parent,
                           int64_t start, int64_t size, int type,
                           const char *label, int flags, void *priv,
                           up_freepriv_part freepriv);
void up_map_free(struct up_map *map);
void up_map_print(struct up_map *map);
struct up_part *up_map_iter(struct up_map *map, struct up_part *prev);
void up_map_freeprivmap_def(struct up_map *map);
void up_map_freeprivpart_def(struct up_map *map, struct up_part *part);

#endif /* HDR_UPART_MAP */
