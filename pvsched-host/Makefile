# ============================================================
# Compiler configuration
# ============================================================
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
INCLUDES = -Iuser -Iinclude

# Final userspace target (VM registration binary)
TARGET = bin/register_vm

# Source files for userspace application
SRCS = user/register_vm.c user/linkedlist.c

# ============================================================
# Default target
# ============================================================
all: bpf userspace

# ============================================================
# Create output directory
# ============================================================
bin:
	mkdir -p bin

# ============================================================
# ============================================================
# eBPF BUILD PIPELINE (RUNS FIRST)
# ============================================================
# Step order:
#   1. compile .bpf.c -> .bpf.o
#   2. generate skeleton header (.skel.h)
#   3. build loader (userspace program that loads BPF)
# ============================================================

BPF_INCLUDE = -Iinclude
# Build eBPF loader (userspace program that loads BPF into kernel)
BPF_CFLAGS := $(shell pkg-config --cflags libbpf)
BPF_LDLIBS := $(shell pkg-config --libs libbpf)

# Compile eBPF program into BPF object file
include/vmlinux.h: | include
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > $@

include:
	mkdir -p include



bin/create_maps.bpf.o: ebpf/create_maps/create_maps.bpf.c | bin include/vmlinux.h
	clang -g -O2 -target bpf -c $< -o $@ $(BPF_INCLUDE)

# Generate skeleton header from BPF object
include/create_maps.skel.h: bin/create_maps.bpf.o
	bpftool gen skeleton $< > $@


bin/create_maps.loader: include/create_maps.skel.h ebpf/create_maps/create_maps.loader.c
	gcc ebpf/create_maps/create_maps.loader.c -o $@ $(BPF_CFLAGS) $(BPF_LDLIBS) $(BPF_INCLUDE)

# eBPF meta target
bpf: bin/create_maps.loader

# ============================================================
# ============================================================
# USERSPACE VM REGISTRATION PROGRAM
# ============================================================
# Depends on:
#   - compiled C sources
#   - eBPF system already built (maps + loader ready)
# ============================================================

userspace: bin $(SRCS)
	$(CC) $(CFLAGS) $(BPF_CFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET) $(BPF_LDLIBS)

# ============================================================
# RUN TARGETS
# ============================================================

run_bpf: bpf
	sudo ./bin/create_maps.loader


# ============================================================
# CLEAN BUILD ARTIFACTS
# ============================================================

clean:
	rm -rf bin include/create_maps.skel.h