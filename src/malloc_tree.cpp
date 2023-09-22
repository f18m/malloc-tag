/*
 * malloc_tree.cpp
 *
 * Author: fmontorsi
 * Created: Aug 2023
 * License: Apache license
 *
 */

#include "private/malloc_tree.h"
#include "private/output_utils.h"
#include <sys/syscall.h>

#define UNLIKELY(x) __builtin_expect((x), 0)

//------------------------------------------------------------------------------
// MallocTree
//------------------------------------------------------------------------------

bool MallocTree::init(
    size_t max_tree_nodes, size_t max_tree_levels, bool is_owned_by_main_thread) // triggers some MEMORY ALLOCATION
{
    assert(m_pNodePool == nullptr);

    // initialize the memory pool of tree nodes
    m_pNodePool = fmpool_create(MallocTreeNode, max_tree_nodes);
    if (!m_pNodePool)
        return false;

    // init the root node:
    m_pRootNode = fmpool_get(MallocTreeNode, m_pNodePool);
    assert(m_pRootNode);
    m_nTreeNodesInUse++;

    // thread name is often not unique; indeed by default secondary threads inherit the same name of their
    // parent thread; it's up to the application to make use of prctl() or pthread_setname_np() to adopt a unique
    // thread name; for this reason we also save the thread ID (TID) which is garantueed to be unique.
    m_nThreadID = syscall(SYS_gettid);

    m_pRootNode->init(NULL, m_nThreadID); // this is the tree root node
    m_pRootNode->set_scope_name_to_threadname();

    // This MallocTree::init() API is invoked each time a malloc() is detected on a previously-unknown thread.
    // By the time that thread reaches the first malloc() point, the dynamic linker & glibc will have already
    // mmap()ed some memory and set the amount of virtual memory available to this thread.
    // So let's save how much memory has been allocated so far: this will help malloc-tag grand-total of tracked memory
    // usage to match the TOTAL VmSize of the whole process
    if (is_owned_by_main_thread) {
        // for main thread we can read the global VmSize of the whole process and consider that as a starting point.
        // By stracing any software you will see that by the time the first malloc() is invoked the glibc/dynamic-linker
        // they have already used mmap() a number of times to make room for dynamic libraries
        m_nVmSizeAtCreation = MallocTagEngine::get_linux_vmsize_in_bytes();
    } else {
        // for secondary threads we don't how much memory has been mmap()ed exactly by the pthreads library.
        // The problem is that
        // * Linux does not account memory on a per-thread basis
        // * pthread library is not using malloc() and instead relies on mmap() syscall directly
        // so we estimate the amount of un-tracked VM usage by taking the pthread stack size:
        pthread_attr_t attr;
        pthread_getattr_np(pthread_self(), &attr);
        pthread_attr_getstacksize(&attr, &m_nVmSizeAtCreation);
        pthread_attr_destroy(&attr);

// Now we've got yet another issue: by default glibc allocator will create a new arena for each new thread.
// Internet is full of people complaining about the high VIRT usage of glibc malloc() implementation for
// multithreading and that's due to the "liberal" use of per-thread arenas of malloc() allocator, which of
// course has been implemented for performance reasons but has the downside of increasing a lot the VIRT memory.
// See also https://siddhesh.in/posts/malloc-per-thread-arenas-in-glibc.html
// Glibc arena VIRT cost contributes an increase so large that sometimes the application layer malloc() are
// irrelevant compared to the amount of virtual memory requested by glibc malloc.
// Of course VIRT memory comes for free... only when the memory is really touched the Linux on-demand paging
// mechanism will provide a real physical page to back that virtual memory. But for malloc-tag purposes, it
// would be nice for the developer to see a sort of match between the VIRT memory and the malloc-tag-tracked
// memory. Since we cannot track the mmap() calls done by malloc itself for his own overhead (at least not in
// userspace) we estimate that. Stracing multithreaded applications on a few Linux RHEL8 systems and according
// to many others on Internet (see e.g. https://bugs.openjdk.org/browse/JDK-8193521), 128MB of virtual memory is
// a good estimate of what a glibc arena will consume. So we use it here as estimate of how much virt memory has
// "escaped" malloc-tag tracking:
#define GLIBC_PER_THREAD_ARENA_VIRT_MEMORY_SIZE_ESTIMATE (128 * 1000 * 1000)
        m_nVmSizeAtCreation += GLIBC_PER_THREAD_ARENA_VIRT_MEMORY_SIZE_ESTIMATE;
    }

    m_nTreeLevels = m_pRootNode->get_tree_level();
    m_nMaxTreeNodes = max_tree_nodes;
    m_nMaxTreeLevels = max_tree_levels;

    m_pCurrentNode = m_pRootNode;

    return true;
}

bool MallocTree::push_new_node(const char* name) // must be malloc-free
{
    if (UNLIKELY(m_pCurrentNode->get_tree_level() == m_nMaxTreeLevels)) {
        // reached max depth level... cannot push anymore
        m_nPushNodeFailures++;
        return false; // do not invoke pop_last_node() since this push has failed
    }

    // from this point onward, we need to be able to read the tree structure (our children)
    // and change the current-node pointer, so grab the lock:
    std::lock_guard<std::mutex> guard(m_lockTreeStructure);

    MallocTreeNode* n = m_pCurrentNode->get_child_by_name(name);
    if (n) {
        // this branch of the tree already exists, just move the cursor:
        m_pCurrentNode = n;
        return true;
    }

    // this branch of the tree needs to be created:
    n = fmpool_get(MallocTreeNode, m_pNodePool);
    if (UNLIKELY(!n)) {
        // memory pool is full... memory profiling results will be INCOMPLETE and possibly MISLEADING:
        m_nPushNodeFailures++;
        return false; // do not invoke pop_last_node() since this push has failed
    }

    m_nTreeNodesInUse++; // successfully obtained a new node from the mempool
    n->init(m_pCurrentNode, m_nThreadID);
    n->set_scope_name(name);
    if (!m_pCurrentNode->link_new_children(n)) {
        // failed to link current node: release node back to the pool
        m_nTreeNodesInUse--;
        fmpool_free(MallocTreeNode, n, m_pNodePool);

        // and record this failure:
        m_nPushNodeFailures++;
        return false; // do not invoke pop_last_node() since this push has failed
    }

    // new node ready, move the cursor:
    m_pCurrentNode = n;
    m_nTreeLevels = std::max(m_nTreeLevels, m_pCurrentNode->get_tree_level());

    return true;
}

