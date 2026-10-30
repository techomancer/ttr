/* threads.c - threads for the debugger: two workers that take turns at
 * work() once both are running, and the first thread waiting for them.
 * They wait in usleep, so each keeps a kernel thread of its own (IRIX's
 * libpthread runs pthreads that do not block on fewer). */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

volatile int g_started;
volatile int g_done;
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

int work(int n)
{
    int sum = n * 10;
    sum += n;
    pthread_mutex_lock(&g_lock);
    g_done = g_done + 1;
    pthread_mutex_unlock(&g_lock);
    return sum;
}

void* worker(void* arg)
{
    int n = (int)(long)arg;
    pthread_mutex_lock(&g_lock);
    g_started = g_started + 1;
    pthread_mutex_unlock(&g_lock);
    while (g_started < 2)
        usleep(1000);
    while (g_done != n - 1)
        usleep(1000);
    return (void*)(long)work(n);
}

int main(void)
{
    pthread_t t[2];
    long r = 0;
    void* v;
    int i;
    for (i = 0; i < 2; i++)
        pthread_create(&t[i], 0, worker, (void*)(long)(i + 1));
    while (g_done < 2)
        usleep(1000);
    for (i = 0; i < 2; i++) {
        pthread_join(t[i], &v);
        r += (long)v;
    }
    printf("%ld %d\n", r, g_done);
    return 0;
}
