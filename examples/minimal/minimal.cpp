/*
    malloc-tag MINIMAL example

    This is a basic example showing how to
    * initialize the malloctag engine using MallocTagEngine::init()
    * create malloc scopes using MallocTagScope
    * produce memory allocation stats using MallocTagEngine::write_stats()
*/

#include "malloc_tag.h"
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <stdint.h>

void FuncA();
void FuncB();
void FuncC();

void TopFunction()
{
    MallocTagScope noname("TopFunc"); // please account all mem allocs under the "TopFunc" name from this point onward

    FuncA();
    malloc(5); // allocation done directly by this TopFunction()... which results in a memleak...
    FuncB();
    FuncC();
}

void FuncA()
{
    MallocTagScope noname("FuncA"); // please account all mem allocs under the "FuncA" name from this point onward

    void* a = malloc(100);
    void* b = realloc(a, 200); // just show also realloc() is accounted for
    FuncB();

    free(b);
}

void FuncB()
{
    MallocTagScope noname("FuncB"); // please account all mem allocs under the "FuncB" name from this point onward

    // use "new" to demonstrate that we also hook "new"
    uint8_t* p = new uint8_t[500]; // new[] will count as "malloc"

    delete[] p; // delete[] will count as "free"
}

void FuncC()
{
    MallocTagScope noname("FuncC"); // please account all mem allocs under the "FuncC" name from this point onward

    // so far we just played with small memory allocations that will NOT likely
    // trigger any OS memory reclaim (through brk() or mmap() syscalls)...
    // now it's time to show what happens when requesting a sensible amount of memory, like 10MB:
    // in practice if you use gdb and break on e.g. mmap() or "strace -e trace=mmap" you will see that
    // the following malloc() triggers mmap() for roughly 10MB. Strace on my pc gives:
    //   mmap(NULL, 10002432, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7f43f4688000
    malloc(10000000);
}

int main()
{
    // as soon as main() is entered, start malloc tag engine:
    MallocTagEngine::init();

    // run some dummy memory allocations
    std::cout << "Hello world from PID " << getpid() << std::endl;
    std::cout << "Starting some dumb allocations to exercise the malloc_tag library" << std::endl;
    TopFunction();

    // dump stats in both JSON and graphviz formats
    if (MallocTagEngine::write_stats())
        std::cout << "Wrote malloctag stats on disk as " << getenv(MTAG_STATS_OUTPUT_JSON_ENV) << " and "
                  << getenv(MTAG_STATS_OUTPUT_GRAPHVIZDOT_ENV) << std::endl;

    // decomment this is you want to have the time to look at this process still running with e.g. "top"
    // sleep(100000);

    std::cout << "Bye!" << std::endl;

    return 0;
}
