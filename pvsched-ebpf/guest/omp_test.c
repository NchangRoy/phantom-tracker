/*
 * omp_test.c — triggers GOMP_parallel repeatedly so the uprobe in
 * omp_thread_reg.bpf.c fires and populates omp_threads_map.
 *
 * Compile:  gcc -O2 -fopenmp omp_test.c -o omp_test
 * Run:      OMP_NUM_THREADS=8 ./omp_test
 *           libgomp reads OMP_NUM_THREADS directly — no C override needed.
 */
#include <stdio.h>
#include <unistd.h>
#include <omp.h>

int main(void)
{
    printf("Using %d OpenMP thread(s)  [set OMP_NUM_THREADS to change]\n\n",
           omp_get_max_threads());

    /* --- Region 1: each thread prints its id and cpu --- */
    printf("[region 1] hello from each thread\n");
    #pragma omp parallel
    {
        int omp_tid = omp_get_thread_num();
        pid_t linux_tid = gettid();
        printf("  omp_thread=%-3d  linux_tid=%-8d  total=%d\n",
               omp_tid, linux_tid, omp_get_num_threads());
    }

    /* --- Region 2: parallel for loop --- */
    printf("\n[region 2] parallel for (summing 1..1000)\n");
    long sum = 0;
    #pragma omp parallel for reduction(+:sum)
    for (int i = 1; i <= 1000; i++) {
        if (i == 1)  /* print once per thread at loop start */
            printf("  [r2] omp_thread=%-3d  linux_tid=%d\n",
                   omp_get_thread_num(), gettid());
        sum += i;
    }
    printf("  sum = %ld  (expected 500500)\n", sum);

    /* --- Region 3: nested regions (each fires its own GOMP_parallel) --- */
    printf("\n[region 3] nested parallel regions\n");
    omp_set_nested(1);
    #pragma omp parallel num_threads(2)
    {
        int outer = omp_get_thread_num();
        #pragma omp parallel num_threads(2)
        {
            int inner = omp_get_thread_num();
            printf("  [r3] outer=%-2d inner=%-2d  linux_tid=%d\n",
                   outer, inner, gettid());
        }
    }

    /* --- Region 4: rapid-fire to stress the map --- */
    printf("\n[region 4] 20 rapid parallel regions\n");
    for (int r = 0; r < 20; r++) {
        #pragma omp parallel
        {
            if (r == 0)  /* print only on first iteration */
                printf("  [r4] omp_thread=%-3d  linux_tid=%d\n",
                       omp_get_thread_num(), gettid());
            volatile int x = omp_get_thread_num() * 2;
            (void)x;
        }
    }
    printf("  done\n");

    printf("\nAll regions complete. Check trace_pipe for uprobe hits.\n");
    return 0;
}
