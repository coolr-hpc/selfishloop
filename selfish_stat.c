#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "selfish_rec.h"

#if 0
static void selfish_rec_output(struct selfish_rec *sr,
			       const char *prefix, int tno,
			       uint64_t tickspersec)
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
		return;
	}

	fprintf(fp, "# start[usec] duration[usec]\n");
	for (i = 0; i < sr->nrecorded; i++) {
		uint64_t s, d;

		s = sr->detours[i].start -  sr->detours[0].start;
		d = sr->detours[i].duration;
		fprintf(fp, "%lf %lf\n",
			(double)s/((double)tickspersec/1000.0/1000.0),
			(double)d/((double)tickspersec/1000.0/1000.0));
	}
	fclose(fp);
}
#endif

static void analyze(struct selfish_rec *sr)
{
	int i, n;
	double  tmp, tmp2;

	n = sr->nrecorded;
	tmp = 0.0;
	for (i = 0; i < n; i++)
		tmp += (double)sr->detours[i].duration;

	sr->sum = tmp;
	sr->mean = tmp / (double)n;

	tmp = 0.0;
	for (i = 0; i < n; i++) {
		tmp2 = (double)sr->detours[i].duration - sr->mean;
		tmp += tmp2 * tmp2;
	}

	sr->sd = sqrt(tmp / (double)n);
}




void report_simple_stat(struct selfish_data sd)
{
	int i;
	struct selfish_rec *sr;

	for (i = 0; i < sd.nth; i++) {
		sr = sd.srs[i];
		analyze(sr);

		printf("%2d: %lf\n", i, sr->sum * 100.0 / (double)sr->elapsed);
	}
}
