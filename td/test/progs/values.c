/* values.c - the program values.ttrdb reads variables in (the script
 * names its lines, so keep them where they are). */
#include <stdio.h>

struct pt {
    int x, y;
    char name[8];
    struct pt* next;
    unsigned flag : 3, kind : 5;
    signed int neg : 4;
};
enum color { RED, GREEN = 5, BLUE };
typedef struct pt Point;

int g_count = 3;
double g_d = 2.5;
const char* g_msg = "hello";
static Point g_pt = { 1, 2, "hi", 0, 5, 17, -3 };
int g_grid[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
union num { int i; float f; } g_num;

int sum(int n, Point* p)
{
    int i, s = 0;
    for (i = 0; i < n; i++) {
        int sq = i * i;
        s += sq + p->x;
    }
    return s;
}

int main(void)
{
    Point a = { 3, 4, "abc", &g_pt, 1, 2, 1 };
    enum color c = GREEN;
    float f = 1.5f;
    short sh = -7;
    unsigned char uc = 200;
    int arr[4] = { 10, 20, 30, 40 };
    int* ip = arr;
    g_num.f = 1.0f;
    printf("%d %d %g %d %d %d\n", sum(3, &a), (int)c, f, sh, uc, ip[2]);
    return 0;
}
