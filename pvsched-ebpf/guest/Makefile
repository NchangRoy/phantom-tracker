BPF_CC ?= bpf-gcc
CC ?= gcc

# -gbtf tells bpf-gcc to embed BTF into the object (bpftool gen skeleton needs it).
# Plain -g produces only DWARF and will cause the "BTF required" error.
BPF_CFLAGS ?= -O2 -gbtf -Wall -Wextra -Wno-unused-parameter
CFLAGS ?= -O2 -g -Wall -Wextra

PROJECT_INCLUDE := ../../include
PVSCHED_HDR := $(PROJECT_INCLUDE)/pvsched.h

LIBBPF_CFLAGS := $(shell pkg-config --cflags libbpf 2>/dev/null)
LIBBPF_LIBS := $(shell pkg-config --libs libbpf 2>/dev/null || echo -lbpf -lelf -lz)

BPF_INCLUDE := -I. -I/usr/include -I$(PROJECT_INCLUDE)

.PHONY: all clean check

all: omp_thread_reg.loader

# Step 1: compile BPF C -> BPF object (vmlinux.h not needed; standard headers used)
omp_thread_reg.bpf.o: omp_thread_reg.bpf.c
	$(BPF_CC) $(BPF_CFLAGS) $(BPF_INCLUDE) -c $< -o $@

# Step 3: generate skeleton header from BPF object
omp_thread_reg.skel.h: omp_thread_reg.bpf.o
	bpftool gen skeleton $< > $@

# Step 4: compile userspace loader (depends on skeleton)
omp_thread_reg.loader: omp_thread_reg.loader.c omp_thread_reg.skel.h
	$(CC) $(CFLAGS) $(LIBBPF_CFLAGS) -I. $< -o $@ $(LIBBPF_LIBS)

check:
	@command -v $(BPF_CC) >/dev/null || { echo "missing BPF compiler: $(BPF_CC)"; exit 1; }
	@command -v bpftool >/dev/null || { echo "missing bpftool"; exit 1; }
	@pkg-config --exists libbpf || echo "warning: pkg-config cannot find libbpf"
	@test -f $(PVSCHED_HDR) || { echo "missing shared header: $(PVSCHED_HDR)"; exit 1; }

clean:
	rm -f omp_thread_reg.bpf.o omp_thread_reg.skel.h omp_thread_reg.loader
