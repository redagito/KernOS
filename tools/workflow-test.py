#!/usr/bin/env python3
"""Exercise the real guest through PS/2 input; never captures a screen.

Run in the build container: python3 tools/workflow-test.py [--editor-only]
Uses a disposable copy of the development disk, then boots that copy twice.
"""
import argparse
import json
import pathlib
import shutil
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"


class VM:
    def __init__(self, disk, name):
        self.last_text = ""
        self.log = BUILD / (name + ".log")
        self.sockpath = BUILD / (name + ".qmp")
        self.sockpath.unlink(missing_ok=True)
        self.errors = open(BUILD / (name + ".stderr"), "w")
        self.proc = subprocess.Popen([
            "qemu-system-x86_64", "-m", "128", "-no-reboot",
            "-drive", f"format=raw,file={BUILD / 'kernos.img'},if=ide",
            "-device", "ich9-ahci,id=ahci",
            "-drive", f"if=none,id=files,format=raw,file={disk}",
            "-device", "ide-hd,drive=files,bus=ahci.0",
            "-serial", f"file:{self.log}", "-display", "none", "-monitor", "none",
            "-qmp", f"unix:{self.sockpath},server=on,wait=off",
        ], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=self.errors)
        deadline = time.monotonic() + 10
        while not self.sockpath.exists():
            if self.proc.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("QEMU did not create its control socket")
            time.sleep(.05)
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.connect(str(self.sockpath))
        self.stream = self.sock.makefile("rb")
        self.stream.readline()
        self.command("qmp_capabilities")
        try:
            self.wait("KernOS console |")
            self.wait("ksh> ")
            self.wait("edit: opened HELLO.C bytes=")
            self.key("ctrl-q")
            self.wait("edit: closed")
        except Exception:
            self.close()
            raise

    def command(self, name, args=None):
        request = {"execute": name}
        if args is not None:
            request["arguments"] = args
        self.sock.sendall(json.dumps(request).encode() + b"\n")
        while True:
            response = json.loads(self.stream.readline())
            if "error" in response:
                raise RuntimeError(response["error"])
            if "return" in response:
                return response["return"]

    def text(self):
        try:
            if self.log.exists():
                self.last_text = self.log.read_text(errors="replace").replace("\r", "")
        except OSError:
            pass  # Docker Desktop bind mounts can briefly report ENODATA.
        return self.last_text

    def wait(self, needle, after=0, timeout=60):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.text()
            if needle in text[after:]:
                return len(text)
            if needle == "ksh: compiler exit=0" and "ksh: compiler exit=1" in text[after:]:
                raise RuntimeError("guest compilation failed\n" + text[-2000:])
            if "EXCEPTION" in text[after:] or "PANIC" in text[after:] or "KernOS FAULT:" in text[after:]:
                raise RuntimeError(text[-3000:])
            if self.proc.poll() is not None:
                break
            time.sleep(.05)
        raise RuntimeError(f"timeout waiting for {needle!r}\n{self.text()[-2000:]}")

    def key(self, key):
        self.command("human-monitor-command", {"command-line": "sendkey " + key + " 10"})
        time.sleep(.045)

    def type(self, text):
        plain = {" ": "spc", "\n": "ret", ".": "dot", ",": "comma", "-": "minus",
                 "=": "equal", ";": "semicolon", "'": "apostrophe", "/": "slash",
                 "\\": "backslash", "[": "bracket_left", "]": "bracket_right"}
        shifted = dict(zip('!@#$%^&*()_+{}:"<>?~|',
                           ['1','2','3','4','5','6','7','8','9','0','minus','equal',
                            'bracket_left','bracket_right','semicolon','apostrophe',
                            'comma','dot','slash','grave_accent','backslash']))
        for ch in text:
            if ch in shifted:
                key = "shift-" + shifted[ch]
            elif ch.isupper():
                key = "shift-" + ch.lower()
            else:
                key = plain.get(ch, ch)
            self.key(key)

    def click(self, dy=0):
        # QMP uses screen coordinates, positive Y down. Split at signed-byte
        # packet limits, preserving every delta and the order of the click.
        while dy:
            step = max(-127, min(127, dy))
            self.command("input-send-event", {"events": [
                {"type": "rel", "data": {"axis": "y", "value": step}}]})
            dy -= step
            time.sleep(.03)
        for down in (True, False):
            self.command("input-send-event", {"events": [
                {"type": "btn", "data": {"button": "left", "down": down}}]})
            time.sleep(.05)

    def close(self):
        if self.proc.poll() is None:
            self.command("quit")
            self.proc.wait(timeout=10)
        self.stream.close()
        self.sock.close()
        self.errors.close()
        self.sockpath.unlink(missing_ok=True)


