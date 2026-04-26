# Wrapper Makefile for yams (Meson/Ninja backend)

.DEFAULT_GOAL := help

help: ## Show this help message
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'

setup: ## Initialize meson build directory
	@meson setup build --buildtype=debugoptimized --optimization=s

build: ## Compile yams using Ninja
	@if [ ! -d "build" ]; then meson setup build; fi
	@ninja -C build

run: ## Run compiled binary
	$(MAKE) clean
	$(MAKE) build
	./build/yams

clean: ## Remove build directory
	@rm -rf build yams

install: build ## Install yams to the system
	@sudo ninja -C build install

uninstall: ## Remove yams from the system
	@sudo rm -f /usr/local/bin/yams
