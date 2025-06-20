CC := gcc
CFLAGS := -Wall -O2 -Iinclude

SRC := src/main.c
BIN := vtop

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	$(RM) $(BIN)

.PHONY: all run clean
