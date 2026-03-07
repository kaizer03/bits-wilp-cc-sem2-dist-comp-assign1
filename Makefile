# Makefile — bits-wilp-cc-sem2-dist-comp-assign1
# Distributed File Server (CLIENT + SERVER1 + SERVER2)

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
BINDIR  = bin

.PHONY: all server1 server2 client clean

## Build all three binaries
all: server2 server1 client

server2: $(BINDIR)/server2

server1: $(BINDIR)/server1

client: $(BINDIR)/client

$(BINDIR)/server2: server2.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BINDIR)/server1: server1.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BINDIR)/client: client.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BINDIR):
	mkdir -p $(BINDIR)

## Remove compiled binaries
clean:
	rm -f $(BINDIR)/server1 $(BINDIR)/server2 $(BINDIR)/client
