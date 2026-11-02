# User-Level Threads in C

A preemptive user-level threading library for Linux. Threads are created, scheduled and switched entirely in user space: a virtual-time timer ends each quantum, and a round-robin scheduler switches contexts with `sigsetjmp` / `siglongjmp`.

## Features

- Up to 100 threads (including the main thread), each with its own 16 KB stack
- Preemptive round-robin scheduling with a configurable quantum (`ITIMER_VIRTUAL` / `SIGVTALRM`)
- Thread operations: spawn, terminate, block, resume and sleep (measured in quantums)
- Per-thread and global quantum counters
- A thread whose entry function returns is terminated automatically

## Design

- **Thread table:** a fixed array of thread control blocks (id, state, saved `sigjmp_buf`, quantum count, wake-up quantum, entry function) plus a fixed array of stacks. The thread id is the index of its slot, and the lowest free slot is reused.
- **States:** `READY`, `RUNNING` and `BLOCKED`. Explicit blocking and sleeping are tracked separately, so a thread that is both blocked and asleep becomes `READY` only after it is resumed *and* its sleep has ended.
- **READY queue:** a circular FIFO buffer. Stale entries for threads that are no longer ready are skipped when they are dequeued.
- **Scheduler (`schedule_next`):** runs on every timer tick and whenever the running thread blocks, sleeps or terminates. It increments the global quantum count, wakes sleepers whose time is up, moves the running thread to the back of the queue, picks the next ready thread, restarts the timer and switches contexts.
- **Critical sections:** library calls block `SIGVTALRM` with `sigprocmask` while they change shared state.
- **Context creation (`src/context.c`):** a new thread's first context is created portably, with no architecture-specific register manipulation. A signal is delivered on the new stack via `sigaltstack`, and the handler saves that context with `sigsetjmp` and jumps back. The first time the scheduler jumps to the saved context, the thread starts in a small wrapper that re-enables the timer signal and calls the user's function.

## API

```c
int uthread_init(int quantum_usecs);
int uthread_spawn(thread_entry_point entry_point);
int uthread_terminate(int tid);   /* tid 0 exits the process */
int uthread_block(int tid);
int uthread_resume(int tid);
int uthread_sleep(int num_quantums);
int uthread_get_tid(void);
int uthread_get_total_quantums(void);
int uthread_get_quantums(int tid);
```

See `include/uthreads.h` for full documentation.

## Build and run

Requires `gcc` and `make` on Linux (tested on x86-64).

```bash
make        # builds build/libuthreads.a and build/uthreads_demo
make run
make clean
```

## Example session

The demo runs three workers: A computes and then returns, B sleeps between steps, and C blocks itself until the main thread resumes it. The exact interleaving depends on CPU speed.

```
$ make run
[A tid=1] step 1 (total quantums 2)
[B tid=2] step 1, sleeping 3 quantums
[C tid=3] blocking myself until main resumes me
[B tid=2] step 2, sleeping 3 quantums
[A tid=1] step 2 (total quantums 9)
[main] resuming thread 3
[B tid=2] step 3, sleeping 3 quantums
[A tid=1] step 3 (total quantums 14)
[C] resumed at total quantum 15
[B] done
[A] done after 7 quantums
[main] all workers finished; total quantums 22, main ran 9
```

## Project layout

```
include/uthreads.h   public API
include/context.h    context-creation helper
src/uthreads.c       scheduler and thread management
src/context.c        portable creation of a new thread's first context
examples/demo.c      demo program
Makefile
```

## Authors

- Alon Hagity ([LinkedIn](https://www.linkedin.com/in/alon-hagity/))

<sub>Credits: Operating Systems, Reichman University, Spring 2026.</sub>
