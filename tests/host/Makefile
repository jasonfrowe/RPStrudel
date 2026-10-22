# Host tests: build the pattern engine with the Mac's compiler and compare
# its output with Strudel's (tests/*_expected.txt, made by tests/ref).
#
#   make -C tests/host

ROOT := ../..
CFLAGS := -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I$(ROOT)/src
SRC := $(ROOT)/src/rat.c $(ROOT)/src/pool.c $(ROOT)/src/pattern.c $(ROOT)/src/query.c $(ROOT)/src/period.c $(ROOT)/src/scales.c $(ROOT)/src/mini.c $(ROOT)/src/prog.c

test: test_mini
	cd $(ROOT) && tests/host/test_mini mini tests/mini_cases.txt > tests/host/mini_actual.txt; \
	  diff -u tests/mini_expected.txt tests/host/mini_actual.txt && echo "mini: all cases match Strudel"
	cd $(ROOT) && tests/host/test_mini prog tests/prog_cases.txt > tests/host/prog_actual.txt; \
	  diff -u tests/prog_expected.txt tests/host/prog_actual.txt && echo "prog: all cases match Strudel"
	cd $(ROOT) && tests/host/test_mini random tests/random_cases.txt

test_mini: test_mini.c $(SRC) $(wildcard $(ROOT)/src/*.h)
	$(CC) $(CFLAGS) -o $@ test_mini.c $(SRC)

# Counts the fraction operations one cycle of the demo needs.
bench: bench.c $(SRC) $(wildcard $(ROOT)/src/*.h)
	$(CC) -std=c11 -O1 -DRAT_COUNT -I$(ROOT)/src -o $@ bench.c $(SRC) && ./bench

clean:
	rm -f test_mini bench mini_actual.txt prog_actual.txt

.PHONY: test clean
