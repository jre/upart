
all: $(ALL_PROGS)

.PHONY: all clean clean-tests cleaner cleanest check regress regen-tests

install: all
	$(MKDIR_P) $(DESTDIR)$(bindir)
	$(INSTALL_PROG) upart $(DESTDIR)$(bindir)
	$(MKDIR_P) $(DESTDIR)$(mandir)/man8
	$(INSTALL_DATA) upart.8 $(DESTDIR)$(mandir)/man8

upart$(EXE_SUF): $(UPART_OBJS)
	$(CC) $(LDFLAGS) $(LIBS) -o $@ $(UPART_OBJS)

$(REGRESS_BIN)$(EXE_SUF): $(REGRESS_SRC) util.h
	$(CC) $(CFLAGS) -I. -o $@ $(REGRESS_SRC)

clean: clean-tests
	$(RM_CMD) .depend $(ALL_PROGS) $(ALL_OBJS)

clean-tests: $(REGRESS_BIN)$(EXE_SUF)
	./$(REGRESS_BIN)$(EXE_SUF) -c
	$(RM_CMD) $(REGRESS_BIN)$(EXE_SUF)

cleaner: clean
	$(RM_CMD) config.cache config.h config.log config.status common.mk

cleanest: cleaner
	$(RM_CMD) configure

check regress: upart$(EXE_SUF) $(REGRESS_BIN)$(EXE_SUF)
	./$(REGRESS_BIN)$(EXE_SUF)

regen-tests: tester$(EXE_SUF)
	./$(REGRESS_BIN)$(EXE_SUF) -r
