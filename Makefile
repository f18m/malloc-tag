# Very simple makefile to build the library and example code

# Configurable flags:

ifeq ($(USE_TCMALLOC),)
# default value: assume you have a package like "gperftools-libs" installed on your system
USE_TCMALLOC=1
endif


# Start of Makefile

CC=g++
CXXFLAGS_OPT= -fPIC -std=c++11 -Iinclude -O3 -pthread -Wno-attributes -Wno-unused-result
CXXFLAGS_DBG= -fPIC -std=c++11 -Iinclude -g -O0 -pthread -Wno-attributes -Wno-unused-result

LIB_HDR = \
	include/malloc_tag.h \
	include/private/malloc_tree_node.h \
	include/private/malloc_tree_registry.h \
	include/private/malloc_tree.h \
	include/private/output_utils.h
LIB_SRC = \
	src/malloc_tag.cpp \
	src/malloc_tree.cpp \
	src/malloc_tree_registry.cpp \
	src/malloc_tree_node.cpp
LIBS = \
	src/libmalloc_tag.so
BINS = \
	examples/minimal/minimal \
	examples/multithread/multithread \
	tests/unit_tests

ifeq ($(USE_TCMALLOC),1)
	BINS += examples/malloctag_and_tcmalloc/malloctag_and_tcmalloc
endif

LIB_OBJ = $(subst .cpp,.o,$(LIB_SRC))
LIB_VER = 1

SHOW_JSON=0
SHOW_DOT=0


# Targets

all: $(BINS) $(LIBS)
	@echo 
	@echo "Build succeeded!"
	@echo 
	@echo "Run:"
	@echo "   make minimal_example"
	@echo "for a short tutorial (read comments in the source code!)"

format_check:
	# if you have clang-format >= 10.0.0, this will work:
	@clang-format --dry-run --Werror $(LIB_HDR) $(LIB_SRC)

minimal_example: $(BINS)
	@echo "Starting example application"
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/minimal/minimal_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/minimal/minimal_stats.dot \
		examples/minimal/minimal
	@dot -Tsvg -O $(PWD)/examples/minimal/minimal_stats.dot
ifeq ($(SHOW_JSON),1)
	@echo
	@echo "JSON of output stats:"
	@jq . $(PWD)/examples/minimal/minimal_stats.json
	@echo
endif
ifeq ($(SHOW_DOT),1)
	@echo
	@echo "Graphviz DOT output stats (copy-paste that into https://dreampuf.github.io/):"
	@cat $(PWD)/examples/minimal/minimal_stats.dot
	@echo
endif

# stracing is Always a Good Thing to learn more about basic things like e.g. dynamic linker, mmap(), brk(), etc
minimal_strace: $(BINS)
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/minimal/minimal_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/minimal/minimal_stats.dot \
		strace -e trace=%memory,%file \
		examples/minimal/minimal

minimal_debug: $(BINS)
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/minimal/minimal_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/minimal/minimal_stats.dot \
		gdb \
		examples/minimal/minimal

#
# If you want to experiment decreasing the glibc VIRT memory usage in multithreaded apps,
# you can add
#	GLIBC_TUNABLES=glibc.malloc.arena_max=1 
# to the env vars used below:
#
multithread_example: $(BINS)
	@echo "Starting example application"
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/multithread/multithread_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/multithread/multithread_stats.dot \
		examples/multithread/multithread
	@dot -Tsvg -O $(PWD)/examples/multithread/multithread_stats.dot
ifeq ($(SHOW_JSON),1)
	@echo
	@echo "JSON of output stats:"
	@jq . $(PWD)/examples/multithread/multithread_stats.json
	@echo
endif
ifeq ($(SHOW_DOT),1)
	@echo
	@echo "Graphviz DOT output stats (copy-paste that into https://dreampuf.github.io/):"
	@cat $(PWD)/examples/multithread/multithread_stats.dot
	@echo
endif

multithread_strace: $(BINS)
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/multithread/multithread_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/multithread/multithread_stats.dot \
		strace -e trace=%memory,%file -t -f \
		examples/multithread/multithread
