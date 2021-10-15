#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include "cachelab.h"

typedef struct {
	unsigned s;			// # of bits for set index
	unsigned b;			// # of bits for block index
	unsigned E;			// # of cache lines/set
	unsigned S;			// # of sets = 2^s
	unsigned nHit;
	unsigned nMiss;
	unsigned nEvict;
	int verbose;		// verbose mode bit (-v option)
} PARAM;

typedef struct {
	uint64_t tag;
	int valid;			// valid bit
	unsigned useTime;	// timestamp of last use
} LINE;

typedef struct {
	LINE* lines;
	unsigned useCnt;	// total # of access to this set so far
} SET;

PARAM accessCache(SET *cache, PARAM param, uint64_t addr) {

	unsigned t = 64 - param.s - param.b;
	uint64_t Tag = addr >> (param.s + param.b);		// shift down
	uint64_t Set = (addr << t) >> (t + param.b);	// clear upper bits, shift down
	unsigned prevHit = param.nHit;
	int setFull = 1;
	unsigned Ei;
	unsigned victimLine, emptyLine;
	unsigned leastRecent;

	SET *set = &(cache[Set]);
	emptyLine = param.E;
	for (Ei = 0; Ei < param.E; Ei++) {			// search for the queried line
		if (set->lines[Ei].valid) {
			if (set->lines[Ei].tag == Tag) {
				set->lines[Ei].useTime = ++(set->useCnt);	// increment and assign use counter
				param.nHit++;
				if (param.verbose) printf(" hit");
				break;
			}
		}										// and at the same time
		else {									// find the lowest-indexed empty line
			emptyLine = (emptyLine < Ei) ? emptyLine : Ei;
			if (setFull) setFull = 0;
		}
	}
	if (prevHit < param.nHit) {
		return param;
	}
	param.nMiss++;
	if (param.verbose) printf(" miss");
	if (setFull) {								// if all lines are valid,
		victimLine = 0;							// evict the line with smallest timestamp value
		leastRecent = set->lines[0].useTime;
		for (Ei = 1; Ei < param.E; Ei++) {
			if (leastRecent > set->lines[Ei].useTime) {
				victimLine = Ei;
				leastRecent = set->lines[Ei].useTime;
			}
		}
		set->lines[victimLine].tag = Tag;
		set->lines[victimLine].useTime = ++(set->useCnt);	// increment and assign use counter
		param.nEvict++;
		if (param.verbose) printf(" eviction");
	}
	else {										// else, use the empty line we found earlier
		set->lines[emptyLine].tag = Tag;
		set->lines[emptyLine].valid = 1;
		set->lines[emptyLine].useTime = ++(set->useCnt);	// increment and assign use counter
	}
	return param;
}

int main(int argc, char **argv) {

	PARAM param;
	unsigned Si;
	FILE *file;
	char *fileName;
	char instr;
	uint64_t addr;
	int size;
	char c;

	// parse command line
	param.verbose = 0;
	while ((c = getopt(argc, argv, "vs:E:b:t:")) != -1) {
		switch (c) {
			case 'v':
				param.verbose = 1; break;
			case 's':
				param.s = atoi(optarg);
				param.S = 1 << param.s; break;
			case 'E':
				param.E = atoi(optarg); break;
			case 'b':
				param.b = atoi(optarg); break;
			case 't':
				fileName = optarg; break;
			default: exit(1);
		}
	}
	param.nHit = 0;
	param.nMiss = 0;
	param.nEvict = 0;
	
	// build cache
	SET *cache = (SET*)malloc(sizeof(SET)*param.S);
	for (Si = 0; Si < param.S; Si++) {
		cache[Si].lines = (LINE*)malloc(sizeof(LINE)*param.E);
		cache[Si].useCnt = 0;
	}

	// parse file
	file = fopen(fileName, "r");
	if (file) {
		while (fscanf(file, " %c %lx,%d", &instr, &addr, &size) == 3) {
			if (param.verbose && instr != 'I')
				printf("%c %lx,%d", instr, addr, size);
			switch (instr) {
				case 'M':
					param = accessCache(cache, param, addr);
				case 'L':
				case 'S':
					param = accessCache(cache, param, addr);
			}
			if (param.verbose && instr != 'I')
				printf(" \n");
		}
	}
	// print summary
	printSummary(param.nHit, param.nMiss, param.nEvict);

	// cleanup
	for (Si = 0; Si < param.S; Si++)
		free(cache[Si].lines);
	free(cache);
	fclose(file);
	return 0;
}