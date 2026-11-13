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

.PHONY: all root.img inet clean
all: $(W)/flash.bin

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

$(W)/init/init.o: $(M)/src/tools/init.c $(HDRS)
	@mkdir -p $(@D)
	@echo CC $<
	@$(CC) -I$(M)/src -c.o -o $@ $< 2>$@.err || { cat $@.err; rm -f $@; false; }

$(W)/ethtest/ethtest.o: $(TOOLS)/ethtest/ethtest.c $(HDRS)
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
	$(MKPROC) $@ $(sh_OBJ) -- --stack 0x2000 >/dev/null
$(W)/bin/ethtest: $(W)/ethtest/ethtest.o $(LINKDEP)
	@mkdir -p $(@D)
	$(MKPROC) $@ $< -- --stack 0x2000 >/dev/null

SIMPLE := cat cp date mkdir pwd sync rmdir ls echo head wc chmod kill touch uname sleep
$(addprefix $(W)/bin/,$(SIMPLE)): $(W)/bin/%: $(W)/simple/%.o $(LINKDEP)
	@mkdir -p $(@D)
	$(MKPROC) $@ $< -- --stack $(if $(filter cp,$*),0x6000,0x2000) >/dev/null

inet: $(W)/inetovl/inet.aout
$(W)/inetovl/inet.aout: $(inet_OBJ) $(inet_generic_OBJ) $(TOOLS)/mkinet.sh $(TOOLS)/ovlprep.py $(LINKDEP)
	ACK=$(ACK) OUT=$(OUT) M=$(M) W=$(W) sh $(TOOLS)/mkinet.sh

# --- the root file system: rm, mv and ln are copies of cp

ROOTBIN := $(SIMPLE) sh ethtest
root.img: $(W)/root.img
$(W)/root.stamp: $(addprefix $(W)/bin/,$(ROOTBIN)) $(W)/inetovl/inet.aout $(wildcard $(TOOLS)/root/etc/*)
	rm -rf $(W)/root
	mkdir -p $(W)/root/bin $(W)/root/etc $(W)/root/dev $(W)/root/usr/adm $(W)/root/usr/bin $(W)/root/tmp
	cp $(addprefix $(W)/bin/,$(ROOTBIN)) $(W)/root/bin/
	cp $(W)/inetovl/inet.aout $(W)/root/bin/inet
	for l in rm mv ln; do cp $(W)/bin/cp $(W)/root/bin/$$l; done
	cp $(TOOLS)/root/etc/* $(W)/root/etc/
	chmod 755 $(W)/root/bin/*
	touch $@
$(W)/root.img: $(W)/root.stamp $(TOOLS)/mkminixfs.py
	python3 $(TOOLS)/mkminixfs.py -o $@ \
	  --dev /dev/console:c:4:0:0620 --dev /dev/ram:b:1:0:0600 --dev /dev/null:c:1:3:0666 \
	  --dev /dev/mem:c:1:1:0640 --dev /dev/tty:c:5:0:0666 --dev /dev/tty00:c:4:16:0620 \
	  --dev /dev/log:c:4:15:0222 --dev /dev/ip:c:7:0:0600 $(W)/root

# --- the kernel and the flash image

$(W)/flash.bin: $(KSRC) $(W)/root.img $(W)/mm.aout $(W)/fs.aout $(W)/init.aout $(OUT)/libc.a $(TOOLS)/mkflash.py
	ACK=$(ACK) OUT=$(OUT) W=$(W)/kernel ROOTFS=$(W)/root.img \
	  sh $(M)/src/kernel/stackette/build.sh $(W)/mm.aout.out $(W)/fs.aout.out $(W)/init.aout.out
	cp $(W)/kernel/flash.bin $@

clean:
	rm -rf $(W)
