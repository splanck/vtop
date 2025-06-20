CC := gcc
CFLAGS := -Wall -O2 -Iinclude
SRC := src/main.c src/proc.c src/control.c src/units.c
BIN := vtop

ifdef WITH_UI
CFLAGS += -DWITH_UI
SRC += src/ui.c
LDLIBS += -lncurses
endif

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LDLIBS) -o $@

run: $(BIN)
	./$(BIN)

clean:
	$(RM) $(BIN)

.PHONY: all run clean
