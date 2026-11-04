#!/usr/bin/env python3
"""qmp-mouse.py — inject relative PS/2 mouse motion via QMP.

usage: qmp-mouse.py <port> <moves> <dx> <dy> <delay_ms>

QEMU must run with -qmp tcp:127.0.0.1:<port>,server,nowait. Each move
is one input-send-event with a relative x and a relative y input event,
sep dl_, sent every delay_ms; replies are printed and any "error"
reply is a nonzero exit.
"""
import json
import socket
import sys
import time


def main() -> int:
    if len(sys.argv) != 6:
        sys.stderr.write(__doc__)
        return 2
    port, moves, dx, dy, delay = (int(sys.argv[1]), int(sys.argv[2]),
                                  int(sys.argv[3]), int(sys.argv[4]),
                                  int(sys.argv[5]))

    s = socket.create_connection(("127.0.0.1", port))
    s.settimeout(20)
    f = s.makefile("rb")
    if "QMP" not in json.loads(f.readline()):
        sys.stderr.write("not a QMP server\n")
        return 1
    s.sendall(b'{"execute":"qmp_capabilities"}\n')
    json.loads(f.readline())

    bad = 0
    for i in range(moves):
        cmd = {"execute": "input-send-event", "arguments": {"events": [
            {"type": "rel", "data": {"axis": "x", "value": dx}},
            {"type": "rel", "data": {"axis": "y", "value": dy}},
        ]}}
        s.sendall(json.dumps(cmd).encode() + b"\n")
        reply = json.loads(f.readline())
        if "error" in reply:
            sys.stderr.write("move %d error: %s\n" % (i, reply["error"]))
            bad += 1
        time.sleep(delay / 1000.0)
    s.close()
    print("qmp-mouse: %d moves of (%d,%d) sent, %d errors" %
          (moves, dx, dy, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
