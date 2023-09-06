#include "malloc_tag.h"
#include <unistd.h> // for linux
#include <iostream>

void FuncA();
void FuncB();

void TopFunction()
{
    MallocTagScope noname("Top"); // call-site "Top"

    FuncA();
    malloc(5); // allocation done directly by this TopFunction()
    FuncB();
}

void FuncA()
{
    MallocTagScope noname("A"); // call-site "A"

    malloc(100);
    FuncB();
}

void FuncB()
{
    MallocTagScope noname("B"); // call-site "B"

    //malloc(100);
    new uint8_t[200];
}

int main()
{
    std::cout << "Hello world! Starting some dumb allocations to exercise the malloc_tag library" << std::endl;

    TopFunction();
    std::cout << malloc_collect_stats() << std::endl;

    std::cout << "Bye!" << std::endl;

    return 0;
}
