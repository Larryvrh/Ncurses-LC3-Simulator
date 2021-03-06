#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "Machine.h"
#include "Memory.h"
#include "Error.h"
#include "Parser.h"

#define WORD_SIZE 2

#ifndef OS_PATH
#error "No Path has been supplied for the Operating System."
#endif

#define STR(x) #x
#define OSPATH(path) STR(path)
#define OS_OBJ_FILE OSPATH(OS_PATH) "/LC3_OS.obj"

int selected = 0;
uint16_t outputHeight = 0;
uint16_t *memoryOutput = NULL;
uint16_t selectedAddress = 0;

bool OSInstalled = false;
static bool symbolsInstalled = false;

static char const *const FORMAT = "0x%04X  %s  0x%04X  %-25s %-50s";
static unsigned int const SELECTED_ATTRIBUTES = A_REVERSE | A_BOLD;
static unsigned int const BREAKPOINT_ATTRIBUTES = COLOR_PAIR(1) | A_REVERSE;

/*
 * Install the Operating System (really, just put it into memory).
 */

static void installOS(struct program *program)
{
        uint16_t OSOrigin, tmp;

        FILE *OSFile = fopen(OS_OBJ_FILE, "r");
        if (NULL == OSFile) {
                perror("LC3-Simulator");
                exit(EXIT_FAILURE);
        }

        if (1 != fread(&OSOrigin, WORD_SIZE, 1, OSFile)) {
                fprintf(stderr, "Unable to read from the OS object file.\n");
                exit(EXIT_FAILURE);
        }

        OSOrigin = (uint16_t) (0xffff & (OSOrigin << 8 | OSOrigin >> 8));

        while (1 == fread(&tmp, WORD_SIZE, 1, OSFile)) {
                program->simulator.memory[OSOrigin] = (struct memorySlot) {
                        .value = (uint16_t) (0xffff & (tmp << 8 | tmp >> 8)),
                        .address = OSOrigin,
                        .isBreakpoint = false,
                };

                OSOrigin++;
        }

        fclose(OSFile);

        if (!OSInstalled) {
                populateOSSymbols();
                OSInstalled = true;
        }
}

/*
 * Populate the memory of the supplied simulator with the contents of
 * the provided file.
 *
 * @program: The program we want to populate the memory of, which also contains the
 *           file we will read the memory from.
 *
 * Returns: 0 on success, >0 on failure.
 */

int populateMemory(struct program *program)
{
        uint16_t tmpPc, instruction;

        FILE *file = fopen(program->objectfile, "rb");
        if (NULL == file) {
                perror("LC3-Simulator");
                exit(EXIT_FAILURE);
        }

        installOS(program);
        symbolsInstalled = false;

        // First word (2 bytes) in the .obj file is the starting PC.
        if (1 != fread(&tmpPc, WORD_SIZE, 1, file)) {
                fprintf(stderr, "Unable to read from %s.\n", program->objectfile);
                exit(EXIT_FAILURE);
        }

        program->simulator.PC = tmpPc = (uint16_t) (0xffff & (tmpPc << 8 | tmpPc >> 8));

        while (1 == fread(&instruction, WORD_SIZE, 1, file)) {
                program->simulator.memory[tmpPc] = (struct memorySlot) {
                        .value = (uint16_t) (0xffff & (instruction << 8 | instruction >> 8)),
                        .address = tmpPc,
                };

                tmpPc++;
        }

        if (!feof(file)) {
                fclose(file);
                tidyUp(program);
                read_error();
        }

        fclose(file);
        return 0;
}

/*
 * Convert a binary instruction to characters.
 *
 * @instr:   The 16 bit binary value that we want to convert.
 * @address: The address of the supplied instruction.
 * @buff:    Where we are to store the converted instruction.
 * @program: The program containing all instructions/files we need.
 *
 * Returns: The buff supplied.
 */

static char *instruction(uint16_t instr, uint16_t address, char *buff,
                         struct program *program)
{
        uint16_t opcode = (uint16_t) (instr & 0xF000);
        int16_t offset;
        char immediate[5];
        struct symbol *symbol;

        if (!symbolsInstalled) {
                populateSymbolsFromFile(program);
                symbolsInstalled = true;
        }

