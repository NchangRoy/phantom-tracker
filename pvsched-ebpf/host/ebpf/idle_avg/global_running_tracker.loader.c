#include "global_running_tracker.skel.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "register_vm_bpf.h"
#include "phantom_tracker.h"

static struct global_running_tracker_bpf *skel;

static void cleanup(int sig)
{
	global_running_tracker_bpf__destroy(skel);
	printf("cleaned up\n");
	_exit(0);
}

int main(void)
{
	__u32 key = 0;
	struct global_runnx_tracker_state init_state = {0};
	int nb_cpus;
	int state_fd;
	int pinned_state_fd;

	skel = global_running_tracker_bpf__open();
	if (!skel) {
		fprintf(stderr, "open failed\n");
		return 1;
	}

	/*
	 * Reuse an already-pinned global_tracker_state map so published
	 * averages survive across reloads, same as create_maps does for
	 * vms/vcpus/map_registry.
	 */
	pinned_state_fd = bpf_obj_get(PIN_PATH_GLOBAL_TRACKER_STATE);
	if (pinned_state_fd >= 0) {
		printf("reusing pinned global_tracker_state map\n");
		if (bpf_map__reuse_fd(skel->maps.global_tracker_state, pinned_state_fd) < 0) {
			perror("reuse global_tracker_state map");
			goto cleanup_skel;
		}
	}

	if (global_running_tracker_bpf__load(skel)) {
		fprintf(stderr, "load failed\n");
		goto cleanup_skel;
	}

	nb_cpus = libbpf_num_possible_cpus();
	if (nb_cpus < 0) {
		fprintf(stderr, "libbpf_num_possible_cpus failed: %d\n", nb_cpus);
		goto cleanup_skel;
	}

	state_fd = bpf_map__fd(skel->maps.global_tracker_state);

	/* Only (re)initialize a freshly created map — overwriting a reused,
	 * pinned map here would wipe out the state it was pinned to preserve. */
	if (pinned_state_fd < 0) {
		init_state.nb_cpus = nb_cpus;
		if (bpf_map_update_elem(state_fd, &key, &init_state, BPF_ANY) < 0) {
			perror("initialize global_tracker_state entry");
			goto cleanup_skel;
		}
	}

	if (global_running_tracker_bpf__attach(skel)) {
		fprintf(stderr, "attach failed\n");
		goto cleanup_skel;
	}

	if (pinned_state_fd < 0) {
		if (bpf_obj_pin(state_fd, PIN_PATH_GLOBAL_TRACKER_STATE) < 0) {
			perror("pin global_tracker_state");
			goto cleanup_skel;
		}
		printf("pinned global_tracker_state map\n");
	}

	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);

	printf("attached — polling direct rq->nr_running across %d cpus\n", nb_cpus);

	for (;;) {
		struct global_runnx_tracker_state cur = {0};

		if (bpf_map_lookup_elem(state_fd, &key, &cur) == 0) {
			printf("[%ld] avg=%llu idle_avg=%llu\n",
			       (long)time(NULL),
			       (unsigned long long)cur.global_runnx_avg,
			       (unsigned long long)cur.last_idle_avg);
			fflush(stdout);
		}

		sleep(1);
	}

	return 0;

cleanup_skel:
	global_running_tracker_bpf__destroy(skel);
	return 1;
}
