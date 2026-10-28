CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -Isrc
LIBS = -lm

ifneq ($(OS),Windows_NT)
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
ifeq ($(UNAME_S),Linux)
    LIBS += -lrt
endif
endif

SRCS = src/input_buffer.c src/predictor.c src/spill_buffer.c src/lexer.c src/statistics.c src/report.c
OBJS = $(SRCS:.c=.o)

BIN_DIR = bin
TARGET = $(BIN_DIR)/plsb_lexer

.PHONY: all clean test benchmark dirs

all: dirs $(TARGET) test_binaries benchmark_binaries

dirs:
	mkdir -p $(BIN_DIR)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) src/main.o
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

src/main.o: src/main.c
	$(CC) $(CFLAGS) -c $< -o $@

TEST_SRCS = tests/test_runner.c \
            tests/unit/test_input_buffer.c \
            tests/unit/test_predictor.c \
            tests/unit/test_spill_buffer.c \
            tests/unit/test_lexer.c \
            tests/integration/test_boundary_conditions.c \
            tests/integration/test_comparison.c \
            tests/integration/test_regression.c

test_binaries: dirs $(OBJS)
	$(CC) $(CFLAGS) $(TEST_SRCS) $(OBJS) -o $(BIN_DIR)/test_runner $(LIBS)

benchmark_binaries: dirs $(OBJS)
	$(CC) $(CFLAGS) benchmarks/benchmark_runner.c $(OBJS) -o $(BIN_DIR)/benchmark_runner $(LIBS)

test: test_binaries
	$(BIN_DIR)/test_runner

benchmark: benchmark_binaries
	$(BIN_DIR)/benchmark_runner

clean:
	rm -f src/*.o $(BIN_DIR)/*
