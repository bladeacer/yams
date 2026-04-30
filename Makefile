# Wrapper Makefile for yams (Meson/Ninja backend)

.DEFAULT_GOAL := help

CYAN  := $(shell printf "\033[36m")
GREEN := $(shell printf "\033[32m")
RESET := $(shell printf "\033[0m")
BOLD  := $(shell printf "\033[1m")
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

install: ## Build release binary and install to system
	@echo "$(CYAN)Configuring optimized release build...$(RESET)"
	@rm -rf build
	@meson setup build --buildtype=release -Dstrip=true --optimization=s -Dc_args="-DNDEBUG"
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
	@printf "\n$(CYAN)[%s Version]$(RESET)\n" "$(RUN_NAME)"
	@printf "$(GREEN)Monitoring %d samples (%ds intervals)...$(RESET)\n" $(ITERATIONS) $(SLEEP_TIME)
	@total_cpu=0; total_mem=0; count=0; \
	for i in $$(seq 1 $(ITERATIONS)); do \
		setsid $(BIN_PATH) > /dev/null 2>&1 & \
		MYPID=$$!; \
		sleep 2; # Let it settle \
		\
		# Get initial CPU ticks and uptime \
		STAT1=$$(cat /proc/$$MYPID/stat 2>/dev/null); \
		if [ -z "$$STAT1" ]; then continue; fi; \
		UTIME1=$$(echo $$STAT1 | cut -d' ' -f14); \
		STIME1=$$(echo $$STAT1 | cut -d' ' -f15); \
		T1=$$(grep '^btime' /proc/stat | cut -d' ' -f2); \
		UP1=$$(cat /proc/uptime | cut -d' ' -f1); \
		\
		sleep $(SLEEP_TIME); \
		\
		# Get final CPU ticks and uptime \
		STAT2=$$(cat /proc/$$MYPID/stat 2>/dev/null); \
		if [ -z "$$STAT2" ]; then kill -9 -$$MYPID 2>/dev/null; continue; fi; \
		UTIME2=$$(echo $$STAT2 | cut -d' ' -f14); \
		STIME2=$$(echo $$STAT2 | cut -d' ' -f15); \
		UP2=$$(cat /proc/uptime | cut -d' ' -f1); \
		\
		# Memory (RSS) in MB \
		SAMPLE_MEM=$$(echo $$STAT2 | cut -d' ' -f24); \
		PAGE_SIZE=$$(getconf PAGESIZE); \
		MEM_MB=$$(echo "scale=2; ($$SAMPLE_MEM * $$PAGE_SIZE) / 1048576" | bc); \
		\
		# Calculate CPU %: (delta_ticks / delta_time) / clk_tck \
		DIFF=$$(echo "scale=4; $$UP2 - $$UP1" | bc); \
		TICKS=$$(( (UTIME2 + STIME2) - (UTIME1 + STIME1) )); \
		HZ=$$(getconf CLK_TCK); \
		SAMPLE_CPU=$$(awk -v t="$$TICKS" -v h="$$HZ" -v d="$$DIFF" 'BEGIN { printf "%.2f", ((t/h)/d)*100 }'); \
		kill -9 -$$MYPID > /dev/null 2>&1 || true; \
		\
		echo "  Sample $$i: CPU $$SAMPLE_CPU%, MEM $$MEM_MB MB"; \
		total_cpu=$$(echo "$$total_cpu + $$SAMPLE_CPU" | bc); \
		total_mem=$$(echo "$$total_mem + $$MEM_MB" | bc); \
		count=$$((count + 1)); \
	done; \
	if [ $$count -gt 0 ]; then \
		avg_cpu=$$(echo "scale=2; $$total_cpu / $$count" | bc); \
		avg_mem=$$(echo "scale=2; $$total_mem / $$count" | bc); \
		printf "\n$(BOLD)AVERAGE STATS (n=$$count):$(RESET)\n"; \
		printf "  %-15s %s%%\n" "CPU Usage:" "$$avg_cpu"; \
		printf "  %-15s %s MB\n" "Memory:" "$$avg_mem"; \
	fi
