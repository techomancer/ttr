/* shlib.c - a shared library for ttrdb's tests. */
int lib_add(int a, int b)
{
    int r = a + b;
    return r;
}

int lib_twice(int x)
{
    return lib_add(x, x);
}
