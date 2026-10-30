/* dlmain.c - loads the test shared library with dlopen, for ttrdb's
 * tests of breakpoints in libraries loaded later. */
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    void* h = dlopen("libtdshlib.so", RTLD_NOW);
    int (*twice)(int);
    if (!h) {
        printf("dlopen: %s\n", dlerror());
        return 1;
    }
    twice = (int (*)(int))dlsym(h, "lib_twice");
    printf("%d\n", twice(argc + 3));
    return 7;
}
