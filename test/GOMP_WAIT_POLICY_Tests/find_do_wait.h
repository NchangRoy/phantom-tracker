#ifndef __FIND_DO_WAIT_H__
#define __FIND_DO_WAIT_H__

/*
 * find_do_wait.h — header-only helper: finds every file offset of inlined
 * do_wait bodies inside a libgomp shared library by disassembling it and
 * matching the signature:
 *
 *   endbr64                         ← CET function entry
 *   ... (≤ LOOKAHEAD_SPIN lines)
 *   pause                           ← cpu_relax() inside do_spin
 *   ... (anywhere in same function body, including before pause)
 *   mov eax,0xca  OR               ← __NR_futex = 202 loaded into eax, OR
 *   mov r13d,0xca                  ← loaded via staging register (hoisted)
 *   ... (≤ LOOKAHEAD_SYSCALL lines after the 0xca load)
 *   syscall                         ← actual futex syscall
 *
 * Because do_wait is inlined and stripped it carries no symbol name.
 * libgomp typically contains multiple copies (one per barrier variant),
 * all of which are returned so the caller can attach a uprobe to each.
 *
 * Usage:
 *   uint64_t offsets[32];
 *   int n = find_do_wait_offsets(libgomp_path, offsets, 32);
 *   for (int i = 0; i < n; i++) { ... attach uprobe at offsets[i] ... }
 *
 * Include this header in any translation unit that needs the function.
 * The 'static' qualifier gives each TU its own copy (no ODR issues).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define _DW_LOOKAHEAD_SPIN    60   /* endbr64 → pause                  */
#define _DW_LOOKAHEAD_FUTEX   80   /* search window past pause for 0xca */
#define _DW_LOOKAHEAD_SYSCALL 150  /* 0xca load → syscall (hoisted)    */
#define _DW_MAX_LINES         600000
#define _DW_LINE_CAP          512

/*
 * _dw_parse_addr — extract the hex address at the start of an objdump line.
 *   "   20930:  f3 0f 1e fa   endbr64"  →  0x20930
 * Returns 0 if the line doesn't begin with a valid hex address.
 */
static uint64_t _dw_parse_addr(const char *line)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if ((*p < '0' || *p > '9') && (*p < 'a' || *p > 'f')) return 0;
    char *end;
    uint64_t addr = (uint64_t)strtoull(p, &end, 16);
    if (*end != ':') return 0;
    return addr;
}

/*
 * find_do_wait_offsets — scan libgomp for all do_wait body offsets.
 *
 * Parameters:
 *   libgomp   — path to the libgomp shared library.
 *   out       — caller-allocated array to receive the found offsets.
 *   out_max   — capacity of out[].
 *
 * Returns the number of offsets written to out[], or -1 on error.
 */
static int find_do_wait_offsets(const char *libgomp,
                                uint64_t   *out,
                                int         out_max)
{
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "objdump -d -M intel %s 2>/dev/null", libgomp);

    FILE *fp = popen(cmd, "r");
    if (!fp) { perror("popen objdump"); return -1; }

    /* Read all disassembly lines into memory */
    int    capacity = _DW_MAX_LINES, nlines = 0;
    char **lines    = malloc(capacity * sizeof(char *));
    if (!lines) { pclose(fp); return -1; }

    char buf[_DW_LINE_CAP];
    while (fgets(buf, sizeof(buf), fp)) {
        if (nlines >= capacity) {
            fprintf(stderr,
                    "find_do_wait: objdump output truncated at %d lines\n",
                    capacity);
            break;
        }
        lines[nlines++] = strdup(buf);
    }
    pclose(fp);

    int found = 0;

    for (int i = 0; i < nlines && found < out_max; i++) {

        /* Step 1: endbr64 — candidate function entry */
        if (!strstr(lines[i], "endbr64"))
            continue;

        uint64_t fn_addr = _dw_parse_addr(lines[i]);
        if (!fn_addr)
            continue;

        /* Step 2: pause within LOOKAHEAD_SPIN lines (cpu_relax inside do_spin) */
        int pause_idx = -1;
        int spin_end  = i + _DW_LOOKAHEAD_SPIN;
        if (spin_end > nlines) spin_end = nlines;

        for (int j = i + 1; j < spin_end; j++) {
            if (strstr(lines[j], "endbr64")) break; /* new function */
            if (strstr(lines[j], "\tpause") || strstr(lines[j], " pause")) {
                pause_idx = j;
                break;
            }
        }
        if (pause_idx < 0)
            continue;

        /*
         * Step 3: look for NR_futex (0xca) loaded into eax or any rNd
         * (r8d-r15d) register BEFORE the pause instruction.
         *
         * In do_wait the compiler always hoists the NR_futex load before the
         * spin loop (before pause). False positives like omp_test_nest_lock
         * and gomp_mutex_lock_slow also have a pause + futex pattern, but
         * their 0xca loads appear AFTER pause in the retry body.
         *
         * Search window: from endbr64 up to (but not past) the pause line.
         *
         * Accepted destinations:  eax,  r8d-r15d  (mov    r<digit>...)
         * Rejected destinations:  ebp, ebx, etc.  (no leading 'r' + digit)
         */
        int futex_nr_idx = -1;

        /* Scan only from endbr64 to the pause line (hoisted-load window) */
        for (int j = i + 1; j < pause_idx; j++) {
            if (!strstr(lines[j], "0xca")) continue;

            /* Accept mov eax,0xca directly */
            if (strstr(lines[j], "mov    eax,0xca")) {
                futex_nr_idx = j;
                break;
            }

            /*
             * Accept mov rNd,0xca for any N that is a digit (r8d-r15d).
             * Find "mov    r" and verify the character after 'r' is a digit.
             * This rejects "mov    ebp,0xca" ("ebp" starts with 'e').
             */
            char *p = lines[j];
            while ((p = strstr(p, "mov    r")) != NULL) {
                char after_r = p[8]; /* char right after 'r' in "mov    r" */
                if (after_r >= '0' && after_r <= '9') {
                    if (strstr(p, ",0xca")) {
                        futex_nr_idx = j;
                        break;
                    }
                }
                p++;
            }
            if (futex_nr_idx >= 0) break;
        }
        if (futex_nr_idx < 0)
            continue;

        /* Step 4: syscall within LOOKAHEAD_SYSCALL lines after the 0xca load */
        int sc_end = futex_nr_idx + _DW_LOOKAHEAD_SYSCALL;
        if (sc_end > nlines) sc_end = nlines;

        int matched = 0;
        for (int j = futex_nr_idx + 1; j < sc_end; j++) {
            if (strstr(lines[j], "endbr64")) break;
            if (strstr(lines[j], "\tsyscall") || strstr(lines[j], " syscall")) {
                matched = 1;
                break;
            }
        }
        if (!matched)
            continue;

        out[found++] = fn_addr;
    }

    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    return found;
}

#undef _DW_LOOKAHEAD_SPIN
#undef _DW_LOOKAHEAD_FUTEX
#undef _DW_LOOKAHEAD_SYSCALL
#undef _DW_MAX_LINES
#undef _DW_LINE_CAP

#endif /* __FIND_DO_WAIT_H__ */
