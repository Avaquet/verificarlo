noinst_LTLIBRARIES = libtinymt64.la libvprec_tools.la libvprec_utils.la libvfc_hashmap.la

libtinymt64_la_CFLAGS = -fPIC -static
libtinymt64_la_SOURCES = \
	tinymt64.h \
	tinymt64.c

libvprec_tools_la_CFLAGS = -fPIC -static
libvprec_tools_la_SOURCES = \
	vprec_tools.h \
	vprec_tools.c

libvfc_hashmap_la_CFLAGS = -fPIC -static
libvfc_hashmap_la_SOURCES = \
	vfc_hashmap.h \
	vfc_hashmap.c

libvprec_utils_la_CFLAGS = -fPIC -static `xml2-config --cflags --libs`
libvprec_utils_la_SOURCES = \
	vprec_utils.h \
	vprec_utils.c

library_includedir =$(includedir)/
