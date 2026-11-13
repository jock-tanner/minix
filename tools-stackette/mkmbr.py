#!/usr/bin/env python3
'''
Builds the disk image of the CompactFlash card: a PC-compatible MBR with a
partition table and, in the first partition, a file system image.

    python mkmbr.py -o cf.img [--start 2048] [--type 0x81] [--extra MiB]... root.img

The partition 1 is the image (Minix, type 0x81, active); each --extra adds an
empty partition of that many MiB after it (Minix type too, for /usr or the
like; a file system is made on it from the system). The partitions begin at a
multiple of 1 MiB (--start sectors for the first one). The MBR has no code:
the sector 0 is the table and the signature 0x55AA, everything that reads
partition tables (Minix, Linux, fdisk) understands it. The whole card can be
written with `dd if=cf.img of=/dev/sdX bs=1M`.
'''
import argparse
import struct
import sys

SECTOR = 512
MIB = 2048          # sectors in a MiB


def chs(lba):
    '''The CHS fields of a partition entry: the LBA marker for the disks that
    are too big, otherwise the translation with 255 heads and 63 sectors.'''
    c, rest = divmod(lba, 255 * 63)
    if c > 1023:
        return bytes((0xfe, 0xff, 0xff))
    h, s = divmod(rest, 63)
    return bytes((h, (s + 1) | (c >> 8) << 6, c & 0xff))


def entry(boot, kind, start, size):
    return (
        bytes((0x80 if boot else 0,)) + chs(start) + bytes((kind,))
        + chs(start + size - 1) + struct.pack('<II', start, size)
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('image')
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--start', type=int, default=MIB)
    ap.add_argument('--type', type=lambda s: int(s, 0), default=0x81)
    ap.add_argument('--extra', type=int, action='append', default=[])
    args = ap.parse_args()
    data = open(args.image, 'rb').read()
    data += bytes(-len(data) % (MIB * SECTOR))
    parts = [(True, args.type, args.start, len(data) // SECTOR)]
    end = args.start + parts[0][3]
    for mb in args.extra[:3]:
        end += -end % MIB
        parts.append((False, args.type, end, mb * MIB))
        end += mb * MIB
    mbr = bytearray(SECTOR)
    for i, p in enumerate(parts):
        mbr[446 + 16 * i:462 + 16 * i] = entry(*p)
    mbr[510:512] = b'\x55\xaa'
    with open(args.output, 'wb') as f:
        f.write(mbr)
        f.write(bytes((args.start - 1) * SECTOR))
        f.write(data)
        f.write(bytes((end - args.start - len(data) // SECTOR) * SECTOR))
    for i, (boot, kind, start, size) in enumerate(parts):
        print(f'partition {i + 1}: type {kind:#04x} start {start} size {size}'
              f' sectors ({size // MIB} MiB){" active" if boot else ""}')


if __name__ == '__main__':
    main()