multithread_debug: $(BINS)
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/multithread/multithread_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/multithread/multithread_stats.dot \
		gdb \
		examples/multithread/multithread


tests: $(BINS)
	@echo "Starting UNIT TESTS application"
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/tests/dummystats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/tests/dummystats.dot \
		tests/unit_tests



ifeq ($(USE_TCMALLOC),1)
	
tcmalloc_example: $(BINS)
	@echo "Starting example application"
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/malloctag_and_tcmalloc/malloctag_and_tcmalloc_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/malloctag_and_tcmalloc/malloctag_and_tcmalloc_stats.dot \
		examples/malloctag_and_tcmalloc/malloctag_and_tcmalloc

tcmalloc_debug: $(BINS)
	@echo "Starting example application"
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/malloctag_and_tcmalloc/malloctag_and_tcmalloc_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/malloctag_and_tcmalloc/malloctag_and_tcmalloc_stats.dot \
		gdb \
		examples/malloctag_and_tcmalloc/malloctag_and_tcmalloc
endif


# build and run all examples at once
examples: \
	minimal_example \
	multithread_example

benchmarks: 
	@echo TODO

clean:
	find -name *.so* -exec rm {} \;
	find -name *.o -exec rm {} \;
	rm -f $(BINS)

install:
ifndef DESTDIR
	@echo "*** ERROR: please call this makefile supplying explicitly the DESTDIR variable. E.g. use DESTDIR=/usr or DESTDIR=/usr/local"
	@exit 1
endif
	@echo "Installing malloc-tag header into $(DESTDIR)/include"
	@mkdir --parents                    $(DESTDIR)/include
	@cp -fv include/malloc_tag.h        $(DESTDIR)/include/
	@echo "Installing malloc-tag library into $(DESTDIR)/lib"
	@mkdir --parents                    $(DESTDIR)/lib
	@cp -fv src/libmalloc_tag.so*       $(DESTDIR)/lib/


.PHONY: all minimal_example multithread_example examples tests clean install


# Rules

%.o: %.cpp $(LIB_HDR)
	$(CC) $(CXXFLAGS_DBG) $(DEBUGFLAGS) -c -o $@ $< 

# rule to COMPILE the malloc_tag code
src/%: src/%.o
	$(CC) -o $@ $^ -pthread

# rule to LINK the malloc_tag library
src/libmalloc_tag.so: $(LIB_HDR) $(LIB_OBJ)
	$(CC) $(LINKER_FLAGS) \
		$(LIB_OBJ) \
		-shared -Wl,-soname,libmalloc_tag.so.$(LIB_VER) \
		-ldl \
		-o $@
	cd src && ln -sf libmalloc_tag.so libmalloc_tag.so.$(LIB_VER) 


# rule to COMPILE test code
tests/%: tests/%.o
	$(CC) -o $@ $^ -pthread 

# rule to LINK test code
TESTS_OBJECTS:=$(patsubst %.cpp,%.o,$(wildcard tests/*.cpp))

tests/unit_tests: $(TESTS_OBJECTS) $(LIBS)
	$(CC) -o tests/unit_tests \
		$(TESTS_OBJECTS) \
		 -ldl -Lsrc -lmalloc_tag -lgtest -pthread



define example_build_targets

# rule to COMPILE example code
examples/$(1)/%: examples/$(1)/%.o
	$(CC) -o $@ $^ -pthread 

# rule to LINK example code
examples/$(1)/$(1): examples/$(1)/$(1).o $(LIBS)
	$(CC) -o examples/$(1)/$(1) \
		examples/$(1)/$(1).o \
		 -ldl -Lsrc -lmalloc_tag -pthread $(2) 

endef


# create rules to build each available example:
$(eval $(call example_build_targets,minimal))
$(eval $(call example_build_targets,multithread))

# malloctag_and_tcmalloc example needs an extra library (-ltcmalloc)
$(eval $(call example_build_targets,malloctag_and_tcmalloc,-ltcmalloc))

