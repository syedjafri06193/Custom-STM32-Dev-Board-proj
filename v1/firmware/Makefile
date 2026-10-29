# Host build of the portable core plus its unit tests.
#
# The core is deliberately free of vendor headers: every file under core/ is
# plain C11 that compiles for the host and for the target from the same source.
# That is what makes the PD policy, the CAN timing solver and the analog
# scaling testable at all -- none of them need an STM32 to be exercised.
#
#   make            build and run every suite
#   make test       the same
#   make sanitize   the same, under AddressSanitizer + UBSan
#   make clean

CC      ?= cc
EXTRA_CFLAGS ?=
CFLAGS  := -std=c11 -Wall -Wextra -Werror -O1 -g -Icore/inc -Itests $(EXTRA_CFLAGS)
BUILD   ?= build

CORE := \
	core/src/pd_policy.c \
	core/src/can_timing.c \
	core/src/ads1220.c \
	core/src/afe.c \
	core/src/vbus.c

SUITES := pd_policy can_timing ads1220 afe_vbus

BINS := $(addprefix $(BUILD)/test_,$(SUITES))

.PHONY: all test sanitize clean

all: test

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/test_%: tests/test_%.c $(CORE) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(CORE)

test: $(BINS)
	@fail=0; \
	for t in $(BINS); do \
		./$$t || fail=1; \
		echo; \
	done; \
	if [ $$fail -ne 0 ]; then echo "FAILED"; exit 1; fi; \
	echo "all suites passed"

# Undefined behaviour in fixed-point scaling code tends to "work" on the host
# and misbehave on the target; the sanitizers make it fail here instead.
sanitize:
	@$(MAKE) --no-print-directory test BUILD=build-san \
		EXTRA_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"

clean:
	@rm -rf $(BUILD) build-san
