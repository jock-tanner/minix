#!/usr/bin/env python3
'''
Builds the flash image of the Stackette Minix system from ACK output files.

    python mkflash.py -k kernel.out -o flash.bin proc.out...

kernel.out is linked with the text at 0 and the rom, data and bss from the data
space address 0x1000 (em_led -b1:0x1000); the processes with em_led -b1:0 (see
ack2minix.py). Each part starts at a page of the flash (4 KiB, the physical
page 0x2000 + n):

    kernel text, kernel rom+data image, the table of the processes (one page),
    for every process: text, rom+data image

The table (16-bit little-endian words) is `KS`, n, then for each process
eight words: first flash page of the text, pages of the text, first flash
page of the data image, bytes of the data image (rom + data), bytes of bss,
total bytes of the data segment, entry (0), 0. The kernel is not in the table:
its start code finds its own image by the symbols endtext, enddata.
'''
import argparse
import struct
import sys

PAGE = 4096
FLASH_PAGES = 256
MAGIC = 0x534b
SECT = '<IIIII'
TEXT, ROM, DATA, BSS = range(4)


def read_out(d):
    magic, _, _, nsect = struct.unpack_from('<HHHH', d)
    if magic != 0x0202 or nsect < 4:
        sys.exit('not an ACK output file with four sections')
    return [struct.unpack_from(SECT, d, 20 + 20 * i) for i in range(nsect)]


def contents(sect, d):
    base, size, foff, flen, _ = sect
    return base, d[foff:foff + flen] + bytes(size - flen)


def parts(path, dbase):
    d = open(path, 'rb').read()
    s = read_out(d)
    tb, text = contents(s[TEXT], d)
    rb, rom = contents(s[ROM], d)
    db, data = contents(s[DATA], d)
    if tb != 0 or rb != dbase or db != dbase + len(rom):
        sys.exit(f'{path}: text at {tb:#x}, rom at {rb:#x}, data at {db:#x}')
    return text, rom + data, s[BSS][1]


def pad(b):
    return b + bytes(-len(b) % PAGE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-k', '--kernel', required=True)
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--stack', type=lambda x: int(x, 0), default=0x2000)
    ap.add_argument('procs', nargs='*')
    a = ap.parse_args()
    image = bytearray()
    ktext, kdata, kbss = parts(a.kernel, 0x1000)
    image += pad(ktext)
    image += pad(kdata)
    table_at = len(image)
    image += bytes(PAGE)
    table = []
    for p in a.procs:
        text, data, bss = parts(p, 0)
        tpage = len(image) // PAGE
        image += pad(text)
        dpage = len(image) // PAGE
        image += pad(data)
        total = (len(data) + bss + a.stack + PAGE - 1) // PAGE * PAGE
        table.append((tpage, len(pad(text)) // PAGE, dpage, len(data), bss,
                      total, 0, 0))
    if len(image) > FLASH_PAGES * PAGE:
        sys.exit('the image does not fit in the flash')
    words = [MAGIC, len(table)]
    for t in table:
        words.extend(t)
    tab = struct.pack('<%dH' % len(words), *words)
    if len(tab) > PAGE:
        sys.exit('too many processes')
    image[table_at:table_at + len(tab)] = tab
    open(a.output, 'wb').write(image)
    print(f'kernel text {len(ktext)}, data {len(kdata)}, bss {kbss}; '
          f'{len(table)} processes; image {len(image)} bytes', file=sys.stderr)


if __name__ == '__main__':
    main()
