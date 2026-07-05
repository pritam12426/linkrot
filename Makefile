UNAME_S := $(shell uname -s)

PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

STRIP ?= strip
PKG_CONFIG ?= pkg-config
INSTALL ?= install

CFLAGS_OPTIMIZATION ?= -O3

BUILD = build
BIN   = linkrot

HEADERS   = $(wildcard src/*.h)
SRC       = $(wildcard src/*.c)
OUT       = $(SRC:%.c=$(BUILD)/%.o)


CFLAGS += -Isrc -std=c17 -DCOMPILED_TIME_PREFIX='"$(PREFIX)"'

CFLAGS +=  -Wall -Wextra -Wpedantic \
           -Wstrict-prototypes -Wmissing-prototypes \
           -Wshadow -Wconversion \
           -Wno-missing-field-initializers


# convert targets to flags for backwards compatibility
O_DEBUG := 0  # debug binary
O_LOG_TIME_STAMP := 0  # debug binary

ifneq ($(filter log_time_stamp,$(MAKECMDGOALS)),)
	O_LOG_TIME_STAMP := 1
endif
ifneq ($(filter debug,$(MAKECMDGOALS)),)
	O_DEBUG := 1
endif

ifeq ($(strip $(O_DEBUG)),1)
	CFLAGS += -g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION
    ifneq (,$(findstring clang,$(CC)))
		CFLAGS += -ffreestanding
    endif
else
	CFLAGS += $(CFLAGS_OPTIMIZATION)
endif
ifeq ($(strip $(O_LOG_TIME_STAMP)),1)
	CFLAGS += -DLOG_SHOW_TIME_STAMP
endif

# Check if the OS is macOS
ifeq ($(UNAME_S),Darwin)
    LDLIBS += -largp
else # Else every thing is linux
    CFLAGS += -D_GNU_SOURCE
endif


# ── Third-party libs ─────────────────────────────────────────────────────────────────
# ifeq ($(shell $(PKG_CONFIG) --exists sdl3 && echo 1),1)
# 	CFLAGS  += $(shell $(PKG_CONFIG) --cflags sdl3)
# 	LDFLAGS += $(shell $(PKG_CONFIG) --libs   sdl3)
# else
#     $(error "sdl3 not found. Install it via your package manager.")
# endif

all: $(BIN)

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
	awk 'BEGIN {FS = ":.*?## "}; {printf "\033[33m%-20s\033[0m %s\n", $$1, $$2}'

$(BUILD): ## Create build directories automatically
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c $(SHARED_HDR) $(DAEMON_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(SRC) $(OUT) $(HEADERS) ## Build the linkrot binary
	$(CC) $(LDFLAGS) -o $@ $(OUT) $(LDLIBS)

debug: $(BIN) ## Build the debug binary run `make debug -B O_DEBUG=1`

install: all ## Install the linkrot binary
	$(INSTALL) -m 0755 -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin

clean: ## Clean up linkrot artifacts
	$(RM) -f $(OUT) $(BIN)

uninstall: ## Uninstall the linkrot binary
	$(RM) $(DESTDIR)$(PREFIX)/bin/$(BIN)

strip: $(BIN) ## Strip the linkrot binary
	$(STRIP) $^

.PHONY: all install uninstall strip clean debug
