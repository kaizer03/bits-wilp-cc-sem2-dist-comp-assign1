# Makefile — bits-wilp-cc-sem2-dist-comp-assign1
# Distributed File System (CLIENT + SERVER1 + SERVER2)
#
# Build:   make            (builds all three binaries into bin/)
# Run:     make run        (starts the interactive client; servers auto-launch)
# Clean:   make clean

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
BINDIR  = bin

# Detect OS for platform-specific flags
UNAME := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(UNAME),Windows_NT)
    EXT    = .exe
    WFLAGS = -lws2_32
else
    EXT    =
    WFLAGS =
endif

.PHONY: all server2 server1 client clean run run-server1 run-server2 help

## Build all three binaries
all: server2 server1 client

server2: $(BINDIR)/server2$(EXT)

server1: $(BINDIR)/server1$(EXT)

client: $(BINDIR)/client$(EXT)

$(BINDIR)/server2$(EXT): server2.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(WFLAGS)

$(BINDIR)/server1$(EXT): server1.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(WFLAGS)

$(BINDIR)/client$(EXT): client.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(WFLAGS)

$(BINDIR):
	mkdir -p $(BINDIR)

## Create client_files directory if not present
client_files:
	mkdir -p client_files

## Run the interactive client (servers are auto-launched by the client)
run: all client_files
	./$(BINDIR)/client$(EXT) 127.0.0.1 5001

## Manually start Server 1 (if you prefer to run servers separately)
run-server1: $(BINDIR)/server1$(EXT)
	./$(BINDIR)/server1$(EXT) 5001 127.0.0.1 5002 ./server1_files

## Manually start Server 2 (if you prefer to run servers separately)
run-server2: $(BINDIR)/server2$(EXT)
	./$(BINDIR)/server2$(EXT) 5002 ./server2_files

## Remove compiled binaries
clean:
	rm -f $(BINDIR)/server1$(EXT) $(BINDIR)/server2$(EXT) $(BINDIR)/client$(EXT)

## Show available targets
help:
	@echo ""
	@echo "  make          — Build all binaries"
	@echo "  make run      — Build and launch the interactive client"
	@echo "  make clean    — Remove compiled binaries"
	@echo ""
	@echo "  Manual server start (advanced):"
	@echo "  make run-server1  — Start Server 1 manually"
	@echo "  make run-server2  — Start Server 2 manually"
	@echo ""
