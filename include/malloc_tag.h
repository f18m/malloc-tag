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

// the main API to collect all results in JSON format
// NOTE: invoking this function will indeed trigger some memory allocation on its own (!!!)
std::string malloctag_collect_stats(MallocTagOutputFormat_e format);

// write JSON stats into a file on disk;
// if an empty string is passed, the full path will be taken from the environment variable
// MTAG_STATS_OUTPUT_JSON_ENV or MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV
bool malloctag_write_stats_on_disk(MallocTagOutputFormat_e format, const std::string& fullpath = "");

class MallocTagScope {
public:
    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" level
    MallocTagScope(const char* tag_name);

    // pop by 1 level the current per-thread cursor
    ~MallocTagScope();
};
