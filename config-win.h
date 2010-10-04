#define _CRT_SECURE_NO_WARNINGS

/* for various types */
#include <crtdefs.h>

typedef ptrdiff_t ssize_t;

/* XXX */
#define MAXPATHLEN	4096

#if _MSC_VER < 0x1000

/* inttypes.h */
#define PRIx64		"I64x"
#define PRId64		"I64d"
#define PRIu64		"I64u"

/* stdint.h */
typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef __int64 int64_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
typedef uint64_t uintmax_t;
#define INT64_MAX _UI64_MAX
#define UINT16_C(n)	n ## ui16
#define UINT32_C(n)	n ## ui32
#define UINT64_C(n)	n ## ui64

#endif

#define PACKAGE_NAME "upart"
#define PACKAGE_VERSION "0.2.5"

#define snprintf _snprintf
#define fseeko _fseeki64
