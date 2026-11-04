# IPVarch - Internet Protocol Version Architect
# The compiler is auto-detected: tcc first, then gcc, then the POSIX name cc.
# command -v prints an absolute path, so the recipes call the compiler by full
# path; force one with 'make CC=gcc'.
# The vendored SQLite amalgamation (vendor/sqlite3.[ch], downloaded from
# https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip) is compiled
# with $(VCC); 'make check' and the sanitizer runner build their own vendor
# objects, since those need gcc/clang modes.

CC := $(shell command -v tcc 2>/dev/null || command -v gcc 2>/dev/null || echo cc)
# Deliberately the same compiler as CC: a tcc-built object references libtcc1
# runtime helpers (__va_arg, __floatundidf) that gcc cannot resolve, so the
# amalgamation has to be built by whatever is going to link it.
VCC := $(CC)

# Tagged with the compiler that built it, because auto-detection means the
# compiler can change without the Makefile changing: vendor/sqlite3.tcc.o and
# vendor/sqlite3.gcc.o then sit side by side and each link picks its own,
# instead of reusing a stale object compiled by the other toolchain.
# Assumes CC and VCC are single words.  Note the *binary* is not tagged: if
# both objects already exist, switching compilers back does not relink an
# up-to-date ipvarch, so 'make clean' is the answer when you care which
# compiler produced the executable.
SQLITE_OBJ := vendor/sqlite3.$(notdir $(VCC)).o

ifneq ($(notdir $(CC)),$(notdir $(VCC)))
$(warning VCC=$(VCC) differs from CC=$(CC): gcc cannot link a tcc-built object)
endif

# Embedder configuration for the vendored amalgamation, chosen so the link
# needs no libraries at all: SQLITE_OMIT_LOAD_EXTENSION drops dlopen/dlclose,
# SQLITE_THREADSAFE=0 drops the pthread mutex layer (the tool is
# single-threaded and never loads extensions) and SQLITE_OMIT_FLOATING_POINT
# drops the one libm reference left, fabs.  glibc >= 2.34 resolved dl and
# pthread into libc, and minimal systems (Buildroot and friends) ship no
# libdl.so/libpthread.so/libm.so dev library, while tcc aborts the entire
# link when a -l flag cannot be satisfied.  Nothing is lost here: the
# registry stores INTEGER and TEXT only -- no REAL column, no double in our
# sources, and every sqlite3_snprintf format is %s or %d.  If a future
# schema needs REAL, drop the last define and link -lm.
# The defines reach every translation unit, ours included: with
# SQLITE_OMIT_FLOATING_POINT sqlite3.h changes its float typedefs, so our
# declarations must match the compiled object.
SQL_DEFS := -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_FLOATING_POINT
CFLAGS := -Wall $(SQL_DEFS)
TARGET := ipvarch

.PHONY: all check test test-sanitize print-flags clean

all: $(TARGET)

$(TARGET): ipvarch.c ipvarch_db.c ipvtbl.c ipvarch_db.h ipvtbl.h schema_sql.c $(SQLITE_OBJ) Makefile
	$(CC) $(CFLAGS) -o $@ ipvarch.c ipvarch_db.c ipvtbl.c schema_sql.c \
		$(SQLITE_OBJ)

