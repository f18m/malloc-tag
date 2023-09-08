/*
 * malloc_tree_registry.cpp
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

#include "private/malloc_tree_registry.h"

bool MallocTreeRegistry::register_tree(MallocTree* ptree)
{
    // thread-safe code
    size_t reservedIdx = m_nMallocTrees.fetch_add(1);
    if (reservedIdx >= MAX_THREADS) {
        // we have reached the max number of trees/threads for this application!
        return false;
    }

    // NOTE: whatever we store in the index 0 is considered to be the "main thread tree"
    //       and all other threads will inherit from that tree a few properties
    m_pMallocTreeRegistry[reservedIdx] = ptree;
    return true;
}

size_t MallocTreeRegistry::get_total_memusage()
{
    // this code is thread-safe because trees can only get registered, never removed:
    size_t num_trees = m_nMallocTrees.load();

    size_t total_bytes = 0;
    for (size_t i = 0; i < num_trees; i++)
        total_bytes += m_pMallocTreeRegistry[i]->get_memory_usage_in_bytes();

    // NOTE: only trees that have been init()ialized contribute to memory usage.
    //       if there is an application thread that somehow is never invoking malloc(),
    //       then its MallocTree will never be init() and it will never be registered...
    //       that's fine because its memory consumption will be roughly zero

    return total_bytes;
}
