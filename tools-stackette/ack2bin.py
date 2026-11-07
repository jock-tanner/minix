#!/usr/bin/env python3
"""
Turns a linked ACK output file (led, `ack -mstackette`) into the flash image of
the Stackette: raw little-endian 16-bit words, starting at address 0.

The image is the text and the rom at their link addresses, then the image of
the initialized data right after the rom, at the address `endrom` that boot.s
copies it from. The data runs from its base address in RAM (descr: -b2).

    python ack2bin.py prog.out -o prog.bin
"""
import argparse
import struct
import sys

O_MAGIC = 0x0202
HEAD = '<HHHHHHII'
SECT = '<IIIII'
NAME = '<IHHI'
TEXT, ROM, DATA, BSS = range(4)


def read_out(d):
    magic, _, _, nsect, nrelo, nname, nemit, nchar = struct.unpack_from(HEAD, d)
    if magic != O_MAGIC:
        sys.exit(f'not an ACK output file (magic {magic:#06x})')
    if nsect < 4:
        sys.exit(f'expected at least 4 sections, found {nsect}')
    off = struct.calcsize(HEAD)
    sects = []
    for _ in range(nsect):
        sects.append(struct.unpack_from(SECT, d, off))
        off += struct.calcsize(SECT)
    names = {}
    if nname:
        noff = off + nemit + 8 * nrelo
        for i in range(nname):
            s_off, _, _, valu = struct.unpack_from(NAME, d, noff + 12 * i)
            if s_off:
                end = d.index(b'\0', s_off)
                names[d[s_off:end].decode()] = valu
    return sects, names, d


def contents(sect, d):
    base, size, foff, flen, _ = sect
    return base, d[foff:foff + flen] + bytes(size - flen)


def build(d):
    sects, names, d = read_out(d)
    tbase, text = contents(sects[TEXT], d)
    rbase, rom = contents(sects[ROM], d)
    dbase, data = contents(sects[DATA], d)
    if tbase != 0:
        sys.exit(f'the text starts at {tbase:#x}, not at 0')
    if rbase < tbase + len(text):
        sys.exit('the rom overlaps the text')
    image = bytearray(text)
    image += bytes(rbase - len(image))
    image += rom
    rom_end = len(image)
    if 'endrom' in names and names['endrom'] != rom_end:
        sys.exit(
            f'endrom is {names["endrom"]:#x}, the rom ends at {rom_end:#x}'
        )
    if len(image) + len(data) > dbase and data:
        print(
            f'warning: the image ends at {len(image) + len(data):#x}, above '
            f'the data base {dbase:#x}: it does not fit in the flash page',
            file=sys.stderr,
        )
    image += data
    if len(image) & 1:
        image += b'\0'
    info = {
        'text': len(text),
        'rom': len(rom),
        'data': len(data),
        'bss': sects[BSS][1],
        'endrom': rom_end,
        'database': dbase,
    }
    return bytes(image), info


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('input')
    ap.add_argument('-o', '--output', required=True)
    args = ap.parse_args()
    image, info = build(open(args.input, 'rb').read())
    open(args.output, 'wb').write(image)
    print(
        ', '.join(f'{k} {v:#x}' for k, v in info.items()),
        f'-> {len(image)} bytes',
        file=sys.stderr,
    )


if __name__ == '__main__':
    main()
