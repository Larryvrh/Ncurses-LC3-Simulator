#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

#include "Parser.h"

static int uppercmp(char *from, char const *const to)
{
	char copy[100];
	strcpy(copy, from);

	for (size_t i = 0; copy[i]; i++) {
		copy[i] = toupper(copy[i]);
	}

	return strcmp(copy, to);
}

static int uppercmpn(char *from, char const *const to, size_t n)
{
	char copy[n + 1];
	strncpy(copy, from, n);
	copy[n] = '\0';

	return uppercmp(copy, to);
}

static uint16_t nzp(char const *const _nzp)
{
	uint16_t __nzp = 0;

	for (size_t i = 0; _nzp[i]; i++) {
		if (_nzp[i] == 'n' || _nzp[i] == 'N') {
			if (__nzp & 0x0800) {
				__nzp |= 0x4;
			}
			__nzp |= 0x0800;
		} else if (_nzp[i] == 'z' || _nzp[i] == 'Z') {
			if (__nzp & 0x0400) {
				__nzp |= 0x2;
			}
			__nzp |= 0x0400;
		} else if (_nzp[i] == 'p' || _nzp[i] == 'P') {
			if (__nzp & 0x0200) {
				__nzp |= 0x1;
			}
			__nzp |= 0x0200;
		}
	}

	return __nzp;
}

static void skipWhitespace(FILE *file)
{
	int c = 0;
	while ((c = fgetc(file)) != EOF && isspace(c)) {
		if (c == '\n')
			break;
	}
	ungetc(c, file);
}

static void nextLine(FILE *file)
{
	int c = 0;
	while ((c = fgetc(file)) != '\n' && c != EOF);
	ungetc(c, file);
}

static void objWrite(uint16_t instruction, FILE *objFile)
{
	unsigned char bytes[2];
	bytes[0] = (instruction & 0xff00) >> 8;
	bytes[1] = instruction & 0xff;
	fwrite(bytes, 2, 1, objFile);
}

