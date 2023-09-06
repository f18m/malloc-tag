# Very simple makefile to build the library and example code

CC=g++
CXXFLAGS_OPT= -fPIC -std=c++11 -Iinclude -O3 -pthread -Wno-attributes -Wno-unused-result
CXXFLAGS_DBG= -fPIC -std=c++11 -Iinclude -g -O0 -pthread -Wno-attributes -Wno-unused-result

LIB_HDR = \
	include/malloc_tag.h
LIB_SRC = \
	src/malloc_tag.cpp
LIBS = \
	src/libmalloc_tag.so
BINS = \
	examples/minimal/minimal


LIB_OBJ = $(subst .cpp,.o,$(LIB_SRC))
LIB_VER = 1


# Targets

all: $(BINS) $(LIBS)
	@echo "Run examples/minimal/minimal for a short tutorial (read comments in the source code!)"

format_check:
	# if you have clang-format >= 10.0.0, this will work:
	@clang-format --dry-run --Werror $(LIB_HDR) $(LIB_SRC)

example: $(BINS)
	@echo "Starting example application"
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) \
	MTAG_STATS_OUTPUT_JSON=/tmp/minimal_stats.json \
	MTAG_STATS_OUTPUT_GRAPHVIZ_DOT=/tmp/minimal_stats.dot \
		examples/minimal/minimal
	@echo
	@echo "JSON of output stats:"
	@jq . /tmp/minimal_stats.json
	@echo
	@echo "Graphviz DOT output stats (copy-paste that into https://dreampuf.github.io/):"
	@cat /tmp/minimal_stats.dot
	@echo

# just a synonim for "example":
examples: example


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

# rule to COMPILE example code
examples/minimal/%: examples/minimal/%.o
	$(CC) -o $@ $^ -pthread 

# rule to LINK example code
examples/minimal/minimal: examples/minimal/minimal.o $(LIBS)
	$(CC) -o $@ \
		examples/minimal/minimal.o \
		-ldl -Lsrc -lmalloc_tag

