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
	uint64_t niterated;

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
	uint64_t niterated = 0;
	int idx = -10; // minus number for warmups

	if (!sr) return;

	sr->nrecorded = 0;
	sr->max = 0;
	sr->min = 1<<31;

	start = prev = rdtsc();
	for (niterated = 0;;niterated++) {
		rdtsc_barrier();
		cur = rdtsc();
		rdtsc_barrier();
		delta = cur - prev;
		if (delta > sr->threshold) {
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
	sr->niterated = niterated;
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

static void selfish_rec_output(struct selfish_rec *sr, const char* prefix, int tno, uint64_t tickspersec)
{
	int i;
	FILE *fp;
	char fn[BUFSIZ];

	if (!sr)
		return;

	snprintf(fn, BUFSIZ, "%s-t%03d.txt", prefix, tno);
	fp = fopen(fn, "w");
	if (!fp) {
		printf("Failed to write to %s\n", fn);
		return ;
	}

	for (i=0; i<sr->nrecorded; i++) {
		uint64_t s, d;
		s = sr->detours[i].start -  sr->detours[0].start;
		d = sr->detours[i].duration;
		fprintf(fp,"%.3lf %.3lf    %lu  %lu\n",
			(double)s/(double)(tickspersec/1000/1000),
			(double)d/(double)(tickspersec/1000/1000),
			s,d );
	}
	fclose(fp);
}


static void selfish_rec_report( struct selfish_rec *sr, int tno)
{
	if (!sr)
		return;
	
	printf("%3d: dutour=%.3lf %%  elapsed=%lu  min=%lu max=%lu nr=%d\n",
	       tno,
	       sr->detour_percent,  sr->elapsed,
	       sr->min, sr->max, sr->nrecorded );
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
	printf("-v : enable verbose output\n");
	printf("-n int : the size of the detour record array\n");
	printf("-t int : timeout in ticks\n");
	printf("-d int : threshold in ticks.  detours longer than this value are recorded\n");
	printf("-o prefix : output detours data per-thread.\n");
	printf("\n");
	printf("\n");
}

static double measure_tickspersec(void)
{
	double    wt1, wt2;
	uint64_t  t1, t2;
	double  timeout = 2.0;
	
	wt1 = omp_get_wtime();
	rdtsc_barrier();
	t1 = rdtsc();
	rdtsc_barrier();

	while(1) {
		if( (omp_get_wtime() - wt1) >= timeout )
			break;
	}
	rdtsc_barrier();
	t2 = rdtsc();
	rdtsc_barrier();
	wt2 = omp_get_wtime();

	return (double)(t2-t1)/(wt2-wt1);
}

int main(int argc, char *argv[])
{
	double  av_percent = 0.0;
	double  av_niterated = 0;
	int nth;
	int opt;
	int ndetours = 3000;
	int verbose = 0;
	uint64_t threshold = 1000;  /* cycles ~400ns (on what arch?) */
	uint64_t timeout = 1ULL*1000*1000*1000;  /* cycles */
	uint64_t tickspersec;
	char oprefix[80];
	oprefix[0] = 0;

	while ((opt = getopt(argc, argv, "vhn:d:t:o:")) != -1) {
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
			verbose++;
			break;
		case 'o':
			strncpy(oprefix, optarg, sizeof(oprefix));
			oprefix[sizeof(oprefix)-1] = 0;
			break;
		default:
			printf("Unknown option: '%s'\n", optarg);
			usage(argv[0]);
			exit(1);
		}
	}

	
	timeout = tickspersec = measure_tickspersec();
	
	printf("# ndetours:     %u\n", ndetours);
	printf("# threshold:    %lu (ticks)\n", threshold);
	printf("# tickspersec:  %lu (ticks)\n", tickspersec);
	printf("# timeout:      %lu (ticks)\n", timeout);
	if (strlen(oprefix) > 0 ) {
		printf("# outputprefix: %s\n", oprefix);
	}

#pragma omp parallel shared(av_percent, av_niterated, nth, ndetours, threshold, timeout)
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

		if (strlen(oprefix) > 0 )
			selfish_rec_output(sr, oprefix, tno, tickspersec);

#pragma omp critical 
		{
			av_percent += sr->detour_percent;
			av_niterated += sr->niterated;
		}

		selfish_rec_finilize(sr);
	}
	printf("detour_mean=%lf %%\n",  av_percent/nth);
	printf("niterated_mean=%g\n",  av_niterated/nth);

	return 0;
}
