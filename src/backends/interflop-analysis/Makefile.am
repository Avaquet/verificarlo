lib_LTLIBRARIES = libinterflop_analysis.la
libinterflop_analysis_la_SOURCES = interflop_analysis.c ../../common/logger.c ../../common/options.c
libinterflop_analysis_la_CFLAGS = -DBACKEND_HEADER="interflop_analysis"
if WALL_CFLAGS
libinterflop_analysis_la_CFLAGS += -Wall -Wextra 
endif
libinterflop_analysis_la_LDFLAGS = -lm -lquadmath
libinterflop_analysis_la_LIBADD = ../../common/libtinymt64.la ../../common/libvfc_hashmap.la
library_includedir =$(includedir)/
