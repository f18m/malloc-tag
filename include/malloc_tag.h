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
};

class MallocTagEngine {
public:
    MallocTagEngine() { }

    // The main API to initialize malloc-tag.
    // Call this function from the main thread, possibly as first thing inside the "main()" function
    // and before your software starts launching threads.
    static bool init(size_t max_tree_nodes = MTAG_DEFAULT_MAX_TREE_NODES, // fn
        size_t max_tree_levels = MTAG_DEFAULT_MAX_TREE_LEVELS);

    // Get the singleton instance.
    // static MallocTagEngine* get() { return m_pInstance; }

    // The main API to collect all results in JSON format
    // NOTE: invoking this function will indeed trigger some memory allocation on its own (!!!)
    static std::string collect_stats(MallocTagOutputFormat_e format);

    // Write memory profiler stats into a file on disk;
    // if an empty string is passed, the full path will be taken from the environment variable
    // MTAG_STATS_OUTPUT_JSON_ENV or MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV, depending on the "format" argument.
    static bool write_stats_on_disk(MallocTagOutputFormat_e format, const std::string& fullpath = "");
};

class MallocTagScope {
public:
    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" level
    MallocTagScope(const char* tag_name);

    // pop by 1 level the current per-thread cursor
    ~MallocTagScope();
};
