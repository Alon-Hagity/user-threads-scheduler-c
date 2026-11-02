/*
 * Preemptive user-level thread library. Threads are scheduled round-robin;
 * a virtual-time timer (SIGVTALRM) ends each quantum, and context switches
 * use sigsetjmp/siglongjmp. New thread stacks are set up by context.c.
 */
#include "uthreads.h"
#include "context.h"

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

/** Possible thread states. */
typedef enum {
    THREAD_UNUSED = 0, /**< Slot is unused. */
    THREAD_READY,      /**< Waiting in the READY queue. */
    THREAD_RUNNING,    /**< Currently executing. */
    THREAD_BLOCKED     /**< Blocked explicitly and/or sleeping. */
} thread_state_t;

/** Thread control block. */
typedef struct {
    int tid;                  /**< Thread identifier (index in the table). */
    thread_state_t state;     /**< Current state. */
    sigjmp_buf env;           /**< Saved execution context. */
    int quantums;             /**< Quantums this thread has run. */
    int sleep_until;          /**< Total-quantum count at which sleep ends (0 = not sleeping). */
    thread_entry_point entry; /**< User entry function. */
} thread_t;

static void schedule_next(void);
static void context_switch(thread_t *current, thread_t *next);
static void timer_handler(int signum);

static thread_t threads[MAX_THREAD_NUM];
static char stacks[MAX_THREAD_NUM][STACK_SIZE];

static int current_tid = -1;
static int total_quantums = 0;
static int quantum_length = 0;

static int ready_queue[MAX_THREAD_NUM];
static int explicitly_blocked[MAX_THREAD_NUM];
static int queue_head = 0;
static int queue_tail = 0;
static int queue_size = 0;

static sigset_t mask_set;

int uthread_init(int quantum_usecs){
    if (quantum_usecs <= 0) {
        fprintf(stderr, "thread library error: quantum_usecs must be positive\n");
        return -1;
    }

    total_quantums = 1;
    queue_head = 0;
    queue_tail = 0;
    queue_size = 0;
    quantum_length = quantum_usecs;
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        threads[i].state = THREAD_UNUSED;
        threads[i].quantums = 0;
        threads[i].sleep_until = 0;
        explicitly_blocked[i] = 0;
    }
    threads[0].tid = 0;
    threads[0].state = THREAD_RUNNING;
    current_tid = 0;
    threads[0].quantums = 1;

    /* Mask used to block the timer signal during critical sections. */
    sigemptyset(&mask_set);
    sigaddset(&mask_set, SIGVTALRM);

    struct sigaction sa;
    sa.sa_handler = timer_handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGVTALRM);
    sa.sa_flags = 0;
    if (sigaction(SIGVTALRM, &sa, NULL) == -1) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }

    /* Virtual timer: one tick per quantum of CPU time. */
    struct itimerval timer;
    timer.it_value.tv_sec = quantum_usecs / 1000000;
    timer.it_value.tv_usec = quantum_usecs % 1000000;
    timer.it_interval.tv_sec = quantum_usecs / 1000000;
    timer.it_interval.tv_usec = quantum_usecs % 1000000;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) == -1) {
        fprintf(stderr, "system error: setitimer failed\n");
        exit(1);
    }
    return 0;
}

static void enqueue(int tid) {
    if (queue_size == MAX_THREAD_NUM) {
        return;
    }
    ready_queue[queue_tail] = tid;
    queue_tail = (queue_tail + 1) % MAX_THREAD_NUM;
    queue_size++;
}

static int dequeue(void) {
    if (queue_size == 0) {
        return -1;
    }
    int tid = ready_queue[queue_head];
    queue_head = (queue_head + 1) % MAX_THREAD_NUM;
    queue_size--;
    return tid;
}

static int remove_from_queue(int tid) {
    int found = 0;
    for (int i = 0; i < queue_size; i++) {
        int index = (queue_head + i) % MAX_THREAD_NUM;
        if (ready_queue[index] == tid) {
            found = 1;
        }
        if (found) {
            int next_index = (index + 1) % MAX_THREAD_NUM;
            ready_queue[index] = ready_queue[next_index];
        }
    }
    if (found) {
        queue_tail = (queue_tail - 1 + MAX_THREAD_NUM) % MAX_THREAD_NUM;
        queue_size--;
    }
    return found;
}

