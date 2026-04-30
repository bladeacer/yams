# Wrapper Makefile for yams (Meson/Ninja backend)

.DEFAULT_GOAL := help

CYAN  := \033[36m
GREEN := \033[32m
RESET := \033[0m
ITERATIONS := 5
SLEEP_TIME := 5

help: ## Show this help message
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'

setup: ## Initialize meson build directory (Debug mode)
	@meson setup build --buildtype=debugoptimized --optimization=s

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
	@meson setup build --buildtype=release -Dstrip=true --optimization=s -Dc_args="-DNDEBUG"
	@ninja -C build
	@echo ""
	@echo "\033[32m--- Binary Info ---\033[0m"
	@ls -lh build/yams
	@echo -n "Total disk usage: "
	@du -h build/yams

install: release ## Build release binary and install to system
	@sudo ninja -C build install

uninstall: ## Remove yams from the system
	@sudo rm -f /usr/local/bin/yams

logs: ## Check logs
	@tail -f yams.log

bench: ## Benchmark binary size, startup, memory, and CPU usage
	@echo "$(CYAN)--- Starting Benchmark ---$(RESET)"
	@pkill -9 yams || true
	@pkill -9 playerctl || true
	@$(MAKE) bench-build BUILD_NAME="Debug" BUILD_CMD="setup build --buildtype=debug" BIN_PATH="./build/yams"
	@$(MAKE) bench-build BUILD_NAME="Release" BUILD_CMD="setup build --buildtype=release -Dstrip=true --optimization=s" BIN_PATH="./build/yams"
	@if command -v yams >/dev/null; then \
		$(MAKE) bench-run RUN_NAME="Installed" BIN_PATH="$$(command -v yams)"; \
	else \
		echo "Skipping Installed check (yams not in PATH)"; \
	fi

bench-build:
	@rm -rf build
	@meson $(BUILD_CMD) > /dev/null
	@ninja -C build > /dev/null
	@$(MAKE) bench-run RUN_NAME="$(BUILD_NAME)" BIN_PATH="$(BIN_PATH)"

bench-run:
	@echo "$(GREEN)Running $(ITERATIONS) samples of $(SLEEP_TIME)s each...$(RESET)"
	@total_cpu=0; total_mem=0; count=0; \
	for i in $$(seq 1 $(ITERATIONS)); do \
		setsid $(BIN_PATH) > /dev/null 2>&1 & \
		MYPID=$$!; \
		sleep 1; # Warmup: let it finish forking/initializing \
		\
		# Take a 5-second sample using top's delta \
		SAMPLE_CPU=$$(top -b -n 2 -d $(SLEEP_TIME) -p $$MYPID | grep $$MYPID | tail -n 1 | awk '{print $$9}'); \
		SAMPLE_MEM=$$(ps -p $$MYPID -o rss= | awk '{print $$1/1024}'); \
		\
		kill -9 -$$MYPID > /dev/null 2>&1 || true; \
		\
		if [ ! -z "$$SAMPLE_CPU" ]; then \
			echo "  Sample $$i: CPU $$SAMPLE_CPU%, MEM $$SAMPLE_MEM MB"; \
			total_cpu=$$(echo "$$total_cpu + $$SAMPLE_CPU" | bc); \
			total_mem=$$(echo "$$total_mem + $$SAMPLE_MEM" | bc); \
			count=$$((count + 1)); \
		fi; \
	done; \
	avg_cpu=$$(echo "scale=2; $$total_cpu / $$count" | bc); \
	avg_mem=$$(echo "scale=2; $$total_mem / $$count" | bc); \
	printf "\n$(BOLD)AVERAGE STATS (n=$$count):$(RESET)\n"; \
	printf "  %-15s %s%%\n" "CPU Usage:" "$$avg_cpu"; \
	printf "  %-15s %s MB\n" "Memory:" "$$avg_mem"
