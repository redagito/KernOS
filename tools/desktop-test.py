#!/usr/bin/env python3
"""Graphical desktop acceptance through PS/2 and serial, without capture.

Run after make image: python3 tools/desktop-test.py
Uses its own filesystem copy. The normal QEMU VBE mode is 1024x768.
"""
import shutil
from pathlib import Path
from runpy import run_path

workflow = run_path(str(Path(__file__).with_name("workflow-test.py")))
VM, BUILD, disk_file = (workflow[key] for key in ("VM", "BUILD", "disk_file"))


def main():
    disk = BUILD / "desktop-test.img"
    shutil.copyfile(BUILD / "ahcidisk.img", disk)
    vm = VM(disk, "desktop-first", keep_editor=True)
    try:
        vm.key("ctrl-n")
        vm.type("abcdef\n")
        vm.move_to(48, 200)  # select bcd in the document, not window chrome
        vm.button(True)
        vm.move_to(72, 200)
        vm.button(False)
        vm.key("ctrl-x")
        vm.key("ctrl-v")
        vm.key("ctrl-s")
        vm.type("UI.C\n")
        vm.wait("edit: saved UI.C bytes=7")
        print("PASS: mouse selection, cut/paste and Save dialog", flush=True)

        # File > New, followed by dirty close > Cancel.
        vm.click_at(50, 171)
        vm.click_at(60, 207)
        vm.type("protected\n")
        vm.click_at(699, 142)
        vm.key("esc")
        vm.key("ctrl-s")
        vm.type("GUARD.C\n")
        vm.wait("edit: saved GUARD.C bytes=10")
        print("PASS: menu New and canceled close retain unsaved text", flush=True)

        vm.click_at(50, 171)
        vm.click_at(60, 237)  # File > Open, existing filename preselected
        vm.type("UI.C\n")
        vm.wait("edit: opened UI.C bytes=7")

        before = len(vm.text())
        vm.move_to(200, 143)
        vm.button(True)
        vm.move_to(300, 183)
        vm.button(False)
        vm.wait("desktop: moved window", after=before)
        vm.move_to(812, 585)
        vm.button(True)
        vm.move_to(852, 625)
        vm.button(False)
        vm.wait("desktop: resized window", after=before)
        vm.click_at(808, 181)
        vm.wait("desktop: maximized window", after=before)
        vm.click_at(970, 28)
        vm.wait("desktop: restored window", after=before)
        vm.click_at(777, 181)  # minimize; icon restores the same document
        vm.wait("desktop: hidden Notepad", after=before)
        vm.click_at(58, 42)
        vm.key("ctrl-s")
        vm.wait("edit: saved UI.C bytes=7", after=before)
        print("PASS: drag, resize, maximize, minimize and icon restore", flush=True)

        vm.click_at(252, 42)
        vm.wait("desktop: focus My files")
        vm.click_at(360, 42)
        vm.wait("edit: opened HELLO.C bytes=")
        vm.key("ctrl-o")
        vm.type("UI.C\n")
        vm.wait("edit: opened UI.C bytes=7")

        before = len(vm.text())
        vm.key("ctrl-n")
        vm.type("different\n")
        vm.key("ctrl-s")
        vm.type("UI.C\n")  # existing file: explicitly cancel replacement
        vm.wait("desktop: dialog Replace", after=before)
        vm.key("esc")
        vm.wait("desktop: dialog canceled", after=before)
        vm.key("ctrl-s")
        vm.type("DIFF.C\n")
        vm.wait("edit: saved DIFF.C bytes=10", after=before)
        vm.key("ctrl-q")
        vm.wait("edit: closed", after=before)
        vm.click_at(570, 725)
        vm.type("echo desktop-console-ok\n")
        vm.wait("desktop-console-ok", after=before)
        print("PASS: icons, Open, overwrite cancellation and console focus", flush=True)
    finally:
        vm.close()
    assert disk_file(disk, "UI.C") == b"abcdef\n"
    assert disk_file(disk, "GUARD.C") == b"protected\n"
    assert disk_file(disk, "DIFF.C") == b"different\n"
    vm = VM(disk, "desktop-reboot")
    try:
        vm.type("edit UI.C\n")
        vm.wait("edit: opened UI.C bytes=7")
        vm.key("ctrl-q")
        vm.wait("edit: closed")
    finally:
        vm.close()
    print("PASS: byte-exact files and reboot persistence", flush=True)


if __name__ == "__main__":
    main()
