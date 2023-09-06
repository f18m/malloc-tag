# Very simple makefile to build the library and example code

CC=g++
CXXFLAGS_OPT= -fPIC -std=c++11 -Iinclude -O3 -pthread -Wno-attributes
CXXFLAGS_DBG= -fPIC -std=c++11 -Iinclude -g -O0 -pthread -Wno-attributes

DEPS = \
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
	@clang-format --dry-run --Werror include/malloc_tag.hpp

example: $(BINS)
	@echo "Starting example application"
	LD_LIBRARY_PATH=$(PWD)/src:$(LD_LIBRARY_PATH) examples/minimal/minimal

# just a synonim for "test":
examples: test


benchmarks: 
	@echo TODO

clean:
	find -name *.so -exec rm {} \;
	find -name *.o -exec rm {} \;

.PHONY: all example examples clean


# Rules

%.o: %.cpp $(DEPS)
	$(CC) $(CXXFLAGS_DBG) $(DEBUGFLAGS) -c -o $@ $< 

src/%: src/%.o
	$(CC) -o $@ $^ -pthread

src/libmalloc_tag.so: $(DEPS) $(LIB_OBJ)
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
	$(CC) -o $@ -ldl -Lsrc -lmalloc_tag examples/minimal/minimal.o $(CXXFLAGS)

