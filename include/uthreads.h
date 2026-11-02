#ifndef UTHREADS_H
#define UTHREADS_H

/** Maximum number of threads (including the main thread). */
#define MAX_THREAD_NUM 100

/** Stack size per thread in bytes (must be at least MINSIGSTKSZ). */
#define STACK_SIZE 16384

/**
 * @brief Function pointer type for a thread's entry point.
 *
 * Each thread's entry function must take no arguments and return void.
 */
typedef void (*thread_entry_point)(void);

/* ===================================================================== */
/*                           External Interface                          */
/* ===================================================================== */

/**
 * @brief Initializes the user-level thread library.
 *
 * This function must be called before any other thread library function.
 * It sets up internal data structures, initializes the main thread (tid == 0) as running,
 * and configures the timer for quantum management. The main thread uses the process's regular stack.
 *
 * @param quantum_usecs Length of a quantum in microseconds (must be positive).
 * @return 0 on success; -1 on error (e.g., if quantum_usecs is non-positive).
 */
int uthread_init(int quantum_usecs);

/**
 * @brief Creates a new thread.
 *
 * Allocates a new TCB and separate stack for the thread.
 * The thread is added to the end of the READY queue.
 * Calling this function with a NULL entry_point or exceeding MAX_THREAD_NUM is an error.
 * If the entry function returns, the thread is terminated automatically.
 *
 * @param entry_point Pointer to the thread's entry function (must not be NULL).
 * @return On success, returns the new thread's ID; on failure, returns -1.
 */
int uthread_spawn(thread_entry_point entry_point);

/**
 * @brief Terminates a thread.
 *
 * Terminates the thread with the specified tid and releases all resources allocated for it.
 * The thread is removed from all scheduling structures. If no thread with the given tid exists,
 * it is considered an error. Terminating the main thread (tid == 0) will terminate the entire process
 * (after releasing allocated resources).
 *
 * @param tid Thread ID to terminate.
 * @return 0 on success; -1 on error. (Note: if a thread terminates itself or if the main thread terminates,
 * the function does not return.)
 */
int uthread_terminate(int tid);

/**
 * @brief Blocks a thread.
 *
 * Moves the thread with the given tid into the BLOCKED state.
 * A blocked thread may later be resumed using uthread_resume.
 * It is an error to block a thread that does not exist or to block the main thread (tid == 0).
 * Blocking a thread that is already BLOCKED is a no-op.
 *
 * @param tid Thread ID to block.
 * @return 0 on success; -1 on error.
 */
int uthread_block(int tid);

/**
 * @brief Resumes a blocked thread.
 *
 * Moves a thread from the BLOCKED state to the READY state.
 * If the thread is already in RUNNING or READY state, this call has no effect.
 * It is an error if no thread with the given tid exists.
 *
 * @param tid Thread ID to resume.
 * @return 0 on success; -1 on error.
 */
int uthread_resume(int tid);

/**
 * @brief Puts the running thread to sleep.
 *
 * Blocks the currently running thread for a specified number of quantums.
 * The current quantum is not counted; sleeping begins with the next quantum.
 * After the sleep period expires, the thread is moved to the end of the READY queue.
 * It is an error for the main thread (tid == 0) to call this function.
 *
 * @param num_quantums Number of quantums to sleep.
 * @return 0 on success; -1 on error.
 */
int uthread_sleep(int num_quantums);

/**
 * @brief Returns the calling thread's ID.
 *
 * @return The thread ID of the calling thread.
 */
int uthread_get_tid(void);

/**
 * @brief Returns the total number of quantums since the library was initialized.
 *
 * The count starts at 1 immediately after uthread_init. Every new quantum, regardless of cause,
 * increments this counter.
 *
 * @return Total number of quantums.
 */
int uthread_get_total_quantums(void);

/**
 * @brief Returns the number of quantums the thread with the specified tid has run.
 *
 * For a thread in the RUNNING state, the current quantum is included.
 * An error is returned if no thread with the given tid exists.
 *
 * @param tid Thread ID.
 * @return Number of quantums for the specified thread; -1 on error.
 */
int uthread_get_quantums(int tid);

#endif /* UTHREADS_H */
