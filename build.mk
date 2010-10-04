
all: $(ALL_PROGS)

.PHONY: all clean clean-tests cleaner cleanest check regress regen-tests

install: all
	$(MKDIR_P) $(DESTDIR)$(bindir)
	$(INSTALL_PROG) upart $(DESTDIR)$(bindir)
	$(MKDIR_P) $(DESTDIR)$(mandir)/man8
	$(INSTALL_DATA) upart.8 $(DESTDIR)$(mandir)/man8

upart$(EXE_SUF): $(UPART_OBJS)
	$(CC) $(LDFLAGS) $(LIBS) -o $@ $(UPART_OBJS)

clean: clean-tests
	$(RM_CMD) .depend $(ALL_PROGS) $(ALL_OBJS)

clean-tests:
	cd $(REGRESS_DIR) && $(REGRESS_CMD) clean

cleaner: clean
	$(RM_CMD) config.cache config.h config.log config.status common.mk

cleanest: cleaner
	$(RM_CMD) configure

check regress: $(ALL_PROGS)
	cd $(REGRESS_DIR) && $(REGRESS_CMD)

regen-tests: $(ALL_PROGS)
	cd $(REGRESS_DIR) && $(REGRESS_CMD) regen
