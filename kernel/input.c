#include <kernel/types.h>
#include <kernel/input.h>

/*
 * input.c — the event ring the IRQ handlers push and syscall nr 7
 * pulls (M6.1). Purely portable C: no ports, no scheduler include —
 * the same object text the host suite (tests/test_input.c) compiles.
 *
 * Serialisation: every producer runs inside an IRQ handler (IF off,
 * single core = no nesting between kbd IRQ1 and mouse IRQ12), the
 * consumer runs inside the syscall gate (IF off). So head/tail move
 * one hand at a time — plain ring indices, kbd-ring discipline.
 */

static volatile struct input_event ring[IN_RING_SIZE];
static volatile u8 head, tail;            /* == : empty; +1 mod: full */
static volatile u64 delivered, dropped;
static u32 seq;                           /* delivery counter (monotonic) */

static void emit(u8 type, u8 code, i16 value)
{
    u8 next = (u8)((head + 1) & (IN_RING_SIZE - 1));
    if (next == tail) {
        dropped++;                        /* named at pull/gates, never
                                             smuggled into seq */
        return;
    }
    volatile struct input_event *e = &ring[head];
    e->type  = type;
    e->code  = code;
    e->value = value;
    e->seq   = ++seq;
    head = next;
    delivered++;
}

void input_push_key(char c)
{
    emit(IN_EVT_KEY, (u8)c, 1);
}

void input_push_rel(i16 dx, i16 dy)
{
    emit(IN_EVT_RELX, 0, dx);
    emit(IN_EVT_RELY, 0, dy);
}

void input_push_buttons(u8 prev, u8 now)
{
    for (u8 b = 0; b < 8; ++b) {
        u8 bit = (u8)(1u << b);
        if ((prev & bit) != (now & bit))
            emit(IN_EVT_BTN, b, (now & bit) ? 1 : 0);
    }
}

int input_pull(struct input_event *out, int max)
{
    int n = 0;
    while (n < max && head != tail) {
        const volatile struct input_event *e = &ring[tail];
        out[n].type  = e->type;
        out[n].code  = e->code;
        out[n].value = e->value;
        out[n].seq   = e->seq;
        tail = (u8)((tail + 1) & (IN_RING_SIZE - 1));
        ++n;
    }
    return n;
}

u64 input_delivered(void) { return delivered; }
u64 input_dropped(void)  { return dropped; }
