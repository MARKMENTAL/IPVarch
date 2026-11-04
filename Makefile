# IPVarch - Internet Protocol Version Architect
# Primary compiler is tcc (Tiny C Compiler); falls back to gcc automatically.
# The vendored SQLite amalgamation (vendor/sqlite3.[ch], downloaded from
# https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip) is compiled
# separately with gcc - tcc links the resulting object fine.

#CC := $(shell command -v tcc 2>/dev/null || command -v gcc 2>/dev/null || echo cc)
CC := tcc
#VCC := gcc
VCC := tcc
CFLAGS := -Wall
TARGET := ipvarch

.PHONY: all check test test-sanitize clean

all: $(TARGET)

$(TARGET): ipvarch.c ipvarch_db.c ipvtbl.c ipvarch_db.h ipvtbl.h vendor/sqlite3.o
	$(CC) $(CFLAGS) -o $@ ipvarch.c ipvarch_db.c ipvtbl.c \
		vendor/sqlite3.o -lpthread -ldl -lm

vendor/sqlite3.o: vendor/sqlite3.c vendor/sqlite3.h
	$(VCC) -O1 -c vendor/sqlite3.c -o $@

# Strict C89 conformance build of our sources (needs gcc; tcc has no such mode).
# The vendor object is linked in, so the binary can be smoke-tested too.
check: $(TARGET)
	@if command -v gcc >/dev/null 2>&1; then \
		gcc -std=c89 -pedantic -Wall -Wextra -Wno-long-long -D_FORTIFY_SOURCE=2 -O2 \
			-o /tmp/opencode/ipvarch-check ipvarch.c ipvarch_db.c ipvtbl.c \
			vendor/sqlite3.o -lpthread -ldl -lm && \
			echo "check: strict C89 build OK (/tmp/opencode/ipvarch-check)"; \
	else \
		echo "check: gcc not available, skipping (tcc has no strict mode)"; fi

# Validity test suite (python3, stdlib only); each run uses a fresh temp DB.
test: $(TARGET)
	python3 tests/runner.py --bin ./$(TARGET)

# Full matrix rebuilt with gcc/clang ASan+UBSan instrumentation.
test-sanitize: $(TARGET)
	python3 tests/runner.py --bin ./$(TARGET) --sanitize

clean:
	rm -f $(TARGET) /tmp/opencode/ipvarch-asan /tmp/opencode/ipvarch-check
	rm -f vendor/sqlite3.o
