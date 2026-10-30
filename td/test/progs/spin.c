/* spin.c - a program to attach to: three threads counting ticks, and
 * the first thread sleeping. */
#include <pthread.h>
#include <unistd.h>

volatile long g_ticks;

void tick(void)
{
    g_ticks = g_ticks + 1;
}

void* loop(void* arg)
{
    for (;;) {
        tick();
        usleep(1000);
    }
    return arg;
}

int main(void)
{
    pthread_t t[3];
    int i;
    for (i = 0; i < 3; i++)
        pthread_create(&t[i], 0, loop, 0);
    for (;;)
        sleep(1);
    return 0;
}
