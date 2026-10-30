/* watch.c - writes to watch: the same value again, a bit field next to
 * another, an array, and a local until its function returns. */
struct flags {
    unsigned a : 3;
    unsigned b : 5;
};

int g_counter;
int g_same = 7;
struct flags g_flags;
long g_pair[2];

int count(int n)
{
    int total = 0;
    int i;
    for (i = 0; i < n; i++)
        total += i;
    return total;
}

int main(void)
{
    g_same = 7;
    g_counter = 1;
    g_flags.a = 2;
    g_flags.b = 9;
    g_pair[1] = 5;
    g_counter = count(4);
    return 0;
}
