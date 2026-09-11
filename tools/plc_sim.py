#!/usr/bin/env python3
"""Stand in for the PLC so the braille reader can be exercised without one.

The firmware is a MODBUS TCP *client*: it connects out to the PLC and polls a
fixed block of 11 coils with function code 0x01. That means a bench session
needs something listening on the PLC's address, or the device never gets past
its CONNECTING state and there is nothing to read.

This is that something. Standard library only, so it runs on a bench machine
with nothing installed.

There are exactly two commands in the normal flow. Run them in this order:

    python3 tools/verify_plc_sim.py        # 1. is this simulator correct?
    sudo python3 tools/plc_sim.py --toggle 7   # 2. stand in for the PLC

The first needs no privileges, no network and nothing installed: it starts this
script on a loopback port by itself, drives it with frames built exactly as
App/Src/modbus_tcp.c builds them, and checks the replies in the same order
modbus_parse_read_coils() checks them. Run it before believing anything this
simulator says about the device. The firmware's parser is strict -- it rejects a
mismatched transaction id, an MBAP length that disagrees with the bytes that
arrived, the wrong unit id, and a byte count inconsistent with the quantity --
so a bug in here reaches the bench wearing the firmware's clothes.

The second needs 10.0.0.204 assigned to the wired interface first:

    sudo ip addr add 10.0.0.204/24 dev eno1    # substitute your interface

Port 503 is privileged, hence sudo. It is 503 and not the MODBUS default of 502
because that is what this installation's PLC uses.

--bind and --port exist for the self-test and for benches that cannot use the
real addressing. **You do not type either one in the normal flow** -- the
defaults already match what the firmware is configured to look for, and
verify_plc_sim.py passes its own when it needs them.

The frame layout mirrors docs/09-modbus-tcp-and-plc-link.md and the firmware's
codec in App/Src/modbus_tcp.c. The full bench procedure, including how to tell
whether the board is on the network at all before any of this matters, is
docs/11-hardware-bring-up.md steps 1.5 and 1.6.
"""

import argparse
import socket
import struct
import sys
import threading
import time

# --- Wire format ------------------------------------------------------------
# A MODBUS TCP frame is a 7-byte MBAP header -- transaction id, protocol id,
# length, unit id -- followed by the PDU.
MBAP_LEN = 7
PROTOCOL_ID = 0
FC_READ_COILS = 0x01
EXCEPTION_FLAG = 0x80
EXC_ILLEGAL_FUNCTION = 0x01
EXC_ILLEGAL_DATA_ADDRESS = 0x02

# The MBAP length field counts everything from the unit id onward.
LENGTH_FIELD_OVERHEAD = 1  # the unit id itself
EXCEPTION_PDU_LEN = 2      # function code + exception code

# --- The package this installation serves -----------------------------------
# Order and names come from App/Src/plc_packet.c. They are logged alongside the
# raw bytes so a reader at the cell can be correlated with what went out.
DEFAULT_COIL_COUNT = 11
FIELD_LABELS = (
    ("AE", "SMEMA A IN"),
    ("AS", "SMEMA A OUT"),
    ("BE", "SMEMA B IN"),
    ("BS", "SMEMA B OUT"),
    ("P", "SENSOR PRESENCA"),
    ("S", "STOP_LINE"),
    ("T", "SEND_TIME"),
    ("V", "VERDE"),
    ("R", "VERMELHO"),
    ("M", "AMARELO"),
    ("L", "released"),
)

DEFAULT_BIND = "10.0.0.204"
DEFAULT_PORT = 503
DEFAULT_UNIT_ID = 1


class CoilBank:
    """The coil states, shared between the server loop and the toggler."""

    def __init__(self, states):
        self._states = list(states)
        self._lock = threading.Lock()

    def __len__(self):
        return len(self._states)

    def snapshot(self):
        with self._lock:
            return list(self._states)

    def flip(self, index):
        with self._lock:
            self._states[index] = not self._states[index]
            return self._states[index]


