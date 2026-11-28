# Builds Minix for the Stackette: the library, MM, FS, init, the commands, inet
# with overlays, the root file system and the flash image.
#
#   make ACK=/path/to/ack/.obj/staging [W=/path/to/workdir] [-jN]
#
# ACK is the staging directory of an ACK build with the stackette platform;
# the result is $(W)/flash.bin (the default W is ./build next to this
# Makefile's tree). ACK's tools (lua, flex, ...) must be in the PATH if the ACK
# needs them. Other targets:
#   make root.img   the root file system only
#   make cf.img     the disk image of the CompactFlash card (an MBR and the root
#                   file system in the partition 1), for ROOT=cf; write it with
#                   dd if=cf.img of=/dev/sdX bs=1M
#   ROOT=cf         the root file system is on the card (CF_MB MiB, 32 by
#                   default, with idepio=4 in the kernel if IDEPIO=4) and
#                   not in the flash, which then holds the kernel only
#   CF_INODES       the number of inodes of the root file system (1024 by
#                   default, for ROOT=cf)
#   EXTRA_ROOT=dir  a tree to copy into the root file system (selfhost/build.sh
#                   makes one with the ACK tools)
#   make inet       $(W)/inetovl/inet.aout
#   make clean      removes $(W)
# The objects depend on the .c file and on all the headers (a header change
# rebuilds the component); the kernel is always compiled again.

ifndef ACK
$(error set ACK=/path/to/ack/.obj/staging)
endif
M := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
W ?= $(M)/build
W := $(abspath $(W))
OUT := $(W)/lib
export ACKDIR := $(ACK)
PLAT := $(ACK)/share/ack/stackette

CC := $(ACK)/bin/ack -mstackette -O -D_MINIX -D_POSIX_SOURCE -I$(M)/include
TOOLS := $(M)/tools-stackette
LIBSH := $(M)/src/lib/stackette
MKPROC := ACK=$(ACK) OUT=$(OUT) sh $(LIBSH)/mkproc.sh

HDRS := $(shell find $(M)/include $(M)/src -maxdepth 1 -name '*.h') \
	$(shell find $(M)/include -name '*.h')
KSRC := $(shell find $(M)/src/kernel $(M)/include -name '*.[chs]')
LIBSRC := $(shell find $(M)/src/lib -name '*.[chs]')

ROOT ?= ram
CF_MB ?= 32
CF_INODES ?= 1024
.PHONY: all root.img cf.img inet clean
ifeq ($(ROOT),cf)
all: $(W)/flash.bin $(W)/cf.img
KDEFS := -DROOT_CF
ifeq ($(IDEPIO),4)
KDEFS += -DIDEPIO_4
endif
KROOT :=
MKFS_ARGS := --blocks $(shell expr $(CF_MB) \* 1024) --inodes $(CF_INODES)
else
all: $(W)/flash.bin
KDEFS :=
KROOT := ROOTFS=$(W)/root.img
MKFS_ARGS :=
endif
cf.img: $(W)/cf.img
$(W)/cf.img: $(W)/root.img $(TOOLS)/mkmbr.py
	python3 $(TOOLS)/mkmbr.py -o $@ $(W)/root.img

# --- the library

$(OUT)/libc.a: $(LIBSRC) $(HDRS) $(LIBSH)/build.sh
	ACK=$(ACK) OUT=$(OUT) sh $(LIBSH)/build.sh
	touch $@
$(OUT)/crtso.o $(OUT)/libsys.a $(OUT)/libm.a: $(OUT)/libc.a

# --- objects: $(call comp,name,source directory,flags)

