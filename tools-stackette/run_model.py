#!/usr/bin/env python3
"""
Runs a flash image (ack2bin.py) on the reference interpreter of the CPU
(cpu_model.Ref) with a flat memory: the flash image at 0, the UART at 0xf000
(a byte or word store prints the low byte) and the interrupt ID at 0xf02e (0).
Stops when an instruction branches to itself, or after a step limit.

    python run_model.py prog.bin [--steps N]
"""
import argparse
import sys

from cpu_model import Ref


class Machine(Ref):
    def __init__(self):
        super().__init__()
        self.out = bytearray()

    def rd16(self, a):
        if a & 0xfffe == 0xf02e:
            return 0
        return super().rd16(a)

    def wr(self, a, s, v):
        if a & 0xfffe == 0xf000:
            self.out.append(v & 0xff)
            return
        super().wr(a, s, v)

    def wr16(self, a, v):
        if a & 0xfffe == 0xf000:
            self.out.append(v & 0xff)
            return
        super().wr16(a, v)


def load(m, image):
    for i in range(0, len(image) - 1, 2):
        m.w[i >> 1] = int.from_bytes(image[i:i + 2], 'little')
    return len(image) >> 1


def fetch(m, pc, n):
    return [m.rd16(pc + 2 * k) for k in range(n)]


def run(m, steps):
    pc = 0
    for count in range(steps):
        w = m.rd16(pc)
        if m.is_skip(w):
            pc += 2
            if m.cond((w >> 6) & 15):
                pc += 2 * m.length(m.rd16(pc))
            continue
        n = m.length(w)
        nxt = pc + 2 * n
        target = m.step(fetch(m, pc, n), nxt)
        new = nxt if target is None else target
        if new == pc:
            return count
        pc = new
    raise SystemExit(f'no halt after {steps} steps, ip {pc:#06x}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('image')
    ap.add_argument('--steps', type=int, default=1000000)
    args = ap.parse_args()
    m = Machine()
    load(m, open(args.image, 'rb').read())
    n = run(m, args.steps)
    sys.stdout.buffer.write(bytes(m.out))
    print(f'\n[halted after {n} steps]', file=sys.stderr)


if __name__ == '__main__':
    main()
