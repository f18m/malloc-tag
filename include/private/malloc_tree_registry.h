/*
 * malloc_tree_registry.h
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
#include <dlfcn.h>
#include <fstream>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MAX_THREADS 128

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

    size_t get_total_memusage(); // FIXME: rename to get_total_memusage_in_bytes()

    bool has_main_thread_tree() { return m_nMallocTrees > 0; }

    MallocTree* get_main_thread_tree()
    {
        if (m_nMallocTrees == 0)
            return NULL;
        return m_pMallocTreeRegistry[0];
    }

    void collect_stats(std::string& stats_str, MallocTagOutputFormat_e format, const std::string& output_options);

private:
    // the registry is the OWNER of m_nMallocTrees whose pointers get stored in m_pMallocTreeRegistry[]
    MallocTree* m_pMallocTreeRegistry[MAX_THREADS];
    std::atomic_uint m_nMallocTrees;
};
