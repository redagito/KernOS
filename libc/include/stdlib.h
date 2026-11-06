#pragma once

/*
 * stdlib.h - exit streets. _exit is the honest syscall; exit() is its
 * normal name (both are the same thing in v0: no flush, no cleanup.
 * There is nothing to flush yet - the kernel gate is the sole channel).
 */

void exit(int code) __attribute__((noreturn));
void _exit(int code) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
