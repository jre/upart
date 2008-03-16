#ifndef HDR_UPART_MAP
#define HDR_UPART_MAP

#include "bsdqueue.h"

struct up_map;
struct up_opts;
struct up_part;

#define UP_TYPE_REGISTERED      (1<<0)
#define UP_TYPE_NOPRINTHDR      (1<<1)

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
    void *              priv;
    struct up_map      *map;
    struct up_map_list  submap;
    SIMPLEQ_ENTRY(up_part) link;
};

enum up_map_type
{
    UP_MAP_NONE = 0,
    /* GPT needs to go before MBR */
    UP_MAP_GPT,
    UP_MAP_MBR,
    UP_MAP_MBREXT,
    UP_MAP_APM,
    UP_MAP_VTOC,
    /* BSD disklabel should be last because the probing it does can
       cause a disklabel to be detected before it's container partition */
    UP_MAP_BSD,
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

void up_map_register(enum up_map_type type, int flags,
                     /* check if map exists and allocate private data */
                     int (*load)(struct up_disk *,const struct up_part *,
                                 void **, const struct up_opts *),
                     /* add partitions, misc setup not done in load */
                     int (*setup)(struct up_map *, const struct up_opts *),
                     /* copy map header line into string */
                     int (*getinfo)(const struct up_map *, int, char *, int),
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

int up_map_loadall(struct up_disk *disk, const struct up_opts *opts);
void up_map_freeall(struct up_disk *disk);

int up_map_load(struct up_disk *disk, struct up_part *parent,
                enum up_map_type type, struct up_map **mapret,
                const struct up_opts *opts);
struct up_part *up_map_add(struct up_map *map, int64_t start, int64_t size,
                           int flags, void *priv);

void up_map_free(struct up_map *map);
void up_map_freeprivmap_def(struct up_map *map, void *priv);
void up_map_freeprivpart_def(struct up_part *part, void *priv);

void up_map_print(const struct up_map *map, void *stream,
                  int verbose, int recurse);
void up_map_dump(const struct up_map *map, void *stream, int recurse);
void up_map_printall(const struct up_disk *disk, void *stream, int verbose);
void up_map_dumpall(const struct up_disk *disk, void *stream);

const struct up_part *up_map_first(const struct up_map *map);
const struct up_part *up_map_next(const struct up_part *part);
const struct up_map  *up_map_firstmap(const struct up_part *part);
const struct up_map  *up_map_nextmap(const struct up_map *map);

#endif /* HDR_UPART_MAP */
