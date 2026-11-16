# Builds the PC-side tool from the same sources as the firmware.
CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -Wall -Wextra
BOARD   := ../components/amoled-board
SOURCES := puzzle_tool.c ../main/puzzle.c ../main/game.c ../main/levels.c ../main/synth.c ../main/generator.c ../main/puzzle_bank.c $(BOARD)/gfx.c
HEADERS := $(wildcard ../main/*.h $(BOARD)/include/gfx.h $(BOARD)/font5x7.h)
TOOL    := ../build-host/puzzle_tool

.PHONY: test bank clean

$(TOOL): $(SOURCES) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I../main -I$(BOARD)/include -I$(BOARD) -o $@ $(SOURCES) -lm

test: $(TOOL)
	$(TOOL) check
	@mkdir -p ../build-host/shots
	$(TOOL) shots ../build-host/shots
	$(TOOL) sound ../build-host/sounds.wav

# Regenerates ../main/puzzle_bank.c. Run after changing levels.c or the generator.
bank: $(TOOL)
	$(TOOL) bank ../main/puzzle_bank.c 40

clean:
	rm -rf ../build-host