define comp
$(1)_OBJ := $$(patsubst $(2)/%.c,$(W)/$(1)/%.o,$$(wildcard $(2)/*.c))
$$($(1)_OBJ): $(W)/$(1)/%.o: $(2)/%.c $$(HDRS) $$(wildcard $(2)/*.h)
	@mkdir -p $$(@D)
	@echo CC $$<
	@$$(CC) $(3) -c.o -o $$@ $$< 2>$$@.err || { cat $$@.err; rm -f $$@; false; }
endef

$(eval $(call comp,mm,$(M)/src/mm,-I$(M)/src/mm -I$(M)/src))
$(eval $(call comp,fs,$(M)/src/fs,-I$(M)/src/fs -I$(M)/src))
$(eval $(call comp,sh,$(M)/src/commands/sh,-I$(M)/src/commands/sh -I$(M)/src))
$(eval $(call comp,simple,$(M)/src/commands/simple,-I$(M)/src/commands/simple -I$(M)/src))
$(eval $(call comp,inet,$(M)/src/inet,-I$(M)/src/inet -I$(M)/src/inet/generic -I$(M)/src))
$(eval $(call comp,inet_generic,$(M)/src/inet/generic,-I$(M)/src/inet -I$(M)/src/inet/generic -I$(M)/src))
$(W)/simple/ls.o: CC += -D__minix=1
$(eval $(call comp,telnetd,$(M)/src/commands/telnetd,-I$(M)/src/commands/telnetd -I$(M)/src))
$(eval $(call comp,telnet,$(M)/src/commands/telnet,-I$(M)/src/commands/telnet -I$(M)/src))
$(eval $(call comp,rlogind,$(M)/src/commands/rlogind,-I$(M)/src/commands/rlogind -I$(M)/src))
$(eval $(call comp,ftp,$(M)/src/commands/ftp,-I$(M)/src/commands/ftp -I$(M)/src))
$(eval $(call comp,ftpd,$(M)/src/commands/ftpd,-I$(M)/src/commands/ftpd -I$(M)/src))

$(W)/init/init.o: $(M)/src/tools/init.c $(HDRS)
	@mkdir -p $(@D)
	@echo CC $<
	@$(CC) -I$(M)/src -c.o -o $@ $< 2>$@.err || { cat $@.err; rm -f $@; false; }

$(W)/ethtest/ethtest.o: $(TOOLS)/ethtest/ethtest.c $(HDRS)
	@mkdir -p $(@D)
	@echo CC $<
	@$(CC) -c.o -o $@ $< 2>$@.err || { cat $@.err; rm -f $@; false; }
$(W)/iotest/iotest.o: $(TOOLS)/iotest/iotest.c $(HDRS)
	@mkdir -p $(@D)
	@echo CC $<
	@$(CC) -c.o -o $@ $< 2>$@.err || { cat $@.err; rm -f $@; false; }
$(W)/hmtest/hmtest.o: $(TOOLS)/hmtest/hmtest.c $(HDRS)
	@mkdir -p $(@D)
	@echo CC $<
	@$(CC) -c.o -o $@ $< 2>$@.err || { cat $@.err; rm -f $@; false; }

# --- programs (mkproc.sh makes X and X.out, the linked file the kernel image takes)

LINKDEP = $(OUT)/libc.a $(LIBSH)/mkproc.sh $(TOOLS)/ack2minix.py
$(W)/mm.aout: $(mm_OBJ) $(LINKDEP)
	$(MKPROC) $@ $(mm_OBJ) -- --stack 0x2000 >/dev/null
$(W)/fs.aout: $(fs_OBJ) $(LINKDEP)
	$(MKPROC) $@ $(fs_OBJ) -- --stack 0x2000 >/dev/null
$(W)/init.aout: $(W)/init/init.o $(LINKDEP)
	$(MKPROC) $@ $< -- --stack 0x2000 >/dev/null
$(W)/bin/sh: $(sh_OBJ) $(LINKDEP)
	@mkdir -p $(@D)
	$(MKPROC) $@ $(sh_OBJ) -- --stack 0x2000 --heap 0x4000 >/dev/null
$(W)/bin/ethtest: $(W)/ethtest/ethtest.o $(LINKDEP)
	@mkdir -p $(@D)
	$(MKPROC) $@ $< -- --stack 0x2000 >/dev/null
$(W)/bin/iotest: $(W)/iotest/iotest.o $(LINKDEP)
	@mkdir -p $(@D)
	$(MKPROC) $@ $< -- --stack 0x2000 >/dev/null
$(W)/bin/hmtest: $(W)/hmtest/hmtest.o $(LINKDEP)
	@mkdir -p $(@D)
	$(MKPROC) $@ $< -- --stack 0x2000 --heap 0x6000 >/dev/null

SIMPLE := cat cmp cp date mkdir pwd sync rmdir ls echo chmod kill touch uname sleep dhrystone fsck
# the network commands (one source file each), installed in /usr/bin
NETSIMPLE := ifconfig add_route ping hostaddr host tcpd rdate login passwd su chown getty pwdauth
$(addprefix $(W)/bin/,$(NETSIMPLE)): $(W)/bin/%: $(W)/simple/%.o $(LINKDEP)
	@mkdir -p $(@D)
	$(MKPROC) $@ $< -- --stack 0x4000 >/dev/null
# multi-file network commands: telnetd telnet rlogind ftp ftpd
NETMULTI := telnetd telnet rlogind ftp ftpd
define netlink
$(W)/bin/$(1): $$($(1)_OBJ) $$(LINKDEP)
	@mkdir -p $$(@D)
	$$(MKPROC) $$@ $$($(1)_OBJ) -- --stack 0x4000 >/dev/null
endef
$(foreach c,$(NETMULTI),$(eval $(call netlink,$(c))))

$(addprefix $(W)/bin/,$(SIMPLE)): $(W)/bin/%: $(W)/simple/%.o $(LINKDEP)
	@mkdir -p $(@D)
	$(MKPROC) $@ $< -- --stack $(if $(filter cp,$*),0x6000,0x2000) $(if $(filter fsck,$*),--heap 0x7000) >/dev/null

inet: $(W)/inetovl/inet.aout
$(W)/inetovl/inet.aout: $(inet_OBJ) $(inet_generic_OBJ) $(TOOLS)/mkinet.sh $(TOOLS)/ovlprep.py $(LINKDEP)
	ACK=$(ACK) OUT=$(OUT) M=$(M) W=$(W) sh $(TOOLS)/mkinet.sh

# --- the root file system: rm, mv and ln are copies of cp

ROOTBIN := $(SIMPLE) sh ethtest iotest hmtest
# The flash holds 1 MiB, so only the commands needed to bring the network up
# and to log in over it are in the image by default; add more with e.g.
#   make ... NET='ifconfig ping tcpd telnetd login add_route telnet'
# (the whole set is $(NETSIMPLE) $(NETMULTI), about 700 KiB more than fits).
NET ?= ifconfig ping tcpd telnetd login add_route passwd su chown ftpd ftp telnet
USRBIN := $(NET)
root.img: $(W)/root.img
$(W)/root.stamp: $(addprefix $(W)/bin/,$(ROOTBIN) $(USRBIN) pwdauth getty) $(W)/inetovl/inet.aout $(wildcard $(TOOLS)/root/etc/*) $(if $(EXTRA_ROOT),$(shell find $(EXTRA_ROOT) -type f))
	rm -rf $(W)/root
	mkdir -p $(W)/root/bin $(W)/root/etc $(W)/root/dev $(W)/root/usr/adm $(W)/root/usr/bin $(W)/root/tmp
	cp $(addprefix $(W)/bin/,$(ROOTBIN)) $(W)/root/bin/
	cp $(addprefix $(W)/bin/,$(USRBIN)) $(W)/root/usr/bin/
	cp $(W)/inetovl/inet.aout $(W)/root/bin/inet
	mkdir -p $(W)/root/usr/lib $(W)/root/usr/sbin; cp $(W)/bin/pwdauth $(W)/root/usr/lib/pwdauth; cp $(W)/bin/getty $(W)/root/usr/sbin/getty
	for l in rm mv ln; do cp $(W)/bin/cp $(W)/root/bin/$$l; done
	cp $(TOOLS)/root/etc/* $(W)/root/etc/
	: > $(W)/root/usr/adm/wtmp; : > $(W)/root/etc/utmp
	chmod 755 $(W)/root/bin/* $(W)/root/usr/bin/*
	chmod 755 $(W)/root/usr/sbin/getty; chmod 4755 $(W)/root/usr/lib/pwdauth
	for f in su passwd; do test -f $(W)/root/usr/bin/$$f && chmod 4755 $(W)/root/usr/bin/$$f; done; true
	$(if $(EXTRA_ROOT),cp -R $(EXTRA_ROOT)/. $(W)/root/)
	touch $@
$(W)/root.img: $(W)/root.stamp $(TOOLS)/mkminixfs.py
	python3 $(TOOLS)/mkminixfs.py -o $@ $(MKFS_ARGS) \
	  --dev /dev/console:c:4:0:0620 --dev /dev/ram:b:1:0:0600 --dev /dev/null:c:1:3:0666 \
	  --dev /dev/mem:c:1:1:0640 --dev /dev/tty:c:5:0:0666 --dev /dev/tty00:c:4:16:0620 \
	  --dev /dev/log:c:4:15:0222 --dev /dev/ip:c:7:0:0600 \
	  $(foreach n,0 1 2 3 4,--dev /dev/hd$(n):b:3:$(n):0600) \
	  $(foreach n,0 1 2 3,--dev /dev/ttyp$(n):c:4:$(shell expr 128 + $(n)):0666 --dev /dev/ptyp$(n):c:4:$(shell expr 192 + $(n)):0666) \
	  $(W)/root

# --- the kernel and the flash image

# the kernel options (KDEFS) are a dependency too
$(W)/kdefs: FORCE
	@mkdir -p $(W)
	@echo '$(KDEFS)' | cmp -s - $@ || echo '$(KDEFS)' > $@
.PHONY: FORCE
FORCE:
$(W)/flash.bin: $(W)/kdefs $(KSRC) $(if $(KROOT),$(W)/root.img) $(W)/mm.aout $(W)/fs.aout $(W)/init.aout $(OUT)/libc.a $(TOOLS)/mkflash.py
	ACK=$(ACK) OUT=$(OUT) W=$(W)/kernel $(KROOT) KDEFS="$(KDEFS)" \
	  sh $(M)/src/kernel/stackette/build.sh $(W)/mm.aout.out $(W)/fs.aout.out $(W)/init.aout.out
	cp $(W)/kernel/flash.bin $@

clean:
	rm -rf $(W)
