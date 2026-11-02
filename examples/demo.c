/*
 * Demo: three threads share the CPU under preemptive round-robin scheduling.
 *  - worker A counts, then returns (and is terminated automatically)
 *  - worker B sleeps for a few quantums between steps
 *  - worker C blocks itself and is resumed by the main thread
 */
#include "uthreads.h"

#include <stdio.h>

#define QUANTUM_USECS 10000
#define STEPS 3

static volatile int finished = 0;
static int worker_c_tid = -1;

static void busy_wait(void)
{
    for (volatile long i = 0; i < 20000000L; i++)
        ;
}

static void worker_a(void)
{
    for (int step = 1; step <= STEPS; step++) {
        printf("[A tid=%d] step %d (total quantums %d)\n",
               uthread_get_tid(), step, uthread_get_total_quantums());
        busy_wait();
    }
    printf("[A] done after %d quantums\n", uthread_get_quantums(uthread_get_tid()));
    finished++;
}

static void worker_b(void)
{
    for (int step = 1; step <= STEPS; step++) {
        printf("[B tid=%d] step %d, sleeping 3 quantums\n", uthread_get_tid(), step);
        uthread_sleep(3);
    }
    printf("[B] done\n");
    finished++;
    uthread_terminate(uthread_get_tid());
}

static void worker_c(void)
{
    printf("[C tid=%d] blocking myself until main resumes me\n", uthread_get_tid());
    uthread_block(uthread_get_tid());
    printf("[C] resumed at total quantum %d\n", uthread_get_total_quantums());
    finished++;
}

int main(void)
{
    if (uthread_init(QUANTUM_USECS) != 0)
        return 1;

    uthread_spawn(worker_a);
    uthread_spawn(worker_b);
    worker_c_tid = uthread_spawn(worker_c);

    int resumed = 0;
    while (finished < 3) {
        if (!resumed && uthread_get_total_quantums() > 10) {
            printf("[main] resuming thread %d\n", worker_c_tid);
            uthread_resume(worker_c_tid);
            resumed = 1;
        }
        busy_wait();
    }
    printf("[main] all workers finished; total quantums %d, main ran %d\n",
           uthread_get_total_quantums(), uthread_get_quantums(0));
    uthread_terminate(0); /* exits the process */
    return 0;
}
