SHELL := /bin/bash
PORT  ?= /dev/ttyACM0

# ESP-IDF, installed in ~/esp/esp-idf by default
IDF_PATH ?= $(HOME)/esp/esp-idf
idf = . $(IDF_PATH)/export.sh >/dev/null && idf.py -C $(1) -p $(PORT)

.PHONY: game-build game-flash game-monitor game-test game-bank

game-build:
	$(call idf,.) build

game-flash:
	$(call idf,.) flash

game-monitor:
	$(call idf,.) monitor

# Runs on this computer, no board needed: checks the rules and every level,
# plays each level with simulated touches, and saves screenshots.
game-test:
	$(MAKE) -C tools test

# Regenerates the bank of pre-computed puzzles (main/puzzle_bank.c).
game-bank:
	$(MAKE) -C tools bank
