/*
 * malloc_tree_registry.h
 * This is a PRIVATE header. It should not be included by any application directly.
 * This is used only during the build of malloc-tag library itself.
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

#include "malloc_tree.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <unistd.h>

#define MTAG_MAX_TREES 128

//------------------------------------------------------------------------------
// MallocTreeRegistry
// A handler of many MallocTree instances, one for each application thread.
// This class is thread-safe and is a singleton.
//------------------------------------------------------------------------------

class MallocTreeRegistry {
public:
    MallocTreeRegistry() { }
    ~MallocTreeRegistry();

    MallocTree* register_main_tree(size_t max_tree_nodes, size_t max_tree_levels); // triggers some mallocs!
    MallocTree* register_secondary_thread_tree(); // triggers some mallocs!

    size_t get_total_memusage_in_bytes();

    bool has_main_thread_tree() { return m_nMallocTrees > 0; }

    MallocTree* get_main_thread_tree()
    {
        if (m_nMallocTrees == 0)
            return NULL;
        return m_pMallocTreeRegistry[0];
    }

    void collect_stats(std::string& stats_str, MallocTagOutputFormat_e format, const std::string& output_options);
    void collect_stats_MAP(MallocTagStatMap_t& out);

protected:
private:
    // the registry is the OWNER of m_nMallocTrees whose pointers get stored in m_pMallocTreeRegistry[]
    MallocTree* m_pMallocTreeRegistry[MTAG_MAX_TREES];
    std::atomic_uint m_nMallocTrees;

    // records the time the memory profiling has started:
    struct tm m_tmStartProfiling;
};
