/* SPDX-License-Identifier: GPL-3.0 with GCC-exception-3.1
 *
 * Contributors:
 *   Human: Himadri Chhaya-Shailesh
 *   AI: ChatGPT-5.6
 */
#ifndef _EBPF_COMPATIBILITY_H
#define _EBPF_COMPATIBILITY_H

#include <linux/btf.h>
#include <linux/btf_ids.h>

/*
 * Newer kernels provide helpers that annotate kfunc definitions and suppress
 * the warnings caused by their intentional lack of normal C declarations.
 * Older kernels require the diagnostic suppression to be written explicitly.
 */
#ifdef __bpf_kfunc_start_defs
#define PVSCHED_KFUNC_DEFS_START()	__bpf_kfunc_start_defs()
#define PVSCHED_KFUNC_DEFS_END()	__bpf_kfunc_end_defs()
#else
#define PVSCHED_KFUNC_DEFS_START()				\
	__diag_push();						\
	__diag_ignore_all("-Wmissing-prototypes",		\
			  "Global kfunc definitions are exposed through BTF")
#define PVSCHED_KFUNC_DEFS_END()	__diag_pop()
#endif

/*
 * Newer kernels use __bpf_kfunc to prevent a kfunc from being inlined or
 * removed by the compiler. Older kernels rely on the function having external
 * linkage and being included in the registered BTF ID set.
 */
#ifdef __bpf_kfunc
#define PVSCHED_KFUNC			__bpf_kfunc
#else
#define PVSCHED_KFUNC
#endif

/*
 * Newer kernels distinguish kfunc ID sets from ordinary BTF ID sets.
 * Older kernels register the same function IDs through BTF_SET8_START().
 */
#ifdef BTF_KFUNCS_START
#define PVSCHED_KFUNCS_START(name)	BTF_KFUNCS_START(name)
#define PVSCHED_KFUNCS_END(name)	BTF_KFUNCS_END(name)
#else
#define PVSCHED_KFUNCS_START(name)	BTF_SET8_START(name)
#define PVSCHED_KFUNCS_END(name)	BTF_SET8_END(name)
#endif

#endif /* _EBPF_COMPATIBILITY_H */