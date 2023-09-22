/*
    malloc-tag test of the corner case where we try to push too many LEVELS into a MallocTree
*/

#include "malloc_tag.h"
#include <iostream>
#include <string.h>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#define MALLOC_AT_LEVEL2 26
#define MALLOC_AT_LEVEL5 1999

void Level2();
void Level3();
void Level4();
void Level5();

void Level1()
{
    MallocTagScope noname("Level1");
    Level2();
}
void Level2()
{
    MallocTagScope noname("Level2");
    Level3();

    void* mypointer = malloc(MALLOC_AT_LEVEL2); // check this gets accounted on the correct scope
    free(mypointer);
}
void Level3()
{
    MallocTagScope noname("Level3");
    Level4();
}
void Level4()
{
    MallocTagScope noname("Level4");
    Level5();
}
void Level5()
{
    MallocTagScope noname("Level5");
    malloc(MALLOC_AT_LEVEL5); // this malloc will be accounted on the last-available scope, which is "Level3"
}

void TooManyLevels_thread()
{
    // push levels into the tree of this thread:
    Level1();

    MallocTagStatMap_t mtag_stats = MallocTagEngine::collect_stats();
    // decomment to debug this unit test:
    // for (const auto& it : mtag_stats)
    //    std::cout << it.first << "=" << it.second << std::endl;

    // check that malloc-tag has correctly handled the "too many tree levels" corner case

    // CHECK1: the malloc at level5 must end up accounted at level3 (last available level)
    std::string k = MallocTagEngine::get_stat_key_prefix_for_thread() + "unit_tests.Level1.Level2.Level3";
    EXPECT_EQ(mtag_stats[k + ".nCallsTo_malloc"], 1);
    EXPECT_GE(mtag_stats[k + ".nBytesSelfAllocated"], MALLOC_AT_LEVEL5);
    EXPECT_EQ(mtag_stats[k + ".nBytesSelfFreed"], 0 /* we produced a memleak no purpose */);

    // CHECK2: the malloc at level2 must end up correctly accounted at level2
    //         (this checks that all "pop" operations by failed MallocTagScope are correctly skipped)
    k = MallocTagEngine::get_stat_key_prefix_for_thread() + "unit_tests.Level1.Level2";
    EXPECT_EQ(mtag_stats[k + ".nCallsTo_malloc"], 1);
    EXPECT_GE(mtag_stats[k + ".nBytesSelfAllocated"], MALLOC_AT_LEVEL2);
    EXPECT_GE(mtag_stats[k + ".nBytesSelfFreed"], MALLOC_AT_LEVEL2);
}

TEST(MallocTagTestsuite, TooManyLevels)
{
    // to ensure each unit test is as isolated as possible, execute it in its own thread context,
    // so the malloc()s done by other unit tests will not interfere with the malloc tree of this unit test
    std::thread isolated_thread(TooManyLevels_thread);
    isolated_thread.join();
}
