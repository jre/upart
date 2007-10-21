CC           = cc
CFLAGS       = -g -Wall -Werror
DEPFLAGS     = -MM
LDFLAGS      = 
LIBS         = 

ALL_PROGS     = upart
ALL_SRCS      = ${UPART_SRCS}
ALL_HDRS      = ${UPART_HDRSRC:=.h}
UPART_SRCS    = ${UPART_HDRSRC:=.c} main.c
UPART_HDRSRC  = disk mbr util

UPART_OBJS    = ${UPART_SRCS:.c=.o}
ALL_OBJS      = ${ALL_SRCS:.c=.o}

.PHONY: all clean

all: ${ALL_PROGS}

upart: ${UPART_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} ${LIBS} -o $@ ${UPART_OBJS}

.c.o:
	${CC} ${CFLAGS} -c -o $@ $<

clean:
	rm -f .depend ${ALL_PROGS} ${ALL_OBJS}

cleaner: clean
	rm -f config.cache config.h config.log config.status

cleanest: cleaner
	rm -f configure
