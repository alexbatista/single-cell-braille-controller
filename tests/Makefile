# Host-side unit tests. These build with the system gcc, not the ARM
# toolchain: every module under test is either pure logic or reached through a
# function-pointer seam, so no HAL is involved. Suites #include the module .c
# directly to reach its static functions.

CC      ?= gcc
# App/Src is on the include path because every suite #includes its module's
# .c directly, which is how the tests reach static functions.
CFLAGS  := -std=c11 -Wall -Wextra -Werror -g -O0 \
           -I../App/Inc -I../App/Src -Istubs -I.
SUITES  := test_braille_dots

BINDIR  := build
BINS    := $(addprefix $(BINDIR)/,$(SUITES))

.PHONY: all test clean
all: test

test: $(BINS)
	@echo "== running host suites =="
	@fail=0; for b in $(BINS); do ./$$b || fail=1; done; exit $$fail

$(BINDIR)/%: %.c $(wildcard ../App/Src/*.c) $(wildcard ../App/Inc/*.h) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BINDIR):
	@mkdir -p $(BINDIR)

clean:
	@rm -rf $(BINDIR)
