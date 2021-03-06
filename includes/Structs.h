#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>
#include <stdbool.h>

struct memorySlot {
	uint16_t address;
	uint16_t value;
	bool isBreakpoint;
};

struct LC3 {
	unsigned char CC;
	uint16_t PC;
	uint16_t IR;
	uint16_t registers[8];
	bool isHalted;
	bool isPaused;
	struct memorySlot memory[0xffff];
};

struct program {
	char *name;
	char *logfile;
	char *objectfile;
	char *assemblyfile;
	char *symbolfile;
	char *hexoutfile;
	char *binoutfile;
        bool warn;

	int verbosity;

	struct LC3 simulator;
};

struct symbol {
	char *name;
	uint16_t address;
	bool fromOS;
	int line;
};

struct symbolTable {
	struct symbol *sym;
	struct symbolTable *next;
};

#endif // STRUCTS_H
