#!/usr/bin/env python3
'''
Builds a Minix V2 file system image (1 KiB blocks, 14 character names) from a
directory tree, on the host.

    python mkminixfs.py -o root.img [--blocks N] [--inodes N] \
        [--dev /dev/console:c:4:0:0620]... DIR

The files take their permissions from the host (the owner is root); the
device files, which the host tree cannot hold, are named with --dev
(path:b or c:major:minor:mode in octal). The image is little-endian, the byte
order of the Stackette.
'''
import argparse
import os
import stat
import struct
import sys
import time

BS = 1024
NDZ = 7
NIND = BS // 4
IS = 64
IPB = BS // IS
MAGIC = 0x2468


class Fs:
    def __init__(s, blocks, inodes):
        s.blocks = blocks
        s.ninodes = inodes
        s.imap_blocks = (inodes + 1 + BS * 8 - 1) // (BS * 8)
        s.zmap_blocks = (blocks + BS * 8 - 1) // (BS * 8)
        itab_blocks = (inodes + IPB - 1) // IPB
        s.first_data = 2 + s.imap_blocks + s.zmap_blocks + itab_blocks
        s.img = bytearray(blocks * BS)
        s.inodes = {}
        s.next_ino = 1
        s.next_zone = s.first_data
        s.now = int(time.time())

    def zone(s):
        z = s.next_zone
        if z >= s.blocks:
            sys.exit('the file system is full')
        s.next_zone += 1
        return z

    def put(s, z, data):
        s.img[z * BS:z * BS + len(data)] = data

    def inode(s, mode, size=0, zones=None, nlinks=1):
        n = s.next_ino
        if n > s.ninodes:
            sys.exit('out of inodes')
        s.next_ino += 1
        s.inodes[n] = [mode, nlinks, size, zones or [0] * 10]
        return n

    def write_data(s, data):
        '''Allocate zones for the data; the 10 zone numbers of the inode.'''
        z = [0] * 10
        blocks = [data[i:i + BS] for i in range(0, len(data), BS)]
        i = 0
        for i in range(min(NDZ, len(blocks))):
            z[i] = s.zone()
            s.put(z[i], blocks[i])
        rest = blocks[NDZ:]
        if rest:
            ind = s.zone()
            z[7] = ind
            ptrs = []
            for b in rest[:NIND]:
                zz = s.zone()
                s.put(zz, b)
                ptrs.append(zz)
            s.put(ind, struct.pack('<%dI' % len(ptrs), *ptrs))
            rest = rest[NIND:]
        if rest:
            dind = s.zone()
            z[8] = dind
            dptrs = []
            for k in range(0, len(rest), NIND):
                ind = s.zone()
                ptrs = []
                for b in rest[k:k + NIND]:
                    zz = s.zone()
                    s.put(zz, b)
                    ptrs.append(zz)
                s.put(ind, struct.pack('<%dI' % len(ptrs), *ptrs))
                dptrs.append(ind)
            s.put(dind, struct.pack('<%dI' % len(dptrs), *dptrs))
        return z

    def dir_data(s, entries):
        d = b''
        for name, ino in entries:
            d += struct.pack('<H14s', ino, name.encode())
        return d

    def finish(s):
        # inodes
        base = (2 + s.imap_blocks + s.zmap_blocks) * BS
        for n, (mode, nl, size, zones) in s.inodes.items():
            raw = struct.pack('<HHhHiIII10I', mode, nl, 0, 0, size, s.now,
                              s.now, s.now, *zones)
            s.img[base + (n - 1) * IS:base + n * IS] = raw
        # bitmaps: bit 0 is not used and is set
        imap = bytearray(s.imap_blocks * BS)
        for n in range(0, s.next_ino):
            imap[n // 8] |= 1 << (n % 8)
        s.img[2 * BS:2 * BS + len(imap)] = imap
        zmap = bytearray(s.zmap_blocks * BS)
        for z in range(s.first_data - 1, s.next_zone):
            b = z - (s.first_data - 1)
            zmap[b // 8] |= 1 << (b % 8)
        # the bits beyond the end of the device are set as mkfs does
        for z in range(s.blocks, s.zmap_blocks * BS * 8 + s.first_data - 1):
            b = z - (s.first_data - 1)
            zmap[b // 8] |= 1 << (b % 8)
        s.img[(2 + s.imap_blocks) * BS:(2 + s.imap_blocks) * BS + len(zmap)] = zmap
        maxsize = (NDZ + NIND + NIND * NIND) * BS
        sb = struct.pack('<HHhhHhihhI', s.ninodes, 0, s.imap_blocks,
                         s.zmap_blocks, s.first_data, 0, maxsize, MAGIC, 0, s.blocks)
        s.img[BS:BS + len(sb)] = sb


def build(fs, path, parent, devs, prefix):
    '''Make the directory of the host path; returns its inode number.'''
    me = fs.inode(stat.S_IFDIR | 0o755, nlinks=2)
    entries = [('.', me), ('..', parent or me)]
    names = sorted(os.listdir(path)) if path else []
    for name in names:
        full = os.path.join(path, name)
        st = os.lstat(full)
        if stat.S_ISDIR(st.st_mode):
            ino = build(fs, full, me, devs, prefix + '/' + name)
            fs.inodes[me][1] += 1
        elif stat.S_ISREG(st.st_mode):
            data = open(full, 'rb').read()
            mode = stat.S_IFREG | (st.st_mode & 0o7777)
            ino = fs.inode(mode, len(data), fs.write_data(data))
        else:
            continue
        entries.append((name, ino))
    # device files named for this directory
    for dpath, (kind, major, minor, mode) in list(devs.items()):
        d, _, name = dpath.rpartition('/')
        if (d or '') == prefix:
            z = [0] * 10
            z[0] = (major << 8) | minor
            fmt = stat.S_IFBLK if kind == 'b' else stat.S_IFCHR
            ino = fs.inode(fmt | mode, 0, z)
            entries.append((name, ino))
    data = fs.dir_data(entries)
    fs.inodes[me][2] = len(data)
    fs.inodes[me][3] = fs.write_data(data)
    return me


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--blocks', type=int, default=0)
    ap.add_argument('--inodes', type=int, default=0)
    ap.add_argument('--dev', action='append', default=[])
    ap.add_argument('root')
    a = ap.parse_args()
    devs = {}
    for d in a.dev:
        p, k, ma, mi, mo = d.split(':')
        devs[p] = (k, int(ma), int(mi), int(mo, 8))
    # size: measure the tree, then add room
    total = 0
    count = 2
    for r, ds, fl in os.walk(a.root):
        count += len(ds) + len(fl)
        for f in fl:
            total += os.path.getsize(os.path.join(r, f)) // BS + 3
    count += len(devs)
    inodes = a.inodes or count + 16
    blocks = a.blocks or total + count // 8 + inodes // IPB + 64
    fs = Fs(blocks, inodes)
    fs.inode(0)  # placeholder, replaced below
    fs.next_ino = 1
    fs.inodes.clear()
    # the root directory is inode 1; its devices have the prefix ''
    build(fs, a.root, 0, devs, '')
    fs.finish()
    open(a.output, 'wb').write(fs.img)
    print(f'{a.output}: {blocks} blocks, {inodes} inodes, {fs.next_ino - 1} used',
          file=sys.stderr)


if __name__ == '__main__':
    main()
