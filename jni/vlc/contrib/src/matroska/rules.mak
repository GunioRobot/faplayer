# matroska

MATROSKA_VERSION := 1.1.0
MATROSKA_URL := http://dl.matroska.org/downloads/libmatroska/libmatroska-$(MATROSKA_VERSION).tar.bz2
#MATROSKA_URL := $(CONTRIB_VIDEOLAN)/libmatroska-$(MATROSKA_VERSION).tar.bz2

PKGS += matroska

$(TARBALLS)/libmatroska-$(MATROSKA_VERSION).tar.bz2:
	$(call download,$(MATROSKA_URL))

.sum-matroska: libmatroska-$(MATROSKA_VERSION).tar.bz2

libmatroska: libmatroska-$(MATROSKA_VERSION).tar.bz2 .sum-matroska
	$(UNPACK)
	mv $@-$(MATROSKA_VERSION) $@
	touch $@

.matroska: libmatroska .ebml
ifdef HAVE_WIN32
	cd $< && $(MAKE) -C make/mingw32 prefix="$(PREFIX)" $(HOSTVARS) SHARED=no EBML_DLL=no libmatroska.a
else
	cd $< && $(MAKE) -C make/linux prefix="$(PREFIX)" $(HOSTVARS) staticlib
endif
	cd $< && $(MAKE) -C make/linux install_staticlib install_headers prefix="$(PREFIX)" $(HOSTVARS)
	$(RANLIB) "$(PREFIX)/lib/libmatroska.a"
	touch $@
