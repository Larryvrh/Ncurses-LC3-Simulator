#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "Argparse.h"
#include "Error.h"

static const char errprefix[] = "\n\t";

static unsigned int errcount, errvalue;

static inline void strmcpy(char **, char const *);


#define ERR(mask, errstring, arg) 						\
	do { 									\
		errvalue |= errvalue & (mask) ? MUL_##mask : mask; 		\
		errcount++; 							\
		if (*(errstring) == NULL) {					\
			strmcpy(errstring, arg);				\
		} else { 							\
			*(errstring) = (char *) realloc(*(errstring), 		\
				sizeof(char) * (strlen(*(errstring)) + 1 + 	\
				strlen(errprefix) + strlen(arg))); 		\
			strcat(*(errstring), errprefix); 			\
			strcat(*(errstring), arg); 				\
		} 								\
	} while (0)


static const char _usage[] =
	"Usage: %s [OPTION] <file>.\n"
	"  -h, --help           show this help text.                          \n"
	"  -f, --infile File    specify the input file to use.                \n"
	"  -o, --outfile File   specify the prefix of the output file to write\n"
	"                       any given assembly file's output to.          \n"
	"                       If this is not supplied, the prefix of the    \n"
	"                       input file is used.                           \n"
	"                       E.g. -o out results in the following files    \n"
	"                       being written:                                \n"
	"                           out.obj (The file used to run the program)\n"
	"                           out.bin (A file full of binary values that\n"
	"                                    correspond to the value at that  \n"
	"                                    address in the program)          \n"
	"                           out.hex (Same as the bin file, but in hex)\n"
	"                           out.sym (A file with labels and their     \n"
	"                                    corresponding address)           \n"
	"  -a, --assemble File  assemble the given file into a .obj file,     \n"
	"                       a .sym file, a .hex file, and a .bin file.    \n"
	"  -l. --log-file File  specify which file to use as a log file when. \n";


/*
 * This should only be printed when the user passes -h as a flag.
 */

static void usage(const char *prog_name)
{
	printf(_usage, prog_name);
}


/*
 * Copy the contents of one string to another, allocating enough memory to the
 * string we want to copy to.
 */

static inline void strmcpy(char **to, char const *from)
{
	size_t len = strlen(from) + 1;
	*to = (char *) malloc(sizeof(char) * len);
	strncpy(*to, from, len);
}

static unsigned int errhandle(struct program *prog) {
	if (errcount > 1)
		fprintf(stderr, "ERROR: There were some error's while parsing your options.\n");
	else
		fprintf(stderr, "ERROR: There was an error while parsing your options.\n");


	if (errvalue & MUL_INPUT_FILES) {
		fprintf(stderr, "\nThe following files were seen as input files "
				"and couldn't be decided upo:.");
		fprintf(stderr, "%s%s",   errprefix, prog->infile);
		fprintf(stderr, "%s%s\n", errprefix, input_files);
	}

	if (errvalue & MUL_INCORRECT_OPT) {
		fprintf(stderr, "\nThe following options weren't recognised:");
		fprintf(stderr, "%s%s\n", errprefix, incorrect_opts);
	} else if (errvalue & INCORRECT_OPT) {
		fprintf(stderr, "\nThe following option wasn't recognised.");
		fprintf(stderr, "%s\n", incorrect_opts);
	}

	if (errvalue & MUL_NO_ARG_PROVIDED) {
		fprintf(stderr, "\nThe following options require arguments:");
		fprintf(stderr, "%s%s\n", errprefix, no_args_provided);
	} else if (errvalue & NO_ARG_PROVIDED) {
		fprintf(stderr, "\n%s requires an argument.\n", no_args_provided);
	}

	if (errvalue & MUL_WARN_DEPRECATED) {

	} else if (errvalue & WARN_DEPRECATED) {

	}

	if (errvalue & MUL_WARN_UNIMPLEMENTED) {
		fprintf(stderr, "\nThe following options aren't fully implemented:");
		fprintf(stderr, "%s%s\n", errprefix, unimplemented_opts);
	} else if (errvalue & WARN_UNIMPLEMENTED) {
		fprintf(stderr, "\n%s is not fully implemented.\n", unimplemented_opts);
	}

	return errvalue;
}


/*
 * Given the argument count, and each argument, go through each argument and
 * compare it with ones we want, and if so, do some pre-defined operation
 * involving that value.
 *
 * Returns:
 * 	- 0 if no errors were found
 * 	- A bitmask of the error value if one (or more) was found.
 */

unsigned int argparse(int argcount, char **argvals, struct program *prog)
{
	int argindex = 1;

	// First argument is program name.
	strmcpy(&prog->name, argvals[0]);

	char const *currentarg = (char const *) NULL;

	while (argindex < argcount) {
		currentarg = argvals[argindex++];

		if (!strcmp(currentarg, "--help") || !strcmp(currentarg, "-h")) {
			usage(prog->name);
			exit(EXIT_SUCCESS);
		} else if (!strcmp(currentarg, "--assemble") || !strcmp(currentarg, "-a")) {
			// TODO: Implement this opt
			ERR(WARN_UNIMPLEMENTED, &unimplemented_opts, currentarg);

			if (argindex == argcount || argvals[argindex][0] == '-')
				ERR(NO_ARG_PROVIDED, &no_args_provided, currentarg);
			else
				argindex++;
		} else if (!strcmp(currentarg, "--infile")   || !strcmp(currentarg, "-f")) {
			if (argindex == argcount || argvals[argindex][0] == '-') {
				ERR(NO_ARG_PROVIDED, &no_args_provided, currentarg);
			} else {
				if (prog->infile == NULL)
					strmcpy(&prog->infile, argvals[argindex]);
				else
					ERR(MUL_INPUT_FILES, &input_files, argvals[argindex]);

				argindex++;
			}
		} else if (!strcmp(currentarg, "--outfile")  || !strcmp(currentarg, "-o")) {
			// TODO: Implement this opt
			ERR(WARN_UNIMPLEMENTED, &unimplemented_opts, currentarg);
		} else if (!strcmp(currentarg, "--logfile")  || !strcmp(currentarg, "-l")) {
			// TODO: Implement this opt
			ERR(WARN_UNIMPLEMENTED, &unimplemented_opts, currentarg);
		} else {
			if (currentarg[0] != '-') {
				if (prog->infile == NULL)
					strmcpy(&prog->infile, currentarg);
				else
					ERR(MUL_INPUT_FILES, &input_files, currentarg);
			} else {
				ERR(INCORRECT_OPT, &incorrect_opts, currentarg);
			}
		}
	}

	if (errvalue)
		return errhandle(prog);

	return errvalue;
}