def pack_coils(states, start, quantity):
    """Pack coil states LSB-first, coil n into bit n, as MODBUS requires."""
    byte_count = (quantity + 7) // 8
    data = bytearray(byte_count)
    for offset in range(quantity):
        if states[start + offset]:
            data[offset // 8] |= 1 << (offset % 8)
    return bytes(data)


def build_response(transaction_id, unit_id, payload):
    """Wrap a PDU in an MBAP header."""
    length = LENGTH_FIELD_OVERHEAD + len(payload)
    return struct.pack(">HHHB", transaction_id, PROTOCOL_ID, length,
                       unit_id) + payload


def build_exception(transaction_id, unit_id, function_code, code):
    return build_response(transaction_id, unit_id,
                          bytes([function_code | EXCEPTION_FLAG, code]))


def recv_exact(conn, count):
    """Read exactly count bytes, or None if the peer closed."""
    buf = b""
    while len(buf) < count:
        chunk = conn.recv(count - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def describe(states):
    """Render the package the way the braille cell will read it out."""
    return " ".join(
        "%s=%s" % (FIELD_LABELS[i][0], "T" if states[i] else "F")
        for i in range(min(len(states), len(FIELD_LABELS)))
    )


def serve_connection(conn, peer, coils, unit_id, verbose):
    requests = 0
    while True:
        mbap = recv_exact(conn, MBAP_LEN)
        if mbap is None:
            break
        transaction_id, protocol_id, length, req_unit = struct.unpack(
            ">HHHB", mbap)
        pdu = recv_exact(conn, length - LENGTH_FIELD_OVERHEAD)
        if pdu is None:
            break

        requests += 1
        if verbose:
            print("  <- %s%s" % (mbap.hex(), pdu.hex()))

        if protocol_id != PROTOCOL_ID:
            # Not MODBUS. Say nothing rather than guess -- a real server would
            # drop this too.
            continue
        if req_unit != unit_id:
            # Addressed to a different slave on the same link.
            continue

        function_code = pdu[0]
        if function_code != FC_READ_COILS:
            response = build_exception(transaction_id, unit_id, function_code,
                                       EXC_ILLEGAL_FUNCTION)
            print("  !! FC 0x%02X unsupported, returning exception"
                  % function_code)
            conn.sendall(response)
            continue

        start, quantity = struct.unpack(">HH", pdu[1:5])
        if quantity == 0 or start + quantity > len(coils):
            response = build_exception(transaction_id, unit_id, function_code,
                                       EXC_ILLEGAL_DATA_ADDRESS)
            print("  !! coils %d..%d out of range, returning exception"
                  % (start, start + quantity - 1))
            conn.sendall(response)
            continue

        states = coils.snapshot()
        data = pack_coils(states, start, quantity)
        payload = bytes([FC_READ_COILS, len(data)]) + data
        response = build_response(transaction_id, unit_id, payload)
        conn.sendall(response)

        if verbose:
            print("  -> %s   [%s]" % (response.hex(), describe(states)))
        elif requests == 1:
            print("  first poll answered: %s" % describe(states))

    print("-- %s:%d disconnected after %d request(s)"
          % (peer[0], peer[1], requests))


def toggler(coils, period, stop):
    """Flip one coil in rotation, so the reader's queued-package path runs."""
    index = 0
    while not stop.wait(period):
        state = coils.flip(index)
        label = FIELD_LABELS[index][0] if index < len(FIELD_LABELS) else str(
            index)
        print("** coil %d (%s) -> %s   [%s]"
              % (index, label, "TRUE" if state else "FALSE",
                 describe(coils.snapshot())))
        index = (index + 1) % len(coils)


def parse_initial(text, count):
    if text is None:
        return [False] * count
    cleaned = text.strip()
    if len(cleaned) != count or any(c not in "01" for c in cleaned):
        raise argparse.ArgumentTypeError(
            "--coils needs exactly %d characters of 0/1 (leftmost is coil 0)"
            % count)
    return [c == "1" for c in cleaned]


def main():
    parser = argparse.ArgumentParser(
        description="Minimal MODBUS TCP server standing in for the PLC.")
    parser.add_argument("--bind", default=DEFAULT_BIND,
                        help="address to listen on (default %s)" % DEFAULT_BIND)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help="TCP port (default %d, not the MODBUS 502)"
                             % DEFAULT_PORT)
    parser.add_argument("--unit", type=int, default=DEFAULT_UNIT_ID,
                        help="unit id to answer for (default %d)"
                             % DEFAULT_UNIT_ID)
    parser.add_argument("--count", type=int, default=DEFAULT_COIL_COUNT,
                        help="coils served (default %d)" % DEFAULT_COIL_COUNT)
    parser.add_argument("--coils", default=None,
                        help="initial states as 0/1, leftmost is coil 0, "
                             "e.g. 00001000000")
    parser.add_argument("--toggle", type=float, metavar="SECONDS", default=None,
                        help="flip one coil in rotation this often, to "
                             "exercise the queued-package chirp")
    parser.add_argument("--quiet", action="store_true",
                        help="log only connections and toggles, not every poll")
    args = parser.parse_args()

    coils = CoilBank(parse_initial(args.coils, args.count))
    verbose = not args.quiet

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        listener.bind((args.bind, args.port))
    except PermissionError:
        sys.exit("cannot bind %s:%d -- ports below 1024 need root (try sudo)"
                 % (args.bind, args.port))
    except OSError as exc:
        sys.exit("cannot bind %s:%d -- %s\n"
                 "is the address assigned to an interface? try:\n"
                 "  sudo ip addr add %s/24 dev <interface>"
                 % (args.bind, args.port, exc, args.bind))
    listener.listen(1)

    print("PLC simulator on %s:%d, unit %d, %d coils"
          % (args.bind, args.port, args.unit, args.count))
    print("initial: %s" % describe(coils.snapshot()))

    stop = threading.Event()
    if args.toggle:
        threading.Thread(target=toggler, args=(coils, args.toggle, stop),
                         daemon=True).start()
        print("toggling one coil every %.1fs" % args.toggle)
    print("waiting for the device to connect (Ctrl-C to stop)")

    try:
        while True:
            conn, peer = listener.accept()
            print("++ %s:%d connected" % (peer[0], peer[1]))
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            try:
                serve_connection(conn, peer, coils, args.unit, verbose)
            except ConnectionResetError:
                print("-- %s:%d reset the connection" % (peer[0], peer[1]))
            finally:
                conn.close()
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        stop.set()
        listener.close()


if __name__ == "__main__":
    main()
