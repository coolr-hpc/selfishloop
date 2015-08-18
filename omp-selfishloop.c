/*
  omp version of selfish noise recoder

  Kazutomo Yoshii <ky@anl.gov>
 */
#define _GNU_SOURCE
#include <sched.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "rdtsc.h"

#include <omp.h>

struct selfish_detour {
	uint64_t start;
	uint64_t duration;
};

struct selfish_rec {
	/* set by caller */
	int ndetours;
	uint64_t timeout;
	uint64_t threshold;

	/* fill by selfishloop() */
	uint64_t max, min;
	struct selfish_detour *detours; // alloc by caller
	uint64_t elapsed;
	int nrecorded;

	/* update by analyzing functions */
	double  detour_percent;
	double  detour_sum;
};

static struct selfish_rec *selfish_rec_init(int ndetours, uint64_t threshold, uint64_t timeout)
{
	struct selfish_rec *sr;

	sr = calloc(1, sizeof(struct selfish_rec));
	if(!sr) 
		return NULL;

	sr->ndetours = ndetours;
	sr->threshold = threshold;
	sr->timeout = timeout;

	if( posix_memalign( (void*)&sr->detours, getpagesize(), sizeof(struct selfish_detour)*sr->ndetours) ) {
		perror("posix_memalign");
		free(sr);
		return NULL;
	}
	memset( sr->detours, 0, sizeof(struct selfish_detour)*sr->ndetours);

	return sr;
}

static void selfish_rec_finilize(struct selfish_rec *sr)
{
	if (!sr)
		return;
	if (sr->detours) {
		free(sr->detours);
		sr->detours = NULL;
	}
	free(sr);
}

static inline void rdtsc_barrier(void)
{
	// lfence prevents from instruction reodering
	asm volatile ( "lfence" : : : "memory" ); // XXX: Intel only
}

void selfish_rec_loop(struct selfish_rec *sr)
{
	uint64_t prev, cur, delta, start;
	int idx = -10; // minus number for warmups

	if(!sr) return;

	sr->nrecorded = 0;
	sr->max = 0;
	sr->min = 1<<31;

	start = prev = rdtsc();
	for(;;) {
		rdtsc_barrier();
		cur = rdtsc();
		rdtsc_barrier();
		delta = cur - prev;
		if(delta > sr->threshold) {
			if (idx >= 0 ) {
				if (idx == 0)
					start = prev;
				sr->detours[idx].start = prev;
				sr->detours[idx].duration = delta;
				sr->nrecorded ++;
			}
			idx ++;
			if (!(idx < sr->ndetours)) 
				break;
		}
		if ((cur - start) >= sr->timeout) 
			break;
		if (delta > sr->max) 
			sr->max = delta;
		if (delta < sr->min)
			sr->min = delta;

		prev = cur;
	}
	rdtsc_barrier();
	sr->elapsed = rdtsc() - sr->detours[0].start;
}

static void calc_detour_percent(struct selfish_rec *sr)
{
	int i;
	double  sum = 0.0;
	for (i=0; i<sr->ndetours; i++) {
		sum += (double)sr->detours[i].duration;
	}
	sr->detour_sum = sum;
	sr->detour_percent = (double)sum*100.0/(double)sr->elapsed;

}

static void selfish_rec_report( struct selfish_rec *sr, int tno)
{
	if (!sr)
		return;
	
	printf("%3d: dutour=%.3lf %%  elapsed=%lu  min=%lu max=%lu \n",
	       tno,
	       sr->detour_percent,  sr->elapsed,
	       sr->min, sr->max );
#if 0	
	for (i=0; i<10; i++) {
		uint64_t s, d;
		s = sr->detours[i].start -  sr->detours[0].start;
		d = sr->detours[i].duration;
		printf("%3d %.1lf %.1lf # %lu  %lu\n", i, 
			(double)s/(2.3*1000.0),(double)d/(2.3*1000.0), s,d );
		
	}
#endif
}


void set_strict_affinity(int cpuid)
{
	cpu_set_t  cpuset_mask;

	CPU_ZERO(&cpuset_mask);
	CPU_SET(cpuid, &cpuset_mask);
	if ( sched_setaffinity(0, sizeof(cpuset_mask), &cpuset_mask) == -1 ) {
		printf("sched_setaffinity() failed\n");
		exit(1);
	}
}

static void  usage(const char* prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("\n");
	printf("[options]\n");
	printf("\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	double  av_percent = 0.0;
	int nth;
	int opt;
	int ndetours = 3000;
	int verbose = 0;
	uint64_t threshold = 1000;  /* cycles ~400ns (on what arch?) */
	uint64_t timeout = 1ULL*1000*1000*1000;  /* cycles */

	while ((opt = getopt(argc, argv, "hn:d:t:")) != -1) {
		switch(opt) {
		case 'h':
			usage(argv[0]);
			exit(1);
		case 'n':
			ndetours = atoi(optarg);
			break;
		case 'd':
			threshold = strtoull(optarg, NULL, 10);
			break;
		case 't':
			timeout = strtoull(optarg, NULL, 10);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			printf("Unknown option: '%s'\n", optarg);
			usage(argv[0]);
			exit(1);
		}
	}

	printf("# ndetours:   %u\n", ndetours);
	printf("# threshold:  %lu\n", threshold);
	printf("# timeout:    %lu\n", timeout);

#pragma omp parallel shared(av_percent, nth, ndetours, threshold, timeout)
	{
		struct selfish_rec *sr;
		int tno;
		tno = omp_get_thread_num();
		if (tno == 0) {
			nth = omp_get_num_threads();
			printf("# n. threads: %d\n", nth);
		}
		set_strict_affinity(tno);
		sr = selfish_rec_init(ndetours, threshold, timeout);
		if (!sr) {
			printf("selfish_rec_init() failed\n");
			exit(1);
		}
#pragma omp barrier
		selfish_rec_loop(sr);
		calc_detour_percent(sr);
#pragma omp barrier
		if (verbose) 
			selfish_rec_report(sr, tno);

#pragma omp critical 
		av_percent += sr->detour_percent;

		selfish_rec_finilize(sr);
	}
	printf("detour_mean=%lf %%\n",  av_percent/nth);

	return 0;
}
