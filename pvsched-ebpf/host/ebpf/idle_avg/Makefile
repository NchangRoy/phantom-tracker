# Step order: .bpf.c -> .bpf.o -> .skel.h -> loader

CC ?= gcc
BPF_CC ?= bpf-gcc

BPF_PROG_CFLAGS ?= -std=gnu11 -O2 -g -gbtf -Wall -Wextra -Wno-unused-parameter
BPF_PROG_CFLAGS += -DPVSCHED_IVSHMEM_PAGE_SIZE=$(shell getconf PAGE_SIZE)
BPF_PROG_CFLAGS += -D__TARGET_ARCH_x86

HOST_INCLUDE := ../../include
PROJECT_INCLUDE := ../../../../include
BPF_INCLUDE := -Ibin -I$(HOST_INCLUDE) -I$(PROJECT_INCLUDE) -I/usr/include

BPF_CFLAGS := $(shell pkg-config --cflags libbpf)
BPF_LDLIBS := $(shell pkg-config --libs libbpf)

all: bin/global_running_tracker.loader

bin:
	mkdir -p bin

# struct bpf_timer has no kernel-BTF representation (kernel only carries
# bpf_timer_kern internally), so bpftool never emits it — append by hand.
bin/vmlinux.h: | bin
	bpftool btf dump file /sys/kernel/btf/vmlinux format c > $@
	grep -q '^struct bpf_timer {' $@ || \
		echo 'struct bpf_timer { __u64 __opaque[2]; } __attribute__((aligned(8)));' >> $@

# A real `for` loop around bpf_per_cpu_ptr() is unreliable on this bpf-gcc
# (12.0.1) — miscompiled as a false "indirect call". Manually duplicated
# blocks compile reliably instead, generated here sized to NB_CPUS so
# coverage still tracks the real CPU count.
NB_CPUS := $(shell nproc)

bin/global_running_tracker_cpu_loop.h: | bin
	@for i in $$(seq 0 $$(($(NB_CPUS) - 1))); do \
		printf '\t{\n\t\tstruct rq *rq%d = bpf_per_cpu_ptr(&runqueues, %d);\n\t\tif (rq%d)\n\t\t\tsample += rq%d->nr_running;\n\t}\n' $$i $$i $$i $$i; \
	done > $@

bin/global_running_tracker.bpf.o: global_running_tracker.bpf.c bin/global_running_tracker_cpu_loop.h bin/vmlinux.h $(PROJECT_INCLUDE)/pvsched.h | bin
	$(BPF_CC) $(BPF_PROG_CFLAGS) $(BPF_INCLUDE) -c $< -o $@

bin/global_running_tracker.skel.h: bin/global_running_tracker.bpf.o
	bpftool gen skeleton $< > $@

bin/global_running_tracker.loader: bin/global_running_tracker.skel.h global_running_tracker.loader.c
	$(CC) global_running_tracker.loader.c -o $@ -Ibin $(BPF_CFLAGS) $(BPF_LDLIBS) $(BPF_INCLUDE)

run: all
	sudo ./bin/global_running_tracker.loader

clean:
	rm -rf bin
