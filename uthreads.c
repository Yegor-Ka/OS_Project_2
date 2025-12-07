/* uthreads.c - Completed implementation with comments */
#include "uthreads.h"
#include <assert.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>

// Architecture-dependent address translation for setjmp/longjmp
// Converts stack pointer and program counter to proper format
typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7
#define UNUSED(x) (void)(x)

address_t translate_address(address_t addr) {
    address_t ret;
    asm volatile("xor %%fs:0x30, %0\n"
                 "rol $0x11, %0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

// Global data structures
static int g_quantum_usecs = 0;                 // Duration of a quantum in microseconds
static int g_total_quantums = 0;                // Total number of quantums since init
static thread_t g_thread[MAX_THREAD_NUM];       // Thread control blocks
static thread_t *g_current = NULL;              // Pointer to currently running thread
static char g_stacks[MAX_THREAD_NUM][STACK_SIZE]; // Static stack space for threads
static int ready_queue[MAX_THREAD_NUM];         // Circular queue for READY threads
static int q_head = 0, q_tail = 0;               // Queue pointers

// Queue management helpers
static void enqueue(int tid) {
    ready_queue[q_tail] = tid;
    q_tail = (q_tail + 1) % MAX_THREAD_NUM;
}

static int dequeue() {
    if (q_head == q_tail) return -1;
    int tid = ready_queue[q_head];
    q_head = (q_head + 1) % MAX_THREAD_NUM;
    return tid;
}

static int is_ready_empty() {
    return q_head == q_tail;
}

// Block SIGVTALRM signal (used before modifying global state)
static void block_timer_signal(sigset_t *old_mask) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    if (sigprocmask(SIG_BLOCK, &set, old_mask) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
}

// Unblock SIGVTALRM signal
static void unblock_timer_signal(sigset_t *old_mask) {
    if (sigprocmask(SIG_SETMASK, old_mask, NULL) < 0) {
        fprintf(stderr, "system error: masking failed\n");
        exit(1);
    }
}

// Initializes thread context: stack pointer and program counter
void setup_thread(int tid, char *stack, thread_entry_point entry_point) {
    address_t sp = (address_t)stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t)entry_point;
    sigsetjmp(g_thread[tid].env, 1);
    g_thread[tid].env->__jmpbuf[JB_SP] = translate_address(sp);
    g_thread[tid].env->__jmpbuf[JB_PC] = translate_address(pc);
    sigemptyset(&g_thread[tid].env->__saved_mask);
}


void timer_handler(int signum);

// Initializes the thread library and starts the timer
int uthread_init(int quantum_usecs) {
    struct sigaction sa = {0}; // Declare and zero-initialize a sigaction struct for setting up the signal handler
    struct itimerval timer; // Declare a timer struct for configuring the virtual timer

    // Validate that the time slice is positive if not, print error
    if (quantum_usecs <= 0) {
        fprintf(stderr, "thread library error: quantum must be > 0\n");
        return -1;
    }

    sigset_t old_mask; // Variable to store the previous signal mask
    block_timer_signal(&old_mask); // Block SIGVTALRM while we set up to prevent races

    memset(g_thread, 0, sizeof(g_thread)); // Clear the global thread table (all TCBs) to a known state
    g_quantum_usecs    = quantum_usecs;  // Record the length of each quantum (in microseconds)
    g_total_quantums   = 1;  // Initialize total quantums count (main thread gets the first quantum)

    // Main thread TCB
    thread_t *main_tcb = &g_thread[0];  // Pointer to the TCB for the main thread (slot 0)
    main_tcb->tid         = 0;  // Assign thread ID 0 to the main thread
    main_tcb->state       = THREAD_RUNNING; // Mark the main thread as currently running
    main_tcb->quantums    = 1; // It has already consumed its first quantum
    main_tcb->sleep_until = 0; // Not sleeping—wake time = 0

    // **Capture the main context.  On a longjmp back here, sigsetjmp returns 1.**
    if (sigsetjmp(main_tcb->env, 1) == 1) {
        // resumed by scheduler → just return to main()
        unblock_timer_signal(&old_mask);
        return 0;
    }
    g_current = main_tcb;

    // Timer setup (SA_RESTART, SIGVTALRM → timer_handler)
    sa.sa_handler = timer_handler;  // Install our timer interrupt handler
    sa.sa_flags   = SA_RESTART;   // Restart interrupted system calls automatically
    sigemptyset(&sa.sa_mask);   // No additional signals blocked during handler execution
    sigaction(SIGVTALRM, &sa, NULL);  // Register handler for virtual timer alarms

    timer.it_value.tv_sec  = quantum_usecs / 1000000; 
    timer.it_value.tv_usec = quantum_usecs % 1000000;
    timer.it_interval      = timer.it_value;
    setitimer(ITIMER_VIRTUAL, &timer, NULL);  //Start timer

    unblock_timer_signal(&old_mask);
    return 0;
}


// Creates a new thread
int uthread_spawn(thread_entry_point entry_point) {

     // Check that the caller provided a valid function pointer, if not, print error
    if (!entry_point) {
        fprintf(stderr, "thread library error: entry point is NULL\n");
        return -1;
    }
    sigset_t old_mask;  // Variable to store the previous signal mask
    block_timer_signal(&old_mask); // Block SIGVTALRM while we set up to prevent races

    // Find free TID
    int tid = -1;  //Init tid to not found
    for (int i = 1; i < MAX_THREAD_NUM; ++i) {  //Skip main thread and seatch for a free slot
        if (g_thread[i].state == THREAD_UNUSED) {
            tid = i; // if found unused slot recerve it and stop searching
            break;
        }
    }
    if (tid == -1) { // if no free slot restore mask and print error
        fprintf(stderr, "thread library error: no available TID\n");
        unblock_timer_signal(&old_mask);
        return -1;
    }

    // Setup thread TCB
    thread_t *t = &g_thread[tid]; //Pointer to a new thread
    t->tid = tid; 
    t->state = THREAD_READY;
    t->quantums = 0;
    t->sleep_until = 0;
    t->entry = entry_point;
    setup_thread(tid, g_stacks[tid], entry_point); // Initialize its stack and context for first run
    enqueue(tid);   // Add the new thread to the ready queue

    unblock_timer_signal(&old_mask);
    return tid;
}

// Terminates thread (and exits process if main thread)
int uthread_terminate(int tid) {
    sigset_t old_mask;
    block_timer_signal(&old_mask);

    if (tid < 0 || tid >= MAX_THREAD_NUM || g_thread[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: invalid TID\n");
        unblock_timer_signal(&old_mask);
        return -1;
    }
    if (tid == 0) exit(0);

    if (g_thread[tid].state == THREAD_RUNNING && g_thread[tid].tid == g_current->tid) {
        g_thread[tid].state = THREAD_TERMINATED;
        schedule_next();
    }

    g_thread[tid].state = THREAD_UNUSED;
    unblock_timer_signal(&old_mask);
    return 0;
}

// Blocks a thread by TID
int uthread_block(int tid) {
    sigset_t old_mask;
    block_timer_signal(&old_mask);
    if (tid == 0 || tid < 0 || tid >= MAX_THREAD_NUM || g_thread[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: invalid block request\n");
        unblock_timer_signal(&old_mask);
        return -1;
    }
    if (g_thread[tid].state == THREAD_BLOCKED) {
        unblock_timer_signal(&old_mask);
        return 0;
    }
    if (tid == g_current->tid) {
        g_thread[tid].state = THREAD_BLOCKED;
        schedule_next();
    } else {
        g_thread[tid].state = THREAD_BLOCKED;
    }
    unblock_timer_signal(&old_mask);
    return 0;
}

// Resumes a blocked thread by TID
int uthread_resume(int tid) {
    sigset_t old_mask;
    block_timer_signal(&old_mask);
    if (tid < 0 || tid >= MAX_THREAD_NUM || g_thread[tid].state == THREAD_UNUSED) {
        fprintf(stderr, "thread library error: invalid resume request\n");
        unblock_timer_signal(&old_mask);
        return -1;
    }
    if (g_thread[tid].state == THREAD_BLOCKED) {
        g_thread[tid].state = THREAD_READY;
        enqueue(tid);
    }
    unblock_timer_signal(&old_mask);
    return 0;
}

// Puts current thread to sleep for num_quantums
int uthread_sleep(int num_quantums) {
    sigset_t old_mask;
    block_timer_signal(&old_mask);
    if (g_current->tid == 0) {
        fprintf(stderr, "thread library error: main thread cannot sleep\n");
        unblock_timer_signal(&old_mask);
        return -1;
    }
    g_current->sleep_until = g_total_quantums + num_quantums;
    g_current->state = THREAD_BLOCKED;
    schedule_next();
    unblock_timer_signal(&old_mask);
    return 0;
}

// Returns current thread ID
int uthread_get_tid() {
    return g_current ? g_current->tid : -1;
}

// Returns total quantum count
int uthread_get_total_quantums() {
    return g_total_quantums;
}

// Returns number of quantums executed by a specific thread
int uthread_get_quantums(int tid) {
    if (tid < 0 || tid >= MAX_THREAD_NUM || g_thread[tid].state == THREAD_UNUSED) {
        return -1;
    }
    return g_thread[tid].quantums;
}

// Saves context and switches to next thread
void context_switch(thread_t *current, thread_t *next) {
    g_current = next;
    if (sigsetjmp(current->env, 1) == 0) {
        siglongjmp(next->env, 1);
    }
}

// Round-robin scheduler: picks next ready or waking thread
void schedule_next(void) {
    int tid, start = q_head;
    thread_t *prev = g_current;

    // <-- NEW: wake up any sleepers whose sleep_until has elapsed
    for (int i = 1; i < MAX_THREAD_NUM; ++i) {
        if (g_thread[i].state == THREAD_BLOCKED
            && g_thread[i].sleep_until != 0
            && g_thread[i].sleep_until <= g_total_quantums)
        {
            g_thread[i].sleep_until = 0;
            g_thread[i].state       = THREAD_READY;
            enqueue(i);
        }
    }

    // now round-robin pick of READY threads
    while ((tid = dequeue()) != -1) {
        if (g_thread[tid].state == THREAD_READY) {
            g_thread[tid].state = THREAD_RUNNING;
            // re-enqueue previous only if it's still RUNNING
            if (prev->state == THREAD_RUNNING) {
                prev->state = THREAD_READY;
                enqueue(prev->tid);
            }
            context_switch(prev, &g_thread[tid]);
            return;
        }
        enqueue(tid);
        if (q_head == start) break;
    }

    // no other runnable thread -> keep running current
}


// Timer interrupt handler
void timer_handler(int signum) {
    UNUSED(signum);
    ++g_total_quantums;
    ++g_current->quantums;
    schedule_next();
}
