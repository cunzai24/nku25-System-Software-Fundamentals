#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2511113

// schedule - pick the next runnable process and switch to it
void
schedule(void)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        current->need_resched = 0;

        // If current is idleproc (not in proc_list), scan the whole list
        // starting from the list head; otherwise start right after current
        // so that the processes are scheduled in a round-robin manner.
        list_entry_t *start = (current == idleproc) ? &proc_list
                                                    : &(current->list_link);
        list_entry_t *le = start;
        struct proc_struct *next = NULL;

        while ((le = list_next(le)) != start)
        {
            // never call le2proc on the list head
            if (le == &proc_list)
            {
                continue;
            }
            struct proc_struct *proc = le2proc(le, list_link);
            if (proc->state == PROC_RUNNABLE)
            {
                next = proc;
                break;
            }
        }

        if (next == NULL)
        {
            if (current != idleproc && current->state == PROC_RUNNABLE)
            {
                next = current;
            }
            else
            {
                next = idleproc;
            }
        }

        next->runs++;
        if (next != current)
        {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}


