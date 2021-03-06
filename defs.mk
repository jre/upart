RM_CMD        = rm -f
EXE_SUF       =

ALL_PROGS     = upart$(EXE_SUF)
ALL_SRCS      = $(UPART_SRCS)
ALL_HDRS      = $(UPART_HDRSRC:.=.h) bsdqueue.h os-private.h
UPART_SRCS    = $(UPART_HDRSRC:.=.c) getopt.c main.c os-bsd.c os-darwin.c \
		os-haiku.c os-linux.c os-solaris.c os-unix.c os-windows.c
UPART_HDRSRC  = apm. bsdlabel. crc32. disk. gpt. img. map. md5. mbr. os. \
		softraid. sunlabel-shared. sunlabel-sparc. sunlabel-x86. util.
REGRESS_SRC   = tests/tester.c
REGRESS_BIN   = tests/tester
REGRESS_CMD   = ./$(REGRESS_BIN)

UPART_OBJS    = $(UPART_SRCS:.c=.o)
ALL_OBJS      = $(ALL_SRCS:.c=.o)
