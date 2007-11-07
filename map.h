#ifndef HDR_UPART_MAP
#define HDR_UPART_MAP

#include "bsdqueue.h"

struct up_map;
struct up_part;

#define UP_PART_EMPTY           (1<<0) /* empty or deleted */
#define UP_PART_OOB             (1<<1) /* out of bounds */

#define UP_PART_IS_BAD(flags)   ((UP_PART_EMPTY|UP_PART_OOB) & (flags))

SIMPLEQ_HEAD(up_map_list, up_map);
SIMPLEQ_HEAD(up_part_list, up_part);

struct up_part
{
    int64_t             start;
    int64_t             size;
    int                 flags;
    int                 depth;
    void *              priv;
    struct up_map      *map;
    struct up_part     *parent;
    struct up_part_list subpart;
    struct up_map_list  submap;
    SIMPLEQ_ENTRY(up_part) link;
};

enum up_map_type
{
    UP_MAP_NONE = 0,
    UP_MAP_MBR,
    //UP_MAP_BSD,
    UP_MAP_TYPE_COUNT
};

struct up_map
{
    struct up_disk     *disk;
    enum up_map_type    type;
    int64_t             start;
    int64_t             size;
    int                 depth;
    void *              priv;
    struct up_part     *parent;
    struct up_part_list list;
    SIMPLEQ_ENTRY(up_map) link;
};

void up_map_register(enum up_map_type type, const char *typestr,
                     /* check if map exists and allocate private data */
                     int (*load)(struct up_disk *, int64_t, int64_t, void **),
                     /* add partitions, misc setup not done in load */
                     int (*setup)(struct up_map *),
                     /* copy map header line into string */
                     int (*getinfo)(const struct up_map *, char *, int),
                     /* copy part index into string */
                     int (*getindex)(const struct up_part *, char *, int),
                     /* copy extra verbose info into string */
                     int (*getextra)(const struct up_part *, int, char *, int),
                     /* print hex dump of raw partition data to stream */
                     void (*dump)(const struct up_map *, void *),
                     /* free map private data, map may be NULL */
                     void (*freeprivmap)(struct up_map *, void *),
                     /* free part private data, part may be NULL */
                     void (*freeprivpart)(struct up_part *, void *));

int up_map_loadall(struct up_disk *disk);
void up_map_freeall(struct up_disk *disk);

int up_map_load(struct up_disk *disk, struct up_part *parent, int64_t start,
                int64_t size, enum up_map_type type, struct up_map **mapret);
struct up_part *up_map_add(struct up_map *map, struct up_part *parent,
                           int64_t start, int64_t size, int flags, void *priv);

void up_map_free(struct up_map *map);
void up_map_freeprivmap_def(struct up_map *map, void *priv);
void up_map_freeprivpart_def(struct up_part *part, void *priv);

void up_map_print(const struct up_map *map, void *stream,
                  int verbose, int recurse);
void up_map_dump(const struct up_map *map, void *stream, int recurse);
void up_map_printall(const struct up_disk *disk, void *stream, int verbose);
void up_map_dumpall(const struct up_disk *disk, void *stream);

const struct up_part *up_map_first(const struct up_map *map);
const struct up_part *up_map_firstsub(const struct up_part *part);
const struct up_part *up_map_next(const struct up_part *part);
const struct up_map  *up_map_firstmap(const struct up_part *part);
const struct up_map  *up_map_nextmap(const struct up_map *map);

#endif /* HDR_UPART_MAP */
