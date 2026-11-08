#!/usr/bin/env python3
'''
Turns an ACK output file linked as a separate I&D program (em_led -b1:0: the
text at 0 in the code space, the rom, the data and the bss from 0 in the data
space) into a Minix a.out executable (include/a.out.h, the short form).

The file is the 32 byte header, the text, then the data space image (the rom
followed by the initialized data). The bss is not stored: a_bss counts it.
a_total is the memory of the data segment, with the bss and the stack, rounded
up to a page, and the stack is at its end; chmem changes it.

    python ack2minix.py prog.out -o prog [--stack BYTES]
'''
import argparse
import struct
import sys

O_MAGIC = 0x0202
HEAD = '<HHHHHHII'
SECT = '<IIIII'
TEXT, ROM, DATA, BSS = range(4)

A_STACKETTE = 0x20
A_EXEC = 0x10
A_SEP = 0x20
PAGE = 4096


def read_out(d):
    magic, _, _, nsect = struct.unpack_from('<HHHH', d)
    if magic != O_MAGIC or nsect < 4:
        sys.exit('not an ACK output file with four sections')
    off = struct.calcsize(HEAD)
    return [struct.unpack_from(SECT, d, off + 20 * i) for i in range(nsect)]


def contents(sect, d):
    base, size, foff, flen, _ = sect
    return base, d[foff:foff + flen] + bytes(size - flen)


def build(d, stack):
    sects = read_out(d)
    tbase, text = contents(sects[TEXT], d)
    rbase, rom = contents(sects[ROM], d)
    dbase, data = contents(sects[DATA], d)
    bbase, bsize = sects[BSS][0], sects[BSS][1]
    if tbase != 0 or rbase != 0:
        sys.exit('link with -b1:0: the text and the rom must start at 0')
    if dbase != len(rom):
        sys.exit(f'the data starts at {dbase:#x}, the rom ends at {len(rom):#x}')
    if bbase != dbase + len(data):
        sys.exit('the bss does not follow the data')
    image = rom + data
    if len(text) & 1:
        text += b'\0'
    bss = bsize
    total = len(image) + bss + stack
    total = (total + PAGE - 1) // PAGE * PAGE
    if total > 0x10000:
        sys.exit(f'the data segment needs {total:#x} bytes, the limit is 0x10000')
    hdr = struct.pack(
        '<BBBBBBHllllll',
        0x01, 0x03, A_EXEC | A_SEP, A_STACKETTE, 32, 0, 0,
        len(text), len(image), bss, 0, total, 0,
    )
    assert len(hdr) == 32, len(hdr)
    return hdr + text + image, len(text), len(image), bss, total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('infile')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--stack', type=lambda x: int(x, 0), default=0x2000)
    a = ap.parse_args()
    out, t, d, b, total = build(open(a.infile, 'rb').read(), a.stack)
    open(a.output, 'wb').write(out)
    print(f'text {t}, data {d}, bss {b}, total {total}', file=sys.stderr)


if __name__ == '__main__':
    main()
