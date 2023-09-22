/*
    malloc-tag test of the snapshotting feature
*/

#include "malloc_tag.h"
#include <fstream>
#include <iostream>
#include <string.h>
#include <sys/time.h>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

bool file_exists(const char* fileName)
{
    std::ifstream infile(fileName);
    return infile.good();
}

void Snapshotting_thread()
{
    struct timeval start_tv;
    EXPECT_EQ(gettimeofday(&start_tv, NULL), 0);

    size_t nwritten = 0;
    while (nwritten < 3) {

        struct timeval now_tv;
        EXPECT_EQ(gettimeofday(&now_tv, NULL), 0);
        if ((now_tv.tv_sec - start_tv.tv_sec) > 10) {
            std::cout << "Failed test: after 10secs we still don't have 3 snapshots produced?" << std::endl;
            EXPECT_TRUE(false);
        }

        nwritten += MallocTagEngine::write_snapshot_if_needed(MTAG_OUTPUT_FORMAT_ALL, "/tmp/snapshot") ? 1 : 0;
        sleep(1);
    }

    EXPECT_EQ(file_exists("/tmp/snapshot.0000.dot"), true);
    EXPECT_EQ(file_exists("/tmp/snapshot.0001.dot"), true);
    EXPECT_EQ(file_exists("/tmp/snapshot.0002.dot"), true);
    EXPECT_EQ(file_exists("/tmp/snapshot.0000.json"), true);
    EXPECT_EQ(file_exists("/tmp/snapshot.0001.json"), true);
    EXPECT_EQ(file_exists("/tmp/snapshot.0002.json"), true);
}

TEST(MallocTagTestsuite, Snapshots)
{
    // to ensure each unit test is as isolated as possible, execute it in its own thread context,
    // so the malloc()s done by other unit tests will not interfere with the malloc tree of this unit test
    std::thread isolated_thread(Snapshotting_thread);
    isolated_thread.join();
}
