/*
    malloc-tag MULTITHREAD example

    This is an example to show/test malloctag used against a multithreaded
    application using the popular pthread library
*/

#include "malloc_tag.h"
#include <iostream>
#include <string.h>
#include <sys/prctl.h>
#include <thread>
#include <unistd.h> // for linux
#include <vector>

#define NUM_THREADS 3

void FuncA();
void FuncB();

void TopFunction(int thread_id)
{
    std::string tName = std::string("exampleThr/") + std::to_string(thread_id);
    prctl(PR_SET_NAME, tName.c_str());

    std::cout << ("Hello world from " + tName + "\n") << std::flush;
    MallocTagScope noname("TopFunc");

    FuncA();
    malloc(5); // allocation done directly by this TopFunction()
    FuncB();
}

void FuncA()
{
    MallocTagScope noname("FuncA");

    malloc(100);
    FuncB();
}

void FuncB()
{
    MallocTagScope noname("FuncB");

    // malloc(100);
    new uint8_t[200];
}

int main()
{
    // as soon as main() is entered AND BEFORE LAUNCHING THREADS, start malloc tag engine:
    MallocTagEngine::init();

    // launch dummy threads
    std::cout << "Hello world!" << std::endl;
    std::cout << "Now launching " << NUM_THREADS << " threads" << std::endl;
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.push_back(std::thread(TopFunction, i));
    }
    for (auto& th : threads) {
        th.join();
    }

    std::cout << "VmRSS: " << MallocTagEngine::get_linux_rss_mem_usage_in_bytes() << "B" << std::endl;

    // dump stats in both JSON and graphviz formats
    if (MallocTagEngine::write_stats_on_disk(MTAG_OUTPUT_FORMAT_JSON))
        // output file is defined by env var MTAG_STATS_OUTPUT_JSON_ENV
        std::cout << "Wrote malloctag stats on file " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << std::endl;
    if (MallocTagEngine::write_stats_on_disk(MTAG_OUTPUT_FORMAT_GRAPHVIZ_DOT))
        // output file is defined by env var MTAG_STATS_OUTPUT_JSON_ENV
        std::cout << "Wrote malloctag stats on file " << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    std::cout << "Bye!" << std::endl;

    return 0;
}
