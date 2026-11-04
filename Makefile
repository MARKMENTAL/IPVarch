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

$(TARGET): ipvarch.c ipvarch_db.c ipvtbl.c ipvarch_db.h ipvtbl.h schema_sql.c vendor/sqlite3.o
	$(CC) $(CFLAGS) -o $@ ipvarch.c ipvarch_db.c ipvtbl.c schema_sql.c \
		vendor/sqlite3.o -lpthread -ldl -lm

# Schema DDL compiled into the binary (single source of truth is
# schema/used_networks.sql; regenerate after editing it, e.g. with make).
# Emits one C string literal per line into a NULL-terminated array, so
# no single literal approaches ISO C90's 509-byte limit and the output
# is C89-pedantic-clean.
schema_sql.c: schema/used_networks.sql
	awk 'BEGIN{print "/* generated from schema/used_networks.sql - do not edit */"; print "const char *ipvarch_schema_lines[] = {"} {gsub(/\\/,"\\\\"); gsub(/"/,"\\\""); printf " \"%s\\n\",\n", $$0} END{print " 0\n};"}' $< > $@

vendor/sqlite3.o: vendor/sqlite3.c vendor/sqlite3.h
	$(VCC) -O1 -c vendor/sqlite3.c -o $@

# Strict C89 conformance build of our sources (needs gcc; tcc has no such mode).
# The vendor object is rebuilt with gcc here: the default tcc-built
# vendor/sqlite3.o relies on tcc's libtcc1 runtime helpers, which gcc
# cannot link, so check compiles its own copy (this is why check always
# rebuilds it and clean removes the cached object).
check: $(TARGET) schema_sql.c
	@if command -v gcc >/dev/null 2>&1; then \
		mkdir -p /tmp/ipvarchtests && \
		gcc -O1 -c vendor/sqlite3.c -o /tmp/ipvarchtests/sqlite3-check.o && \
		gcc -std=c89 -pedantic -Wall -Wextra -Wno-long-long -D_FORTIFY_SOURCE=2 -O2 \
			-o /tmp/ipvarchtests/ipvarch-check ipvarch.c ipvarch_db.c ipvtbl.c schema_sql.c \
			/tmp/ipvarchtests/sqlite3-check.o -lpthread -ldl -lm && \
			echo "check: strict C89 build OK (/tmp/ipvarchtests/ipvarch-check)"; \
	else \
		echo "check: gcc not available, skipping (tcc has no strict mode)"; fi

# Validity test suite (python3, stdlib only); each run uses a fresh temp DB.
test: $(TARGET)
	python3 tests/runner.py --bin ./$(TARGET)

# Full matrix rebuilt with gcc/clang ASan+UBSan instrumentation.
test-sanitize: $(TARGET)
	python3 tests/runner.py --bin ./$(TARGET) --sanitize

clean:
	rm -f $(TARGET) /tmp/ipvarchtests/ipvarch-asan /tmp/ipvarchtests/ipvarch-check
	rm -f schema_sql.c /tmp/ipvarchtests/sqlite3-check.o
	rm -f vendor/sqlite3.o
