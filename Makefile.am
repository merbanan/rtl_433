AUTOMAKE_OPTIONS = foreign dist-bzip2 1.6
ACLOCAL_AMFLAGS = -I m4

INCLUDES = $(all_includes) -I$(top_srcdir)/include
SUBDIRS = include src

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = rtl433.pc

BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

EXTRA_DIST = git-version-gen

if HAVE_DOXYGEN

pkgdocdir=$(docdir)/$(PACKAGE)-$(VERSION)
doc_htmldir=$(pkgdocdir)/html

doc_html_DATA = $(top_builddir)/doc/html.tar

$(doc_html_DATA): $(top_builddir)/doc/html/index.html
	cd $(top_builddir)/doc && tar cf html.tar html

$(top_builddir)/doc/html/index.html: $(SOURCES) Doxyfile
	@rm -rf doc
	mkdir -p doc
	$(DOXYGEN) Doxyfile

install-data-hook:
	cd $(DESTDIR)$(doc_htmldir) && tar xf html.tar --strip-components 1 && rm -f html.tar

uninstall-hook:
	cd $(DESTDIR) && rm -rf $(doc_htmldir)

DX_CLEAN = doc/{html,latex}/* doc/html.tar

endif

MOSTLYCLEANFILES = $(DX_CLEAN)