def disk_file(disk, name):
    """Independent FAT readback after QEMU exits; proves actual on-disk bytes."""
    with open(disk, "rb") as f:
        boot = f.read(512)
        u16 = lambda b, p: int.from_bytes(b[p:p+2], "little")
        u32 = lambda b, p: int.from_bytes(b[p:p+4], "little")
        reserved, nfats, fats, spc = u16(boot, 14), boot[16], u32(boot, 36), boot[13]
        data = reserved + nfats * fats
        def cluster(c):
            f.seek((data + (c - 2) * spc) * 512)
            return f.read(spc * 512)
        def next_cluster(c):
            f.seek(reserved * 512 + c * 4)
            return int.from_bytes(f.read(4), "little") & 0xfffffff
        base, _, ext = name.upper().partition(".")
        wanted = (base.ljust(8) + ext.ljust(3)).encode()
        c = u32(boot, 44)
        for _ in range(1000):
            entries = cluster(c)
            for p in range(0, len(entries), 32):
                e = entries[p:p+32]
                if e[:11] == wanted:
                    c = u16(e, 26) | u16(e, 20) << 16
                    size, result = u32(e, 28), bytearray()
                    while len(result) < size:
                        if not 2 <= c < 0xffffff8:
                            raise AssertionError("truncated saved file chain")
                        result += cluster(c)
                        c = next_cluster(c)
                    return bytes(result[:size])
            c = next_cluster(c)
            if c >= 0xffffff8:
                break
    raise AssertionError("saved file absent: " + name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor-only", action="store_true")
    args = parser.parse_args()
    disk = BUILD / "workflow-test.img"
    shutil.copyfile(BUILD / "ahcidisk.img", disk)
    source = '#include <stdio.h>\nint twice(int x) { return x * 2; }\nint main(void) { printf("hello %d\\n", twice(21)); return 7; }\n'
    vm = VM(disk, "workflow-first")
    try:
        vm.type("edit TEST.C\n")
        vm.wait("edit: opened TEST.C bytes=0")
        vm.type(source)
        vm.key("ctrl-s")
        vm.wait(f"edit: saved TEST.C bytes={len(source)}")
        if not args.editor_only:
            # Keep the editor open while the independent console compiles
            # and runs its saved source. Pointer starts in the lower pane.
            start = len(vm.text())
            vm.click()
            vm.wait("focus: rehome owner=3", after=start)
            start = len(vm.text())
            vm.type("cc TEST.C TEST.ELF\n")
            vm.wait("ksh: compiler exit=0", after=start)
            start = len(vm.text())
            vm.type("run TEST\n")
            vm.wait("hello 42", after=start)
            vm.wait("exited code=7", after=start)
            start = len(vm.text())
            vm.click(-635)
            vm.wait("focus: rehome owner=4", after=start)
            start = len(vm.text())
            vm.type("@\n")
            vm.key("ctrl-s")
            vm.wait(f"edit: saved TEST.C bytes={len(source) + 2}", after=start)
            vm.click(635)
            start = len(vm.text())
            vm.type("cc TEST.C\n")
            vm.wait("ksh: compiler exit=1", after=start)
            vm.wait("kcc: TEST.C:", after=start)
            # A failed compile must not replace the last good executable.
            start = len(vm.text())
            vm.type("run TEST\n")
            vm.wait("hello 42", after=start)
            vm.wait("exited code=7", after=start)
            start = len(vm.text())
            vm.click(-635)
            vm.wait("focus: rehome owner=4", after=start)
            vm.key("backspace")
            vm.key("backspace")
            vm.key("ctrl-s")
            vm.wait(f"edit: saved TEST.C bytes={len(source)}", after=start)
            vm.click(635)
            start = len(vm.text())
            vm.type("cc TEST.C\n")
            vm.wait("ksh: compiler exit=0", after=start)
            start = len(vm.text())
            vm.type("run TEST\n")
            vm.wait("hello 42", after=start)
            vm.wait("exited code=7", after=start)
            vm.click(-635)
            print("PASS: compiler diagnostic, previous executable retained, correction and recompile", flush=True)
        vm.key("ctrl-q")
        vm.wait("edit: closed")
        if not args.editor_only:
            matrix = ('#include <stdio.h>\n#include <stdarg.h>\n'
                      'int total(int n, ...) { va_list a; va_start(a, n); int s=0; '
                      'for(int i=0;i<n;i++) s+=va_arg(a,int); va_end(a); return s; }\n'
                      'struct item { int x; };\n'
                      'int main(void) { struct item a={3}; double x=1.25; double y=0x1.8p1; '
                      'printf("matrix %d %d %d\\n",a.x,total(3,2,4,6),(int)(x*4+y)); return 0; }\n')
            start = len(vm.text())
            vm.type("edit MATH.C\n")
            vm.wait("edit: opened MATH.C bytes=0", after=start)
            vm.type(matrix)
            vm.key("ctrl-s")
            vm.wait(f"edit: saved MATH.C bytes={len(matrix)}", after=start)
            vm.key("ctrl-q")
            vm.wait("edit: closed", after=start)
            start = len(vm.text())
            vm.type("cc MATH.C\n")
            vm.wait("ksh: compiler exit=0", after=start)
            start = len(vm.text())
            vm.type("run MATH\n")
            vm.wait("matrix 3 12 8", after=start)
            vm.wait("exited code=0", after=start)
            print("PASS: loops, structs, variadic functions, decimal and hex floating literals", flush=True)
    finally:
        vm.close()
    assert disk_file(disk, "TEST.C") == source.encode(), "saved source differs"
    print("PASS: typed source, editor save, byte-exact disk readback", flush=True)
    if not args.editor_only:
        assert disk_file(disk, "TEST.ELF").startswith(b"\x7fELF")
        print("PASS: compiled and ran C inside KernOS (output and exit code)", flush=True)
    vm = VM(disk, "workflow-reboot")
    try:
        vm.type("edit TEST.C\n")
        vm.wait(f"edit: opened TEST.C bytes={len(source)}")
        vm.key("ctrl-q")
        vm.wait("edit: closed")
    finally:
        vm.close()
    assert disk_file(disk, "TEST.C") == source.encode()
    print("PASS: reboot and reopen against the same persistent disk", flush=True)


if __name__ == "__main__":
    main()
