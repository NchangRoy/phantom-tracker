// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Loader for barrier_sync.bpf.c — skeleton-based approach.
 *
 * Attaches four programs to the OpenMP / kernel stack:
 *   1. gomp_switch_handler          → tp/sched/sched_switch
 *      Attached via bpf_program__attach().
 *
 *   2. gomp_futex_enter             → tp/syscalls/sys_enter_futex
 *      Attached via bpf_program__attach().
 *
 *   3. gomp_do_wait_handler         → usdt:libgomp:gomp:do_wait
 *      Attached via bpf_program__attach_usdt().
 *
 *   4. gomp_phantom_average_handler → usdt:libgomp:gomp:phantom_average
 *      Attached via bpf_program__attach_usdt().
 *
 * All programs remain attached until SIGINT or SIGTERM (Ctrl+C) is received.
 *
 * The libgomp path is fixed at build time via the LIBGOMP_PATH make
 * variable, e.g.:
 *
 *   make LIBGOMP_PATH=/home/fureh_mitoto/gcc-install/lib64/libgomp.so.1
 *
 * Prerequisites:
 *   - omp_thread_reg loader must be running (omp_threads_map pinned at
 *     /sys/fs/bpf/omp_threads_map).
 *   - Run as root.
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
#include "barrier_sync.skel.h"

#ifndef LIBGOMP_PATH
#error "LIBGOMP_PATH is not set. Build with: make LIBGOMP_PATH=/path/to/libgomp.so.1"
#endif

/* Reuse the omp_threads_map already pinned by omp_thread_reg */
#define PIN_OMP_THREADS_MAP "/sys/fs/bpf/omp_threads_map"

static volatile sig_atomic_t exiting = 0;

static void handle_signal(int sig)
{
	(void)sig;
	exiting = 1;
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void)
{
	struct barrier_sync_bpf *skel = NULL;
	struct bpf_link *switch_link = NULL;
	struct bpf_link *futex_link = NULL;
	struct bpf_link *do_wait_link = NULL;
	struct bpf_link *phantom_avg_link = NULL;

	const char *libgomp_path = LIBGOMP_PATH;
	int err = 0;

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	/* 1 — open BPF skeleton */
	skel = barrier_sync_bpf__open();
	if (!skel) {
		fprintf(stderr, "failed to open BPF skeleton\n");
		return 1;
	}

	/*
	 * 2 — reuse the omp_threads_map pinned by omp_thread_reg BEFORE load()
	 *     so BPF programs query the active pinned map.
	 */
	int pinned_map_fd = bpf_obj_get(PIN_OMP_THREADS_MAP);
	if (pinned_map_fd < 0) {
		fprintf(stderr,
			"error: cannot open pinned omp_threads_map at %s: %s\n"
			"       Is the omp_thread_reg loader running?\n",
			PIN_OMP_THREADS_MAP, strerror(errno));
		err = -1;
		goto cleanup;
	}
	if (bpf_map__reuse_fd(skel->maps.omp_threads_map, pinned_map_fd) < 0) {
		perror("bpf_map__reuse_fd omp_threads_map");
		close(pinned_map_fd);
		err = -1;
		goto cleanup;
	}
	close(pinned_map_fd);
	printf("Reusing pinned omp_threads_map from omp_thread_reg loader\n");

	err = barrier_sync_bpf__load(skel);
	if (err) {
		fprintf(stderr, "failed to load BPF skeleton\n");
		goto cleanup;
	}

	/* 3a — attach sched_switch tracepoint */
	switch_link = bpf_program__attach(skel->progs.gomp_switch_handler);
	err = libbpf_get_error(switch_link);
	if (err) {
		switch_link = NULL;
		fprintf(stderr, "failed to attach tp/sched/sched_switch: %s\n",
			strerror(-err));
		goto cleanup;
	}
	printf("Attached → tp/sched/sched_switch          (gomp_switch_handler)\n");

	/* 3b — attach sys_enter_futex tracepoint */
	futex_link = bpf_program__attach(skel->progs.gomp_futex_enter);
	err = libbpf_get_error(futex_link);
	if (err) {
		futex_link = NULL;
		fprintf(stderr,
			"failed to attach tp/syscalls/sys_enter_futex: %s\n",
			strerror(-err));
		goto cleanup;
	}
	printf("Attached → tp/syscalls/sys_enter_futex    (gomp_futex_enter)\n");

	/* 3c — attach usdt:libgomp:gomp:do_wait */
	do_wait_link = bpf_program__attach_usdt(skel->progs.gomp_do_wait_handler,
						 -1, libgomp_path, "gomp",
						 "do_wait", NULL);
	err = libbpf_get_error(do_wait_link);
	if (err) {
		do_wait_link = NULL;
		fprintf(stderr, "failed to attach usdt:%s:gomp:do_wait: %s\n",
			libgomp_path, strerror(-err));
		goto cleanup;
	}
	printf("Attached → %s : gomp:do_wait         (gomp_do_wait_handler)\n",
	       libgomp_path);

	/* 3d — attach usdt:libgomp:gomp:phantom_average */
	phantom_avg_link = bpf_program__attach_usdt(
		skel->progs.gomp_phantom_average_handler, -1, libgomp_path,
		"gomp", "phantom_average", NULL);
	err = libbpf_get_error(phantom_avg_link);
	if (err) {
		phantom_avg_link = NULL;
		fprintf(stderr,
			"failed to attach usdt:%s:gomp:phantom_average: %s\n",
			libgomp_path, strerror(-err));
		goto cleanup;
	}
	printf("Attached → %s : gomp:phantom_average (gomp_phantom_average_handler)\n",
	       libgomp_path);

	printf("\nAll programs attached. Running... Press Ctrl+C to exit.\n");

	while (!exiting)
		sleep(1);

	printf("\nExiting and detaching programs...\n");

cleanup:
	bpf_link__destroy(phantom_avg_link);
	bpf_link__destroy(do_wait_link);
	bpf_link__destroy(futex_link);
	bpf_link__destroy(switch_link);
	barrier_sync_bpf__destroy(skel);
	return err ? 1 : 0;
}
