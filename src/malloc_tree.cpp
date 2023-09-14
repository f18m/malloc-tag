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

#define UNLIKELY(x) __builtin_expect((x), 0)

//------------------------------------------------------------------------------
// MallocTree
//------------------------------------------------------------------------------

bool MallocTree::init(size_t max_tree_nodes, size_t max_tree_levels) // triggers some MEMORY ALLOCATION
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

    // IMPORTANT: thread name is often not unique; indeed by default secondary threads inherit the same name of their
    // parent
    //            thread; it's up to the application to make use of prctl() or pthread_setname_np() to adopt a unique
    //            thread name; for this reason we also save the thread ID (TID) which is garantueed to be unique.
    m_nThreadID = syscall(SYS_gettid);

    m_pRootNode->init(NULL, m_nThreadID); // this is the tree root node
    m_pRootNode->set_sitename_to_threadname();

    m_nTreeLevels = m_pRootNode->get_tree_level();
    m_nMaxTreeNodes = max_tree_nodes;
    m_nMaxTreeLevels = max_tree_levels;

    m_pCurrentNode = m_pRootNode;

    return true;
}

void MallocTree::push_new_node(const char* name) // must be malloc-free
{
    if (UNLIKELY(m_pCurrentNode->get_tree_level() == m_nMaxTreeLevels)) {
        // reached max depth level... cannot push anymore
        m_nPushNodeFailures++;
        m_bLastPushWasSuccessful = false;
        return;
    }

    // from this point onward, we need to be able to read the tree structure (our children)
    // and change the current-node pointer, so grab the lock:
    std::lock_guard<std::mutex> guard(m_lockTreeStructure);

    MallocTreeNode* n = m_pCurrentNode->get_child_by_name(name);
    if (n) {
        // this branch of the tree already exists, just move the cursor:
        m_pCurrentNode = n;
        m_bLastPushWasSuccessful = true;
        return;
    }

    // this branch of the tree needs to be created:
    n = fmpool_get(MallocTreeNode, m_pNodePool);
    if (UNLIKELY(!n)) {
        // memory pool is full... memory profiling results will be INCOMPLETE and possibly MISLEADING:
        m_nPushNodeFailures++;
        m_bLastPushWasSuccessful = false;
        return;
    }

    m_nTreeNodesInUse++; // successfully obtained a new node from the mempool
    n->init(m_pCurrentNode, m_nThreadID);
    n->set_sitename(name);
    if (!m_pCurrentNode->link_new_children(n)) {
        // failed to link current node: release node back to the pool
        m_nTreeNodesInUse--;
        fmpool_free(MallocTreeNode, n, m_pNodePool);

        // and record this failure:
        m_nPushNodeFailures++;
        m_bLastPushWasSuccessful = false;
        return;
    }

    // new node ready, move the cursor:
    m_pCurrentNode = n;
    m_bLastPushWasSuccessful = true;
    m_nTreeLevels = std::max(m_nTreeLevels, m_pCurrentNode->get_tree_level());
}

void MallocTree::pop_last_node() // must be malloc-free
{
    if (m_bLastPushWasSuccessful) {
        std::lock_guard<std::mutex> guard(m_lockTreeStructure);
        MallocTreeNode* n = m_pCurrentNode->get_parent();
        assert(n); // if n == NULL it means m_pCurrentNode is pointing to the tree root... cannot pop... this is a
                   // logical mistake...
        m_pCurrentNode = n;
    }
    // else: the node pointer has not been moved by last push_new_node() so we don't need to really pop the node
    // pointer
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
    m_pRootNode->compute_node_weights_recursively(m_pRootNode->get_total_bytes());

    // now, till we hold the lock which garantuees that the total bytes / node weights just computed are still accurate,
    // do a last recursive walk to encode all stats in JSON/Graphviz/etc format:

    switch (format) {
    case MTAG_OUTPUT_FORMAT_JSON:
        out += "\"tree_for_thread_" + m_pRootNode->get_node_name() + "\": {";
        out += "\"nTreeLevels\": " + std::to_string(m_nTreeLevels) + ",";
        out += "\"nTreeNodesInUse\": " + std::to_string(m_nTreeNodesInUse) + ",";
        out += "\"nMaxTreeNodes\": " + std::to_string(m_nMaxTreeNodes) + ",";
        out += "\"nPushNodeFailures\": " + std::to_string(m_nPushNodeFailures) + ",";
        m_pRootNode->collect_json_stats_recursively(out);
        out += "}";
        break;

    case MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT: {
        std::string graphviz_name = "TID" + std::to_string(m_pRootNode->get_tid());

        // let's use the digraph/subgraph label to convey extra info about this MallocTree:
        std::vector<std::string> labels;
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

        m_pRootNode->collect_graphviz_dot_output_recursively(out);
        out += "}\n"; // close this digraph/subgraph
    } break;

    default:
        break;
    }
}
