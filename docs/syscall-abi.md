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
| 3 | `exit`  | code                        | does not return               |
| 4 | `read`  | fd(0 = COM1 console in), buf, n | bytes read (can block), or -1 |
| 5 | `spawn` | name(8.1; ".ELF" assumed if absent) | new pid, or -1        |
| 6 | `wait`  | —                           | a child's exit code, or -1    |
| 7 | `input` | buf, max (≤ 64 events/call) | events drained now, or -1     |

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

M5.5 notes — the first IRREVERSIBLY blocking numbers, and the proof
that invariant 1 needed no grafting:

- `read` (4): fd 0 only (COM1 console in: the IRQ1 keyboard ring).
  Bytes are drained in batches (≤64 per call). EMPTY ring = the call
  PARKS the caller's captured frame on `SCHED_WQ_KBD`; the keyboard
  IRQ's push (empty → non-empty transition) flips the woken flag and
  the same gate stub iretq resumes the exact caller with its own
  IF restored. Non-empty = immediate return, so short reads are
  honest non-blocking behavior, never a sleep-loop.
- `spawn` (5): name → root-dir 8.3 lookup → FAT read → ELF64 loader
  builds the child's space → new thread with `ppid = caller`.
  Refusals (no such file, loader refusal, table full) return -1
  with NOTHING left behind.
- `wait` (6): collect a reaped child's exit code (per-tid
  `exited[]/exit_codes[]` tables outlive the thread record;
  collectible once). No children = -1. Children exist but none
  exited yet = park on `SCHED_WQ_CHILD`; the exit path wakes it.
- `exit` (3) gained the code+parent contract: records exit status,
  wakes WQ_CHILD parallels, thread becomes zombie until collected.
- Evidence discipline for all three: the gate emits named serial
  lines at boot (`kbd: IRQ1 armed`, `gate5: shell live/...`) and the
  shell round-trip is typed into a real QEMU by a committed driver
  (`tools/ksh-driver.ps1`); negative control = corrupted SHELL.ELF
  magic → `proc: elf refused - bad magic` + `gate5: shell load
  refused` + no prompt.

M6.1 notes — the per-event input channel:

- `input` (7): drains the kernel's ONE event ring (both devices feed
  it: translated keyboard keystrokes AND PS/2 mouse packet deltas and
  button transitions). NONBLOCKING by contract: `0` = nothing right
  now (the honest poll); batch cap 64 events/call, same bound shape as
  read's 64 bytes. A BLOCKING input variant, if the M6 GUI needs one,
  arrives as number 8+ per invariant 2 — never as flags on this one.
- The event record is the normative ABI shape too: fixed 8 bytes
  {type, code, value, seq}, types frozen as 1=KEY (code=ASCII post-
  translation, value=1), 2=RELX / 3=RELY (value=one packet's signed
  delta; one PS/2 packet = two ordered events), 4=BTN (code=button
  bit 0..2, value=0|1, TRANSITIONS ONLY - held buttons do not spam).
  `seq` is the kernel-owned monotonic delivery number: a full ring's
  drops are counted (`input: ... dropped=`), never smuggled into seq.
- Supplier code is portable (`kernel/input.c`, host-tested by
  tests/test_input.c to the drop boundary, including seq monotonicity
  across a flood); producer code lives in the kbd/mouse IRQ handlers;
  the consumer is this gate (all three hands run IF-off; single core
  serializes by construction, the kbd-ring discipline since M5.5).
- Evidence: u1's observatory phase (typed `run u1`) classifies the
  drained events — positive = `input: gate ok rx=.. ry=.. keys=..`
  with QMP-injected rel motion staged in the ring BEFORE spawn;
  negative = `input: idle - no rel motion after N polls (negative
  control ok)`; both exit 0 and the shell round-trip proof (M5.5
  gate) continues on the same `run u1` typing, unchanged.