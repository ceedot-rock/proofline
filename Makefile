# ProofLine Makefile
#
# Dependency: PmCp (https://github.com/ceedot-rock/pmcp, Apache-2.0).
# Point PMCP_DIR at a PmCp checkout (default: ../pmcp, i.e. cloned next
# to this repo). The PmCp sources are compiled in; nothing is vendored.
#
# Crypto: libsodium. If pkg-config finds libsodium, the system headers are
# used; otherwise the build falls back to the vendored public headers in
# third_party/libsodium/include and links the system libsodium library.

PMCP_DIR ?= ../pmcp
PMCP_SRC  = $(PMCP_DIR)/src

CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

# --- libsodium detection ---
ifneq ($(shell pkg-config --exists libsodium 2>/dev/null && echo yes),yes)
  SODIUM_CFLAGS := -Ithird_party/libsodium/include
  # No -dev symlink on this box; link the versioned soname directly.
  SODIUM_SONAME := $(shell ldconfig -p 2>/dev/null | grep -o 'libsodium\.so\.[0-9][0-9]*' | head -1)
  SODIUM_LIBS := $(if $(SODIUM_SONAME),-l:$(SODIUM_SONAME),-lsodium)
  $(info libsodium not in pkg-config; using vendored headers + $(SODIUM_LIBS))
else
  SODIUM_CFLAGS := $(shell pkg-config --cflags libsodium)
  SODIUM_LIBS := $(shell pkg-config --libs libsodium)
endif

INCLUDES = -Isrc -I$(PMCP_SRC) $(SODIUM_CFLAGS)
LIBS     = $(SODIUM_LIBS) -lm

PROOFLINE_SRCS = src/proofline.c src/journal.c src/classifier.c src/sign_sodium.c
PMCP_SRCS      = $(PMCP_SRC)/region.c $(PMCP_SRC)/journal.c \
                 $(PMCP_SRC)/isa.c $(PMCP_SRC)/sha256.c

OBJS = $(PROOFLINE_SRCS:.c=.o) $(PMCP_SRCS:.c=.o)

all: proofline

proofline: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# PmCp objects live in the PmCp tree; pattern rule covers them via INCLUDES.
$(PMCP_SRC)/%.o: $(PMCP_SRC)/%.c
	$(CC) $(CFLAGS) -I$(PMCP_SRC) -c $< -o $@

test: all
	./tests/test.sh

clean:
	rm -f proofline $(PROOFLINE_SRCS:.c=.o)
	rm -f $(PMCP_SRC)/region.o $(PMCP_SRC)/journal.o $(PMCP_SRC)/isa.o $(PMCP_SRC)/sha256.o

.PHONY: all test clean
