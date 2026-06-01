#include "tc.skel.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

static struct tc_bpf *skel;
static struct bpf_tc_hook hook;
static struct bpf_tc_opts opts;

static void cleanup(int sig) {
    bpf_tc_detach(&hook, &opts);
    bpf_tc_hook_destroy(&hook);
    tc_bpf__destroy(skel);
    printf("cleaned up\n");
    _exit(0);
}

int main() {
    signal(SIGINT,  cleanup);
    signal(SIGTERM, cleanup);

    skel = tc_bpf__open_and_load();
    if (!skel) { fprintf(stderr, "open/load failed\n"); return 1; }

    LIBBPF_OPTS(bpf_tc_hook, h, .ifindex = 1, .attach_point = BPF_TC_INGRESS);
    LIBBPF_OPTS(bpf_tc_opts, o, .prog_fd = bpf_program__fd(skel->progs.tc_prog));
    hook = h; opts = o;

    bpf_tc_hook_create(&hook);
    if (bpf_tc_attach(&hook, &opts)) {
        fprintf(stderr, "tc attach failed\n"); return 1;
    }

    printf("attached — send any packet to lo to start timer\n");
    printf("watch: cat /sys/kernel/debug/tracing/trace_pipe\n");
    pause();
    return 0;
}
