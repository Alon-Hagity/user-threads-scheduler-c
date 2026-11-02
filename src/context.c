#include "context.h"

#include <signal.h>
#include <string.h>

/* State shared between context_init() and the bootstrap handler. */
static sigjmp_buf *volatile target_env;
static volatile context_entry_t target_entry;
static sigjmp_buf caller_env;

/* Runs on the new stack. Save a resumable context there, then return control
 * to context_init(). When the thread is first scheduled, sigsetjmp() returns
 * a second time (non-zero) and we call the entry function. */
static void bootstrap_handler(int sig)
{
    (void)sig;
    context_entry_t entry = target_entry;

    /* savemask = 0: the mask inside a handler blocks SIGUSR2; the scheduler
     * restores the proper mask itself when it switches threads. */
    if (sigsetjmp(*target_env, 0) == 0)
        siglongjmp(caller_env, 1);

    entry();
}

int context_init(sigjmp_buf *env, char *stack, size_t stack_size, context_entry_t entry)
{
    stack_t new_stack, old_stack;
    struct sigaction action, old_action;
    sigset_t unblock_usr2, old_mask;

    if (env == NULL || stack == NULL || entry == NULL)
        return -1;

    new_stack.ss_sp = stack;
    new_stack.ss_size = stack_size;
    new_stack.ss_flags = 0;
    if (sigaltstack(&new_stack, &old_stack) != 0)
        return -1;

    memset(&action, 0, sizeof(action));
    action.sa_handler = bootstrap_handler;
    action.sa_flags = SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR2, &action, &old_action) != 0) {
        sigaltstack(&old_stack, NULL);
        return -1;
    }

    sigemptyset(&unblock_usr2);
    sigaddset(&unblock_usr2, SIGUSR2);
    sigprocmask(SIG_UNBLOCK, &unblock_usr2, &old_mask);

    target_env = env;
    target_entry = entry;

    if (sigsetjmp(caller_env, 1) == 0) {
        raise(SIGUSR2);
        return -1; /* not reached: the handler jumps back to caller_env */
    }

    /* Back on the original stack: restore everything we changed. */
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
    sigaction(SIGUSR2, &old_action, NULL);
    sigaltstack(&old_stack, NULL);
    return 0;
}
