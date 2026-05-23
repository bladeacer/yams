# Wrapper Makefile for yams (Meson/Ninja backend)

.DEFAULT_GOAL := help

CYAN  := $(shell printf "\033[36m")
GREEN := $(shell printf "\033[32m")
RESET := $(shell printf "\033[0m")
BOLD  := $(shell printf "\033[1m")

ARCH := $(shell uname -m)
OS   := $(shell uname -s)

# Base Flags
COMMON_FLAGS := -DNDEBUG -O3 -fno-plt

ifeq ($(ARCH), x86_64)
    OPTIM_FLAGS := -march=native -mtune=native
else ifeq ($(ARCH), aarch64)
    OPTIM_FLAGS := -march=native
else ifeq ($(OS), Darwin)
    OPTIM_FLAGS := -mcpu=apple-m1
endif

FULL_CFLAGS := $(COMMON_FLAGS) $(OPTIM_FLAGS)

help: ## Show this help message
	@echo "Usage: make [target]"
	@echo ""
	@echo "Build targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Binary sizes (x86_64 Linux):"
	@echo "  make run           ~ 300 kB (debug, dynamically linked, unstripped)"
	@echo "  make release       ~ 1.5 MB (release, statically linked, stripped)"
	@echo "  make release-dynamic ~ 250 kB (release, dynamically linked, stripped)"
	@echo "  make install       ~ 1.5 MB (release, statically linked, installed)"
	@echo "  make install-dynamic ~ 250 kB (release, dynamically linked, installed)"
	@echo ""
	@echo "Static builds require static library versions of all dependencies"
	@echo "(libchafa, ffmpeg, glib, ncursesw, panelw, cmocka)."

setup: ## Initialize meson build directory (Debug mode)
	@meson setup build --buildtype=debugoptimized --optimization=3

build: ## Compile yams using Ninja
	@if [ ! -d "build" ]; then meson setup build; fi
	@ninja -C build

run: ## Run compiled binary (Clean & Debug)
	$(MAKE) clean
	$(MAKE) build
	./build/yams

clean: ## Remove build directory
	@rm -rf build

test: ## Build and run unit tests
	@echo "$(CYAN)Configuring debug build with tests...$(RESET)"
	@if [ -d "build" ]; then \
		meson setup build --reconfigure --buildtype=debug -Dlogging=true -Dtests=true 2>/dev/null; \
	else \
		meson setup build --buildtype=debug -Dlogging=true -Dtests=true; \
	fi
	@ninja -C build
	@echo "$(GREEN)Running unit tests...$(RESET)"
	@for t in build/tests/test_metadata build/tests/test_utils build/tests/test_config build/tests/test_art; do \
		if [ -x "$$t" ]; then \
			"$$t" 2>&1 | grep -E '^\[.*(RUN|OK|FAILED|========)' | sed 's/^\[[[:space:]]*/[/'; \
		fi; \
	done

release: clean ## Build a stripped static release binary without debug/tests
	@meson setup build \
		--buildtype=release \
		--optimization=3 \
		-Db_lto=true \
		-Dstrip=true \
		-Dtests=false \
		-Dlogging=false \
		-Dc_args="$(FULL_CFLAGS) -static"
	@ninja -C build
	@echo ""
	@echo "\033[32m--- Binary Info ---\033[0m"
	@ls -lh build/yams

release-dynamic: clean ## Build a stripped dynamically-linked release binary
	@meson setup build \
		--buildtype=release \
		--optimization=3 \
		-Db_lto=true \
		-Dstrip=true \
		-Dtests=false \
		-Dlogging=false \
		-Dc_args="$(FULL_CFLAGS)"
	@ninja -C build
	@echo ""
	@echo "\033[32m--- Binary Info ---\033[0m"
	@ls -lh build/yams

install: ## Build static release binary and install to system
	@echo "$(CYAN)Configuring static release build...$(RESET)"
	@rm -rf build
	@meson setup build \
		--buildtype=release \
		--optimization=3 \
		-Db_lto=true \
		-Dstrip=true \
		-Dtests=false \
		-Dlogging=false \
		-Dc_args="$(FULL_CFLAGS) -static"
	@ninja -C build
	@echo "$(GREEN)Installing to system...$(RESET)"
	@sudo ninja -C build install

install-dynamic: ## Build dynamically-linked release binary and install to system
	@echo "$(CYAN)Configuring dynamic release build...$(RESET)"
	@rm -rf build
	@meson setup build \
		--buildtype=release \
		--optimization=3 \
		-Db_lto=true \
		-Dstrip=true \
		-Dtests=false \
		-Dlogging=false \
		-Dc_args="$(FULL_CFLAGS)"
	@ninja -C build
	@echo "$(GREEN)Installing to system...$(RESET)"
	@sudo ninja -C build install

uninstall: ## Remove yams from the system
	@sudo rm -f /usr/local/bin/yams
