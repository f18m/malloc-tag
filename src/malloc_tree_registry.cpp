/*
 * malloc_tree_registry.cpp
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

#include "private/malloc_tree_registry.h"
#include "private/output_utils.h"

//------------------------------------------------------------------------------
// Globals
//------------------------------------------------------------------------------

extern std::atomic<size_t> g_bytes_allocated_before_init;

//------------------------------------------------------------------------------
// MallocTreeRegistry
//------------------------------------------------------------------------------

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

    // when we register the main tree, it means basically the memory profiling session is starting;
    // so let's remember the date/time to put it later in stats:
    time_t current_time;
    time(&current_time); // Get the current time
    localtime_r(&current_time, &m_tmStartProfiling); // Convert to local time

    MallocTree* t = new MallocTree();
    if (!t || !t->init(max_tree_nodes, max_tree_levels, true /* main thread */))
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

void MallocTreeRegistry::collect_stats(
    std::string& stats_str, MallocTagOutputFormat_e format, const std::string& output_options)
{
    // this code is thread-safe because trees can only get registered, never removed:
    size_t num_trees = m_nMallocTrees.load();

    // we provide 2 timestamps:
    // * start of memory profiling session
    // * current timestamp
    char tmStartProfilingStr[64];
    strftime(tmStartProfilingStr, sizeof(tmStartProfilingStr), "%Y-%m-%d @ %H:%M:%S %Z", &m_tmStartProfiling);

    time_t current_time;
    struct tm current_time_tm;
    time(&current_time); // Get the current time
    localtime_r(&current_time, &current_time_tm); // Convert to local time

    char tmCurrentStr[64];
    strftime(tmCurrentStr, sizeof(tmCurrentStr), "%Y-%m-%d @ %H:%M:%S %Z", &current_time_tm);

    size_t vmSizeNow = MallocTagEngine::get_linux_vmsize_in_bytes();

    switch (format) {
    case MTAG_OUTPUT_FORMAT_JSON: {
        JsonUtils::start_document(stats_str);
        JsonUtils::append_field(stats_str, "PID", getpid());
        JsonUtils::append_field(stats_str, "tmStartProfiling", tmStartProfilingStr);
        JsonUtils::append_field(stats_str, "tmCurrentSnapshot", tmCurrentStr);

        size_t tot_tracked_mem_bytes = g_bytes_allocated_before_init;
        for (size_t i = 0; i < num_trees; i++) {
            m_pMallocTreeRegistry[i]->collect_stats_recursively(stats_str, format, output_options);
            tot_tracked_mem_bytes += m_pMallocTreeRegistry[i]->get_total_bytes_tracked();
            stats_str += ",";
        }

        JsonUtils::append_field(stats_str, "nBytesAllocBeforeInit", g_bytes_allocated_before_init);
        JsonUtils::append_field(stats_str, "nBytesMallocTagSelfUsage", get_total_memusage());

        // VmSizeNow and nTotalTrackedBytes should be similar ideally. In practice nTotalTrackedBytes>>VmSizeNow
        // because free() operations do not reduce the value of nTotalTrackedBytes but they can potentially reduce
        // VmSizeNow
        JsonUtils::append_field(stats_str, "VmSizeNow", vmSizeNow);
        JsonUtils::append_field(stats_str, "nTotalTrackedBytes", tot_tracked_mem_bytes, true /* is_last */);
        JsonUtils::end_document(stats_str);
    } break;

    case MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT: {
        if (output_options == MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE) {
            // create a single unique graph for ALL threads/trees, named "MallocTree"
            GraphVizUtils::start_digraph(stats_str, "MallocTree");
        }
        size_t tot_tracked_mem_bytes = g_bytes_allocated_before_init;
        for (size_t i = 0; i < num_trees; i++) {
            m_pMallocTreeRegistry[i]->collect_stats_recursively(stats_str, format, output_options);
            tot_tracked_mem_bytes += m_pMallocTreeRegistry[i]->get_total_bytes_tracked();
            stats_str += "\n";
        }

        // use labels to convey extra info:
        std::vector<std::string> labels;
        labels.push_back("Memory allocated before MallocTag initialization = "
            + GraphVizUtils::pretty_print_bytes(g_bytes_allocated_before_init));
        labels.push_back(
            "Memory allocated by MallocTag itself =" + GraphVizUtils::pretty_print_bytes(get_total_memusage()));
        labels.push_back("Total memory tracked by MallocTag across all threads ="
            + GraphVizUtils::pretty_print_bytes(tot_tracked_mem_bytes));
        labels.push_back("VmSizeNow = " + GraphVizUtils::pretty_print_bytes(vmSizeNow));
        labels.push_back("Memory profiling session start timestamp = " + std::string(tmStartProfilingStr));
        labels.push_back("This snapshot timestamp = " + std::string(tmCurrentStr));
        labels.push_back("PID = " + std::to_string(getpid()));

        if (output_options == MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE)
            GraphVizUtils::end_digraph(stats_str, labels); // close the MallocTree

        if (output_options != MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE) {
            // add a few nodes "external" to the tree:
            GraphVizUtils::start_digraph(stats_str, "MallocTree_globals", labels);
            GraphVizUtils::end_digraph(stats_str); // close the MallocTree_globals
        }
    } break;

    default:
        break;
    }
}