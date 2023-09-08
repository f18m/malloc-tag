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
// A handler of many MallocTree_t instances, one for each application thread.
// This class is thread-safe and is a singleton.
//------------------------------------------------------------------------------

class MallocTreeRegistry {
public:
    bool register_tree(MallocTree_t* ptree);
    size_t get_total_memusage();

    bool has_main_thread_tree() { return m_nMallocTrees > 0; }

    MallocTree_t* get_main_thread_tree()
    {
        if (m_nMallocTrees == 0)
            return NULL;
        return m_pMallocTreeRegistry[0];
    }

private:
    MallocTree_t* m_pMallocTreeRegistry[MAX_THREADS];
    std::atomic_uint m_nMallocTrees;
};
