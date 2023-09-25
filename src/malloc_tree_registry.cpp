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
    // atomically flag that the registry cannot be used anymore:
    m_bShutdownStarted = true;

    size_t toDelete = m_nMallocTrees.fetch_sub(1);
    while (toDelete > 0) {
        delete m_pMallocTreeRegistry[toDelete];
        toDelete = m_nMallocTrees.fetch_sub(1);
    }

    // delete the last tree
    delete m_pMallocTreeRegistry[0];
}

MallocTree* MallocTreeRegistry::register_main_tree(size_t max_tree_nodes, size_t max_tree_levels)
{
    if (MTAG_UNLIKELY(m_bShutdownStarted))
        return nullptr;
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
    if (MTAG_UNLIKELY(m_bShutdownStarted))
        return nullptr;

    // thread-safe code
    size_t reservedIdx = m_nMallocTrees.fetch_add(1);
    if (MTAG_UNLIKELY(reservedIdx >= MTAG_MAX_TREES)) {
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

size_t MallocTreeRegistry::get_total_memusage_in_bytes()
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

    size_t vmSizeNowBytes = MallocTagEngine::get_linux_vmsize_in_bytes();
    size_t vmRSSNowBytes = MallocTagEngine::get_linux_vmrss_in_bytes();

    // IMPORTANT:
    // To collect total allocated/freed across all trees/threads, the collect_allocated_freed_recursively() will
    // grab a lock and then release it.
    // By the time we have finished accumulating these stats across all threads, probably some tree has already
    // been updated by another thread. So what we get is an APPROXIMATED count of total ALLOCATED/FREED.
    // This is fast to obtain and good enough for our purposes.
    size_t nTotalBytesAllocatedFromAllTrees = 0;
    size_t nTotalBytesFreedFromAllTrees = 0;
    for (size_t i = 0; i < num_trees; i++)
        m_pMallocTreeRegistry[i]->collect_allocated_freed_recursively(
            &nTotalBytesAllocatedFromAllTrees, &nTotalBytesFreedFromAllTrees);

    switch (format) {
    case MTAG_OUTPUT_FORMAT_HUMANFRIENDLY_TREE: {
        stats_str += "Started profiling on " + std::string(tmStartProfilingStr) + "\n";
        stats_str += "This snapshot done on " + std::string(tmCurrentStr) + "\n";
        stats_str += "Process VmSize=" + GraphVizUtils::pretty_print_bytes(vmSizeNowBytes) + "\n";
        stats_str += "Process VmRSS=" + GraphVizUtils::pretty_print_bytes(vmRSSNowBytes) + "\n";

        size_t tot_tracked_mem_bytes = g_bytes_allocated_before_init;
        for (size_t i = 0; i < num_trees; i++) {
            m_pMallocTreeRegistry[i]->collect_stats_recursively(
                stats_str, format, output_options, nTotalBytesAllocatedFromAllTrees);
            tot_tracked_mem_bytes += m_pMallocTreeRegistry[i]->get_total_allocated_bytes_tracked();
        }
    } break;

    case MTAG_OUTPUT_FORMAT_JSON: {
        JsonUtils::start_document(stats_str);
        JsonUtils::append_field(stats_str, "PID", getpid());
        JsonUtils::append_field(stats_str, "tmStartProfiling", tmStartProfilingStr);
        JsonUtils::append_field(stats_str, "tmCurrentSnapshot", tmCurrentStr);

        size_t tot_tracked_mem_bytes = g_bytes_allocated_before_init;
        for (size_t i = 0; i < num_trees; i++) {
            m_pMallocTreeRegistry[i]->collect_stats_recursively(
                stats_str, format, output_options, nTotalBytesAllocatedFromAllTrees);
            tot_tracked_mem_bytes += m_pMallocTreeRegistry[i]->get_total_allocated_bytes_tracked();
            stats_str += ",";
        }

        JsonUtils::append_field(stats_str, "nBytesAllocBeforeInit", g_bytes_allocated_before_init);
        JsonUtils::append_field(stats_str, "nBytesMallocTagSelfUsage", get_total_memusage_in_bytes());

        // vmSizeNowBytes and nTotalTrackedBytes should be similar ideally. In practice
        // nTotalTrackedBytes>>vmSizeNowBytes because free() operations do not reduce the value of nTotalTrackedBytes
        // but they can potentially reduce vmSizeNowBytes
        JsonUtils::append_field(stats_str, "vmSizeNowBytes", vmSizeNowBytes);
        JsonUtils::append_field(stats_str, "vmRSSNowBytes", vmRSSNowBytes);
        JsonUtils::append_field(stats_str, "nTotalTrackedBytes", tot_tracked_mem_bytes, true /* is_last */);
        JsonUtils::end_document(stats_str);
    } break;

    case MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT: {
        // create a single unique graph for ALL threads/trees, named "MallocTree"
        GraphVizUtils::start_digraph(stats_str, "AllMallocTrees");

        // use labels to convey extra info:
        std::vector<std::string> labels;
        labels.push_back("Whole process stats");
        labels.push_back(
            "allocated_mem_before_malloctag_init=" + GraphVizUtils::pretty_print_bytes(g_bytes_allocated_before_init));
        labels.push_back(
            "allocated_mem_by_malloctag_itself=" + GraphVizUtils::pretty_print_bytes(get_total_memusage_in_bytes()));
        labels.push_back("allocated_mem=" + GraphVizUtils::pretty_print_bytes(nTotalBytesAllocatedFromAllTrees));
        labels.push_back("vm_size_now=" + GraphVizUtils::pretty_print_bytes(vmSizeNowBytes));
        labels.push_back("vm_rss_now=" + GraphVizUtils::pretty_print_bytes(vmRSSNowBytes));
        labels.push_back("malloctag_start_ts=" + std::string(tmStartProfilingStr));
        labels.push_back("this_snapshot_ts=" + std::string(tmCurrentStr));
        std::string mainNode = "Process " + std::to_string(getpid());
        GraphVizUtils::append_node(stats_str, mainNode, labels);

        size_t tot_tracked_mem_bytes = g_bytes_allocated_before_init;
        for (size_t i = 0; i < num_trees; i++) {
            m_pMallocTreeRegistry[i]->collect_stats_recursively(
                stats_str, format, output_options, nTotalBytesAllocatedFromAllTrees);
            tot_tracked_mem_bytes += m_pMallocTreeRegistry[i]->get_total_allocated_bytes_tracked();
            stats_str += "\n";

            float w = 0;
            if (nTotalBytesAllocatedFromAllTrees)
                w = 100 * m_pMallocTreeRegistry[i]->get_total_allocated_bytes() / nTotalBytesAllocatedFromAllTrees;
            char wstr[16];
            // ensure only 2 digits of accuracy:
            snprintf(wstr, 15, "w=%.2f%%", w);

            GraphVizUtils::append_edge(
                stats_str, mainNode, m_pMallocTreeRegistry[i]->get_graphviz_root_node_name(), wstr);
        }

        std::vector<std::string> legend;
        legend.push_back("Legend:");
        legend.push_back(
            "'w' stands for self-allocation-weight, defined as BYTES_ALLOCATED_BY_NODE / BYTES_ALLOCATED_BY_ALL_TREES");
        legend.push_back("Square box: indicates the root node of a MallocTree for an application thread");
        legend.push_back("Nodes having an high self-allocation-weight are drawn bigger and with dark shade of red");

        GraphVizUtils::end_digraph(stats_str, legend); // close the MallocTree
    } break;

    default:
        break;
    }
}

void MallocTreeRegistry::collect_stats_MAP(MallocTagStatMap_t& out)
{
    size_t num_trees = m_nMallocTrees.load();

    out[".nTrees"] = num_trees;
    for (size_t i = 0; i < num_trees; i++) {
        m_pMallocTreeRegistry[i]->collect_stats_recursively_MAP(out);
    }
}
