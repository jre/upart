.include "auto.mk"
.include "defs.mk"
.include "build.mk"

.PHONY: depend dep

depend dep:
	${CC} ${CFLAGS} ${DEPFLAGS} ${ALL_SRCS} > .depend

${ALL_OBJS}: Makefile
