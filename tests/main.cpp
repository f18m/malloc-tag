/*
    malloc-tag unit test main
*/

#include "malloc_tag.h"

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // as soon as main() is entered, start malloc tag engine:
    MallocTagEngine::init(
        50 /* just 50 nodes in this test! */, 3 /* just 3 levels in this test! */, 1 /* 1sec of snapshot interval */);

    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();

    // dump stats in both JSON and graphviz formats... they might be useful to debug unit test failures
    if (MallocTagEngine::write_stats())
        std::cout << "Wrote malloctag stats on disk as " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << " and "
                  << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    // decomment this is you want to have the time to look at this process still running with e.g. "top"
    // sleep(100000);

    return ret;
}