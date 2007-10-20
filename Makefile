.include "common.mk"

.PHONY: depend dep

depend dep:
	${CC} ${CFLAGS} ${DEPFLAGS} ${ALL_SRCS} > .depend 2> /dev/null

${ALL_OBJS}: Makefile
