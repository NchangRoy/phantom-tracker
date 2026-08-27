// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Loader for omp_thread_reg.bpf.c — skeleton-based approach.
 *
 * Attaches to libgomp (x86-64), system-wide (-1):
 *   1. capture_omp_master_thread  → GOMP_parallel        (uprobe, exported symbol name)
 *   2. capture_omp_worker_threads → usdt:gomp:gomp_thread_start
 *
 * The libgomp path is fixed at build time via the LIBGOMP_PATH make
 * variable, e.g.:
 *
 *   make LIBGOMP_PATH=/home/fureh_mitoto/gcc-install/lib64/libgomp.so.1
 *
 * libbpf cannot pin a USDT link (bpf_program__attach_usdt() may attach
 * several underlying uprobes — one per inlined call site — behind a single
 * synthetic bpf_link, and that wrapper type has no pin support). So unlike
 * the other four links here, capture_omp_worker_threads only stays attached
 * for as long as this process is alive: it blocks until SIGINT/SIGTERM
 * instead of pinning-then-exiting.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "omp_thread_reg.skel.h"

#ifndef LIBGOMP_PATH
#error "LIBGOMP_PATH is not set. Build with: make LIBGOMP_PATH=/path/to/libgomp.so.1"
#endif

#define PIN_OMP_THREADS_MAP "/sys/fs/bpf/omp_threads_map"
#define PIN_MASTER_LINK "/sys/fs/bpf/links/master"
#define PIN_SWITCH_LINK "/sys/fs/bpf/links/sched_switch"
#define PIN_EXIT_LINK "/sys/fs/bpf/links/sched_process_exit"
#define PIN_EXEC_LINK "/sys/fs/bpf/links/sched_process_exec"

static volatile sig_atomic_t exiting = 0;

static void handle_signal(int sig)
{
    (void)sig;
    exiting = 1;
}

int main(void)
{
    struct omp_thread_reg_bpf *skel  = NULL;
    struct bpf_link            *link = NULL, *worker_link = NULL, *switch_link = NULL;
    struct bpf_link            *exit_link = NULL, *exec_link = NULL;

    int     err = 0;
    const char *libgomp_path = LIBGOMP_PATH;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* 3 — load BPF skeleton */
    skel = omp_thread_reg_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "failed to open and load BPF skeleton\n");
        return 1;
    }

    /* 4a — attach master probe: GOMP_parallel (exported symbol) */
    LIBBPF_OPTS(bpf_uprobe_opts, uprobe_master_opts,
        .func_name = "GOMP_parallel",
    );
    link = bpf_program__attach_uprobe_opts(
        skel->progs.capture_omp_master_thread,
        -1, libgomp_path, 0, &uprobe_master_opts);

    err = libbpf_get_error(link);
    if (err) {
        link = NULL;
        fprintf(stderr, "failed to attach uprobe → %s:GOMP_parallel: %s\n",
                libgomp_path, strerror(-err));
        goto cleanup;
    }
    printf("Attached → %s : GOMP_parallel          (master)\n", libgomp_path);

    /* 4b — attach worker probe: usdt:gomp:gomp_thread_start */
    worker_link = bpf_program__attach_usdt(
        skel->progs.capture_omp_worker_threads,
        -1,
        libgomp_path,
        "gomp",
        "gomp_thread_start",
         NULL);

    err = libbpf_get_error(worker_link);
    if (err) {
        worker_link = NULL;
        fprintf(stderr, "failed to attach usdt probe to binary %s: %s\n",
                libgomp_path, strerror(-err));
        goto cleanup;
    }

    //pin links (worker_link is a USDT link, which libbpf cannot pin — it
    //stays attached only for as long as this process keeps running)
    if (bpf_link__pin(link, PIN_MASTER_LINK) < 0) {
        perror("pin master link");
        goto cleanup;
    }
    if (bpf_map__pin(skel->maps.omp_threads_map, PIN_OMP_THREADS_MAP) < 0) {
        perror("pin omp_threads_map");
        goto cleanup;
    }
    printf("Attached → %s : gomp:gomp_thread_start (workers)\n",
           libgomp_path);

    /* 4c — attach sched_switch tracepoint */
    switch_link = bpf_program__attach(skel->progs.handle_switch);
    err = libbpf_get_error(switch_link);
    if (err) {
        switch_link = NULL;
        fprintf(stderr, "failed to attach tp/sched/sched_switch: %s\n",
                strerror(-err));
        goto cleanup;
    }
    if (bpf_link__pin(switch_link, PIN_SWITCH_LINK) < 0) {
        perror("pin sched_switch link");
        goto cleanup;
    }
    printf("Attached → tp/sched/sched_switch          (handle_switch)\n");

    /* 4d — attach sched_process_exit/exec tracepoints (stale TID cleanup) */
    exit_link = bpf_program__attach(skel->progs.remove_exited_omp_thread);
    err = libbpf_get_error(exit_link);
    if (err) {
        exit_link = NULL;
        fprintf(stderr, "failed to attach tp/sched/sched_process_exit: %s\n",
                strerror(-err));
        goto cleanup;
    }
    if (bpf_link__pin(exit_link, PIN_EXIT_LINK) < 0) {
        perror("pin sched_process_exit link");
        goto cleanup;
    }
    printf("Attached → tp/sched/sched_process_exit    (remove_exited_omp_thread)\n");

    exec_link = bpf_program__attach(skel->progs.remove_execed_omp_thread);
    err = libbpf_get_error(exec_link);
    if (err) {
        exec_link = NULL;
        fprintf(stderr, "failed to attach tp/sched/sched_process_exec: %s\n",
                strerror(-err));
        goto cleanup;
    }
    if (bpf_link__pin(exec_link, PIN_EXEC_LINK) < 0) {
        perror("pin sched_process_exec link");
        goto cleanup;
    }
    printf("Attached → tp/sched/sched_process_exec    (remove_execed_omp_thread)\n");

    printf("\nAll programs attached. Running... Press Ctrl+C to exit.\n");

    while (!exiting)
        sleep(1);

    printf("\nExiting and detaching programs...\n");

cleanup:
    bpf_link__destroy(exec_link);
    bpf_link__destroy(exit_link);
    bpf_link__destroy(switch_link);
    bpf_link__destroy(worker_link);
    bpf_link__destroy(link);
    omp_thread_reg_bpf__destroy(skel);
    return err ? 1 : 0;
}