static bool endOfLine(char const *const instruction,
		FILE *file, int const *const line)
{
	skipWhitespace(file);
	int c = fgetc(file);
	if (c != ';' && c != '\n') {
		fprintf(stderr, "  Line %d: Incorrect number of arguments "
			"supplied to %s.\n", *line, instruction);
		nextLine(file);
		return false;
	}

	ungetc(c, file);
	return true;
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

struct symbol {
	char *name;
	uint16_t address;
};

struct symbolTable {
	struct symbol *sym;
	struct symbolTable *next;
};

static struct symbolTable table = {
	.sym = NULL,
	.next = NULL,
};

static void free_table(struct symbolTable *table)
{
	if (NULL != table->sym) {
		free(table->sym->name);
		free(table->sym);
	}
	if (NULL != table->next) {
		free_table(table->next);
		free(table->next);
	}
}

static struct symbol* findSymbol(char const *const name)
{
	struct symbolTable *symTable = &table;

	if (NULL == symTable->sym) {
		return NULL;
	}

	while (NULL != symTable->next) {
		if (!strcmp(symTable->sym->name, name)) {
			return symTable->sym;
		}
		symTable = symTable->next;
	}
	if (!strcmp(symTable->sym->name, name)) {
		return symTable->sym;
	} else {
		return NULL;
	}
}

static int addSymbol(char const *const name, uint16_t address)
{
	struct symbol *sym;
	struct symbolTable *symTable = &table;

	if (NULL == symTable->sym) {
		sym = malloc(sizeof(struct symbol));
		strmcpy(&sym->name, name);
		sym->address = address;
		table.sym = sym;
		return 0;
	}

	sym = findSymbol(name);

	if (NULL == sym) {
		while (1) {
			if (symTable->next == NULL) {
				sym = malloc(sizeof(struct symbol));
				strmcpy(&sym->name, name);
				sym->address = address;

				struct symbolTable *_table =
					malloc(sizeof(struct symbolTable));
				_table->sym = sym;
				_table->next = NULL;
				symTable->next = _table;
				return 0;
			}

			symTable = symTable->next;
		}
	}
	return 1;
}

static void process(FILE *file, FILE *symFile)
{
	static char const *const symTable =
		"// Symbol Table\n"
		"// Scope Level 0:\n"
		"//\tSymbol Name       Page Address\n"
		"//\t----------------  ------------------\n";

	fprintf(symFile, symTable);

	bool origSeen = false, endSeen = false, doContinue = false;
	int c, currentLine = 0, errors = 0;
	uint16_t pc = 0, instruction = 0;

	char line[100] = { 0 }, operand1[100] = { 0 }, *end = NULL;

	printf("STARTING FIRST PASS...\n");

	while (1) {
		skipWhitespace(file);

		c = fgetc(file);

		if (c == EOF) {
			break;
		} else if (c == '\n') {
			currentLine++;
			continue;
		} else if (c == ';') {
			nextLine(file);
			continue;;
		} else if (c == ':') {
			continue;
		} else if (c == '.') {
			ungetc(c, file);
			fscanf(file, "%99s", line);

			if (!uppercmp(line, ".ORIG")) {
				if (origSeen) {
					fprintf(stderr, "  Line %d: Extra .ORIG"
						" directive.\n", currentLine);
					errors ++;
					continue;
				} else if (endSeen) {
					fprintf(stderr, "  Line %d: .END seen "
						"before .ORIG.\n", currentLine);
					errors ++;
					continue;
				}
				while (isspace(c = fgetc(file))) {
					if (c == '\n') {
						fprintf(stderr, "Line %d: "
							"No address given for "
							".ORIG directive.\n",
							currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue)
					continue;

				fscanf(file, "%99s", operand1);
				instruction = (uint16_t) strtoul(
						operand1, &end, 16);
				pc = instruction;

				if (*end) {
					fprintf(stderr, "Line %d: "
						"Invalid address '%s'",
						currentLine, operand1);
					errors++;
					continue;
				}
				origSeen = true;
			} else if (!uppercmp(line, ".BLKW")) {
				fscanf(file, "%s", line);

				skipWhitespace(file);
				c = fgetc(file);
				if (c != ';' && c != '\n') {
					fprintf(stderr, "  Line %d: Invalid "
						"number of arguments supplied "
						"for .BLKW.\n", currentLine);
					errors++;
					continue;
				}

				ungetc(c, file);
				if (line[0] != '#' && line[0] != 'x' &&
						line[0] != 'X' && !isdigit(line[0])) {
					fprintf(stderr, "  Line %d: Invalid "
						"operand for .BLKW.\n", currentLine);
					errors++;
					continue;
				} else if (line[0] == '#') {
					instruction = (int16_t) strtol(line + 1, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for "
							"base 10.\n", currentLine);
						errors++;
						continue;
					}
				} else if (line[0] == 'x' || line[0] == 'X' ||
						(line[0] == '0' && (line[1] == 'x'
								|| line[1] == 'X'))) {
					instruction = (int16_t) strtol(line, &end, 16);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for "
							"basee 16 (%x).\n",
							currentLine, instruction);
						errors++;
						continue;
					}
				} else {
					instruction = (int16_t) strtol(line, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for "
							"base 10.\n", currentLine);
						errors++;
						continue;
					}
				}

				if (instruction < 1) {
					fprintf(stderr, "  Line %d: .BLKW "
						"requires an argument > 0.\n",
						currentLine);
					errors++;
					continue;
				} else if (instruction > 150) {
					fprintf(stderr, "  Line %d: .BLKW "
						"requires an argument < 150.\n",
						currentLine);
					errors++;
					continue;
				}
				while (instruction--)
					pc++;
			} else if (!uppercmp(line, ".STRINGZ")) {
				skipWhitespace(file);
				c = fgetc(file);

				if (c != '"') {
					ungetc(c, file);
					fprintf(stderr, "  Line %d: No string "
						"supplied to .STRINGZ.\n",
						currentLine);
					errors++;
					continue;
				}
				memset(line, 0, 100);
				for (size_t i = 0; (c = fgetc(file)) != '"' && i < 100; i++) {
					if (c == '\\') {
						switch (c = fgetc(file)) {
						case 'n':
							line[i] = '\n';
							break;
						case 't':
							line[i] = '\t';
							break;
						case '"':
							line[i] = c;
							break;
						default:
							ungetc(c, file);
							break;
						}
					} else {
						line[i] = c;
					}
					pc++;
				}
				pc++; // Implicit null terminator
			} else if (!uppercmp(line, ".FILL")) {
				if (origSeen)
					pc++;
				nextLine(file);
				continue;
			} else if (!uppercmp(line, ".END")) {
				endSeen = true;
				nextLine(file);
				continue;
			}
		} else {
			ungetc(c, file);
			fscanf(file, "%99s", line);

			if (!uppercmp(line, "ADD") || !uppercmp(line, "AND") ||
					!uppercmp(line, "NOT")    ||
					!uppercmp(line, "RET")    ||
					!uppercmp(line, "JSR")    ||
					!uppercmpn(line, "BR", 2) ||
					!uppercmp(line, "JSRR")   ||
					!uppercmp(line, "TRAP")   ||
					!uppercmp(line, "GETC")   ||
					!uppercmp(line, "PUTC")   ||
					!uppercmp(line, "OUT")    ||
					!uppercmp(line, "PUTS")   ||
					!uppercmp(line, "PUTSP")  ||
					!uppercmp(line, "HALT")   ||
					!uppercmp(line, "ST")     ||
					!uppercmp(line, "STR")    ||
					!uppercmp(line, "STI")    ||
					!uppercmp(line, "LD")     ||
					!uppercmp(line, "LDI")    ||
					!uppercmp(line, "LDR")    ||
					!uppercmp(line, "LEA")    ||
					!uppercmp(line, "JMP")    ||
					!uppercmp(line, "IN")) {
				if (origSeen)
					pc++;
				nextLine(file);
			} else {
				end = strchr(line, ':');
				if (NULL != end) {
					char *colon = end + strlen(end) - 1;
					for (; colon != end; colon--) {
						ungetc(*colon, file);
						*colon = '\0';
					}
					ungetc(*colon, file);
					*colon = '\0';
				}
				printf("FOUND LABEL ==> %s  AT %#x\n", line, pc);
				if (addSymbol(line, pc)) {
					fprintf(stderr, "  Line %d: "
						"Multiple definitions of label "
						"'%s'\n", currentLine, line);
					errors++;
				} else {
					fprintf(symFile, "//\t%-16s %4x\n",
							line, pc);
				}
			}
		}
	}
	printf("%d error%s found in first pass.\n", errors,
			errors == 1 ? "" : "'s");

	rewind(file);
}

