# Host tests for the Cardea evidence engine.
#
# The engine is deliberately free of Flipper headers, so it compiles and runs
# on a laptop, under the same warning settings the firmware build uses.
#
#     make -C test

CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow -Wconversion

BIN := host_detect_test
SRC := host_detect_test.c ../helpers/cdr_detect.c

.PHONY: all clean
all: $(BIN)
	./$(BIN)

$(BIN): $(SRC) ../helpers/cdr_detect.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -rf $(BIN) $(BIN).dSYM
