APPS = syscall
bpftool = $(shell which bpftool || ../tools/bpftool)
LIBBPF_SRC := $(abspath ../libbpf/src)
LIBBPF_OBJ := $(abspath libbpf/libbpf.a)
SYS_LIBBPF_CFLAGS := $(shell pkg-config --cflags libbpf 2>/dev/null)
SYS_LIBBPF_LIBS := $(shell pkg-config --libs libbpf 2>/dev/null)
HAVE_LOCAL_LIBBPF := $(wildcard $(LIBBPF_OBJ))
HAVE_SYS_LIBBPF := $(shell pkg-config --exists libbpf && echo yes || echo no)

INCLUDES_LOCAL := -Ilibbpf/usr/include -I../libbpf/include/uapi -I/usr/include/x86_64-linux-gnu -I.
INCLUDES_SYS := $(SYS_LIBBPF_CFLAGS) -I/usr/include/x86_64-linux-gnu -I.
LDLIBS_LOCAL := $(LIBBPF_OBJ) -lelf -lz -lzstd
LDLIBS_SYS := $(SYS_LIBBPF_LIBS)

ifeq ($(HAVE_LOCAL_LIBBPF),)
  ifeq ($(HAVE_SYS_LIBBPF),yes)
    INCLUDES := $(INCLUDES_SYS)
    LDLIBS := $(LDLIBS_SYS)
    APP_LIB_DEPS :=
    NEED_BUILD_LOCAL_LIBBPF := no
  else
    INCLUDES := $(INCLUDES_LOCAL)
    LDLIBS := $(LDLIBS_LOCAL)
    APP_LIB_DEPS := $(LIBBPF_OBJ)
    NEED_BUILD_LOCAL_LIBBPF := yes
  endif
else
  INCLUDES := $(INCLUDES_LOCAL)
  LDLIBS := $(LDLIBS_LOCAL)
  APP_LIB_DEPS := $(LIBBPF_OBJ)
  NEED_BUILD_LOCAL_LIBBPF := no
endif

.PHONY: all
all: $(APPS)

$(APPS): %: %.bpf.c %.c $(APP_LIB_DEPS) $(wildcard %.h)
	clang -g -O2 -target bpf -D__TARGET_ARCH_x86 $(INCLUDES) -c $@.bpf.c -o $@.bpf.o
	$(bpftool) gen skeleton $@.bpf.o > $@.skel.h
	clang -g -O2 -Wall $(INCLUDES) -c $@.c -o $@.o
	clang -Wall -O2 -g $@.o $(LDLIBS) -o $@
	# clang -Wall -O2 -g $@.o -static $(LIBBPF_OBJ) -lelf -lz -lzstd -o $@

vmlinux:
	$(bpftool) btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

libbpf: $(LIBBPF_OBJ)

ifeq ($(NEED_BUILD_LOCAL_LIBBPF),yes)
$(LIBBPF_OBJ): $(wildcard $(LIBBPF_SRC)/*.[ch] $(LIBBPF_SRC)/Makefile)
	make -C $(LIBBPF_SRC) BUILD_STATIC_ONLY=1 OBJDIR=$(dir $@) DESTDIR=$(dir $@) install
else
$(LIBBPF_OBJ):
	@true
endif

format:
	VERSION_CONTROL=none indent -linux *.h *.c

clean:
	rm -rf $(APPS) *.o
