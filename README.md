🧵 uthreads — User-Level Thread Library

Reichman University – Operating Systems – Spring 2025

👥 Authors

Yegor Karaev
Ram Eliyahu Hamrani — (https://github.com/RamHamrani)

📦 Project Overview

This project implements a full user-level threading library in C, supporting preemptive scheduling, context switching, sleep functionality, thread blocking, and a complete thread lifecycle.
The implementation uses:

sigsetjmp / siglongjmp for context switching

A virtual timer (SIGVTALRM) for preemptive quantum-based scheduling

Per-thread stacks

A ready queue, blocked queue, and TCB array

C23 standard, compiled with GCC-13 on Ubuntu 24.04 LTS

All core logic is implemented according to the assignment requirements and aligned with UNIX signal & timer behavior.

🛠 Compilation
gcc -std=c23 -Wall -Wextra -o uthreads uthreads.c


(Adjust filename if needed.)

🏁 Final Score

🥇 88 / 100
