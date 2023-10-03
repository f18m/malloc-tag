/*
    malloc-tag test of the corner case where we try to push too many NODES into a MallocTree
*/

#include "malloc_tag.h"
#include <iostream>
#include <string.h>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#define DEBUG_UNIT_TEST 0
#define MALLOC_AT_LEVEL2 26
#define MALLOC_AT_LEVEL5 1999

void Push10Nodes(int prefix)
{
    for (unsigned int i = 0; i < 10; i++) {
        MallocTagScope m(("dummy" + std::to_string(prefix) + "/" + std::to_string(i)).c_str());
    }
}

void TooManyNodes_thread()
{
    size_t max_nodes = MallocTagEngine::get_limit("max_tree_nodes");
    EXPECT_GT(max_nodes, 0);

    // push nodes into the tree for this thread
    // but do that distributing the nodes in a 2-level hierarchy to avoid
    // - hitting the limit on max_siblings
    // - hitting the limit on max_tree_levels
    // since here we want to test the max_nodes limit
    for (unsigned int i = 0; i < max_nodes / 10; i++) {
        MallocTagScope m(("dummy" + std::to_string(i)).c_str());
        Push10Nodes(i);
    }

    MallocTagStatMap_t mtag_stats = MallocTagEngine::collect_stats();

#if DEBUG_UNIT_TEST
    for (const auto& it : mtag_stats)
        std::cout << it.first << "=" << it.second << std::endl;
#endif

    // check that malloc-tag has correctly handled the "too many nodes" corner case

    // CHECK1: number of reported nodes is exactly 10 (see main.cpp)
    std::string k = MallocTagEngine::get_stat_key_prefix_for_thread();
    EXPECT_EQ(mtag_stats[k + ".nTreeNodesInUse"], max_nodes);

    // CHECK2: the last entry in the tree should be "dummy4", there should be no "dummy5"
    EXPECT_TRUE(mtag_stats.find(k + "unit_tests.dummy4.nBytesSelfAllocated") != mtag_stats.end());
    EXPECT_TRUE(mtag_stats.find(k + "unit_tests.dummy5.nBytesSelfAllocated") == mtag_stats.end());
}

TEST(MallocTagTestsuite, TooManyNodes)
{
    // to ensure each unit test is as isolated as possible, execute it in its own thread context,
    // so the malloc()s done by other unit tests will not interfere with the malloc tree of this unit test
    std::thread isolated_thread(TooManyNodes_thread);
    isolated_thread.join();
}
