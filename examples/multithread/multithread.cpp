/*
    malloc-tag MULTITHREAD example

    This is an example to show/test malloctag used against a multithreaded
    application using the popular pthread library
*/

#include "malloc_tag.h"
#include <iostream>
#include <map>
#include <set>
#include <string.h>
#include <sys/prctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define NUM_THREADS 2

void FuncA(int thread_id);
void FuncB(int thread_id);

void ThreadMain(int thread_id)
{
    std::string tName = std::string("exampleThr/") + std::to_string(thread_id);
    prctl(PR_SET_NAME, tName.c_str());

    std::cout << ("Hello world from " + tName + "\n") << std::flush;
    MallocTagScope noname("ThreadMain");

    FuncA(thread_id);
    malloc(5); // allocation done directly by this ThreadMain()
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

void NonInstrumentedThread()
{
    prctl(PR_SET_NAME, "NonInstrumented");
    std::set<std::string> letsConsumeMemory;

    for (size_t i = 0; i < 1000; i++)
        letsConsumeMemory.insert(std::string(100 + (rand() % 101), 'c'));
}

void FuncC()
{
    std::map<std::string, uint64_t> mytestmap;
    for (unsigned int i = 0; i < 300; i++)
        mytestmap["onemorekey" + std::to_string(i)] = i;
}

void ThreadMain2(int thread_id)
{
    std::string tName = std::string("exampleThr/") + std::to_string(thread_id);
    prctl(PR_SET_NAME, tName.c_str());

    std::cout << ("Hello world from " + tName + "\n") << std::flush;
    MallocTagScope noname("ThreadMain2");

    FuncB(thread_id);
    FuncC();
}

int main()
{
    // as soon as main() is entered AND BEFORE LAUNCHING THREADS, start malloc tag engine:
    MallocTagEngine::init();

    // launch a few mostly-identical threads
    std::cout << "Hello world from PID " << getpid() << std::endl;

    std::cout << "Now launching " << NUM_THREADS << " threads" << std::endl;
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.push_back(std::thread(ThreadMain, i));
    }

    std::cout << "Now launching a non-instrumented thread" << std::endl;
    threads.push_back(std::thread(NonInstrumentedThread));

    // wait till all threads are terminated:
    for (auto& th : threads) {
        th.join();
    }
    threads.clear();

    // launch one more dummy threads
    threads.push_back(std::thread(ThreadMain2, NUM_THREADS));

    // wait till all threads are terminated:
    for (auto& th : threads) {
        th.join();
    }
    threads.clear();

    // dump stats in both JSON and graphviz formats
    if (MallocTagEngine::write_stats_on_disk())
        std::cout << "Wrote malloctag stats on disk as " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << " and "
                  << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    std::cout << "Bye!" << std::endl;

    return 0;
}
