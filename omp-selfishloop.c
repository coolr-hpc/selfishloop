/*
 * OpenMP version of selfish noise recoder
 *
 * Coding style: https://www.kernel.org/doc/Documentation/CodingStyle
 *
 * Contact:  Kazutomo Yoshii <ky@anl.gov>
 */
#define _GNU_SOURCE
#include <sched.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "rdtsc.h"

#include <omp.h>

#include "selfish_rec.h"

static struct selfish_rec *selfish_rec_init(
			  int ndetours, uint64_t threshold,
			  uint64_t timeout)
{
	struct selfish_rec *sr;

	sr = calloc(1, sizeof(struct selfish_rec));
	if (!sr)
		return NULL;

	sr->ndetours = ndetours;
	sr->threshold = threshold;
	sr->timeout = timeout;

	if (posix_memalign((void *)&sr->detours, getpagesize(),
			   sizeof(struct selfish_detour) * sr->ndetours)) {
		perror("posix_memalign");
		free(sr);
		return NULL;
	}
	memset(sr->detours, 0, sizeof(struct selfish_detour) * sr->ndetours);

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
	/* lfence prevents from instruction reodering */
	asm volatile ("lfence" : : : "memory"); /* XXX: Intel only */
}

void selfish_rec_loop(struct selfish_rec *sr)
{
	uint64_t prev, cur, delta, start;
	uint64_t niterated = 0;
	int idx = -10; /* minus number for warmups */

	if (!sr)
		return;

	sr->nrecorded = 0;
	sr->max = 0;
	sr->min = 1<<31;

	start = prev = rdtsc();
	for (niterated = 0; ; niterated++) {
		rdtsc_barrier();
		cur = rdtsc();
		rdtsc_barrier();
		delta = cur - prev;
		if (delta > sr->threshold) {
			if (idx >= 0) {
				if (idx == 0)
					start = prev;
				sr->detours[idx].start = prev;
				sr->detours[idx].duration = delta;
				sr->nrecorded++;
			}
			idx++;
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


void set_strict_affinity(int cpuid)
{
	cpu_set_t  cpuset_mask;

	CPU_ZERO(&cpuset_mask);
	CPU_SET(cpuid, &cpuset_mask);
	if (sched_setaffinity(0, sizeof(cpuset_mask), &cpuset_mask) == -1) {
		printf("sched_setaffinity() failed\n");
		exit(1);
	}
}

static void  usage(const char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("\n");
	printf("[options]\n");
	printf("\n");
	printf("-v : enable verbose output\n");
	printf("-n int : the size of the detour record array\n");
	printf("-t int : timeout in ticks\n");
	printf("-d int : threshold in ticks.\n");
	printf("         detours longer than this value are recorded\n");
	printf("-o prefix : prefix for storing per-thread detour data\n");
	printf("            for quick drawing using gnuplot\n");
	printf("-j filename : json output\n");
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

	while (1) {
		if ((omp_get_wtime() - wt1) >= timeout)
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
	int i, opt;
	struct selfish_data sd;

	memset(&sd, 0, sizeof(struct selfish_data));

	/* default setting */
	sd.ndetours = 3000;
	sd.threshold = 1000;  /* cycles ~400ns (on what arch?) */
	sd.timeoutsec = 2;
	sd.verbose = 0;

	sd.nth = omp_get_max_threads();
	sd.srs = (struct selfish_rec **)
		malloc(sizeof(struct selfish_rec *) * sd.nth);
	if (!sd.srs) {
		perror("malloc");
		exit(1);
	}

	while ((opt = getopt(argc, argv, "vhn:d:t:o:")) != -1) {
		switch (opt) {
		case 'h':
			usage(argv[0]);
			exit(1);
		case 'n':
			sd.ndetours = atoi(optarg);
			break;
		case 'd':
			sd.threshold = strtoull(optarg, NULL, 10);
			break;
		case 't':
			sd.timeoutsec = strtol(optarg, NULL, 10);
			break;
		case 'v':
			sd.verbose++;
			break;
		case 'o':
			strncpy(sd.output_jsonfn, optarg,
				sizeof(sd.output_jsonfn));
			sd.output_jsonfn[sizeof(sd.output_jsonfn)-1] = 0;
		default:
			printf("Unknown option: '%s'\n", optarg);
			usage(argv[0]);
			exit(1);
		}
	}

	sd.tickspersec = measure_tickspersec();
	sd.timeoutticks = sd.tickspersec * sd.timeoutsec;

	printf("# [config]\n");
	printf("# maxrecordsize=%u\n", sd.ndetours);
	printf("# thresholdticks=%lu\n", sd.threshold);
	printf("# tickspersec=%lu\n", sd.tickspersec);
	printf("# timeoutsec=%d\n", sd.timeoutsec);
	printf("# timeoutticks=%lu\n", sd.timeoutticks);

	if (strlen(sd.output_jsonfn) > 0)
		printf("# output=%s\n", sd.output_jsonfn);


#pragma omp parallel shared(sd)
	{
		int tno;
		struct selfish_rec *sr;

		tno = omp_get_thread_num();
		if (tno == 0) {
			sd.nth = omp_get_num_threads();
			printf("# nompthreads=%d\n", sd.nth);
		}
		set_strict_affinity(tno);
		/* sr is allocated and touched by each thread, so
		 * memory should be local on NUMA.
		 */
		sr = selfish_rec_init(sd.ndetours,
				      sd.threshold, sd.timeoutticks);
		if (!sr) {
			printf("selfish_rec_init() failed\n");
			exit(1);
		}
		sd.srs[tno] = sr;
#pragma omp barrier
		selfish_rec_loop(sr);
	}

	if (strlen(sd.output_jsonfn) > 0)
		output_json(&sd);

	report_simple_stat(&sd);

	for (i = 0; i < sd.nth; i++)
		selfish_rec_finilize(sd.srs[i]);

	return 0;
}
