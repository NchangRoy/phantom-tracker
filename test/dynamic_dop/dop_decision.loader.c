// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Loader for dop_decision.bpf.c — skeleton-based approach.
 *
 * Attaches gomp_dop_decision_handler to usdt:libgomp:gomp:dop_decision,
 * fired once per parallel region by pv_gomp_dynamic_max_threads() when
 * GOMP_DYNAMIC_POLICY=pvsched is active.
 *
 * Remains attached until SIGINT or SIGTERM (Ctrl+C) is received.
 *
 * The libgomp path is fixed at build time via the LIBGOMP_PATH make
 * variable, e.g.:
 *
 *   make LIBGOMP_PATH=/home/fureh_mitoto/gcc-install/lib64/libgomp.so.1
 *
 * Run as root.
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include "dop_decision.skel.h"

#ifndef LIBGOMP_PATH
#error "LIBGOMP_PATH is not set. Build with: make LIBGOMP_PATH=/path/to/libgomp.so.1"
#endif

static volatile sig_atomic_t exiting = 0;

static void handle_signal(int sig)
{
	(void)sig;
	exiting = 1;
}

int main(void)
{
	struct dop_decision_bpf *skel = NULL;
	struct bpf_link *dop_decision_link = NULL;
	const char *libgomp_path = LIBGOMP_PATH;
	int err = 0;

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	skel = dop_decision_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "failed to open/load BPF skeleton\n");
		return 1;
	}

	dop_decision_link = bpf_program__attach_usdt(
		skel->progs.gomp_dop_decision_handler, -1, libgomp_path,
		"gomp", "dop_decision", NULL);
	err = libbpf_get_error(dop_decision_link);
	if (err) {
		dop_decision_link = NULL;
		fprintf(stderr,
			"failed to attach usdt:%s:gomp:dop_decision: %s\n",
			libgomp_path, strerror(-err));
		goto cleanup;
	}
	printf("Attached → %s : gomp:dop_decision (gomp_dop_decision_handler)\n",
	       libgomp_path);

	printf("\nAttached. Running... Press Ctrl+C to exit.\n");

	while (!exiting)
		sleep(1);

	printf("\nExiting and detaching...\n");

cleanup:
	bpf_link__destroy(dop_decision_link);
	dop_decision_bpf__destroy(skel);
	return err ? 1 : 0;
}
