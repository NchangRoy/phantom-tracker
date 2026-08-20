/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reads the latest phantom average published by the host, by mmap()ing
 * the H2G page exposed by /dev/guest_ivshmem (see guest_ivshmem_mmap()
 * and H2G_LATEST_SLOT in pvsched.h).
 *
 * Build:  gcc -O2 -I<path-to-include> -o read_phantom_avg read_phantom_avg.c
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "pvsched.h"

#define GUEST_IVSHMEM_DEV "/dev/guest_ivshmem"
#define PAGE_SIZE 4096
int main(void)
{
	int fd = open(GUEST_IVSHMEM_DEV, O_RDONLY);
	if (fd < 0) {
		perror("open " GUEST_IVSHMEM_DEV);
		return 1;
	}

	

	void *map = mmap(NULL, PAGE_SIZE , PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return 1;
	}

	struct hg_message *slots = map;
	long phantom_avg = slots[H2G_LATEST_SLOT].msg;

	printf("phantom average: %llu\n", (unsigned long long)phantom_avg);

	munmap(map, PAGE_SIZE);
	close(fd);
	return 0;
}
