# Threads

A simple, bare-bones, user-space threading library with support for preemption written in C. 
Most of the interesting code is in [`thread.c`](thread.c) -- with [`thread_yield()`](thread.c#L317) being particularly fascinating. 
The tests are named `test_*.c`. To build all the tests, run: `make`

Disclaimer: This repo is a cleaned up archive of an assignment written (in|for) school. Please don't plagarize.
