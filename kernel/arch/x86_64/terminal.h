#pragma once
#include <kernel/types.h>
void terminal_reset(int tid);
void terminal_putc(int tid, int parent, char c);
void terminal_paintc(int tid, int parent, char c);
long terminal_control(int tid, int parent, long op, long col, long row);