        switch (opcode) {
        case AND:
        case ADD:
                strcpy(buff, AND == opcode ? "AND R" : "ADD R");
                buff[5] = (char) (((instr >> 9) & 0x7) + 0x30);
                strcat(buff, ", R");
                buff[9] = (char) (((instr >> 6) & 0x7) + 0x30);
                strcat(buff, ", ");

                if (instr & 0x20) {
                        strcat(buff, "#");
                        snprintf(immediate, 5, "%hd",
                                (int16_t) (((int16_t) (instr << 11)) >> 11));
                        strcat(buff, immediate);
                } else {
                        strcat(buff, "R");
                        buff[13] = (char) ((instr & 0x7) + 0x30);
                }
                break;
        case JMP:
                if (0xC1C0 == instr) {
                        strcpy(buff, "RET");
                } else {
                        strcpy(buff, "JMP R");
                        buff[5] = (char) (((instr >> 9) & 0x7) + 0x30);
                }
                break;
        case BR:
                strcpy(buff, "BR");
                if (!(instr & 0x0E00)) {
                        strcpy(buff, "NOP");
                        break;
                }

                if (instr & 0x0800) {
                        strcat(buff, "n");
                }
                if (instr & 0x0400) {
                        strcat(buff, "z");
                }
                if (instr & 0x0200) {
                        strcat(buff, "p");
                }

                strcat(buff, " ");
                if (instr & 0x0100) {
                        offset = ((int16_t) (instr << 7)) >> 7;
                        symbol = findSymbolByAddress((uint16_t) address +
                                                     (uint16_t) offset);
                } else {
                        symbol = findSymbolByAddress((uint16_t) (address +
                                                                 (instr & 0x00FF)));
                }

                if (NULL != symbol) {
                        strcat(buff, symbol->name);
                } else {
                        // It should be pretty safe to assume that if we can't
                        // find the symbol, it doesn't exist and the
                        // 'instruction' is just a value in memory.
                        strcpy(buff, "NOP");
                }
                break;
        case JSR:
                strcpy(buff, "JSR ");
                if (instr & 0x0400) {
                        offset = ((int16_t) (instr << 5)) >> 5;
                        symbol = findSymbolByAddress((uint16_t) address +
                                                     (uint16_t) offset);
                } else {
                        symbol = findSymbolByAddress((uint16_t) (address + (instr & 0x03FF)));
                }

                if (NULL != symbol) {
                        strcat(buff, symbol->name);
                } else {
                        strcpy(buff, "NOP");
                }
                break;
        case LEA:
        case LD:
        case LDI:
        case ST:
        case STI:
                switch (opcode) {
                case LEA:
                        strcpy(buff, "LEA R");
                        break;
                case LD:
                        strcpy(buff, "LD R");
                        break;
                case LDI:
                        strcpy(buff, "LDI R");
                        break;
                case ST:
                        strcpy(buff, "ST R");
                        break;
                case STI:
                        strcpy(buff, "STI R");
                        break;
                default:
                        break;
                }

                buff[strlen(buff)] = (char) (((instr >> 9) & 0x7) + 0x30);
                strcat(buff, ", ");
                if (instr & 0x0100) {
                        offset = ((int16_t) (instr << 7)) >> 7;
                        symbol = findSymbolByAddress((uint16_t) address +
                                                     (uint16_t) offset);
                } else {
                        symbol = findSymbolByAddress((uint16_t) address +
                                                     (uint16_t) (instr & 0x00FF));
                }

                if (NULL != symbol) {
                        strcat(buff, symbol->name);
                } else {
                        strcpy(buff, "NOP");
                }
                break;
        case LDR:
        case STR:
                strcpy(buff, STR == opcode ? "STR R" : "LDR R");
                buff[5] = (char) ((instr >> 9 & 0x7) + 0x30);
                strcat(buff, ", R");
                buff[9] = (char) ((instr >> 6 & 0x7) + 0x30);
                strcat(buff, ", #");
                snprintf(immediate, 5, "%hd",
                        (int16_t) ((int16_t) (instr << 10) << 10));
                strcat(buff, immediate);
                break;
        case NOT:
                strcpy(buff, "NOT R");
                buff[5] = (char) ((instr >> 9 & 0x7) + 0x30);
                strcat(buff, ", R");
                buff[9] = (char) ((instr >> 6 & 0x7) + 0x30);
                break;
        case TRAP:
                switch (instr & 0x00FF) {
                case 0x25:
                        strcpy(buff, "HALT");
                        break;
                case 0x24:
                        strcpy(buff, "PUTSP");
                        break;
                case 0x23:
                        strcpy(buff, "IN");
                        break;
                case 0x22:
                        strcpy(buff, "PUTS");
                        break;
                case 0x21:
                        strcpy(buff, "OUT");
                        break;
                case 0x20:
                        strcpy(buff, "GETC");
                        break;
                default:
                        strcpy(buff, "NOP");
                        break;
                }
                break;
        default:
                break;
        }

