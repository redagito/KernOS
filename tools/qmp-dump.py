#!/usr/bin/env python3
"""qmp-dump.py — take a screendump of a running QEMU via QMP.

usage: qmp-dump.py <port> <filename> [ppm|png]

Connects to 127.0.0.1:<port> (QEMU must have been started with
-qmp tcp:127.0.0.1:<port>,server,nowait), negotiates capabilities and
issues a screendump. Exit 0 on {"return": {}}, nonzero otherwise.
"""
import json
import socket
import sys


def main() -> int:
    if len(sys.argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    port, filename = int(sys.argv[1]), sys.argv[2]
    fmt = sys.argv[3] if len(sys.argv) > 3 else "png"

    s = socket.create_connection(("127.0.0.1", port))
    s.settimeout(20)
    f = s.makefile("rb")

    greet = json.loads(f.readline())
    if "QMP" not in greet:
        sys.stderr.write("not a QMP server: %s\n" % greet)
        return 1

    s.sendall(b'{"execute":"qmp_capabilities"}\n')
    json.loads(f.readline())

    cmd = {"execute": "screendump", "arguments":
           {"filename": filename, "format": fmt}}
    s.sendall(json.dumps(cmd).encode() + b"\n")
    reply = json.loads(f.readline())
    s.close()

    if "error" in reply:
        sys.stderr.write("screendump error: %s\n" % reply["error"])
        return 1
    if "return" not in reply:
        sys.stderr.write("unexpected reply: %s\n" % reply)
        return 1
    print("screendump ok: %s (%s)" % (filename, fmt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
