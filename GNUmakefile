include common.mk

.depend: ${ALL_SRCS} ${ALL_HDRS} GNUmakefile
	${CC} ${CFLAGS} ${DEPFLAGS} ${ALL_SRCS} > $@ 2> /dev/null

${ALL_OBJS}: GNUmakefile

-include .depend
