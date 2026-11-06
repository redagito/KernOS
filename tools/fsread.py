#!/usr/bin/env python3
"""Extract a FAT root file for independent host-side inspection."""
import pathlib
import runpy
import sys
root = pathlib.Path(__file__).resolve().parent.parent
read_file = runpy.run_path(str(root / "tools/workflow-test.py"))["disk_file"]
disk, name, output = sys.argv[1:]
disk, output = pathlib.Path(disk).resolve(), pathlib.Path(output).resolve()
if not disk.is_relative_to(root) or not output.is_relative_to(root):
    raise SystemExit("disk and output must be inside the project")
output.write_bytes(read_file(disk, name))