void MallocTree::pop_last_node() // must be malloc-free
{
    std::lock_guard<std::mutex> guard(m_lockTreeStructure);

    m_pCurrentNode->track_node_leave();

    MallocTreeNode* n = m_pCurrentNode->get_parent();
    assert(n); // if n == NULL it means m_pCurrentNode is pointing to the tree root... cannot pop... this is a
               // logical mistake...
    m_pCurrentNode = n;
}

void MallocTree::collect_stats_recursively(
    std::string& out, MallocTagOutputFormat_e format, const std::string& output_options)
{
    // during the following tree traversal, we need the tree structure to be consistent across threads:
    std::lock_guard<std::mutex> guard(m_lockTreeStructure);

    // NOTE: order is important:

    // STEP1: compute "bytes total" across the whole tree
    m_pRootNode->compute_bytes_totals_recursively();

    // STEP2: compute node weigth across the whole tree:
    m_pRootNode->compute_node_weights_recursively(m_pRootNode->get_total_allocated_bytes());

    // now, till we hold the lock which garantuees that the total bytes / node weights just computed are still accurate,
    // do a last recursive walk to encode all stats in JSON/Graphviz/etc format:

    switch (format) {
    case MTAG_OUTPUT_FORMAT_HUMANFRIENDLY_TREE:
        out += "** Thread [" + m_pRootNode->get_node_name() + "] with TID=" + std::to_string(m_nThreadID) + "\n";
        if (m_nPushNodeFailures) {
            out += "  WARNING: NOT ENOUGH NODES AVAILABLE FOR THE FULL TREE, RESULTS WILL BE INACCURATE/MISLEADING\n";
            out += "  TreeNodesInUse/Max=" + std::to_string(m_nTreeNodesInUse) + "/" + std::to_string(m_nMaxTreeNodes);
        }

        m_pRootNode->collect_stats_recursively_HUMANFRIENDLY(out);
        break;

    case MTAG_OUTPUT_FORMAT_JSON:
        JsonUtils::start_object(out, "tree_for_TID" + std::to_string(m_nThreadID));

        JsonUtils::append_field(out, "TID", m_nThreadID);
        JsonUtils::append_field(out, "ThreadName", m_pRootNode->get_node_name());
        JsonUtils::append_field(out, "nTreeLevels", m_nTreeLevels);
        JsonUtils::append_field(out, "nTreeNodesInUse", m_nTreeNodesInUse);
        JsonUtils::append_field(out, "nMaxTreeNodes", m_nMaxTreeNodes);
        JsonUtils::append_field(out, "nPushNodeFailures", m_nPushNodeFailures);
        JsonUtils::append_field(out, "nFreeTrackingFailed", m_nFreeTrackingFailed);
        JsonUtils::append_field(out, "nVmSizeAtCreation", m_nVmSizeAtCreation); // in bytes

        m_pRootNode->collect_stats_recursively_JSON(out);

        JsonUtils::end_object(out);
        break;

    case MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT: {
        std::string graphviz_name = "TID" + std::to_string(m_nThreadID);

        // let's use the digraph/subgraph label to convey extra info about this MallocTree:
        std::vector<std::string> labels;
        labels.push_back("TID=" + std::to_string(m_nThreadID));
        labels.push_back("nPushNodeFailures=" + std::to_string(m_nPushNodeFailures));
        labels.push_back(
            "nTreeNodesInUse/Max=" + std::to_string(m_nTreeNodesInUse) + "/" + std::to_string(m_nMaxTreeNodes));

        if (output_options != MTAG_GRAPHVIZ_OPTION_UNIQUE_TREE) {
            // create one tree for each MallocTree:
            GraphVizUtils::start_digraph(out, graphviz_name, labels);
        } else {
            // create one subcluster for each MallocTree
            GraphVizUtils::start_subgraph(out, graphviz_name, labels);
        }

        m_pRootNode->collect_stats_recursively_GRAPHVIZDOT(out);
        GraphVizUtils::end_subgraph(out); // close this digraph/subgraph
    } break;

    default:
        break;
    }
}

void MallocTree::collect_stats_recursively_MAP(MallocTagStatMap_t& out)
{
    // during the following tree traversal, we need the tree structure to be consistent across threads:
    std::lock_guard<std::mutex> guard(m_lockTreeStructure);

    // provide a unique prefix to all keys contained in this tree:
    std::string thisNodeName = "tid" + std::to_string(m_nThreadID);

    // recurse starting from root node:
    m_pRootNode->collect_stats_recursively_MAP(out, thisNodeName /* root prefix */);

    // add general tree attributes to the map:
    out[thisNodeName + ":.nTreeNodesInUse"] = m_nTreeNodesInUse;
    out[thisNodeName + ":.nMaxTreeNodes"] = m_nMaxTreeNodes;
    out[thisNodeName + ":.nPushNodeFailures"] = m_nPushNodeFailures;
    out[thisNodeName + ":.nFreeTrackingFailed"] = m_nFreeTrackingFailed;
}
