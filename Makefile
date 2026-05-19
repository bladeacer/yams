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

release: clean ## Build a stripped release binary and check size
	@meson setup build \
		--buildtype=release \
		--optimization=3 \
		-Db_lto=true \
		-Dstrip=true \
		-Dc_args="$(FULL_CFLAGS)"
	@ninja -C build
	@echo ""
	@echo "\033[32m--- Binary Info ---\033[0m"
	@ls -lh build/yams
	@echo -n "Total disk usage: "
	@du -h build/yams

install: ## Build release binary and install to system
	@echo "$(CYAN)Configuring optimized release build...$(RESET)"
	@rm -rf build
	@meson setup build \
		--buildtype=release \
		--optimization=3 \
		-Db_lto=true \
		-Dstrip=true \
		-Dc_args="$(FULL_CFLAGS)"
	@ninja -C build
	@ninja -C build
	@echo "$(GREEN)Installing to system...$(RESET)"
	@sudo ninja -C build install
	@# Verify that the installed binary doesn't have debug strings
	@if strings $$(command -v yams) | grep -q "yams.log"; then \
		echo "Warning: Installed binary still contains debug references!"; \
	else \
		echo "Success: Optimized binary installed."; \
	fi

uninstall: ## Remove yams from the system
	@sudo rm -f /usr/local/bin/yams
