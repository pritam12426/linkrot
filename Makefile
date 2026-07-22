UNAME_S := $(shell uname -s)

PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

STRIP ?= strip
PKG_CONFIG ?= pkg-config
INSTALL ?= install

BUILD = build
BIN   = linkrot

HEADERS   = $(wildcard src/*.h)
SRC       = $(wildcard src/*.c)

CFLAGS += -Isrc -std=c17 -DCOMPILED_TIME_PREFIX='"$(PREFIX)"'

CFLAGS +=  -Wshadow -Wconversion \
           -Wall -Wextra -Wpedantic \
           -Wno-missing-field-initializers \
           -Wstrict-prototypes -Wmissing-prototypes

# Common flags
CFLAGS += -Isrc -std=c17
LDLIBS += -lpthread

# Build options (set via command line, e.g. `make O_DEBUG=1`)
O_DEBUG := 0                     ## Enable debug build (ASan, UBSan, -g3)
O_LOG_SHOW_SOURCE_LOCATION := 0  ## Prepend [file:line:func] to log output
O_LOG_SHOW_TIME_STAMP := 0       ## Prepend [HH:MM:SS.ffffff] to log output

# Auto-enable flags for debug builds
ifneq ($(filter debug,$(MAKECMDGOALS)),)
	O_DEBUG := 1
	O_LOG_SHOW_SOURCE_LOCATION := 1
	O_LOG_SHOW_TIME_STAMP := 1
endif

ifeq ($(strip $(O_DEBUG)),1)
	CFLAGS += -g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION

	LDFLAGS += -fsanitize=address -fsanitize=undefined
	CFLAGS += -fstack-usage \
	          -fsanitize=address \
	          -fsanitize=undefined

    ifneq (,$(findstring clang,$(CC)))
		CFLAGS += -ffreestanding
    endif
else
	CFLAGS += -O3
endif

# Convert O_ variables to -D flags
ifeq ($(strip $(O_LOG_SHOW_SOURCE_LOCATION)),1)
	CFLAGS += -DLOG_SHOW_SOURCE_LOCATION
endif

ifeq ($(strip $(O_LOG_SHOW_TIME_STAMP)),1)
	CFLAGS += -DLOG_SHOW_TIME_STAMP
endif

# Platform-specific settings
ifeq ($(UNAME_S),Darwin)
	# macOS: need argp from Homebrew (brew install argp-standalone)
	LDLIBS += -largp
else
	# Linux: _GNU_SOURCE for strptime, etc.
	CFLAGS += -D_GNU_SOURCE
endif


# ── Third-party libs ─────────────────────────────────────────────────────────────────
# ifeq ($(shell $(PKG_CONFIG) --exists sdl3 && echo 1),1)
# 	CFLAGS  += $(shell $(PKG_CONFIG) --cflags sdl3)
# 	LDFLAGS += $(shell $(PKG_CONFIG) --libs   sdl3)
# else
#     $(error "sdl3 not found. Install it via your package manager.")
# endif

OUT += $(SRC:%.c=$(BUILD)/%.o)

all: $(BIN)

help:  ## Show this help
	@echo "Variable:"
	@awk 'BEGIN {FS="  ## "} \
		/^O_[a-zA-Z_]+[[:space:]]*:=/ { \
		split($$1, a, /[[:space:]]*:=/); \
		printf "  \033[36m%-30s\033[0m %s\n", a[1], $$2; \
	}' $(MAKEFILE_LIST)

	@echo
	@echo "Targets:"
	@grep -hE '^[a-zA-Z_-]+:.*  ## ' $(MAKEFILE_LIST) | \
	awk 'BEGIN {FS="  ## "}; {printf "  \033[33m%-15s\033[0m %s\n", $$1, $$2}'

$(BUILD): ## Create build directories automatically
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(SRC) $(OUT)  ## Build the linkrot binary
	$(CC) $(LDFLAGS) -o $@ $(OUT) $(LDLIBS)

debug: $(BIN)  ## Build the debug binary run `make debug -B O_DEBUG=1`

install: all  ## Install the linkrot binary
	$(INSTALL) -m 0755 -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin

clean:  ## Clean up linkrot artifacts
	$(RM) -f $(OUT) $(BIN)

uninstall:  ## Uninstall the linkrot binary
	$(RM) $(DESTDIR)$(PREFIX)/bin/$(BIN)

strip: $(BIN)  ## Strip the linkrot binary
	$(STRIP) $^

.PHONY: all install uninstall strip clean debug
