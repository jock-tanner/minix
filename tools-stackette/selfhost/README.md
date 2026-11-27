# ACK on stackette

This directory builds the ACK C compiler for stackette with ACK itself, so that
`cc hello.c` runs on the board (or the model): `cpp`, `prune`, `cemcom`,
`em_opt`, `ncg`, `as`, `em_led` and the `ack` driver, each a Minix program
with its own 64 KiB data space.

## What is here

| file | what |
| --- | --- |
| `build.sh` | builds everything, in steps: `source cross libs tools install` |
| `check.sh` | builds an image with the tools and compiles `test/hello.c` on the model |
| `ack-alloc.patch` | applied to the copy of ack: the pools of cemcom take 10 structures at a time |
| `led-target.patch` | applied to the copy of ack: led's configuration for a small machine |
| `cemcom-parameters.h` | the parameters of cemcom for a small machine (`SmallPars`, with the comments made fit for ack) |
| `cc` | stands in for the host `cc` while ninja's compile commands are run: it calls `ack -mstackette` and keeps the generated assembly (for the overlays) |
| `inc/stdint.h` | the 64 bit types of the ack sources are 32 bit here |
| `src/prune.c` | drops the declarations of the headers that nothing uses (`prune.py` is the same in Python, as a reference) |
| `src/ack2minix.c` | the part of `ack2minix.py` that runs on the target: the linker's output to a Minix a.out |
| `src/syssystem.c`, `src/maketempfile*.c`, `src/extra.c` | what the ack sources want from the system, as Minix has it |
| `tree/` | the descr of the machine for the target, `cc`, the linker script `mled` and the preprocessor script `cpp.ansi` |
| `runmodel.py` | boots an image on the model and runs shell commands |

The changes to the ack sources that are the same everywhere are in the ack
repository (`#ifdef __ACK__` in `cemcom`, `em_code`, `idf`, `as`; no zero sized
arrays or empty initializers in `ncg` and `ack`). The two patches are what
does not fit there.

## Building

You need an ack checkout that is built (`make` in it, with the stackette
platform) and the Minix libraries (`make` in `tools-stackette` leaves them in
`$MXW/lib`). ack's own tools (lua, flex) must be in the `PATH`.

    ACK=/path/to/ack MXW=/path/to/minix-build W=/path/to/work sh build.sh

`W` is where everything goes (default `selfhost/build`), `MXW` is the `W` of
the Makefile (default `tools-stackette/build`). They must be different
directories: the script replaces `lib`, `bin` and `root` in `W`, which are the
Minix libraries, programs and root tree in `MXW`. The steps:

1. `source`: `git archive HEAD` of ack, the two patches, and the host build of
   the tools, which generates the sources.
2. `cross`: the compile commands that ninja would run for the objects of the
   six programs are run by hand, with `cc` in the `PATH`. (Asking ninja to
   rebuild the objects does not work: the host tools share them.)
3. `libs`: the objects the programs share go in `cemlibs.a`, plus the
   functions of the system that the programs need.
4. `tools`: links the programs. cemcom, cpp, ncg and as are split in overlays
   (the groups are in `build.sh`) and `ack`, `em_opt`, `led` are not; `prune`
   and `ack2minix` are small. The sizes must come out as: cemcom text 131072
   data 22594 bss 2926, cpp 81920/8506/10206, em_opt 51578/19312/1292, ncg
   98304/25666/5474, as 81920/16050/10622, ack 46366/4898/608.
5. `install`: `$W/root` is the tree for the image: `/opt/pkg/ack`, `/bin/ack`,
   `/bin/cc`, `/bin/prune`, the headers in `/inc` and the test in `/t`.

Then the image, and the test:

    make -C tools-stackette ACK=/path/to/ack/.obj/staging W=$MXW ROOT=cf EXTRA_ROOT=$W/root
    ACK=... STACKETTE=/path/to/stackette sh check.sh

`check.sh` compiles `/t/hello.c` on the model; it takes about 15 minutes of
the model's time (cc is slow: cemcom takes most of it) and ends with `SAME`
(the program is the one that the host linked) and the program's output.

## Things to know

- cemcom is the program that is at the limit of the data space: its stack and
  heap are 0xc00 and 0x8f00 bytes, and the pools, the identifier blocks and
  the parameters in this directory are what makes it fit. A change in the
  ack sources can break that: build.sh prints the sizes.
- `prune` is needed because the headers are too big for cemcom: a preprocessed
  `<stdio.h>` has hundreds of declarations, a program uses a few. cpp's output
  goes through it in the script `cpp.ansi`.
- The Minix `sh` does not split `"$@"` (it does not tell it from `$*`), so the
  scripts use `$*`; the sh has a 16 KiB heap (in the Makefile) for the long
  linker command.
- The image needs more inodes than the default (`CF_INODES`, 1024).
- The programs are found through the path compiled into the driver,
  `/opt/pkg/ack`; the machine is `-mstackette` (`cc` is `ack -mstackette
  -D_MINIX -D_POSIX_SOURCE -I/inc`). Overlays are not made by `ack2minix` on
  the target: programs linked there are limited to 64 KiB of code.
