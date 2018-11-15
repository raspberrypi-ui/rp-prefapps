bin_PROGRAMS = rp-prefapps

rp_prefapps_CFLAGS = \
	-I$(top_srcdir) \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)/rp-prefapps"\" \
	-DPACKAGE_UI_DIR=\""$(datadir)/rp-prefapps/ui"\" \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

rp_prefapps_SOURCES = rp_prefapps.c

rp_prefapps_includedir = $(includedir)/rp-prefapps

rp_prefapps_include_HEADERS =

rp_prefapps_LDFLAGS = $(DYNAMIC_FLAGS)
rp_prefapps_DEPENDENCIES_EXTRA = $(BUILTIN_PLUGINS)

rp_prefapps_LDADD = \
		$(BUILTIN_PLUGINS) \
		$(PACKAGE_LIBS) \
		$(X11_LIBS) \
		$(INTLLIBS)

EXTRA_DIST =
