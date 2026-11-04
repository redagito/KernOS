#!/usr/bin/env python3
"""qmp-mice.py — ask QEMU what pointing devices exist (human-monitor
"info mice" through QMP). usage: qmp-mice.py <port>"""
import json
import socket
import sys

port = int(sys.argv[1])
s = socket.create_connection(("127.0.0.1", port))
s.settimeout(15)
f = s.makefile("rb")
f.readline()
s.sendall(b'{"execute":"qmp_capabilities"}\n')
f.readline()
s.sendall(json.dumps({"execute": "human-monitor-command",
                      "arguments": {"command-line": "info mice"}}).encode() + b"\n")
reply = json.loads(f.readline())
out = reply.get("return", "")
print(out)
