CFLAGS ?= -std=c99 -O2 -Wall -Wextra -pedantic -ffp-contract=off

modelita: modelita.c modelita.h
	$(CC) $(CFLAGS) -o $@ modelita.c -lm

test: modelita
	cd .. && python3 tests/run.py -- c/modelita

clean:
	rm -f modelita

.PHONY: test clean
