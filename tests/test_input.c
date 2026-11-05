/*
 * test_input.c - host unit tests for the portable event core.
 *
 * The kernel's kbd IRQ1 and mouse IRQ12 handlers push into the SAME
 * ring code compiled here (kernel/input.c). The host checks the
 * container contract: fixed 8-byte records, seq monotonicity across
 * everything delivered, drop accounting for a full ring (drops never
 * touch seq), SPSC pull semantics (batched drain, exact drain order,
 * ring wraparound), and the transition-only BTN contract.
 *
 * Exit code 0 means the whole suite passed.
 */
#include <stdio.h>
#include <string.h>
#include <kernel/input.h>

static int t_no = 0, n_fail = 0;

static void check(int cond, const char *title)
{
    ++t_no;
    printf("%2d. %-52s %s\n", t_no, title, cond ? "PASS" : "FAIL");
    if (!cond)
        ++n_fail;
}

static const unsigned char SENTINEL[8] = { 0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA };

int main(void)
{
    struct input_event ev[16];

    /* basics: one key event, seen exactly once, then empty */
    memset(ev, 0xAA, sizeof ev);
    input_push_key('a');
    check(input_pull(ev, 16) == 1, "one keyboard event drains once");
    check(ev[0].type == IN_EVT_KEY && ev[0].code == 'a' && ev[0].value == 1,
          "key event is type KEY code 'a' value 1");
    check(ev[0].seq == 1, "first delivered event has seq 1");
    check(input_pull(ev + 1, 16) == 0, "ring is empty after the drain");
    check(memcmp(&ev[1], SENTINEL, sizeof SENTINEL) == 0,
          "no garbage written past the batch");

    /* rel motion: one packet = ordered RELX+RELY pair */
    input_push_rel((i16)-7, (i16)15);
    check(input_pull(ev, 16) == 2, "one packet drains as two REL events");
    check(ev[0].type == IN_EVT_RELX && ev[0].value == -7 &&
          ev[1].type == IN_EVT_RELY && ev[1].value == 15,
          "REL pair is x then y with packet deltas");
    check(ev[0].seq == 2 && ev[1].seq == 3, "seq advances across the pair");

    /* button transitions only */
    input_push_buttons(0x00, 0x01);            /* left button down    */
    check(input_pull(ev, 16) == 1, "one pressed button = one BTN event");
    check(ev[0].type == IN_EVT_BTN && ev[0].code == 0 && ev[0].value == 1,
          "BTN event names bit 0 with press state");
    input_push_buttons(0x01, 0x01);            /* held: no spam       */
    check(input_pull(ev, 16) == 0, "held button emits nothing");
    input_push_buttons(0x01, 0x04);            /* bit0 up, bit2 down  */
    check(input_pull(ev, 16) == 2, "two transitions = two BTN events");
    check(ev[0].value == 0 && ev[1].value == 1, "release is 0, press is 1");

    /* batch drain honours max and keeps order across a manual fill */
    for (int i = 0; i < 5; ++i) input_push_key((char)('A' + i));
    check(input_pull(ev, 3) == 3, "pull honours the max batch size");
    check(ev[0].code == 'A' && ev[1].code == 'B' && ev[2].code == 'C',
          "batch preserves input order");
    check(input_pull(ev, 16) == 2 && ev[0].code == 'D' && ev[1].code == 'E',
          "second batch continues exactly where the first stopped");

    /* overflow honesty, then recovery */
    {
        u64 d0 = input_delivered();
        for (int i = 0; i < 200; ++i) input_push_key('q');
        check(input_delivered() == d0 + 127, "full ring accepts exactly 127 more");
        check(input_dropped() == 200 - 127, "refused events are counted, not hidden");

        int total = 0, mono = 1;
        u32 lastseq = 0;
        for (;;) {
            int g = input_pull(ev, 16);
            if (!g) break;
            for (int i = 0; i < g; ++i) {
                if (lastseq && ev[i].seq != lastseq + 1) mono = 0;
                lastseq = ev[i].seq;
            }
            total += g;
        }
        check(total == 127, "every event the flood delivered is pullable");
        check(mono, "seq advances by exactly 1 across the whole drain");
    }

    /* pair atomicity at the drop boundary (M5.10, F10): a packet is
       REFUSED AS A UNIT when only one slot is free - no half pairs. */
    {
        u64 d0, drops0;
        int total = 0, rels = 0, mono2 = 1;
        u32 lastseq;
        while (input_pull(ev, 16)) { }          /* ring empty */
        d0 = input_delivered();
        drops0 = input_dropped();
        for (int i = 0; i < 126; ++i) input_push_key('k');
        /* 126 occupants in a 127-capacity ring = exactly ONE slot free */
        check(input_delivered() == d0 + 126 && input_dropped() == drops0,
              "fill to one free slot delivers all 126");

        input_push_rel((i16)5, (i16)-5);         /* does NOT fit as a pair */
        check(input_delivered() == d0 + 126 &&
              input_dropped() == drops0 + 2,
              "one-slot boundary refuses the packet AS A UNIT (+2 dropped)");
        for (;;) {
            int g = input_pull(ev, 16);
            if (!g) break;
            if (total && ev[0].seq != lastseq + 1) mono2 = 0;
            lastseq = ev[g - 1].seq;             /* batch's LAST event   */
            total += g;
            for (int i = 0; i < g; ++i)
                if (ev[i].type == IN_EVT_RELX || ev[i].type == IN_EVT_RELY)
                    ++rels;
        }
        input_push_rel((i16)1, (i16)2);          /* recovery: it fits now */
        int pr = input_pull(ev, 2);
        check(total == 126 && rels == 0 && pr == 2 &&
              ev[0].type == IN_EVT_RELX && ev[1].type == IN_EVT_RELY && mono2,
              "no half pair in ring; seq intact; full pair passes again");
    }

    /* ABI shape locked: 8 bytes, and the trap types never move */
    check(sizeof(struct input_event) == 8, "input_event is 8 bytes");
    check(IN_EVT_KEY == 1 && IN_EVT_RELX == 2 && IN_EVT_RELY == 3 &&
          IN_EVT_BTN == 4, "event type codes are frozen");

    printf("%d checks, %d failures\n", t_no, n_fail);
    return n_fail ? 1 : 0;
}