static void setup_thread(int tid, char *stack);

static void block_mask(void) {
    if (sigprocmask(SIG_BLOCK, &mask_set, NULL) == -1) {
        fprintf(stderr, "system error: sigprocmask failed\n");
        exit(1);
    }
}

static void unblock_mask(void) {
    if (sigprocmask(SIG_UNBLOCK, &mask_set, NULL) == -1) {
        fprintf(stderr, "system error: sigprocmask failed\n");
        exit(1);
    }
}

int uthread_spawn(thread_entry_point entry_point){
    if (entry_point == NULL) {
        fprintf(stderr, "thread library error: entry_point cannot be NULL\n");
        return -1;
    }
    block_mask();
    int new_id = -1;
    for(int i = 0; i < MAX_THREAD_NUM; i++) {
        if (threads[i].state == THREAD_UNUSED) {
            new_id = i;
            break;
        }
    }
    if (new_id == -1) {
        fprintf(stderr, "thread library error: maximum thread limit reached\n");
        unblock_mask();
        return -1;
    }
    threads[new_id].tid = new_id;
    threads[new_id].state = THREAD_READY;
    threads[new_id].quantums = 0;
    threads[new_id].sleep_until = 0;
    threads[new_id].entry = entry_point;
    unblock_mask();
    setup_thread(new_id, stacks[new_id]);
    block_mask();
    enqueue(new_id);
    unblock_mask();
    return new_id;
}

int uthread_terminate(int tid) {
    block_mask();
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: thread does not exist\n");
        unblock_mask();
        return -1;
    }
    if (tid == 0) {
        for (int i = 0; i < MAX_THREAD_NUM; i++) {
            threads[i].state = THREAD_UNUSED;
            explicitly_blocked[i] = 0;
        }
        for (int i = 0; i < MAX_THREAD_NUM; i++) {
            remove_from_queue(i);
        }
        unblock_mask();
        exit(0);
    }
    threads[tid].state = THREAD_UNUSED;
    threads[tid].quantums = 0;
    threads[tid].sleep_until = 0;
    explicitly_blocked[tid] = 0;
    remove_from_queue(tid);
    if (tid == current_tid) {
        schedule_next();
    }
    unblock_mask();
    return 0;
}

int uthread_block(int tid){
    block_mask();
    if (tid == 0) {
        fprintf(stderr, "thread library error: cannot block main thread\n");
        unblock_mask();
        return -1;
    }
    else if (tid < 0 || tid >= MAX_THREAD_NUM) {
        fprintf(stderr, "thread library error: thread id does not exist\n");
        unblock_mask();
        return -1;
    }
    else if (threads[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: thread does not exist\n");
        unblock_mask();
        return -1;
    }
    // else if (threads[tid].state == THREAD_BLOCKED) {
    //     explicitly_blocked[tid] = 1;
    //     unblock_mask();
    //     return 0;
    // }

    else {
        threads[tid].state = THREAD_BLOCKED;
        explicitly_blocked[tid] = 1;
        if (tid == current_tid) {
            schedule_next();
        }
        remove_from_queue(tid);
        unblock_mask();
        return 0;
    }
}

static int is_in_queue(int tid) {
    for (int i = 0; i < queue_size; i++) {
        int index = (queue_head + i) % MAX_THREAD_NUM;
        if (ready_queue[index] == tid) {
            return 1;
        }
    }
    return 0;
}

int uthread_resume(int tid) {
    block_mask();
    if (tid < 0){
        fprintf(stderr, "thread library error: invalid tid\n");
        unblock_mask();
        return -1;
    }
    else if (tid >= MAX_THREAD_NUM) {
        fprintf(stderr, "thread library error: tid exceeds maximum limit\n");
        unblock_mask();
        return -1;
    }
    else if (threads[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: thread does not exist\n");
        unblock_mask();
        return -1;
    }
    else if (threads[tid].state == THREAD_RUNNING || threads[tid].state == THREAD_READY) {
        unblock_mask();
        return 0;
    }

    explicitly_blocked[tid] = 0;
    if (threads[tid].state == THREAD_BLOCKED && threads[tid].sleep_until <= total_quantums) {
        threads[tid].state = THREAD_READY;
        enqueue(tid);
    }
    unblock_mask();
    return 0;
}

int uthread_sleep(int num_quantums){
    block_mask();
    if (current_tid == 0) {
        fprintf(stderr, "thread library error: main thread cannot sleep\n");
        unblock_mask();
        return -1;
    }
    else if (num_quantums <= 0) {
        fprintf(stderr, "thread library error: num_quantums must be positive\n");
        unblock_mask();
        return -1;
    }
    threads[current_tid].sleep_until = total_quantums + num_quantums + 1;
    threads[current_tid].state = THREAD_BLOCKED;
    schedule_next();
    unblock_mask();
    return 0;
}

int uthread_get_tid(void) {
    return current_tid;
}

int uthread_get_total_quantums(void) {
    return total_quantums;
}

int uthread_get_quantums(int tid){
    if (tid < 0 || tid >= MAX_THREAD_NUM || threads[tid].state == THREAD_UNUSED){
        fprintf(stderr, "thread library error: thread does not exist\n");
        return -1;
    }
    return threads[tid].quantums;
}

static void reset_timer(void) {
    struct itimerval timer;
    timer.it_value.tv_sec = quantum_length / 1000000;
    timer.it_value.tv_usec = quantum_length % 1000000;
    timer.it_interval.tv_sec = quantum_length / 1000000;
    timer.it_interval.tv_usec = quantum_length % 1000000;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) == -1) {
        fprintf(stderr, "system error: setitimer failed\n");
        exit(1);
    }
 }

