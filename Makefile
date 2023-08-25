# Very simple makefile to build the library and example code

CC=g++
CXXFLAGS_OPT= -fPIC -std=c++11 -Iinclude -O3 -pthread
CXXFLAGS_DBG= -fPIC -std=c++11 -Iinclude -g -O0 -pthread

DEPS = \
	include/malloc_tag.h
LIB_SRC = \
	src/malloc_tag.cpp
LIBS = \
	src/libmalloc_tag.a
BINS = \
	examples/minimal/minimal


LIB_OBJ = $(subst .cpp,.o,$(LIB_SRC))


# Targets

all: $(BINS) $(LIBS)
	@echo "Run examples/minimal/minimal for a short tutorial (read comments in the source code!)"

format_check:
	# if you have clang-format >= 10.0.0, this will work:
	@clang-format --dry-run --Werror include/malloc_tag.hpp

test: $(BINS)
	tests/unit_tests --log_level=all --show_progress

# just a synonim for "test":
tests: test


benchmarks: 
	@echo TODO

clean:
	rm -f $(BINS) tests/*.o examples/minimal/*.o

.PHONY: all test tests clean


# Rules

%.o: %.cpp $(DEPS)
	$(CC) $(CXXFLAGS_DBG) $(DEBUGFLAGS) -c -o $@ $< 

src/%: src/%.o
	$(CC) -o $@ $^ -pthread

src/libmalloc_tag.a: $(DEPS) $(LIB_OBJ)
	ar crus $@ $(LIB_OBJ)

examples/minimal/%: examples/minimal/%.o
	$(CC) -o $@ $^ -pthread

examples/minimal/minimal: examples/minimal/minimal.o $(LIBS)
	$(CC) -o $@ -Lsrc -lmalloc_tag examples/minimal/minimal.o $(CXXFLAGS)

