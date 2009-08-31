#ifndef HDR_UPART_MAP
#define HDR_UPART_MAP

#include "bsdqueue.h"

struct disk;
struct map;
struct part;

#define UP_TYPE_REGISTERED      (1<<0)
#define UP_TYPE_NOPRINTHDR      (1<<1)

#define UP_PART_EMPTY           (1<<0) /* empty or deleted */
#define UP_PART_OOB             (1<<1) /* out of bounds */

#define UP_PART_IS_BAD(flags)   ((UP_PART_EMPTY|UP_PART_OOB) & (flags))

SIMPLEQ_HEAD(map_list, map);
SIMPLEQ_HEAD(part_list, part);

struct part {
	int64_t start;
	int64_t size;
	int flags;
	void *priv;
	struct map *map;
	struct map_list submap;
	SIMPLEQ_ENTRY(part) link;
};

enum mapid {
	UP_MAP_NONE = 0,
	/* GPT needs to go before MBR */
	UP_MAP_GPT,
	UP_MAP_MBR,
	UP_MAP_MBREXT,
	UP_MAP_APM,
	UP_MAP_SUN_SPARC,
	UP_MAP_SUN_X86,
	/* BSD disklabel should be last because the probing it does can
	   cause a disklabel to be detected before it's container partition */
	UP_MAP_BSD,
	UP_MAP_ID_COUNT
};

struct map {
	const struct disk *disk;
	enum mapid type;
	int64_t start;
	int64_t size;
	int depth;
	void *priv;
	struct part *parent;
	struct part_list list;
	SIMPLEQ_ENTRY(map) link;
};

void		 up_map_register(enum mapid, const char *, int,
    /* load: check if map exists and allocate private data */
    int (*)(const struct disk *, const struct part *, void **),
    /* setup: add partitions, misc setup not done in load */
    int (*)(struct disk *, struct map *),
    /* getinfo: copy map header line into string */
    int (*)(const struct map *, char *, int),
    /* getindex: copy part index into string */
    int (*)(const struct part *, char *, int),
    /* getextrahdr: copy header for extra verbose info into string */
    int (*)(const struct map *, char *, int),
    /* getextra: copy extra verbose info into string */
    int (*)(const struct part *, char *, int),
    /* getdumpextra: copy extra information for sector dump into string */
    int (*)(const struct map *, int64_t, const void *, int64_t, int,
	char *, int),
    /* freeprivmap: free map private data, map may be NULL */
    void (*)(struct map *, void *),
    /* freeprivpart: free part private data, part may be NULL */
    void (*)(struct part *, void *));

int		 up_map_loadall(struct disk *);
void		 up_map_freeall(struct disk *);

int		 up_map_load(struct disk *, struct part *, enum mapid,
    struct map **);
struct part	*up_map_add(struct map *, int64_t, int64_t, int, void *);

void		 up_map_free(struct disk *, struct map *);
void		 up_map_freeprivmap_def(struct map *, void *);
void		 up_map_freeprivpart_def(struct part *, void *);

const char	*up_map_label(const struct map *);
void		 up_map_print(const struct map *, void *, int);
void		 up_map_dumpsect(const struct map *, void *, int64_t,
    int64_t, const void *, int);
void		 up_map_printall(const struct disk *, void *);

const struct part *up_map_first(const struct map *);
const struct part *up_map_next(const struct part *);
const struct map *up_map_firstmap(const struct part *);
const struct map *up_map_nextmap(const struct map *);

#endif /* HDR_UPART_MAP */
