#pragma once
#include <kernel/input.h>
#include <kernel/types.h>
#include <user/desktop.h>
int desktop_active(void);
void desktop_init(void);
void desktop_pump(void);
int desktop_owner(int tid, int parent);
int desktop_read(int tid, char *out, int max);
long desktop_call(int tid, long op, void *data, long arg);
int desktop_has_window(int tid, int parent);
void desktop_putc(int tid, int parent, char c, int mirror);
long desktop_term(int tid, int parent, long op, long col, long row);
void desktop_flush(void);
void desktop_exit(int tid);
