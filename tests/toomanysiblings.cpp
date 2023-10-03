/*
    malloc-tag test of the corner case where we try to push too many SIBLING NODES into the
    same LEVEL of a MallocTree
*/

#include "malloc_tag.h"
#include <iostream>
#include <string.h>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#define DEBUG_UNIT_TEST 0

void TooManySiblings_thread()
{
    size_t max_nodes = MallocTagEngine::get_limit("max_tree_nodes");
    size_t sibling_limits = MallocTagEngine::get_limit("max_node_siblings");

    EXPECT_GT(max_nodes, sibling_limits);

    // push levels into the tree of this thread:
    {
        MallocTagScope noname("TooManySib");

        for (size_t i = 0; i < sibling_limits + 1; i++) {
            MallocTagScope m(("dummy" + std::to_string(i)).c_str());
        }
    }

    MallocTagStatMap_t mtag_stats = MallocTagEngine::collect_stats();
#if DEBUG_UNIT_TEST
    for (const auto& it : mtag_stats)
        std::cout << it.first << "=" << it.second << std::endl;
#endif

    // check that malloc-tag has correctly handled the "too many siblings" corner case
    // CHECK1: the malloc at level5 must end up accounted at level3 (last available level)
    std::string k = MallocTagEngine::get_stat_key_prefix_for_thread();
    EXPECT_EQ(
        mtag_stats[k + ".nPushNodeFailures"], 1); // we iterate to "sibling_limits + 1" so we should get 1 push fail

    // CHECK2: the last entry in the tree should be "dummy15", there should be no "dummy16"
    EXPECT_TRUE(mtag_stats.find(k + "unit_tests.TooManySib.dummy15.nBytesSelfAllocated") != mtag_stats.end());
    EXPECT_TRUE(mtag_stats.find(k + "unit_tests.TooManySib.dummy16.nBytesSelfAllocated") == mtag_stats.end());
}

TEST(MallocTagTestsuite, TooManySiblings)
{
    // to ensure each unit test is as isolated as possible, execute it in its own thread context,
    // so the malloc()s done by other unit tests will not interfere with the malloc tree of this unit test
    std::thread isolated_thread(TooManySiblings_thread);
    isolated_thread.join();
}
