#include "rq_compare.skel.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "register_vm_bpf.h"

static struct rq_compare_bpf *skel;

static void cleanup(int sig)
{
	rq_compare_bpf__destroy(skel);
	printf("cleaned up\n");
	_exit(0);
}

int main(void)
{
	__u32 key = 0;
	struct global_runnx_tracker_state init_state = {0};
	int nb_cpus;
	int state_fd;

	skel = rq_compare_bpf__open();
	if (!skel) {
		fprintf(stderr, "open failed\n");
		return 1;
	}

	if (rq_compare_bpf__load(skel)) {
		fprintf(stderr, "load failed\n");
		goto cleanup_skel;
	}

	nb_cpus = libbpf_num_possible_cpus();
	if (nb_cpus < 0) {
		fprintf(stderr, "libbpf_num_possible_cpus failed: %d\n", nb_cpus);
		goto cleanup_skel;
	}

	init_state.nb_cpus = nb_cpus;

	state_fd = bpf_map__fd(skel->maps.global_tracker_state);
	if (bpf_map_update_elem(state_fd, &key, &init_state, BPF_ANY) < 0) {
		perror("initialize global_tracker_state entry");
		goto cleanup_skel;
	}

	if (rq_compare_bpf__attach(skel)) {
		fprintf(stderr, "attach failed\n");
		goto cleanup_skel;
	}

	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);

	printf("attached — polling direct rq->nr_running across %d cpus\n", nb_cpus);

	for (;;) {
		struct global_runnx_tracker_state cur = {0};

		if (bpf_map_lookup_elem(state_fd, &key, &cur) == 0 &&
		    cur.published_window_duration_ns > 0) {
			double avg = (double)cur.published_weighted_sum_ns /
				     (double)cur.published_window_duration_ns;
			double idle_avg = avg > nb_cpus ? 0.0 : nb_cpus - avg;

			printf("[%ld] avg=%.2f idle_avg=%.2f\n",
			       (long)time(NULL), avg, idle_avg);
			fflush(stdout);
		}

		sleep(1);
	}

	return 0;

cleanup_skel:
	rq_compare_bpf__destroy(skel);
	return 1;
}
