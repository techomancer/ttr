/* shmain.c - calls into a shared library, for ttrdb's tests (the .ttrdb
 * scripts name its lines). */
#include <stdio.h>

int lib_add(int a, int b);
int lib_twice(int x);

int main(void)
{
    int s = lib_add(2, 3);
    int t = lib_twice(s);
    printf("%d %d\n", s, t);
    return t - 3;
}