/**
 * @brief Scheduler: Selects the next thread to run.
 *
 * This function examines the READY queue and selects the next thread for execution.
 * It handles state transitions and triggers a context switch.
 */
static void schedule_next(void) {

    total_quantums++;
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (threads[i].state == THREAD_BLOCKED && threads[i].sleep_until > 0 && threads[i].sleep_until <= total_quantums) {
            threads[i].sleep_until = 0;
            if (explicitly_blocked[i] == 0) {
                threads[i].state = THREAD_READY;
                if (!is_in_queue(i)) {
                    enqueue(i);
                }
            }
        }
    }

    if (threads[current_tid].state == THREAD_RUNNING) {
        threads[current_tid].state = THREAD_READY;
        enqueue(current_tid);
    }

    int next_tid = -1;
    /* Skip stale entries of threads that are no longer READY. */
    while (queue_size > 0) {
        int temp_tid = dequeue();
        if (threads[temp_tid].state == THREAD_READY) {
            next_tid = temp_tid;
            break;
        }
    }

    if (next_tid == -1) {
        if (threads[current_tid].state == THREAD_READY) {
            next_tid = current_tid;
        }
        else {
            fprintf(stderr, "thread library warning: no ready threads to schedule\n");
            exit(1);
        }
    }

    int prev_id = current_tid;
    current_tid = next_tid;
    threads[current_tid].state = THREAD_RUNNING;
    threads[current_tid].quantums++;
    reset_timer();
    if (prev_id != current_tid) {
        context_switch(&threads[prev_id], &threads[current_tid]);
    }
}

/**
 * @brief Context switch helper.
 *
 * Uses sigsetjmp and siglongjmp to save the current thread's context and restore the context of the next thread.
 *
 * @param current Pointer to the current thread's TCB.
 * @param next Pointer to the next thread's TCB.
 */
static void context_switch(thread_t *current, thread_t *next) {
    int ret = sigsetjmp(current->env, 1);
    if (ret == 0) {
        siglongjmp(next->env, 1);
    }
}

/**
 * @brief Timer signal handler.
 *
 * Registered as the handler for timer signals, this function updates global quantum counters
 * and initiates a scheduling decision when a quantum expires.
 *
 * @param signum The signal number (e.g., SIGVTALRM).
 */
static void timer_handler(int signum) {
    (void)signum;
    block_mask();
    schedule_next();
    unblock_mask();
    return;
}

/* First code every spawned thread runs: re-enable the timer signal (it was
 * blocked by the scheduler that switched to us), run the user function, and
 * terminate the thread if that function ever returns. */
static void thread_start(void)
{
    unblock_mask();
    threads[current_tid].entry();
    uthread_terminate(current_tid);
}

/* Prepare the saved context of a new thread so it starts in thread_start(). */
static void setup_thread(int tid, char *stack)
{
    if (context_init(&threads[tid].env, stack, STACK_SIZE, thread_start) != 0) {
        fprintf(stderr, "system error: failed to set up thread context\n");
        exit(1);
    }
}
