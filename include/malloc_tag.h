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
// glibc overrides
//------------------------------------------------------------------------------

extern "C"
{
    // the malloc()/free() interceptor defined by this library
    void *malloc(size_t size);
    void free(void *__ptr) __THROW;
};

//------------------------------------------------------------------------------
// malloc_tag public API
//------------------------------------------------------------------------------

// the main API to collect all results in a human-friendly format
// NOTE: invoking this function will indeed trigger some memory allocation on its own (!!!)
std::string malloc_collect_stats();

class MallocTagScope
{
public:
    // advance the per-thread cursor inside the malloc tree by 1 more level, adding the "tag_name" level
    MallocTagScope(const char *tag_name);

    // pop by 1 level the current per-thread cursor
    ~MallocTagScope();
};
