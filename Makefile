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

run: clean ## Clean, build, and run
	@$(MAKE) build
	@./build/yams < /dev/tty

watch: ## Watch source files using fswatch and hot-reload
	@printf "\033[33mWatching for changes in .c and .h files (Ctrl+C to stop)...\033[0m\n"

	@# Initial run
	@$(MAKE) run; \
		fswatch -o . -e "build/.*" -e ".*\.git/.*" -i ".*\.[ch]$$" | while read -r event; do \
		clear; \
		printf "\033[33mChange detected, restarting...\033[0m\n"; \
		killall yams 2>/dev/null || true; \
		$(MAKE) run; \
		done

clean: ## Remove build directory
	@rm -rf build yams

install: build ## Install yams to the system
	@sudo ninja -C build install

uninstall: ## Remove yams from the system
	@sudo rm -f /usr/local/bin/yams 