# Schema DDL compiled into the binary (single source of truth is
# schema/used_networks.sql; regenerate after editing it, e.g. with make).
# Emits one C string literal per line into a NULL-terminated array, so no
# single literal approaches ISO C90's 509-byte limit.  Leading "--"
# comments and blank lines stay in the SQL file for readers and are not
# embedded here: the array then holds only live DDL, which carries no
# double quotes to escape.  Escaping uses printf + POSIX sed only, since
# awk's gsub() replacement handling is implementation-defined and BusyBox
# awk drops the backslash in \" -- that produced invalid C on a BusyBox
# system.  This target also depends on the Makefile: a changed generator
# must re-run even when the .sql timestamp is older than a stale,
# previously broken schema_sql.c.
# The output is staged in a temp file, asserted non-empty, and compiled
# (-x c -c -o /dev/null; tcc has no real -fsyntax-only and would try to
# link) before being moved into place, so a broken generator fails here
# instead of surfacing later as a confusing compile error.
# Note the doubled $$ in the sed script: make reads $/ as an (empty)
# variable reference, which would silently emit an empty array.
schema_sql.c: schema/used_networks.sql Makefile
	{ printf '/* generated from schema/used_networks.sql - do not edit */\nconst char *ipvarch_schema_lines[] = {\n'; \
	  sed -e '/^--/d' -e '/^$$/d' -e 's/\\/\\\\/g' -e 's/"/\\"/g' \
	      -e 's/^/ "/' -e 's/$$/\\n",/' $<; \
	  printf ' 0\n};\n'; } > $@.tmp && grep -q 'CREATE TABLE' $@.tmp \
	  && $(CC) -x c -c $@.tmp -o /dev/null && mv -f $@.tmp $@

# Depends on the Makefile too, so a changed SQL_DEFS rebuilds the object rather
# than leaving one compiled with the old flags (the same trap schema_sql.c
# documents above).  A changed *compiler* needs no such care: the object name
# carries the compiler, so a switch simply compiles its own file.
$(SQLITE_OBJ): vendor/sqlite3.c vendor/sqlite3.h Makefile
	$(VCC) -O1 $(SQL_DEFS) -c vendor/sqlite3.c -o $@

# Strict C89 conformance build of our sources (needs gcc; tcc has no such mode).
# The vendor object is rebuilt with gcc here: a tcc-built
# vendor/sqlite3.$(notdir $(VCC)).o relies on tcc's libtcc1 runtime helpers,
# which gcc cannot link, so check compiles its own copy (this is why check always
# rebuilds it and clean removes the cached object).  No -l flags: see
# SQL_DEFS above.
check: $(TARGET) schema_sql.c
	@if command -v gcc >/dev/null 2>&1; then \
		mkdir -p /tmp/ipvarchtests && \
		gcc -O1 $(SQL_DEFS) -c vendor/sqlite3.c -o /tmp/ipvarchtests/sqlite3-check.o && \
		gcc -std=c89 -pedantic -Wall -Wextra -Wno-long-long -D_FORTIFY_SOURCE=2 -O2 $(SQL_DEFS) \
			-o /tmp/ipvarchtests/ipvarch-check ipvarch.c ipvarch_db.c ipvtbl.c schema_sql.c \
			/tmp/ipvarchtests/sqlite3-check.o && \
			echo "check: strict C89 build OK (/tmp/ipvarchtests/ipvarch-check)"; \
	else \
		echo "check: gcc not available, skipping (tcc has no strict mode)"; fi

# Build-time facts for the sanitizer runner: one source of truth for the
# SQLite embedder defines, so an ASan build can never be configured
# differently from the real one (same principle as the schema generator).
print-flags:
	@printf '%s\n' $(SQL_DEFS)

# Validity test suite (python3, stdlib only); each run uses a fresh temp DB.
test: $(TARGET)
	python3 tests/runner.py --bin ./$(TARGET)

# Full matrix rebuilt with gcc/clang ASan+UBSan instrumentation.
test-sanitize: $(TARGET)
	python3 tests/runner.py --bin ./$(TARGET) --sanitize

# The untagged vendor/sqlite3.o is listed alongside the glob so the artifact
# left behind by the pre-tagging naming is swept too; the glob itself covers
# every vendor/sqlite3.<compiler>.o.
clean:
	rm -f $(TARGET) /tmp/ipvarchtests/ipvarch-asan /tmp/ipvarchtests/ipvarch-check
	rm -f schema_sql.c schema_sql.c.tmp /tmp/ipvarchtests/sqlite3-check.o
	rm -f vendor/sqlite3.o vendor/sqlite3*.o
