VERSION = @VERSION@
SUBDIRS = src
man_MANS = man/jp2a.1
bashcompdir = @bashcompdir@
dist_bashcomp_DATA = completion/bash/jp2a
zshcompdir = @zshcompdir@
dist_zshcomp_DATA = completion/zsh/_jp2a
EXTRA_DIST = tests/??*.txt tests/??*.html tests/??*.jpg tests/??*.png tests/run-tests.sh tests/Makefile include/??*.h man/jp2a.1 Doxyfile.in LICENSES
MOSTLYCLEANFILES = $(DX_CLEANFILES)
CLEANFILES = Doxyfile

.PHONY: doc test check

test: check

check: all
	$(MAKE) -C tests check

if DOC
@DX_RULES@
editdx = sed -e 's|@PACKAGE_NAME[@]|$(PACKAGE_NAME)|g' -e 's|@PACKAGE_VERSION[@]|$(PACKAGE_VERSION)|g' -e 's|@top_srcdir[@]|$(top_srcdir)|g'
Doxyfile: Doxyfile.in
	$(editdx) $(srcdir)/$@.in > $@.tmp
	mv $@.tmp $@
else !DOC
Doxyfile:
	@echo "Building doxygen documentation not enabled."
	@echo "To build doxygen documentation:"
	@echo "1. Install autoconf-archive and doxygen"
	@echo "2. autoreconf -vi"
	@echo "3. ./configure"
	@echo "4. make Doxyfile"
	@echo "5. doxygen"
	@false
endif

doc: Doxyfile doc/*.md include/*.h
	@echo "Running doxygen..."
	doxygen
	@echo "Documentation build to doxygen-doc/"
	@echo "You can view the HTML documentation by opening doxygen-doc/html/index.html, e. g. with:"
	@echo "xdg-open doxygen-doc/html/index.html"
