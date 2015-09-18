#ifndef __SELFISH_H_DEFINE__
#define __SELFISH_H_DEFINE__

#include <stdint.h>

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
	struct selfish_detour *detours; /* alloc by caller */
	uint64_t elapsed;
	int nrecorded;
	uint64_t niterated;

	/* update by analyzing functions */
	double  sum; /* sum of all detours */
	double  mean;
	double  sd;
};

struct selfish_data {
	int nth;
	int ndetours;
	uint64_t threshold;
	int timeoutsec;
	uint64_t timeoutticks;
	uint64_t tickspersec;

	struct selfish_rec **srs;

	int verbose;
	char output_jsonfn[512];
};

extern void output_json(struct selfish_data *sd);
extern void report_simple_stat(struct selfish_data *sd);

#endif
