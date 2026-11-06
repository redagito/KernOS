#!/usr/bin/env python3
"""qmp-click.py — inject one left-button press+release via QMP.

usage: qmp-click.py <port>

QEMU must run with -qmp tcp:127.0.0.1:<port>,server,nowait. The kernel
sees BTN events only on TRANSITIONS (M6.1 ABI), so a click is up AFTER
down; the pair lands as one press the focus hit-test consumes (M6.5:
a button press is a focus command). Any "error" reply is nonzero exit.
"""
import json
import socket
import sys


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    port = int(sys.argv[1])

    s = socket.create_connection(("127.0.0.1", port))
    s.settimeout(20)
    f = s.makefile("rb")
    if "QMP" not in json.loads(f.readline()):
        sys.stderr.write("not a QMP server\n")
        return 1
    s.sendall(b'{"execute":"qmp_capabilities"}\n')
    json.loads(f.readline())

    bad = 0
    for down in (True, False):
        cmd = {"execute": "input-send-event", "arguments": {"events": [
            {"type": "btn", "data": {"down": down, "button": "left"}},
        ]}}
        s.sendall(json.dumps(cmd).encode() + b"\n")
        reply = json.loads(f.readline())
        if "error" in reply:
            sys.stderr.write("btn %s error: %s\n" % (down, reply["error"]))
            bad += 1
    s.close()
    print("qmp-click: press+release sent, %d errors" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