        return buff;
}

static void winPrint(WINDOW *window, struct program *program, size_t address,
                     int y, int x)
{
        struct symbol *symbol = NULL;
        char instr[100] = {0};
        char label[100] = {0};

        char binary[] = "0000000000000000";
        for (int i = 15, bit = 1; i >= 0; i--, bit <<= 1) {
                binary[i] =
                        (char) (program->simulator.memory[address].value & bit ?
                                '1' : '0');
        }

        instruction(program->simulator.memory[address].value,
                (uint16_t) (address + 1), instr, program);

        symbol = findSymbolByAddress((uint16_t) address);
        if (NULL != symbol) {
                strcpy(label, symbol->name);
        }

        mvwprintw(window, y, x, FORMAT, address, binary,
                program->simulator.memory[address].value, label, instr);
        wrefresh(window);
}

void update(WINDOW *window, struct program *program)
{
        memoryOutput[selected] = program->simulator.memory[selectedAddress].value;
        wattron(window, SELECTED_ATTRIBUTES);
        winPrint(window, program, selectedAddress, selected + 1, 1);
        wattroff(window, SELECTED_ATTRIBUTES);
}

/*
 * Redraw the memory view. Called after every time the user moves up / down in
 * the area.
 */

static void redraw(WINDOW *window, struct program *program)
{
        for (int i = 0; i < outputHeight; ++i) {
                if (i < selected) {
                        if (program->simulator.memory[selectedAddress - selected + i].isBreakpoint) {
                                wattron(window, BREAKPOINT_ATTRIBUTES);
                        }

                        winPrint(window, program,
                                (size_t) selectedAddress - (size_t) selected +
                                (size_t) i, i + 1, 1);

                        if (program->simulator.memory[selectedAddress - selected + i].isBreakpoint) {
                                wattroff(window, BREAKPOINT_ATTRIBUTES);
                        }
                } else {
                        if (program->simulator.memory[selectedAddress + i].isBreakpoint) {
                                wattron(window, BREAKPOINT_ATTRIBUTES);
                        }

                        winPrint(window, program, (size_t) selectedAddress +
                                                  (size_t) i, i + 1, 1);

                        if (program->simulator.memory[selectedAddress + i].isBreakpoint) {
                                wattroff(window, BREAKPOINT_ATTRIBUTES);
                        }
                }
        }

        wattron(window, SELECTED_ATTRIBUTES);
        winPrint(window, program, selectedAddress, selected + 1, 1);
        wattroff(window, SELECTED_ATTRIBUTES);
}


/*
 * Given a currently selected index and address, populate the memoryOutput
 * array to contain relative values.
 */

void generateContext(WINDOW *window, struct program *program, int _selected,
                     uint16_t _selectedAddress)
{
        int i = 0;

        selected = _selected;
        selectedAddress = _selectedAddress;

        selected =
                ((selectedAddress + (outputHeight - 1 - selected)) > 0xfffe) ?
                (outputHeight - (0xfffe - selectedAddress)) - 1 : selected;

        for (; i < selected; i++)
                memoryOutput[i] = program->simulator.memory[
                        selectedAddress - selected + i].value;
        for (; i < outputHeight; i++)
                memoryOutput[i] = program->simulator.memory[
                        selectedAddress + i].value;

        redraw(window, program);
        memPopulated = selectedAddress;
}

void moveContext(WINDOW *window, struct program *program, enum DIRECTION direction)
{
        bool _redraw = false;
        int prev = selected;
        uint16_t previousAddress = selectedAddress;

        switch (direction) {
        case UP:
                selectedAddress -= selectedAddress != 0;
                selected = !selected ? _redraw = true, 0 : selected - 1;
                break;
        case DOWN:
                selectedAddress += (0xFFFE == selectedAddress) ? 0 : 1;
                selected = ((outputHeight - 1) == selected) ? _redraw = true,
                        (outputHeight - 1) : selected + 1;
                break;
        default:
                break;
        }

        wattron(window, SELECTED_ATTRIBUTES);
        winPrint(window, program, selectedAddress, selected + 1, 1);
        wattroff(window, SELECTED_ATTRIBUTES);

        if (program->simulator.memory[previousAddress].isBreakpoint) {
                wattron(window, BREAKPOINT_ATTRIBUTES);
        }

        winPrint(window, program, previousAddress, prev + 1, 1);

        if (program->simulator.memory[previousAddress].isBreakpoint) {
                wattroff(window, BREAKPOINT_ATTRIBUTES);
        }

        if (_redraw) {
                generateContext(window, program, selected, selectedAddress);
        }
}

