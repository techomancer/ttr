/* crash.c - a program that crashes, for signals and backtraces. */
#include <stdio.h>

static int* g_null;

void poke(int depth)
{
    if (depth == 0)
        *g_null = 1;
    else
        poke(depth - 1);
}

int main(void)
{
    puts("crashing");
    poke(2);
    return 0;
}
