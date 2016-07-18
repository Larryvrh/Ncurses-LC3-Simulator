# Use gcc as the compiler
CC = gcc

# The program name
EXECUTABLE = LC3Simulator

# Source directory
SRCDIR = source
# Source files
SRC   := $(wildcard $(SRCDIR)/*.c)
# Object files (gathered by substitution)
OBJS  := $(SRC:.c=.o)

# Includes directory
INCDIR   = includes
# Flag to add the above directory to gcc's search path
INCLUDES = -I$(INCDIR)
# Library dependencies
LIBS     = -lncurses

# Flags to use when compiling
CFLAGS = $(INCLUDES) $(LIBS) -std=c11
# Flags to add when compiling the debug version
DFLAGS = -Wall -Werror -Wpedantic -Wextra

GCC6 := $(shell which gcc-6 2>/dev/null)

RM = rm -f


# Default target if none is given.
default: build

ifdef GCC6
# Using Homebrew's version of gcc-6 for debug, because clang throws a
# warning when -lncurses is linked but not used.
debug: CC = gcc-6
endif

# Add the debug flags
debug: CFLAGS += $(DFLAGS)
# Build the system
debug: build

# Use ctags to generate tags for the c files
tags: $(SRC)
	ctags --sort=yes --verbose $(wildcard $(INCDIR)/*.h) $^

# Build the executable file
build: $(OBJS)
	$(CC) $(CFLAGS) -o $(EXECUTABLE) $(OBJS)

%.o: %.c $(INCDIR)/%.h
	$(CC) $(CFLAGS) -c $< -o $@

run: build
	./$(EXECUTABLE)

# Remove all generated files
clean:
	$(RM) $(EXECUTABLE) $(OBJS)
