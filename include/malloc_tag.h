/*
 * Malloc hooks that implement a low-overhead memory profiler
 *
 * Inspired by:
 *  - Pixar TfMallocTag tool, see https://openusd.org/dev/api/page_tf__malloc_tag.html
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

#pragma once

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------

#include <malloc.h> // provides prototypes for malloc()/free()/etc
#include <string>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MTAG_STATS_OUTPUT_JSON_ENV "MTAG_STATS_OUTPUT_JSON"
#define MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV "MTAG_STATS_OUTPUT_GRAPHVIZ_DOT"

#define MTAG_DEFAULT_MAX_TREE_NODES 256
#define MTAG_DEFAULT_MAX_TREE_LEVELS 4

#define MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE "uniquetree"

//------------------------------------------------------------------------------
// glibc overrides
//------------------------------------------------------------------------------

extern "C" {
// the malloc()/free() interceptor defined by this library
void* malloc(size_t size);
void free(void* __ptr) __THROW;
};

//------------------------------------------------------------------------------
// malloc_tag public API
//------------------------------------------------------------------------------

enum MallocTagOutputFormat_e {
    MTAG_OUTPUT_FORMAT_JSON,
    MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT,

    MTAG_OUTPUT_FORMAT_ALL
};

class MallocTagEngine {
public:
    MallocTagEngine() { }

    // The main API to initialize malloc-tag.
    // Call this function from the main thread, possibly as first thing inside the "main()" function
    // and before your software starts launching threads.
    static bool init( // fn
        size_t max_tree_nodes = MTAG_DEFAULT_MAX_TREE_NODES, // fn
        size_t max_tree_levels = MTAG_DEFAULT_MAX_TREE_LEVELS);

    // The main API to collect all results in JSON format
    // NOTE: invoking this function will indeed trigger some memory allocation on its own (!!!)
    static std::string collect_stats(
        MallocTagOutputFormat_e format, const std::string& output_options = MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE);

    // Write memory profiler stats into a file on disk;
    // if an empty string is passed, the full path will be taken from the environment variable
    // MTAG_STATS_OUTPUT_JSON_ENV or MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV, depending on the "format" argument.
    static bool write_stats_on_disk(MallocTagOutputFormat_e format = MTAG_OUTPUT_FORMAT_ALL,
        const std::string& fullpath = "", const std::string& output_options = MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE);

    // Get the RSS memory reported by Linux for this process.
    // This is Linux-specific utility.
    // This utility function has nothing to do with malloctag but it can be used to get the
    // OS-view of memory usage and see if it roughly matches with malloctag-reported results.
    // The total memory allocations intercepted by malloctag will never match exactly the VIRT/RSS memory
    // reported by Linux for a number of reasons:
    //  * malloctag is not aware about the internal-allocator (e.g. glibc ptmalloc) overhead and
    //    logic to acquire memory from the OS
    //  * some memory allocations might happen via alternative methods compared to malloc()/new,
    //    e.g. invoking directly mmap() or sbrk() syscalls
    static size_t get_linux_rss_mem_usage_in_bytes();
};

class MallocTagScope {
public:
    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" level
    MallocTagScope(const char* tag_name);

    // pop by 1 level the current per-thread cursor
    ~MallocTagScope();
};
