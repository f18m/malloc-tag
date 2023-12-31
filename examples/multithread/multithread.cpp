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
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define NUM_EXAMPLE_THREADS 2

void FuncA(int thread_id);
void FuncB(int thread_id);

void ExampleThread(int thread_id)
{
    std::string tName = std::string("ExampleThr/") + std::to_string(thread_id);
    prctl(PR_SET_NAME, tName.c_str());

    std::cout << ("Hello world from " + tName + "\n") << std::flush;
    MallocTagScope noname("ExampleThread");

    FuncA(thread_id);
    malloc(5); // allocation done directly by this ExampleThread()
    FuncB(thread_id);

    // decomment this is you want to have the time to look at this process still running with e.g. "top"
    // sleep(100000);
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

    // a std::map will trigger a "shower" of malloc() operations: see the "FuncB" malloc scope in results
    std::map<std::string, uint64_t> mytestmap;
    for (unsigned int i = 0; i < 1000 + thread_id * 1000; i++)
        mytestmap["onemorekey" + std::to_string(i)] = i;
}

void NonInstrumentedThread()
{
    prctl(PR_SET_NAME, "NonInstrThr");

    // a std::set will trigger a "shower" of malloc() operations: see the "FuncB" malloc scope in results
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

class YetAnotherThread {
public:
    static void MainEvLoop(int thread_id)
    {
        std::string tName = std::string("YetAnThr/") + std::to_string(thread_id);
        prctl(PR_SET_NAME, tName.c_str());

        std::cout << ("Hello world from " + tName + "\n") << std::flush;
        MallocTagScope noname("YetAnThr", __FUNCTION__);

        FuncB(thread_id);
        FuncC();

        // decomment this is you want to have the time to look at this process still running with e.g. "top"
        // sleep(100000);
    }
};

int main()
{
    // as soon as main() is entered AND BEFORE LAUNCHING THREADS, start malloc tag engine:
    MallocTagEngine::init();

    // launch a few mostly-identical threads
    std::cout << "Hello world from PID " << getpid() << std::endl;

    // see how VM is before starting secondary threads:
    std::cout << "** main THREAD VmSize: " << MallocTagEngine::get_linux_vmsize_in_bytes() << std::endl;

    std::cout << "Now launching " << NUM_EXAMPLE_THREADS << " threads" << std::endl;
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_EXAMPLE_THREADS; i++) {
        threads.push_back(std::thread(ExampleThread, i));
    }

    // see how VM is AFTER starting secondary threads:
    std::cout << "** main THREAD VmSize: " << MallocTagEngine::get_linux_vmsize_in_bytes() << std::endl;

    std::cout << "Now launching a non-instrumented thread" << std::endl;
    threads.push_back(std::thread(NonInstrumentedThread));
    // wait till all threads are terminated:
    for (auto& th : threads) {
        th.join();
    }
    threads.clear();

    // launch 2 more dummy threads
    for (int i = 0; i < NUM_EXAMPLE_THREADS; i++) {
        threads.push_back(std::thread(YetAnotherThread::MainEvLoop, i));
    }

    // wait till all threads are terminated:
    for (auto& th : threads) {
        th.join();
    }
    threads.clear();

    // see how VM is AFTER starting secondary threads:
    std::cout << "** main THREAD VmSize: " << MallocTagEngine::get_linux_vmsize_in_bytes() << std::endl;

    // dump stats in both JSON and graphviz formats
    if (MallocTagEngine::write_stats())
        std::cout << "Wrote malloctag stats on disk as " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << " and "
                  << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    // if you want to dig into the GNU libc malloc this can be useful:
    // std::cout << MallocTagEngine::malloc_info() << std::endl;

    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << "Human-friendly dump on stdout:" << std::endl;

    std::string bigstr = MallocTagEngine::collect_stats(MTAG_OUTPUT_FORMAT_HUMANFRIENDLY_TREE);
    std::cout << bigstr << std::endl;

    std::cout << std::endl;
    std::cout << "Multithread example says bye!" << std::endl;

    return 0;
}
