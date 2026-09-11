#!/usr/bin/env python3
"""Self-test for tools/plc_sim.py, using frames shaped exactly like the firmware's.

The simulator exists to judge the device, which makes it worth judging first: a
bug in plc_sim.py presents at the bench as a bug in the firmware, because the
firmware's parser is strict enough to reject a malformed reply loudly. This
script removes that ambiguity before a bench session starts.

It deliberately does not use a MODBUS library. It builds the same 12-byte
request App/Src/modbus_tcp.c builds and applies the same checks, in the same
order, that modbus_parse_read_coils() applies -- so passing here means the
simulator satisfies the actual consumer, not a third party's idea of MODBUS.

Run from the repository root, needs nothing installed and no privileges:

    python3 tools/verify_plc_sim.py

Exits non-zero on the first failing expectation.
"""
import socket, struct, subprocess, sys, time

HOST, PORT, UNIT, QTY = "127.0.0.1", 5503, 1, 11
fails = []

def check(name, cond, detail=""):
    print(("  ok   " if cond else "  FAIL ") + name + (("  " + detail) if detail else ""))
    if not cond:
        fails.append(name)

def build_request(tid, unit, addr, qty):
    return struct.pack(">HHHBBHH", tid, 0, 6, unit, 0x01, addr, qty)

def parse_response(frame, tid, unit, qty):
    """Return (status, bits). Mirrors modbus_parse_read_coils()'s check order."""
    if len(frame) < 9:                                   return ("TOO_SHORT", None)
    r_tid, pid, length, r_unit, fc = struct.unpack(">HHHBB", frame[:8])
    if pid != 0:                                         return ("PROTOCOL_ID", None)
    if r_tid != tid:                                     return ("TRANSACTION_ID", None)
    if r_unit != unit:                                   return ("UNIT_ID", None)
    if length != len(frame) - 6:                         return ("LENGTH_FIELD", None)
    if fc & 0x80:                                        return ("EXCEPTION", (fc, frame[8]))
    if fc != 0x01:                                       return ("FUNCTION", None)
    byte_count = frame[8]
    if byte_count != (qty + 7) // 8:                     return ("BYTE_COUNT", None)
    if len(frame) < 7 + 2 + byte_count:                  return ("TOO_SHORT", None)
    bits = 0
    for i in range(byte_count):
        bits |= frame[9 + i] << (i * 8)
    if qty < 16:
        bits &= (1 << qty) - 1
    return ("OK", bits)

def roundtrip(sock, tid, addr=0, qty=QTY, unit=UNIT, fc=0x01):
    req = build_request(tid, unit, addr, qty)
    if fc != 0x01:
        req = req[:7] + bytes([fc]) + req[8:]
    sock.sendall(req)
    head = sock.recv(7)
    if len(head) < 7: return None
    length = struct.unpack(">H", head[4:6])[0]
    body = b""
    while len(body) < length - 1:
        body += sock.recv(length - 1 - len(body))
    return head + body

def run(coils_arg, extra=()):
    proc = subprocess.Popen(
        [sys.executable, "tools/plc_sim.py", "--bind", HOST, "--port", str(PORT),
         "--coils", coils_arg, "--quiet", *extra],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    for _ in range(50):
        time.sleep(0.1)
        try:
            s = socket.create_connection((HOST, PORT), timeout=2)
            return proc, s
        except OSError:
            if proc.poll() is not None:
                print(proc.stderr.read().decode()); sys.exit(1)
    sys.exit("simulator never came up")

print("1. all coils false")
p, s = run("0" * 11)
st, bits = parse_response(roundtrip(s, 0x1234), 0x1234, UNIT, QTY)
check("status OK", st == "OK", st); check("bits == 0x0000", bits == 0x0000, hex(bits or 0))
s.close(); p.terminate(); p.wait()

print("2. all eleven coils true -> 0x07FF (padding must stay clear)")
p, s = run("1" * 11)
st, bits = parse_response(roundtrip(s, 0x0001), 0x0001, UNIT, QTY)
check("status OK", st == "OK", st); check("bits == 0x07FF", bits == 0x07FF, hex(bits or 0))
s.close(); p.terminate(); p.wait()

print("3. bit order: each coil alone lands in its own bit")
p, s = run("0" * 11)
ok = True
for i in range(11):
    s.close(); p.terminate(); p.wait()
    pattern = "".join("1" if j == i else "0" for j in range(11))
    p, s = run(pattern)
    st, bits = parse_response(roundtrip(s, 0x2000 + i), 0x2000 + i, UNIT, QTY)
    if st != "OK" or bits != (1 << i):
        ok = False; print("     coil %d -> %s %s" % (i, st, hex(bits or 0)))
check("all 11 coils map to bit n", ok)
s.close(); p.terminate(); p.wait()

print("4. transaction id is echoed, not invented")
p, s = run("00000100000")
ok = True
for tid in (0x0000, 0x00FF, 0xABCD, 0xFFFF):
    st, _ = parse_response(roundtrip(s, tid), tid, UNIT, QTY)
    if st != "OK": ok = False; print("     tid %04X -> %s" % (tid, st))
check("four transaction ids echoed", ok)

print("5. unsupported function code returns an exception echoing that code")
st, info = parse_response(roundtrip(s, 0x3001, fc=0x03), 0x3001, UNIT, QTY)
check("status EXCEPTION", st == "EXCEPTION", st)
check("echoed fc is 0x83, not 0x81", info and info[0] == 0x83, hex(info[0]) if info else "-")
check("exception code 0x01 (illegal function)", info and info[1] == 0x01, hex(info[1]) if info else "-")

print("6. out-of-range quantity returns an exception")
st, info = parse_response(roundtrip(s, 0x3002, qty=64), 0x3002, UNIT, 64)
check("status EXCEPTION", st == "EXCEPTION", st)
check("echoed fc is 0x81 -- the only exception the firmware recognises",
      info and info[0] == 0x81, hex(info[0]) if info else "-")
check("exception code 0x02 (illegal data address)", info and info[1] == 0x02,
      hex(info[1]) if info else "-")

print("7. a poll for the wrong unit id is ignored, not answered wrongly")
s.sendall(build_request(0x4000, UNIT + 7, 0, QTY))
s.settimeout(0.6)
try:
    stray = s.recv(64); check("no reply to foreign unit", stray == b"", stray.hex())
except socket.timeout:
    check("no reply to foreign unit", True)
s.settimeout(None)
st, _ = parse_response(roundtrip(s, 0x4001), 0x4001, UNIT, QTY)
check("still serving after the ignored frame", st == "OK", st)
s.close(); p.terminate(); p.wait()

print("8. --toggle actually changes the package")
p, s = run("0" * 11, extra=("--toggle", "0.4"))
_, first = parse_response(roundtrip(s, 0x5000), 0x5000, UNIT, QTY)
time.sleep(1.1)
_, later = parse_response(roundtrip(s, 0x5001), 0x5001, UNIT, QTY)
check("package changed after toggling", first != later, "%s -> %s" % (hex(first), hex(later)))
s.close(); p.terminate(); p.wait()

print()
print("FALHAS: %d" % len(fails))
for f in fails: print("  - " + f)
sys.exit(1 if fails else 0)
