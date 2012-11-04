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
#define UP_PART_VIRTDISK	(1<<2) /* partition defines a virtual disk */
#define UP_PART_UNREADABLE	(1<<3) /* ignore partition contents */

#define UP_PART_IS_BAD(flags) \
	((UP_PART_EMPTY|UP_PART_OOB|UP_PART_UNREADABLE) & (flags))

#define UP_MAP_VIRTADDR(m)		((m)->virtstart)
#define UP_MAP_PHYSADDR(m)		((m)->virtstart + (m)->virtoff)
#define UP_MAP_PHYS_TO_VIRT(m, a)	((a) - (m)->virtoff)
#define UP_MAP_VIRT_TO_PHYS(m, a)	((a) + (m)->virtoff)
#define UP_PART_VIRTADDR(p)		((p)->virtstart)
#define UP_PART_PHYSADDR(p) \
	((p)->virtstart + ((p)->map ? (p)->map->virtoff : 0))
#define UP_PART_VIRTOFFSET(p) \
	(((p)->map ? (p)->map->virtoff : 0) + \
	    ((p)->flags & UP_PART_VIRTDISK ? (p)->virtstart : 0))
#define UP_PART_PHYS_TO_VIRT(p, a)	((a) - UP_PART_VIRTOFFSET(p))
#define UP_PART_VIRT_TO_PHYS(p, a)	((a) + UP_PART_VIRTOFFSET(p))

SIMPLEQ_HEAD(map_list, map);
SIMPLEQ_HEAD(part_list, part);

struct part {
	int64_t virtstart;
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
	/* BSD disklabel should be last because the probing it can
	   cause a disklabel to be detected before its container partition */
	UP_MAP_BSD,
	UP_MAP_SOFTRAID,
	UP_MAP_ID_COUNT
};

struct map {
	const struct disk *disk;
	enum mapid type;
	int64_t virtstart;
	int64_t size;
	int64_t virtoff;
	int depth;
	void *priv;
	struct part *parent;
	struct part_list list;
	SIMPLEQ_ENTRY(map) link;
};

typedef int (*map_load_fn)(const struct disk *, const struct part *, void **);
typedef int (*map_setup_fn)(struct disk *, struct map *);
typedef int (*map_getmap_fn)(const struct map *, char *, size_t);
typedef int (*map_getpart_fn)(const struct part *, char *, size_t);
typedef int (*map_printmap_fn)(const struct map *, FILE *);
typedef int (*map_printpart_fn)(const struct part *, FILE *);
typedef int (*map_printdump_fn)(const struct map *, int64_t, const void *,
    int64_t, int, FILE *);
typedef void (*map_freemap_fn)(struct map *, void *);
typedef void (*map_freepart_fn)(struct part *, void *);

struct map_funcs
{
	char *label;
	unsigned int flags;
	/* check if map exists and allocate private data */
	map_load_fn load;
	/* add partitions, and any misc. setup not done in load */
	map_setup_fn setup;
	/* print map header, can be several lines */
	map_printmap_fn print_header;
	/* retrieve partition index */
	map_getpart_fn get_index;
	/* print header for verbose partition info */
	map_printmap_fn print_extrahdr;
	/* print verbose partition info */
	map_printpart_fn print_extra;
	/* print extra information for sector dump */
	map_printdump_fn dump_extra;
	/* free private map data, map may be NULL if it was never allocated */
	map_freemap_fn free_mappriv;
	/* free private partition data */
	map_freepart_fn free_partpriv;
};

void		 up_map_funcs_init(struct map_funcs *);
void		 up_map_register(enum mapid, const struct map_funcs *);

int		 up_map_loadall(struct disk *);
void		 up_map_freeall(struct disk *);

int		 up_map_load(struct disk *, struct part *, enum mapid,
    struct map **);
struct part	*up_map_add(struct map *, int64_t, int64_t, int, void *);

void		 up_map_free(struct disk *, struct map *);
void		 up_map_freeprivmap_def(struct map *, void *);
void		 up_map_freeprivpart_def(struct part *, void *);

const char	*up_map_label(const struct map *);
void		 up_map_dumpsect(const struct map *, FILE *, int64_t,
    int64_t, const void *, int);
void		 up_map_printall(const struct disk *, void *);

const struct part *up_map_first(const struct map *);
const struct part *up_map_next(const struct part *);
const struct map *up_map_firstmap(const struct part *);
const struct map *up_map_nextmap(const struct map *);

#endif /* HDR_UPART_MAP */
