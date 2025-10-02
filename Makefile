# HPC-IDS C System Makefile
# Hardware Performance Counter-Based Intrusion Detection System

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
INCLUDES = -Iinclude
LIBS = -lm

# Directories
SRCDIR = src
INCDIR = include
OBJDIR = obj

# Create object directory
$(shell mkdir -p $(OBJDIR))

# Source files
CORE_SOURCES = $(SRCDIR)/core.c $(SRCDIR)/detection.c $(SRCDIR)/perf_integration.c \
               $(SRCDIR)/statistics.c $(SRCDIR)/simple_json.c $(SRCDIR)/config.c

CORE_OBJECTS = $(CORE_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Main targets
.PHONY: all clean install help

all: hpc_ids baseline_collector energy_monitor test_cpu test_memory

# Main HPC-IDS binary
hpc_ids: $(CORE_OBJECTS) $(OBJDIR)/baseline_collector.o $(OBJDIR)/hpc_ids_main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Baseline collector utility
baseline_collector: $(CORE_OBJECTS) $(OBJDIR)/baseline_collector.o $(OBJDIR)/baseline_collector_main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Energy monitor utility
energy_monitor: $(CORE_OBJECTS) $(OBJDIR)/energy_monitor.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Object file compilation
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test programs
test_cpu: test_cpu.c
	$(CC) $(CFLAGS) -o $@ $<

test_memory: test_memory.c
	$(CC) $(CFLAGS) -o $@ $<

# Clean build artifacts
clean:
	rm -rf $(OBJDIR)
	rm -f hpc_ids baseline_collector energy_monitor test_cpu test_memory
	rm -f *.log *.jsonl *.json

# Install system-wide (requires sudo)
install: all
	sudo cp hpc_ids /usr/local/bin/
	sudo cp baseline_collector /usr/local/bin/
	sudo cp energy_monitor /usr/local/bin/
	sudo mkdir -p /etc/hpc-ids
	sudo cp config/*.json /etc/hpc-ids/

# Development targets
debug: CFLAGS += -g -DDEBUG
debug: all

# Help target
help:
	@echo "HPC-IDS C System Build Targets:"
	@echo "  all              - Build all binaries"
	@echo "  hpc_ids          - Build main IDS binary"
	@echo "  baseline_collector - Build baseline collection utility"
	@echo "  energy_monitor   - Build energy monitoring utility"
	@echo "  clean            - Remove build artifacts"
	@echo "  install          - Install system-wide (requires sudo)"
	@echo "  debug            - Build with debug symbols"
	@echo "  help             - Show this help message"

# Dependencies
$(OBJDIR)/core.o: $(INCDIR)/hpc_ids.h
$(OBJDIR)/detection.o: $(INCDIR)/hpc_ids.h
$(OBJDIR)/perf_integration.o: $(INCDIR)/hpc_ids.h
$(OBJDIR)/statistics.o: $(INCDIR)/hpc_ids.h
$(OBJDIR)/config.o: $(INCDIR)/hpc_ids.h
$(OBJDIR)/hpc_ids_main.o: $(INCDIR)/hpc_ids.h
$(OBJDIR)/baseline_collector.o: $(INCDIR)/hpc_ids.h
$(OBJDIR)/baseline_collector_main.o: $(INCDIR)/hpc_ids.h
$(OBJDIR)/energy_monitor.o: $(INCDIR)/hpc_ids.h
