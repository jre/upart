CC       ?= cc
CFLAGS   ?= -g -Wall -Werror
DEPFLAGS ?= -MM -MG
LDFLAGS  ?= 
LIBS     ?= 

ALL_PROGS = upart
ALL_SRCS  = ${UPART_SRCS}
ALL_HDRS  = disk.h
UPART_SRCS= disk.c main.c

UPART_OBJS= ${UPART_SRCS:.c=.o}
ALL_OBJS  = ${ALL_SRCS:.c=.o}

.PHONY: all clean

all: ${ALL_PROGS}

upart: ${UPART_OBJS}
	${CC} ${CFLAGS} ${LDFLAGS} ${LIBS} -o $@ $^

.c.o:
	${CC} ${CFLAGS} -c -o $@ $<

.depend.mk: ${ALL_SRCS} ${ALL_HDRS} GNUmakefile
	${CC} ${CFLAGS} ${DEPFLAGS} ${ALL_SRCS} > $@ 2> /dev/null

clean:
	rm -f .depend.mk ${ALL_PROGS} ${ALL_OBJS}

cleaner: clean
	rm -f config.cache config.h config.log config.status

cleanest: cleaner
	rm -f configure

${ALL_OBJS}: GNUmakefile

-include .depend.mk
