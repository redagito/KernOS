# KernOS syscall ABI (v0, normative)

Userland talks to the kernel through one gate: `int 0x80`. This
document is the contract between kernels, libc and userland; a kernel
may refuse more numbers than it implements, but never fewer than it
claims, and never different semantics. The ABI version rides in the
table below — extending means appending, never reordering.

## Calling convention

| Item     | Rule                                                        |
|----------|-------------------------------------------------------------|
| Vector   | `int 0x80`, interrupt gate, **DPL 3** from v0 onward         |
| Number   | `RAX`                                                        |
| Args     | `RDI, RSI, RDX, R10, R8, R9` (arg1..6)                       |
| Return   | `RAX`: ≥ 0 = success value; < 0 = *negated* error code       |
| State    | `IF` is off for the duration of the call (interrupt gate)    |
| Clobber  | **Everything preserved except `RAX`.** `RSP` is the caller's |

Why `R10` carries arg4 (and `RCX`/`R11` are never argument slots): the
register order is the Linux `syscall`-instruction shape, kept from
*this* int-gate v0 so a later move to the `syscall` instruction —
which destroys `RCX`/`R11` — cannot re-shuffle argument positions.
Numbering, like everything here, is KernOS's own; nothing is
Linux-compatible by intent.

v0 rules beyond the table:

- Arity is per-number, fixed. Passing extra registers is allowed but
  unspecified (they must still survive the call).
- Pointer arguments are linear virtual addresses *in the caller's
  address space*. v0 runs with kernel-owned mappings for kernel
  callers; when CPL3 callers arrive (process model), the kernel is
  expected to validate that the claimed pages are mapped in the
  caller's CR3 world — that check is a debt on M5.2, not a license
  for userland to lie.
- Unknown numbers return `-1` (`EINVAL`-equivalent) silently; a
  kernel may log, but must not crash on them.
- Syscalls in v0 are non-preemptible (no tick inside the gate).
  Long-blocking syscalls arrive with the wait-queue work.

## v0 numbers

| # | Name    | Args (in order)             | Returns                       |
|---|---------|-----------------------------|-------------------------------|
| 0 | `probe` | arg1 (untouched)            | arg1 itself                   |
| 1 | `getpid`| —                           | current thread id             |
| 2 | `write` | fd(1 = COM1 console), buf, n| bytes written, or -1          |

v0 specifics worth stating:

- `probe` exists so the ABI is observable end-to-end (argument
  latching through the gate and back) with zero state.
- `pid == tid` in v0 — the kernel thread ids and process ids share a
  namespace until real processes exist (documented split point:
  M5.2). Thread 0 is the boot thread.
- `write` v0 knows exactly one fd: 1, the console (COM1). Other fds
  return -1. An out-of-line `buf` is the caller's crime and, with the
  pointer-validation debt still open, also the kernel's risk.

## Substrate invariants (why an async surface, if ever, needs no graft)

The ABI above is synchronous and stays that way; but the *substrate*
it runs on is designed so any future async family is an appendix in
already-prepared ground, never a bolt-on like a late io_uring:

1. **Resumability by construction.** A gate entry captures the
   caller's full frame (RSP/RIP/RFLAGS included) and the dispatcher
   touches only RAX. Therefore a syscall that cannot complete now can
   park that exact frame in a wait queue and be resumed through the
   same stub — no second mechanism, no ABI change. v0 never parks;
   M5.2's blocking syscalls become the first caller of a path that
   has existed since the gate was built.
2. **Append-only number space.** v0 numbers and their semantics are
   frozen forever. New capabilities (including async *variants* of an
   old sync number, if ever justified) arrive as NEW numbers beside
   the old ones; the old surface is never bent to match.
3. **The fd namespace is small integers, permanently.** 0/1 exist in
   v0 (console out). poll/eventfd/async-fd style families, if they
   arrive (M6 paint events, M7 sockets), ride the same namespace and
   the same "fd <-> kernel object with a wait-list" shape instead of
   inventing a parallel universe.

What this rules out, on purpose: retrofitting batching/rings onto
existing numbers, second entry points, and per-call flag words that
change a call's semantics. Sync is the model; async, if needed, is a
neighbor — designed to look like the one it neighbors.

## Arrival plan

| Kernel version | Caller                 | Notes                    |
|----------------|------------------------|--------------------------|
| v0 (M5.1)      | kernel threads         | gate machinery + gate self-test |
| M5.2 (landed)  | CPL3 processes         | pointer validation live, real pids, `exit` (nr 3) |
| M5.3+          | same, more numbers     | `open/read/close`, exec, mmap...  |

M5.2 notes: the substrate invariant held exactly as written - blocking-
shaped work needed NO new machinery, because the first non-returning
syscall (`exit`) simply parked the captured frame's thread in the
scheduler (zombie + swtch-away) through the ordinary paths. Pointer
validation at the gate: P|U walk over the caller's CR3 for CPL3
callers only (kernel-side v0 callers skip it).