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
	@echo "Targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'

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

release: clean ## Build a stripped release binary without debug/tests
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

install: ## Build release binary (no debug/tests) and install to system
	@echo "$(CYAN)Configuring optimized release build...$(RESET)"
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
