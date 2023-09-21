/*
    malloc-tag test of the corner case where we try to push too many NODES into a MallocTree
*/

#include "malloc_tag.h"
#include <iostream>
#include <string.h>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#define MALLOC_AT_LEVEL2 26
#define MALLOC_AT_LEVEL5 1999

void TooManyNodes_thread()
{
    malloc(1); // FIXME: we need at least 1 malloc in the new thread to initialize malloc-tag (!!!) before first
               // MallocTagScope is created

    // push nodes into the tree for this thread
    for (unsigned int i = 0; i < 20; i++) {
        MallocTagScope m(("dummy" + std::to_string(i)).c_str());
    }

    MallocTagStatMap_t mtag_stats = MallocTagEngine::collect_stats();

    // decomment to debug this unit test:
    // for (const auto& it : mtag_stats)
    //    std::cout << it.first << "=" << it.second << std::endl;

    // check that malloc-tag has correctly handled the "too many nodes" corner case

    // CHECK1: number of reported nodes is exactly 10 (see main.cpp)
    std::string k = MallocTagEngine::get_stat_key_prefix_for_thread();
    EXPECT_EQ(mtag_stats[k + ".nTreeNodesInUse"], 10);

    // CHECK2: the last entry in the tree should be "dummy8", there should be no "dummy9"
    EXPECT_TRUE(mtag_stats.find(k + "unit_tests.dummy8.nBytesSelf") != mtag_stats.end());
    EXPECT_TRUE(mtag_stats.find(k + "unit_tests.dummy9.nBytesSelf") == mtag_stats.end());
}

TEST(MallocTagTestsuite, TooManyNodes)
{
    // to ensure each unit test is as isolated as possible, execute it in its own thread context,
    // so the malloc()s done by other unit tests will not interfere with the malloc tree of this unit test
    std::thread isolated_thread(TooManyNodes_thread);
    isolated_thread.join();
}
