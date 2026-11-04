# IPVarch - Internet Protocol Version Architect
# Primary compiler is tcc (Tiny C Compiler); falls back to gcc automatically.
# Override explicitly if desired: make CC=clang

CC := $(shell command -v tcc 2>/dev/null || command -v gcc 2>/dev/null || echo cc)
CFLAGS := -Wall
TARGET := ipvarch

.PHONY: all check test test-sanitize clean

all: $(TARGET)

$(TARGET): ipvarch.c
	$(CC) $(CFLAGS) -o $@ ipvarch.c

# Strict C89 conformance + glibc fortify build (needs gcc; tcc has no such mode)
check:
	@if command -v gcc >/dev/null 2>&1; then \
		gcc -std=c89 -pedantic -Wall -Wextra -D_FORTIFY_SOURCE=2 -O2 \
			-o /tmp/opencode/ipvarch-check ipvarch.c && \
			echo "check: strict C89 build OK (/tmp/opencode/ipvarch-check)"; \
	else \
		echo "check: gcc not available, skipping (tcc has no strict mode)"; fi

# Validity test suite (python3, stdlib only)
test: $(TARGET)
	python3 tests/runner.py --bin ./$(TARGET)

# Full matrix rebuilt with gcc/clang ASan+UBSan instrumentation
test-sanitize: $(TARGET)
	python3 tests/runner.py --bin ./$(TARGET) --sanitize

clean:
	rm -f $(TARGET) /tmp/opencode/ipvarch-asan
