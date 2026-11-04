#pragma once

#include <kernel/types.h>

/*
 * The event core (M6.1): ONE ring both input devices feed, sized and
 * shaped for userland to pull through syscall nr 7 (docs/syscall-abi.md).
 * The portable center of gravity is here (kernel/input.c) exactly like
 * fat32.c / elf64.c: the host unit tests run the SAME code the IRQ
 * handlers and the gate run.
 *
 * Event discipline:
 *   - fixed 8-byte events {type, code, value, seq}: one motion or
 *     keypress is never split across records, and seq is a kernel-owned
 *     monotonic delivery number; dropped events (full ring) are counted
 *     in input_dropped(), never silently skipped in seq;
 *   - REL events carry one signed 16-bit delta each (x and y are two
 *     events - the PS/2 packet's two halves land in order);
 *   - KEY events are POST-translation keypresses only (the same ASCII
 *     the text channel saw); break codes and prefixes never enter;
 *   - BTN events fire on TRANSITIONS ONLY (a bit that state-changed
 *     between packets), so a held button does not spam the ring;
 *   - the ring is SPSC-by-serialization: handlers run IF-off and the
 *     consumer (syscall gate) runs IF-off, so plain indices suffice -
 *     the same discipline the kbd byte ring has used since M5.5.
 */

#define IN_RING_BITS 7                    /* 128 events                  */
#define IN_RING_SIZE (1u << IN_RING_BITS)

/* event types (v0 ABI: appended at 7, never reordered) */
#define IN_EVT_KEY  1     /* code = ASCII char, value = 1                */
#define IN_EVT_RELX 2     /* value = signed dx of one PS/2 packet        */
#define IN_EVT_RELY 3     /* value = signed dy of one PS/2 packet        */
#define IN_EVT_BTN  4     /* code = button bit 0..2, value = 0|1 (state) */

struct input_event {
    u8  type;
    u8  code;
    i16 value;
    u32 seq;
};

/* Consumer (IF-off): pull up to max events, return the count 0..max. */
int  input_pull(struct input_event *out, int max);

/* Producers (IRQ, IF-off). push_rel emits RELX+RELY in packet order. */
void input_push_key(char c);
void input_push_rel(i16 dx, i16 dy);
void input_push_buttons(u8 prev, u8 now);   /* transitions only      */

u64  input_delivered(void);   /* events that made it into the ring   */
u64  input_dropped(void);     /* events refused (full ring) - named  */
