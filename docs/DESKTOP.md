# Clean desktop

The graphical desktop replaces the earlier two fixed text bands. Notepad
and Console remain separate user processes. Desktop icons, window borders,
menus, dialogs, the dock and My files are rendered by the kernel window
manager. Compilation still happens in the guest through KCC.

## Harbor palette

| Role | Color | Purpose |
|---|---|---|
| Wallpaper | `#23384F` | Quiet ink blue behind applications |
| Shortcut area | `#2D4862` | Slightly lighter blue behind desktop icons |
| Window chrome | `#E6EDF0` | Cool frames separate applications from the wallpaper |
| Editor paper | `#FAF9F5` | Warm white reduces the stark contrast of pure white |
| Text | `#263C4C` | Dark blue ink on paper |
| Active accent | `#257D84` | Teal for the focused window, caret and primary buttons |
| Console | `#142535` / `#DCE9EC` | Navy with pale text |

Window focus uses a teal title-bar marker and a selected dock button.
The mouse has a dark outline and white interior so it remains visible on
both paper and the console. Color is paired with labels and placement.

## Working behavior

- Single-click Notepad, Console, My files or HELLO.C on the desktop.
- Drag window titles, resize from the bottom-right grip, minimize, maximize
  and restore. The dock restores hidden windows and changes keyboard focus.
- Notepad has File and Edit menus, a Save button, file/dirty-state title,
  line/column status, mouse selection, and an internal cut/copy/paste buffer.
- New and Save as use filename dialogs. Open also creates a new empty
  document when its valid filename does not exist. Names remain flat 8.3.
- Save/Discard/Cancel protects dirty documents. Replacing another file
  requires a separate Replace/Cancel confirmation. Failed saves keep edits.
- Closing Notepad hides its window and keeps its process/document resident;
  minimizing also preserves its document. Console `exit` hides its window.
  This first desktop has one instance of each app.
- My files enumerates the actual root directory, with paging and refresh
  when opened. It shows up to 96 root entries. Binary/oversize files are
  refused by Notepad. Directories, rename, delete and long filenames are
  outside this version.
- Arrow keys scroll the text viewport as the caret moves. Mouse-wheel
  scrolling, scrollbars, undo, shift-selection, Windows clipboard sharing,
  Unicode editing and dynamic app/window creation are not implemented.

## Ownership and rendering

`kernel/arch/x86_64/desktop.c` owns three windows, their order, focus,
per-window cell/selection buffers, event queues and pixel composition.
The kernel allocates and touches its software framebuffer before creating
the user address spaces, so shared page tables are ready in every process.
The link script rejects kernel data that would overlap the 4 MiB user area.

All desktop state changes happen with interrupts disabled in the syscall
gate. Keyboard and mouse IRQs enqueue events and wake waiters; they never
draw or mutate window state. The desktop consumes the ordered event ring,
drains the mirrored byte FIFO, and routes each key to the window focused
at delivery. Unfocused readers cannot drain another window's key queue.
Drag events coalesce; each app has a bounded 511-event effective queue.
Queued typing and Escape carry across the command-to-dialog transition.

Application painting changes its backing cells, not another window's
pixels. Damage rectangles reconstruct the wallpaper, overlapping windows,
menus and dock in order, then copy only changed pixels to the hardware
framebuffer. The pointer is suspended during that copy and composited last.
Console children inherit their parent's output window. Editor diagnostics
go to serial; its document uses the separate PAINT syscall.

## Build and acceptance

`scripts/build-native.ps1` builds all kernel/boot objects, libc, user apps,
TinyCC/KCC and image tools from source using Windows LLVM and Visual Studio
host libraries. It isolates intermediates under `build/native`, installs
system files while preserving user source, and updates the ordinary
launcher image. `-TestOnly` installs into a copy instead. Stop QEMU before
rebuilding its images. `scripts/run.ps1` runs native QEMU directly.

`scripts/test-native.ps1` runs the twelve C host suites. The desktop suite
checks overlapping/hidden contents, output inheritance, focus, menus,
dialogs, input bursts, resize bounds, selection styles and framebuffer bounds.

`node tools/desktop-native-test.mjs` drives a native headless QEMU through
PS/2 keys and mouse packets. It covers selection, menus, dirty-close and
overwrite cancellation, moving/resizing/maximizing/minimizing, icon restore,
in-guest compilation/execution, syntax-error recovery, variadic/float C,
byte-exact FAT readback, and reboot/reopen. Logs and disposable images stay
under `build/native`. No screen capture is used. Acceptance uses the
1024x768 video mode with the 8x16 font. Other resolutions and real hardware
remain unverified.

Verified on 2026-11-13: the complete native LLVM build succeeds; all twelve
host suites pass 238 checks. Native Windows QEMU passes the complete
desktop/compiler/two-boot acceptance. Evidence is in
`build/native/host-tests.log`, `desktop-acceptance.log`,
`desktop-native.log` and `desktop-native-reboot.log`. The tests also check
that an empty clipboard cannot erase selected text and that LP64 hexadecimal
formatting produces all 16 digits in an actual guest program.
