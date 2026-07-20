/*
 * omp_test.c — enters a single parallel region and spins each thread
 * indefinitely, so the uprobe in omp_thread_reg.bpf.c fires once and the
 * resulting vCPU state should stay continuously "1" until Ctrl-C.
 *
 * Compile:  gcc -O2 -fopenmp omp_test.c -o omp_test
 * Run:      OMP_NUM_THREADS=8 ./omp_test
 *           libgomp reads OMP_NUM_THREADS directly — no C override needed.
 */
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <omp.h>

static volatile sig_atomic_t exiting;

static void sig_handler(int sig)
{
    (void)sig;
    exiting = 1;
}

int main(void)
{
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("Using %d OpenMP thread(s)  [set OMP_NUM_THREADS to change]\n\n",
           omp_get_max_threads());
    printf("Spinning indefinitely in one parallel region (Ctrl-C to stop)...\n");

    #pragma omp parallel
    {
        int omp_tid = omp_get_thread_num();
        pid_t linux_tid = gettid();
        volatile unsigned long counter = 0;

        printf("  omp_thread=%-3d  linux_tid=%-8d  total=%d\n",
               omp_tid, linux_tid, omp_get_num_threads());

        while (!exiting)
            counter++;
    }

    printf("Stopped.\n");
    return 0;
}
