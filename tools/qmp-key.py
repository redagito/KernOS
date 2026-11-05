#!/usr/bin/env python3
"""qmp-key.py — send one or more monitor sendkey names.
usage: qmp-key.py <monitor-port> <key> [key...]
human-monitor-command sendkey through QMP (single channel)."""
import json
import socket
import sys
import time

port = int(sys.argv[1])
keys = sys.argv[2:]
if not keys:
    sys.stderr.write(__doc__)
    sys.exit(2)

s = socket.create_connection(("127.0.0.1", port))
s.settimeout(15)
f = s.makefile("rb")
f.readline()
s.sendall(b'{"execute":"qmp_capabilities"}\n')
f.readline()
for key in keys:
    s.sendall(json.dumps({"execute": "human-monitor-command",
                          "arguments": {"command-line": "sendkey " + key}}).encode() + b"\n")
    json.loads(f.readline())
    time.sleep(0.05)
print("sent %d keys" % len(keys))
