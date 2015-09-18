#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "selfish_rec.h"


void output_json(struct selfish_data sd)
{
	printf("nth=%d\n", sd.nth);
}       


