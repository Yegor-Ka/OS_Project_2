User-Level Threading Library in C

Authors: Yegor Karaev, Ram Eliyahu Hamrani (https://github.com/RamHamrani)

A lightweight user-level threading library implemented in C as part of an Operating Systems project.
This library provides user-space concurrency using setjmp/longjmp, manual context switching, signal-based preemption, and full thread lifecycle management — all without relying on kernel threads.

This project demonstrates low-level systems programming, CPU context manipulation, memory management, and custom scheduling logic.

🚀 Features
✔ User-Level Thread Creation

Manually allocates a dedicated stack for each thread

Initializes thread context using setjmp

Begins execution at a user-defined start function

✔ Preemptive Scheduling

Implements a timer-based scheduler using SIGVTALRM

Each quantum triggers a context switch via signal handler

Round-robin or priority-based scheduling (depending on assignment spec)

✔ Full Thread Lifecycle

READY → RUNNING → BLOCKED → TERMINATED states

Controlled transitions and safe cleanup of resources

Thread blocking/unblocking supported via API functions

✔ Context Switching

Saves CPU state using setjmp

Restores registers & program counter using longjmp

Enables seamless switching between multiple user threads

✔ Memory Management

Each thread receives its own allocated stack

Proper stack pointer + program counter initialization

Cleanup performed on thread termination to avoid leaks
