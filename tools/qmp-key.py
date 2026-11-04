#!/usr/bin/env python3
"""qmp-key.py — send ONE monitor sendkey by name.
usage: qmp-key.py <monitor-port> <key>
human-monitor-command sendkey through QMP (single channel)."""
import json
import socket
import sys
import time

port, key = int(sys.argv[1]), sys.argv[2]
s = socket.create_connection(("127.0.0.1", port))
s.settimeout(15)
f = s.makefile("rb")
f.readline()
s.sendall(b'{"execute":"qmp_capabilities"}\n')
f.readline()
s.sendall(json.dumps({"execute": "human-monitor-command",
                      "arguments": {"command-line": "sendkey " + key}}).encode() + b"\n")
r = json.loads(f.readline())
time.sleep(0.3)
print(r)
