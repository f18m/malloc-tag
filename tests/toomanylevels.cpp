/*
    malloc-tag test of the corner case where we try to push too many LEVELS into a MallocTree
*/

#include "malloc_tag.h"
#include <iostream>
#include <string.h>
#include <unistd.h>
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

    malloc(MALLOC_AT_LEVEL2); // check this gets accounted on the correct scope
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

TEST(MallocTagTestsuite, TooManyLevels)
{
    int ret_code = 0;

    // as soon as main() is entered, start malloc tag engine:
    MallocTagEngine::init(MTAG_DEFAULT_MAX_TREE_NODES, 3 /* just 3 levels in this test! */);

    // run some dummy memory allocations
    Level1();

    MallocTagStatMap_t mtag_stats = MallocTagEngine::collect_stats();
    for (const auto& it : mtag_stats)
        std::cout << it.first << "=" << it.second << std::endl;

    // check that malloc-tag has correctly handled the "too many tree levels" corner case

    // CHECK1: the malloc at level5 must end up accounted at level3 (last available level)
    std::string key_for_inner_scope
        = MallocTagEngine::get_stat_key_prefix_for_thread() + "toomanylevels.Level1.Level2.Level3";
    EXPECT_EQ(mtag_stats[key_for_inner_scope + ".nCallsTo_malloc"], 1);
    EXPECT_GE(mtag_stats[key_for_inner_scope + ".nBytesSelf"], MALLOC_AT_LEVEL5);

    // CHECK2: the malloc at level2 must end up correctly accounted at level2
    //         (this checks that all "pop" operations by failed MallocTagScope are correctly skipped)
    key_for_inner_scope = MallocTagEngine::get_stat_key_prefix_for_thread() + "toomanylevels.Level1.Level2";
    if (mtag_stats[key_for_inner_scope + ".nCallsTo_malloc"] == 1
        && mtag_stats[key_for_inner_scope + ".nBytesSelf"] >= MALLOC_AT_LEVEL5)
        std::cout << "SUCCESS: level3 accounted for the malloc at level5" << std::endl;
    else {
        std::cout << "FAILURE in level3 accounting" << std::endl;
        ret_code = 1;
    }

    // dump stats in both JSON and graphviz formats
    if (MallocTagEngine::write_stats())
        std::cout << "Wrote malloctag stats on disk as " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << " and "
                  << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    // decomment this is you want to have the time to look at this process still running with e.g. "top"
    // sleep(100000);

    std::cout << "Bye!" << std::endl;
}
