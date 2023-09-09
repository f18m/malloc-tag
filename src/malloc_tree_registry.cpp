/*
 * malloc_tree_registry.cpp
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

#include "private/malloc_tree_registry.h"

MallocTreeRegistry::~MallocTreeRegistry()
{
    size_t toDelete = m_nMallocTrees.fetch_sub(1);
    while (toDelete > 0) {
        delete m_pMallocTreeRegistry[toDelete];
        toDelete = m_nMallocTrees.fetch_sub(1);
    }
    delete m_pMallocTreeRegistry[0];
}

MallocTree* MallocTreeRegistry::register_main_tree(size_t max_tree_nodes, size_t max_tree_levels)
{
    assert(m_nMallocTrees.fetch_add(1) == 0); // the main tree must be the first one to get created

    MallocTree* t = new MallocTree();
    if (!t || !t->init(max_tree_nodes, max_tree_levels))
        return nullptr; // out of memory

    assert(t->is_ready()); // it's a logical mistake to try to register a non-ready tree
    m_pMallocTreeRegistry[0] = t;
    return t;
}

MallocTree* MallocTreeRegistry::register_secondary_thread_tree()
{
    // thread-safe code
    size_t reservedIdx = m_nMallocTrees.fetch_add(1);
    if (reservedIdx >= MAX_THREADS) {
        // we have reached the max number of trees/threads for this application!
        return nullptr;
    }

    MallocTree* t = new MallocTree();
    if (!t || !t->init(m_pMallocTreeRegistry[0]))
        return nullptr; // out of memory

    // NOTE: whatever we store in the index 0 is considered to be the "main thread tree"
    //       and all other threads will inherit from that tree a few properties
    assert(t->is_ready()); // it's a logical mistake to try to register a non-ready tree
    m_pMallocTreeRegistry[reservedIdx] = t;
    return t;
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

extern std::atomic<size_t> g_bytes_allocated_before_init;

void MallocTreeRegistry::collect_stats(std::string& stats_str, MallocTagOutputFormat_e format)
{
    // this code is thread-safe because trees can only get registered, never removed:
    size_t num_trees = m_nMallocTrees.load();

    switch (format) {
    case MTAG_OUTPUT_FORMAT_JSON:
        stats_str += "{";
        stats_str += "\"nBytesAllocBeforeInit\": " + std::to_string(g_bytes_allocated_before_init) + ",";
        stats_str += "\"nBytesMallocTagSelfUsage\": " + std::to_string(get_total_memusage()) + ",";

        for (size_t i = 0; i < num_trees; i++) {
            m_pMallocTreeRegistry[i]->collect_stats_recursively(stats_str, format);
            if (i != num_trees - 1)
                stats_str += ",";
        }
        stats_str += "}";
        break;

    case MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT:
        // see https://graphviz.org/doc/info/lang.html
        for (size_t i = 0; i < num_trees; i++) {
            m_pMallocTreeRegistry[i]->collect_stats_recursively(stats_str, format);
            stats_str += "\n";
        }

        // add a few nodes "external" to the tree:
        stats_str += "digraph MallocTree_globals {\n";
        GraphVizUtils::append_node(stats_str, "__before_init_node__",
            "Memory Allocated\\nBefore MallocTag Init\\n"
                + GraphVizUtils::pretty_print_bytes(g_bytes_allocated_before_init));
        GraphVizUtils::append_node(stats_str, "__malloctag_self_memory__",
            "Memory Allocated\\nBy MallocTag itself\\n" + GraphVizUtils::pretty_print_bytes(get_total_memusage()));
        stats_str += "}";

        break;
    }
}