/*
    malloc-tag MULTITHREAD example

    This is an example to show/test malloctag used against a multithreaded
    application using the popular pthread library
*/

#include "malloc_tag.h"
#include <iostream>
#include <map>
#include <string.h>
#include <sys/prctl.h>
#include <thread>
#include <vector>

#define NUM_THREADS 3

void FuncA(int thread_id);
void FuncB(int thread_id);

void TopFunction(int thread_id)
{
    std::string tName = std::string("exampleThr/") + std::to_string(thread_id);
    prctl(PR_SET_NAME, tName.c_str());

    std::cout << ("Hello world from " + tName + "\n") << std::flush;
    MallocTagScope noname("TopFunc");

    FuncA(thread_id);
    malloc(5); // allocation done directly by this TopFunction()
    FuncB(thread_id);
}

void FuncA(int thread_id)
{
    MallocTagScope noname("FuncA");

    // each thread allocates a slightly different memory, to make the example more "realistic"
    malloc(100 + thread_id * 1024);
    FuncB(thread_id);
}

void FuncB(int thread_id)
{
    MallocTagScope noname("FuncB");

    std::map<std::string, uint64_t> mytestmap;
    for (unsigned int i = 0; i < 1000 + thread_id * 1000; i++)
        mytestmap["onemorekey" + std::to_string(i)] = i;
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

    // wait till all threads are terminated:
    for (auto& th : threads) {
        th.join();
    }
    threads.clear();

    // launch more dummy threads
    for (int i = NUM_THREADS; i < NUM_THREADS * 2; i++) {
        threads.push_back(std::thread(TopFunction, i));
    }
    // wait till all threads are terminated:
    for (auto& th : threads) {
        th.join();
    }
    threads.clear();

    std::cout << "VmRSS: " << MallocTagEngine::get_linux_rss_mem_usage_in_bytes() << "B" << std::endl;

    // dump stats in both JSON and graphviz formats
    if (MallocTagEngine::write_stats_on_disk())
        std::cout << "Wrote malloctag stats on disk as " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << " and "
                  << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    std::cout << "Bye!" << std::endl;

    return 0;
}
