# Very simple makefile to build the library and example code

CC=g++
CXXFLAGS_OPT= -fPIC -std=c++11 -Iinclude -O3 -pthread -Wno-attributes -Wno-unused-result
CXXFLAGS_DBG= -fPIC -std=c++11 -Iinclude -g -O0 -pthread -Wno-attributes -Wno-unused-result

LIB_HDR = \
	include/malloc_tag.h \
	include/private/malloc_tree_node.h \
	include/private/malloc_tree_registry.h \
	include/private/malloc_tree.h
LIB_SRC = \
	src/malloc_tag.cpp \
	src/malloc_tree.cpp \
	src/malloc_tree_registry.cpp \
	src/malloc_tree_node.cpp
LIBS = \
	src/libmalloc_tag.so
BINS = \
	examples/minimal/minimal \
	examples/multithread/multithread
EXAMPLE_LIST = \
	minimal \
	multithread

LIB_OBJ = $(subst .cpp,.o,$(LIB_SRC))
LIB_VER = 1


# Targets

all: $(BINS) $(LIBS)
	@echo "Run examples/minimal/minimal for a short tutorial (read comments in the source code!)"

format_check:
	# if you have clang-format >= 10.0.0, this will work:
	@clang-format --dry-run --Werror $(LIB_HDR) $(LIB_SRC)

minimal_example: $(BINS)
	@echo "Starting example application"
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/minimal/minimal_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/minimal/minimal_stats.dot \
		examples/minimal/minimal
	@echo
	@echo "JSON of output stats:"
	@jq . $(PWD)/examples/minimal/minimal_stats.json
	@echo
	@echo "Graphviz DOT output stats (copy-paste that into https://dreampuf.github.io/):"
	@cat $(PWD)/examples/minimal/minimal_stats.dot
	@echo
	@dot -Tsvg -O $(PWD)/examples/minimal/minimal_stats.dot

 multithread_example: $(BINS)
	@echo "Starting example application"
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=$(PWD)/examples/multithread/multithread_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=$(PWD)/examples/multithread/multithread_stats.dot \
		examples/multithread/multithread
	@echo
	@echo "JSON of output stats:"
	@jq . $(PWD)/examples/multithread/multithread_stats.json
	@echo
	@echo "Graphviz DOT output stats (copy-paste that into https://dreampuf.github.io/):"
	@cat $(PWD)/examples/multithread/multithread_stats.dot
	@echo
	@dot -Tsvg -O $(PWD)/examples/multithread/multithread_stats.dot


# build and run all examples at once
examples: \
	minimal_example \
	multithread_example

benchmarks: 
	@echo TODO

clean:
	find -name *.so -exec rm {} \;
	find -name *.o -exec rm {} \;
	rm $(BINS)

.PHONY: all example examples clean


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


define example_build_targets

# rule to COMPILE example code
examples/$(1)/%: examples/$(1)/%.o
	$(CC) -o $@ $^ -pthread 

# rule to LINK example code
examples/$(1)/$(1): examples/$(1)/$(1).o $(LIBS)
	$(CC) -o examples/$(1)/$(1) \
		examples/$(1)/$(1).o \
		-ldl -Lsrc -lmalloc_tag -pthread

endef

$(foreach ex, $(EXAMPLE_LIST), $(eval $(call example_build_targets,$(ex))))


