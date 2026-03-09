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

TEST_FLAGS = -O0 -g -Wall -I test
UNITY_SRC  = test/unity.c

.PHONY: all server2 server1 client clean run run-server1 run-server2 test help

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

## Build and run all unit tests (each test is its own binary)
## Results from every suite are collected and a grand total is printed at the end.
TEST_SUMMARY = $(BINDIR)/.test_summary

test: $(BINDIR)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_pathsanitize     test/test_pathsanitize.c       $(UNITY_SRC)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_sanitize_name    test/test_sanitize_name.c      $(UNITY_SRC)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_cancel_token     test/test_cancel_token.c       $(UNITY_SRC)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_buffers_equal    test/test_buffers_equal.c      $(UNITY_SRC)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_server_status    test/test_server_status_code.c $(UNITY_SRC)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_file_identical   test/test_file_identical.c     $(UNITY_SRC)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_server_ready     test/test_server_ready.c       $(UNITY_SRC)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_recv_length      test/test_recv_length_guard.c  $(UNITY_SRC)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_file_size        test/test_file_size_guard.c    $(UNITY_SRC)
	$(CC) $(TEST_FLAGS) -o $(BINDIR)/test_target_label     test/test_target_label.c       $(UNITY_SRC)
	@> $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_pathsanitize"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_pathsanitize | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_sanitize_name"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_sanitize_name | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_cancel_token"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_cancel_token | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_buffers_equal"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_buffers_equal | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_server_status_code"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_server_status | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_file_identical"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_file_identical | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_server_ready"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_server_ready | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_recv_length_guard"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_recv_length | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_file_size_guard"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_file_size | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "──────────────────────────────────────────"
	@echo "  Running: test_target_label"
	@echo "──────────────────────────────────────────"
	@./$(BINDIR)/test_target_label | tee -a $(TEST_SUMMARY) | tee -a $(TEST_SUMMARY)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo "  GRAND TOTAL"
	@echo "══════════════════════════════════════════"
	@awk '/^[0-9]+ Tests/ { t+=$$1; f+=$$3; i+=$$5 } \
	      END { \
	        printf "  %d Tests  %d Failures  %d Ignored\n", t, f, i; \
	        if (f == 0) printf "  ALL TESTS PASSED\n"; \
	        else        printf "  SOME TESTS FAILED\n"; \
	      }' $(TEST_SUMMARY)
	@echo "══════════════════════════════════════════"
	@echo ""

## Remove compiled binaries and test binaries
clean:
	rm -f $(BINDIR)/server1$(EXT) $(BINDIR)/server2$(EXT) $(BINDIR)/client$(EXT)
	rm -f $(BINDIR)/test_pathsanitize  $(BINDIR)/test_sanitize_name \
	      $(BINDIR)/test_cancel_token  $(BINDIR)/test_buffers_equal \
	      $(BINDIR)/test_server_status $(BINDIR)/test_file_identical \
	      $(BINDIR)/test_server_ready  $(BINDIR)/test_recv_length    \
	      $(BINDIR)/test_file_size     $(BINDIR)/test_target_label   \
	      $(TEST_SUMMARY)

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
	@echo "  make test         — Build and run unit tests"
	@echo ""
