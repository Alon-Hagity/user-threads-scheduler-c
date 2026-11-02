#ifndef UTHREADS_CONTEXT_H
#define UTHREADS_CONTEXT_H

#include <setjmp.h>
#include <stddef.h>

/** Entry function of a new execution context. */
typedef void (*context_entry_t)(void);

/**
 * @brief Prepare a jump buffer that starts running @p entry on @p stack.
 *
 * After this call, siglongjmp(*env, 1) begins executing entry() on the given
 * stack. The technique is portable (no architecture-specific register
 * mangling): a signal handler is delivered on the new stack via sigaltstack(),
 * the handler saves its context with sigsetjmp() and returns control to the
 * caller; jumping to that saved context later starts the thread.
 *
 * @param env        Jump buffer to initialize.
 * @param stack      Base of the memory to use as the new stack.
 * @param stack_size Size of @p stack in bytes (at least MINSIGSTKSZ).
 * @param entry      Function to run; it must not return (call a terminate
 *                   routine instead).
 * @return 0 on success; -1 on failure.
 */
int context_init(sigjmp_buf *env, char *stack, size_t stack_size, context_entry_t entry);

#endif /* UTHREADS_CONTEXT_H */
