/* demo.c - the program ttrdb's tests debug (the .ttrdb scripts name its
 * lines, so keep them where they are). */
#include <stdio.h>

int square(int x);

int fact(int n)
{
    if (n <= 1)
        return 1;
    return n * fact(n - 1);
}

int main(void)
{
    int a = square(3);
    int b = fact(4);
    printf("%d %d\n", a, b);
    return a + b - 26;
}
