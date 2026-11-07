CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2

minicc: minicc.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f minicc

.PHONY: clean
