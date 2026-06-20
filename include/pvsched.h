/*
 * SPDX-FileCopyrightText: 2021-2025 Inria
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Contributors:
 *   Humans: Himadri Chhaya-Shailesh, Jean-Pierre Lozi, Julia Lawall
 *   AI: Claude Sonnet 4.6, ChatGPT-5.5
 */

#ifndef _PVSCHED_H
#define _PVSCHED_H

/* Kbuild should define this for the pvsched-shmem kernel modules */
#ifdef __KERNEL__

#include <linux/types.h>
typedef u64 pvsched_u64;

/* 
 * GCC / Clang should define this for the eBPF programs.
 * eBPF programs should include vmlinux.h first. 
 */
#elif defined(__BPF__)

typedef __UINT64_TYPE__ pvsched_u64;

/* Userspace libgomp case */
#else

#include <stdint.h>
typedef uint64_t pvsched_u64;

#endif /* Kernel || eBPF || Userspace */

/* 
 * Best effort cache-line-alignment using padding because
 * __attribute__((aligned(...))) is not allowed in eBPF.
 */
struct hg_message {
	pvsched_u64 msg;
	pvsched_u64 padding [7];
};

#define HOST_IVSHMEM_MSG_SIZE sizeof(struct hg_message)

#ifndef PVSCHED_IVSHMEM_PAGE_SIZE
#error "PVSCHED_IVSHMEM_PAGE_SIZE must be defined by the Makefile for eBPF programs and by the .c file for the kernel module"
#endif

#define NR_HOST_IVSHMEM_MSGS \
		(PVSCHED_IVSHMEM_PAGE_SIZE / HOST_IVSHMEM_MSG_SIZE)

#endif /* _PVSCHED_H */