bool parse(char const *fileName)
{
	FILE *file = fopen(fileName, "r");

	FILE *symFile, *hexFile, *binFile, *objFile;

	size_t length = strlen(fileName);

	char *symFileName = malloc(length + 5);
	char *hexFileName = malloc(length + 5);
	char *binFileName = malloc(length + 5);
	char *objFileName = malloc(length + 5);

	char *fileBase = strstr(fileName, ".asm");
	if (NULL != fileBase) {
		size_t pos = fileBase - fileName;

		strncpy(symFileName, fileName, pos);
		strcpy(symFileName + pos, ".sym");

		strncpy(hexFileName, fileName, pos);
		strcpy(hexFileName + pos, ".hex");

		strncpy(binFileName, fileName, pos);
		strcpy(binFileName + pos, ".bin");

		strncpy(objFileName, fileName, pos);
		strcpy(objFileName + pos, ".obj");
	} else {
		strcpy(symFileName, fileName);
		strcat(symFileName, ".sym");

		strcpy(symFileName, fileName);
		strcat(hexFileName, ".hex");

		strcpy(symFileName, fileName);
		strcat(binFileName, ".bin");

		strcpy(symFileName, fileName);
		strcat(objFileName, ".obj");
	}

	symFile = fopen(symFileName, "w+");
	hexFile = fopen(hexFileName, "w+");
	binFile = fopen(binFileName, "w+");
	objFile = fopen(objFileName, "wb+");

	process(file, symFile);

	printf("STARTING SECOND PASS...\n");

	int c, currentLine = 1, errors = 0, commaCount = 0;

	char line[100] = { 0 };
	char label[100] = { 0 };

	char operand1[100] = { 0 }, operand2[100] = { 0 }, operand3[100] = { 0 };
	char *end = NULL;

	bool origSeen = false, endSeen = false, doContinue = false;

	uint16_t instruction = 0, pc = 0;
	int16_t tmp = 0;

	while (1) {
		tmp = 0;
		commaCount = 0;
		doContinue = false;
		memset(line,     0, sizeof(line));
		memset(label,    0, sizeof(label));
		memset(operand1, 0, sizeof(operand1));
		memset(operand2, 0, sizeof(operand2));
		memset(operand3, 0, sizeof(operand3));

		skipWhitespace(file);

		c = fgetc(file);

		if (c == EOF) {
			break;
		} else if (c == '\n') {
			currentLine++;
			continue;
		} else if (c == ';') {
			nextLine(file);
			continue;;
		} else if (c == ':') {
			continue;
		} else if (c == '.') {
			ungetc(c, file);
			fscanf(file, "%99s", line);

			if (!uppercmp(line, ".ORIG")) {
				if (origSeen) {
					fprintf(stderr, "  Line %d: Extra .ORIG "
						"directive.\n", currentLine);
					errors ++;
					continue;
				} else if (endSeen) {
					fprintf(stderr, "  Line %d: .END seen "
						"before .ORIG.\n", currentLine);
					errors ++;
					continue;
				}

				skipWhitespace(file);
				c = fgetc(file);

				if (c == '\n') {
					fprintf(stderr, "Line %d: "
						"No address given for "
						".ORIG directive.\n",
						currentLine);
					errors++;
					continue;
				}

				fscanf(file, "%99s", operand1);
				instruction = (uint16_t) strtoul(
						operand1, &end, 16);

				if (*end) {
					fprintf(stderr, "Line %d: "
						"Invalid address '%s'",
						currentLine, operand1);
					errors++;
					continue;
				}

				pc = instruction;
				objWrite(instruction, objFile);
				origSeen = true;
				printf(".ORIG  %x\n", instruction);
			} else if (!uppercmp(line, ".STRINGZ")) {
				skipWhitespace(file);
				c = fgetc(file);

				if (c != '"') {
					fprintf(stderr, "  Line %d: No string "
						"supplied to .STRINGZ.\n",
						currentLine);
					errors++;
					continue;
				}

				memset(line, 0, 100);
				for (size_t i = 0; (c = fgetc(file)) != '"' && i < 100; i++) {
					if (c == '\\') {
						switch (c = fgetc(file)) {
						case 'n':
							line[i] = '\n';
							instruction = 0x000a;
							break;
						case 't':
							line[i] = '\t';
							instruction = 0x0009;
							break;
						case '"':
							line[i] = '"';
							instruction = 0x0022;
							break;
						case '\\':
							line[i] = '\\';
							instruction = 0x005c;
							break;
						default:
							ungetc(c, file);
							break;
						}
					} else {
						line[i] = c;
						instruction = c & 0xff;
					}
					pc++;
					objWrite(instruction, objFile);
				}
				pc++;
				objWrite(0, objFile); // Implicit null terminator

				printf(".STRINGZ \"%s\"\n", line);
			} else if (!uppercmp(line, ".BLKW")) {
				fscanf(file, "%s", line);

				skipWhitespace(file);
				c = fgetc(file);

				if (c != ';' && c != '\n') {
					fprintf(stderr, "  Line %d: Invalid "
						"number of arguments supplied "
						"for .BLKW.\n", currentLine);
					continue;
				}

				ungetc(c, file);

				if (line[0] != '#' && line[0] != 'x' &&
						line[0] != 'X' && !isdigit(line[0])) {
					fprintf(stderr, "  Line %d: Invalid "
						"operand for .BLKW.\n", currentLine);
					continue;
				} else if (line[0] == '#') {
					instruction = (int16_t) strtol(line + 1, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for "
							"base 10.\n", currentLine);
					}
				} else if (line[0] == 'x' || line[0] == 'X' ||
						(line[0] == '0' && (line[1] == 'x'
								|| line[1] == 'X'))) {
					instruction = (int16_t) strtol(line, &end, 16);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for "
							"basee 16 (%x).\n",
							currentLine, instruction);
					}
				} else {
					instruction = (int16_t) strtol(line, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for "
							"base 10.\n", currentLine);
					}
				}

				if (instruction < 1) {
					fprintf(stderr, "  Line %d: .BLKW "
						"requires an argument > 0.\n",
						currentLine);
					continue;
				} else if (instruction > 150) {
					fprintf(stderr, "  Line %d: .BLKW "
						"requires an argument < 150.\n",
						currentLine);
					continue;
				}

				pc += instruction;
				while (instruction--) {
					objWrite(0, objFile);
				}

				printf(".BLKW  %s\n", line);
			} else if (!uppercmp(line, ".FILL")) {
				skipWhitespace(file);
				c = fgetc(file);

				if (origSeen)
					pc++;

				if (c == '#' || isdigit(c) || c == '-') {
					if (c == '0' && ((c = fgetc(file)) == 'x'
							|| c == 'X')) {
						for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
							operand1[i] = c;
						}
					} else {
						tmp = 0;
						if (c == '#') {
							if ((c = fgetc(file)) == '-') {
								operand1[0] = c;
								tmp = 1;
								c = fgetc(file);
							}
						}
						ungetc(c, file);

						for (; isdigit(c = fgetc(file)); tmp++) {
							operand1[tmp] = c;
						}
					}
				} else if (c == 'x' || c == 'X') {
					for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
						operand1[i] = c;
					}
				} else {
					fprintf(stderr, "  Line %d: Invalid literal for .FILL.\n",
						currentLine);
					errors++;
					continue;
				}

				if (!endOfLine(".FILL", file, &currentLine)) {
					errors++;
					continue;
				}

				if (operand1[0] == 'x' || operand1[0] == 'X' ||
						(operand1[0] == '0' &&
						(operand1[1] == 'x' ||
						operand1[1] == 'X'))) {
					tmp = 16;
				} else {
					tmp = 10;
				}

				instruction = (int) strtol(operand1, &end, tmp);

				if (*end) {
					fprintf(stderr, "  Line %d: Invalid "
						"literal for base %d.\n",
						currentLine, tmp);
					errors++;
					continue;
				}

				objWrite(instruction, objFile);
				printf(".FILL  #%d\n", instruction);
			} else if (!uppercmp(line, ".END")) {
				printf(".END\n");
				if (!origSeen) {
					fprintf(stderr, "  Line %d: .END seen "
						"before .ORIG.\n", currentLine);
					errors ++;
				} else if (endSeen) {
					fprintf(stderr, "  Line %d: Extra .END "
						"directive.\n", currentLine);
					errors ++;
				}
				endSeen = true;
			} else {
				fprintf(stderr, "  Line %d: Unknown directive "
					"'%s'\n", currentLine, line);
				errors ++;
			}
		} else {
			ungetc(c, file);
			fscanf(file, "%99s", line);

			if (!uppercmpn(line, "BR", 2)) {
				if (origSeen)
					pc++;
				if (!origSeen) {
					fprintf(stderr, "  Line %d: "
						"BR instruction before .ORIG"
						"directive.\n", currentLine);
					errors++;
					continue;
				} else if (endSeen) {
					fprintf(stderr, "  Line %d: "
						"BR instruction after .END"
						"directive.\n", currentLine);
					errors++;
					continue;
				}

				if (strlen(line) == 2) {
					instruction = 0x7 << 9;
				} else {
					instruction = nzp(line + 2);
				}

				if (instruction & 7) {
					fprintf(stderr, "  Line %d: "
						"Invalid BR instruction.\n",
						currentLine);
					errors ++;
					continue;
				}

				instruction &= 0x0e00;

				skipWhitespace(file);
				fscanf(file, "%99s", operand1);

				if (!endOfLine(line, file, &currentLine)) {
					errors++;
					continue;
				}

				struct symbol *sym = findSymbol(operand1);
				if (NULL == sym) {
					fprintf(stderr, "  Line %d: Invalid "
						"label '%s'.\n", currentLine,
						operand1);
					errors++;
					continue;
				}

				tmp = sym->address - pc;

				if (tmp < -256 || tmp > 255) {
					fprintf(stderr, "  Line %d: Label is "
						"too far away.\n", currentLine);
					errors++;
					continue;
				}

				instruction |= (tmp & 0x1ff);
				objWrite(instruction, objFile);
				printf("%s  %s (%d spaces away)\n", line, operand1, tmp);
			} else if (!uppercmp(line, "AND")) {
				if (origSeen)
					pc++;

				skipWhitespace(file);
				c = fgetc(file);

				if (c == '\n' || c == ';') {
					fprintf(stderr, "  Line %d: No operands "
						"supplied to  AND.\n",
						currentLine);
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for AND.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for AND"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for AND"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				commaCount = 0;
				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for AND.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand2[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand2[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for AND"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for AND"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r' && c != '#' &&
						!isdigit(c) && c != 'x' &&
						c != 'X' && c != '-') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for AND.\n",
						currentLine);
					errors++;
					continue;
				}
				operand3[0] = c;

				if (c == 'R' || c == 'r') {
					if ((c = fgetc(file)) < '0' || c > '7') {
						while ((c = fgetc(file)) != '\n');
						fprintf(stderr, "Line %d: No such "
							"register (%d).\n", currentLine,
							c);
						currentLine++;
						errors++;
						continue;
					} else {
						operand3[1] = c;
					}
				} else if (c == '#' || isdigit(c) || c == '-') {
					if (c == '0' && ((c = fgetc(file)) == 'x'
							|| c == 'X')) {
						for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
							operand3[i] = c;
						}
						if (!isspace(c) && c != ';') {
							continue;
						}
						ungetc(c, file);
					} else {
						if (operand3[0] == '0') {
							ungetc(c, file);
						} else if (operand3[0] == '#') {
							if ((c = fgetc(file)) == '-') {
								operand3[1] = c;
								c = fgetc(file);
							}
						}

						for (size_t i = 1; isdigit(c); i++) {
							if (operand3[i])
								i++;
							operand3[i] = c;
							c = fgetc(file);
						}
						if (!isspace(c) && c != ';') {
							continue;
						}
						ungetc(c, file);
					}
				} else if (c == 'x' || c == 'X') {
					for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
						operand3[i] = c;
					}
					if (!isspace(c) && c != ';') {
						fprintf(stderr, "  Line %d: Too "
							"many operands given for"
							" ADD.\n",
							currentLine);
						errors++;
						continue;
					}
					ungetc(c, file);
				}

				skipWhitespace(file);
				c = fgetc(file);

				if (c != ';' && c != '\n') {
					fprintf(stderr, "  Line %d: Too many "
						"operands given for AND.\n",
						currentLine);
					errors++;
					continue;
				}
				ungetc(c, file);

				instruction = 0x5000;
				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (operand2[1] - 0x30) << 6;

				if (operand3[0] == 'R' || operand3[0] == 'r') {
					instruction |= operand3[1] - 0x30;
				} else if (operand3[0] == '#') {
					instruction |= 0x20;
					tmp = (int16_t) strtol(operand3 + 1, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 10.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -16 || tmp > 15) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
					instruction |= tmp & 0x1f;
				} else if (operand3[0] == '-') {
					instruction |= 0x20;
					tmp = (int16_t) strtol(operand3, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 10.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -16 || tmp > 15) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
					instruction |= tmp & 0x1f;
				} else if (operand3[0] == 'x' || operand3[0] == 'X') {
					instruction |= 0x20;
					tmp = (int16_t) strtol(operand3, &end, 16);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 16.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -16 || tmp > 15) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
					instruction |= tmp & 0x1f;
				}

				objWrite(instruction, objFile);
				printf("AND  %s  %s  %s\n", operand1, operand2, operand3);
			} else if (!uppercmp(line, "ADD")) {
				if (origSeen)
					pc++;

				skipWhitespace(file);
				c = fgetc(file);
				if (c == '\n' || c == ';') {
					fprintf(stderr, "  Line %d: No operands "
						"supplied to ADD.\n",
						currentLine);
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for ADD.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for ADD"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for ADD"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				commaCount = 0;
				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for ADD.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand2[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand2[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for ADD"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for ADD"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r' && c != '#' &&
						!isdigit(c) && c != 'x' &&
						c != 'X' && c != '-') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for ADD.\n",
						currentLine);
					errors++;
					continue;
				}
				operand3[0] = c;

				if (c == 'R' || c == 'r') {
					if ((c = fgetc(file)) < '0' || c > '7') {
						while ((c = fgetc(file)) != '\n');
						fprintf(stderr, "Line %d: No such "
							"register (%d).\n", currentLine,
							c);
						currentLine++;
						errors++;
						continue;
					} else {
						operand3[1] = c;
					}
				} else if (c == '#' || isdigit(c) || c == '-') {
					if (c == '0' && ((c = fgetc(file)) == 'x'
							|| c == 'X')) {
						for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
							operand3[i] = c;
						}
						if (!isspace(c) && c != ';') {
							continue;
						}
						ungetc(c, file);
					} else {
						if (operand3[0] == '0') {
							ungetc(c, file);
						} else if (operand3[0] == '#') {
							if ((c = fgetc(file)) == '-') {
								operand3[1] = c;
								c = fgetc(file);
							}
						}

						for (size_t i = 1; isdigit(c); i++) {
							if (operand3[i])
								i++;
							operand3[i] = c;
							c = fgetc(file);
						}
						if (!isspace(c) && c != ';') {
							continue;
						}
						ungetc(c, file);
					}
				} else if (c == 'x' || c == 'X') {
					for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
						operand3[i] = c;
					}
					if (!isspace(c) && c != ';') {
						fprintf(stderr, "  Line %d: Too "
							"many operands given for"
							" ADD.\n",
							currentLine);
						errors++;
						continue;
					}
					ungetc(c, file);
				}

				instruction = 0x1000;

				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (operand2[1] - 0x30) << 6;

				if (operand3[0] == 'R' || operand3[0] == 'r') {
					instruction |= operand3[1] - 0x30;
				} else if (operand3[0] == '#') {
					instruction |= 0x20;
					tmp = (int16_t) strtol(operand3 + 1, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 10.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -16 || tmp > 15) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
					instruction |= tmp & 0x1f;
				} else if (operand3[0] == '-') {
					instruction |= 0x20;
					tmp = (int16_t) strtol(operand3, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 10.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -16 || tmp > 15) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
					instruction |= tmp & 0x1f;
				} else if (operand3[0] == 'x' || operand3[0] == 'X') {
					instruction |= 0x20;
					tmp = (int16_t) strtol(operand3, &end, 16);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 16.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -16 || tmp > 15) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
					instruction |= tmp & 0x1f;
				}

				objWrite(instruction, objFile);
				printf("ADD  %s  %s  %s\n", operand1, operand2, operand3);
			} else if (!uppercmp(line, "NOT")) {
				if (origSeen)
					pc++;

				skipWhitespace(file);
				c = fgetc(file);

				if (c == '\n' || c == ';') {
					fprintf(stderr, "  Line %d: No operands "
						"supplied to NOT.\n",
						currentLine);
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for NOT.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for NOT"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for NOT"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				commaCount = 0;
				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for NOT.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand2[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand2[1] = c;

				while (isspace(c = fgetc(file))) {
					if (c == '\n') {
						ungetc(c, file);
						break;
					}
				}

				if (c != ';' && c != '\n') {
					fprintf(stderr, "  Line %d: Too "
						"many operands given for"
						" NOT.\n",
						currentLine);
					errors++;
					continue;
				}
				ungetc(c, file);

				instruction = 0x903f;
				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (operand2[1] - 0x30) << 6;

				objWrite(instruction, objFile);
				printf("NOT  %s  %s\n", operand1, operand2);
			} else if (!uppercmp(line, "JMP")) {
				while (isspace(c = fgetc(file))) {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"No operands supplied to"
							" NOT.\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for NOT.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file))) {
					if (c == '\n') {
						ungetc(c, file);
						break;
					}
				}

				if (c != ';' && c != '\n') {
					printf("%c", c);
					fprintf(stderr, "  Line %d: Too "
						"many operands given for"
						" NOT.\n",
						currentLine);
					errors++;
					continue;
				}
				ungetc(c, file);

				instruction = 0xc000;
				instruction |= (operand1[1] - 0x30) << 6;

				objWrite(instruction, objFile);
				printf("JMP  %s\n", operand1);
			} else if (!uppercmp(line, "JSR")) {
				if (origSeen)
					pc++;
				instruction = 0x4800;

				skipWhitespace(file);

				fscanf(file, "%99s", operand1);

				c = fgetc(file);
				ungetc(c, file);

				if (!isspace(c) || c == ';') {
					fprintf(stderr, "  Line %d: "
						"Label too long.\n",
						currentLine);
					errors++;
					continue;
				}

				struct symbol *sym = findSymbol(operand1);
				if (NULL == sym) {
					fprintf(stderr, "  Line %d: Invalid "
						"label '%s'.\n", currentLine,
						operand1);
					errors++;
					continue;
				}

				tmp = sym->address - pc;

				if (tmp < -1024 || tmp > 1023) {
					fprintf(stderr, "  Line %d: Label is "
						"too far away.\n", currentLine);
					errors++;
					continue;
				}

				instruction |= (tmp & 0x7ff);
				objWrite(instruction, objFile);
				printf("JSR  %s (%d spaces away)\n", operand1, tmp);
			} else if (!uppercmp(line, "JSRR")) {
				if (origSeen)
					pc++;
				printf("JSRR ");
			} else if (!uppercmp(line, "LEA")) {
				if (origSeen)
					pc++;
				while (isspace(c = fgetc(file))) {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"No operands supplied to"
							" LEA.\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for LEA.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for LEA"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for LEA"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				ungetc(c, file);

				fscanf(file, "%99s", operand2);

				c = fgetc(file);
				ungetc(c, file);

				if (!isspace(c) || c == ';') {
					fprintf(stderr, "  Line %d: "
						"Label too long.\n",
						currentLine);
					errors++;
					continue;
				}

				struct symbol *sym = findSymbol(operand2);
				if (NULL == sym) {
					fprintf(stderr, "  Line %d: Invalid "
						"label '%s'.\n", currentLine,
						operand2);
					errors++;
					continue;
				}

				tmp = sym->address - pc;

				if (tmp < -256 || tmp > 255) {
					fprintf(stderr, "  Line %d: Label is "
						"too far away.\n", currentLine);
					errors++;
					continue;
				}

				instruction = 0xe000;
				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (tmp & 0x1ff);

				objWrite(instruction, objFile);
				printf("LEA  %s  %s  (%d spaces away)\n", operand1, operand2, tmp);
			} else if (!uppercmp(line, "LD")) {
				if (origSeen)
					pc++;
				while (isspace(c = fgetc(file))) {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"No operands supplied to"
							" LD.\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for LD.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for LD"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for LD"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				ungetc(c, file);

				fscanf(file, "%99s", operand2);

				c = fgetc(file);
				ungetc(c, file);

				if (!isspace(c) || c == ';') {
					fprintf(stderr, "  Line %d: "
						"Label too long.\n",
						currentLine);
					errors++;
					continue;
				}

				struct symbol *sym = findSymbol(operand2);
				if (NULL == sym) {
					fprintf(stderr, "  Line %d: Invalid "
						"label '%s'.\n", currentLine,
						operand1);
					errors++;
					continue;
				}

				tmp = sym->address - pc;

				if (tmp < -256 || tmp > 255) {
					fprintf(stderr, "  Line %d: Label is "
						"too far away.\n", currentLine);
					errors++;
					continue;
				}

				instruction = 0x2000;
				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (tmp & 0x1ff);

				objWrite(instruction, objFile);
				printf("LD  %s  %s(%d spaces away)\n", operand1, operand2, tmp);
			} else if (!uppercmp(line, "LDI")) {
				if (origSeen)
					pc++;
				while (isspace(c = fgetc(file))) {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"No operands supplied to"
							" LDI.\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for LDI.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for LDI"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for LDI"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				ungetc(c, file);

				fscanf(file, "%99s", operand2);

				c = fgetc(file);
				ungetc(c, file);

				if (!isspace(c) || c == ';') {
					fprintf(stderr, "  Line %d: "
						"Label too long.\n",
						currentLine);
					errors++;
					continue;
				}

				struct symbol *sym = findSymbol(operand2);
				if (NULL == sym) {
					fprintf(stderr, "  Line %d: Invalid "
						"label '%s'.\n", currentLine,
						operand1);
					errors++;
					continue;
				}

				tmp = sym->address - pc;

				if (tmp < -256 || tmp > 255) {
					fprintf(stderr, "  Line %d: Label is "
						"too far away.\n", currentLine);
					errors++;
					continue;
				}

				instruction = 0xa000;
				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (tmp & 0x1ff);

				objWrite(instruction, objFile);
				printf("LDI  %s  %s (%d spaces away)\n", operand1, operand2, tmp);
			} else if (!uppercmp(line, "LDR")) {
				if (origSeen)
					pc++;
				while (isspace(c = fgetc(file))) {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"No operands supplied to"
							" AND.\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for AND.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for AND"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for AND"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				commaCount = 0;
				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for AND.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand2[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand2[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for AND"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for AND"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != '#' && !isdigit(c) && c != 'x' &&
						c != 'X' && c != '-') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for AND.\n",
						currentLine);
					errors++;
					continue;
				}
				operand3[0] = c;

				if (c == '#' || isdigit(c) || c == '-') {
					if (c == '0' && ((c = fgetc(file)) == 'x'
							|| c == 'X')) {
						for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
							operand3[i] = c;
						}
						if (!isspace(c) && c != ';') {
							continue;
						}
						ungetc(c, file);
					} else {
						if (operand3[0] == '0') {
							ungetc(c, file);
						} else if (operand3[0] == '#') {
							if ((c = fgetc(file)) == '-') {
								operand3[1] = c;
								c = fgetc(file);
							}
						}

						for (size_t i = 1; isdigit(c); i++) {
							if (operand3[i])
								i++;
							operand3[i] = c;
							c = fgetc(file);
						}
						if (!isspace(c) && c != ';') {
							continue;
						}
						ungetc(c, file);
					}
				} else if (c == 'x' || c == 'X') {
					for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
						operand3[i] = c;
					}
					if (!isspace(c) && c != ';') {
						fprintf(stderr, "  Line %d: Too "
							"many operands given for"
							" ADD.\n",
							currentLine);
						errors++;
						continue;
					}
					ungetc(c, file);
				}

				while (isspace(c = fgetc(file))) {
					if (c == '\n')
						break;
				}

				if (c != ';' && c != '\n') {
					fprintf(stderr, "  Line %d: Too many "
						"operands given for AND.\n",
						currentLine);
				}

				instruction = 0x6000;
				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (operand2[1] - 0x30) << 6;

				if (operand3[0] == '#') {
					tmp = (int16_t) strtol(operand3 + 1, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 10.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -16 || tmp > 15) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
				} else if (operand3[0] == '-') {
					tmp = (int16_t) strtol(operand3, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 10.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -32 || tmp > 31) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
				} else if (operand3[0] == 'x' || operand3[0] == 'X') {
					tmp = (int16_t) strtol(operand3, &end, 16);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 16.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -32 || tmp > 31) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
				}
				instruction |= tmp & 0x3f;

				objWrite(instruction, objFile);
				printf("LDR  %s  %s  %s\n", operand1, operand2, operand3);
			} else if (!uppercmp(line, "ST")) {
				if (origSeen)
					pc++;
				while (isspace(c = fgetc(file))) {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"No operands supplied to"
							" ST.\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for ST.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (R%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for ST"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for ST"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				ungetc(c, file);

				fscanf(file, "%99s", operand2);

				c = fgetc(file);
				ungetc(c, file);

				if (!isspace(c) || c == ';') {
					fprintf(stderr, "  Line %d: "
						"Label too long.\n",
						currentLine);
					errors++;
					continue;
				}

				if (!endOfLine("STR", file, &currentLine)) {
					errors++;
					continue;
				}


				struct symbol *sym = findSymbol(operand2);
				if (NULL == sym) {
					fprintf(stderr, "  Line %d: Invalid "
						"label '%s'.\n", currentLine,
						operand1);
					errors++;
					continue;
				}

				tmp = sym->address - pc;

				if (tmp < -256 || tmp > 255) {
					fprintf(stderr, "  Line %d: Label is "
						"too far away.\n", currentLine);
					errors++;
					continue;
				}

				instruction = 0x3000;
				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (tmp & 0x1ff);

				objWrite(instruction, objFile);
				printf("ST %s  %s (%d spaces away)\n", operand1, operand2, tmp);
			} else if (!uppercmp(line, "STI")) {
				if (origSeen)
					pc++;

				skipWhitespace(file);
				c = fgetc(file);

				if (c == '\n' || c == ';') {
					fprintf(stderr, "  Line %d: No operands "
						"supplied to  STI.\n",
						currentLine);
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for STI.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (R%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for STI"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for STI"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				ungetc(c, file);

				fscanf(file, "%99s", operand2);

				c = fgetc(file);
				ungetc(c, file);

				if (!isspace(c) || c == ';') {
					fprintf(stderr, "  Line %d: "
						"Label too long.\n",
						currentLine);
					errors++;
					continue;
				}

				if (!endOfLine("STI", file, &currentLine)) {
					errors++;
					continue;
				}

				struct symbol *sym = findSymbol(operand2);
				if (NULL == sym) {
					fprintf(stderr, "  Line %d: Invalid "
						"label '%s'.\n", currentLine,
						operand1);
					errors++;
					continue;
				}

				tmp = sym->address - pc;

				if (tmp < -256 || tmp > 255) {
					fprintf(stderr, "  Line %d: Label is "
						"too far away.\n", currentLine);
					errors++;
					continue;
				}

				instruction = 0xb000;
				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (tmp & 0x1ff);

				objWrite(instruction, objFile);
				printf("STI %s  %s (%d spaces away)\n", operand1, operand2, tmp);
			} else if (!uppercmp(line, "STR")) {
				if (origSeen)
					pc++;

				skipWhitespace(file);
				c = fgetc(file);

				if (c == '\n' || c == ';') {
					fprintf(stderr, "  Line %d: No operands "
						"supplied to  STR.\n",
						currentLine);
					errors++;
					continue;
				}

				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for STR.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand1[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (R%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand1[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for STR"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for STR"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				commaCount = 0;
				if (c != 'R' && c != 'r') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for STR.\n",
						currentLine);
					currentLine++;
					errors++;
					continue;
				}
				operand2[0] = c;

				c = fgetc(file);
				if (c < '0' || c > '7') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "Line %d: No such "
						"register (R%c).\n", currentLine,
						c);
					currentLine++;
					errors++;
					continue;
				}
				operand2[1] = c;

				while (isspace(c = fgetc(file)) || c == ',') {
					if (c == '\n') {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for STR"
							".\n", currentLine);
						doContinue = true;
						break;
					} else if (c == ',' && ++commaCount > 1) {
						fprintf(stderr, "  Line %d: "
							"Incorrect number of "
							"operands given for STR"
							".\n", currentLine);
						doContinue = true;
						break;
					}
				}

				if (doContinue) {
					errors++;
					continue;
				}

				if (c != '#' && !isdigit(c) && c != 'x' &&
						c != 'X' && c != '-') {
					while ((c = fgetc(file)) != '\n');
					fprintf(stderr, "  Line %d: "
						"Invalid operand for STR.\n",
						currentLine);
					errors++;
					continue;
				}
				operand3[0] = c;

				if (c == '#' || isdigit(c) || c == '-') {
					if (c == '0' && ((c = fgetc(file)) == 'x'
							|| c == 'X')) {
						for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
							operand3[i] = c;
						}
						if (!isspace(c) && c != ';') {
							continue;
						}
						ungetc(c, file);
					} else {
						if (operand3[0] == '0') {
							ungetc(c, file);
						} else if (operand3[0] == '#') {
							if ((c = fgetc(file)) == '-') {
								operand3[1] = c;
								c = fgetc(file);
							}
						}

						for (size_t i = 1; isdigit(c); i++) {
							if (operand3[i])
								i++;
							operand3[i] = c;
							c = fgetc(file);
						}
						if (!isspace(c) && c != ';') {
							continue;
						}
						ungetc(c, file);
					}
				} else if (c == 'x' || c == 'X') {
					for (size_t i = 1; isxdigit(c = fgetc(file)); i++) {
						operand3[i] = c;
					}
					if (!isspace(c) && c != ';') {
						fprintf(stderr, "  Line %d: Too "
							"many operands given for"
							" STR.\n",
							currentLine);
						errors++;
						continue;
					}
					ungetc(c, file);
				}

				if (!endOfLine("STR", file, &currentLine)) {
					errors++;
					continue;
				}

				instruction = 0x7000;
				instruction |= (operand1[1] - 0x30) << 9;
				instruction |= (operand2[1] - 0x30) << 6;

				if (operand3[0] == '#') {
					tmp = (int16_t) strtol(operand3 + 1, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 10.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -16 || tmp > 15) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
				} else if (operand3[0] == '-') {
					tmp = (int16_t) strtol(operand3, &end, 10);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 10.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -32 || tmp > 31) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
				} else if (operand3[0] == 'x' || operand3[0] == 'X') {
					tmp = (int16_t) strtol(operand3, &end, 16);
					if (*end) {
						fprintf(stderr, "  Line %d: "
							"Invalid literal for base 16.\n",
							currentLine);
						errors++;
						continue;
					}
					if (tmp < -32 || tmp > 31) {
						fprintf(stderr, "  Line %d: "
							"Immediate value requires "
							"more than 5 bits.\n",
							currentLine);
						errors++;
						continue;
					}
				}
				instruction |= (tmp & 0x3f);

				objWrite(instruction, objFile);
				printf("STR  %s  %s  %s\n", operand1, operand2, operand3);
			} else if (!uppercmp(line, "RET")) {
				if (origSeen)
					pc++;

				if (!endOfLine("RET", file, &currentLine)) {
					errors++;
					continue;
				}

				objWrite(0xc1c0, objFile);
				printf("RET\n");
			} else if (!uppercmp(line, "TRAP")) {
				if (origSeen)
					pc++;
				if (c == 'T') {
					c = fgetc(file);
					c = fgetc(file);
				}

				if (!fscanf(file, "%hx", &instruction)) {
					fprintf(stderr, "  Line %d: Invalid "
						"literal for base 16.\n",
						currentLine);
					errors++;
					continue;
				}

				while (isspace(c) || isxdigit(c) || c == 'x' ||
						c == 'X') {
					if (c == '\n') {
						break;
					}
					c = fgetc(file);
				}
				ungetc(c, file);

				if (!endOfLine("TRAP", file, &currentLine)) {
					errors++;
					continue;
				}

				if (tmp < 0x20 || tmp > 0x25) {
					fprintf(stderr, "  Line %d: Invalid "
						"TRAP Routine.\n", currentLine);
					errors++;
					continue;
				}
				instruction = 0xf000 | tmp;

				objWrite(instruction, objFile);
				printf("TRAP x%x\n", tmp);
			} else if (!uppercmp(line, "HALT")       ||
					!uppercmp(line, "PUTS")  ||
					!uppercmp(line, "PUTC")  ||
					!uppercmp(line, "GETC")  ||
					!uppercmp(line, "OUT")   ||
					!uppercmp(line, "PUTSP") ||
					!uppercmp(line, "IN")) {
				if (origSeen)
					pc++;

				if (!endOfLine(line, file, &currentLine)) {
					errors++;
					continue;
				}

				if (!uppercmp(line, "GETC")) {
					instruction = 0xf020;
				} else if (!uppercmp(line, "PUTC") || !uppercmp(line, "OUT")) {
					instruction = 0xf021;
				} else if (!uppercmp(line, "PUTS")) {
					instruction = 0xf022;
				} else if (!uppercmp(line, "IN")) {
					instruction = 0xf023;
				} else if (!uppercmp(line, "PUTSP")) {
					instruction = 0xf024;
				} else {
					instruction = 0xf025;
				}

				objWrite(instruction, objFile);
				printf("%s\n", line);
			}
		}
	}

	printf("%d error%s\n", errors, errors != 1 ? "'s" : "");

	free(symFileName);
	free(hexFileName);
	free(binFileName);
	free(objFileName);

	free_table(&table);

	fclose(file);
	fclose(symFile);
	fclose(hexFile);
	fclose(binFile);
	fclose(objFile);

	return errors == 0;
}

