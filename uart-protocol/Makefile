# Makefile for UART Protocol Library and Test
# Works on Linux (Ubuntu/Arch) and macOS

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c99 -Iinclude
LDFLAGS  =
RM       = rm -f

# Detect OS for platform‑specific tweaks (if any)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    # Linux specific flags (none needed)
endif
ifeq ($(UNAME_S),Darwin)
    # macOS specific flags (none needed)
endif

TARGET   = test_uart_protocol
SRCDIR   = src
INCDIR   = include
BUILDDIR = build

SOURCES  = $(SRCDIR)/uart_protocol.c $(SRCDIR)/test_protocol.c
OBJECTS  = $(addprefix $(BUILDDIR)/, $(notdir $(SOURCES:.c=.o)))

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

clean:
	$(RM) -r $(BUILDDIR) $(TARGET)

.PHONY: all clean