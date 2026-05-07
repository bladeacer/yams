# Wrapper Makefile for yams (Meson/Ninja backend)

.DEFAULT_GOAL := help

CYAN  := $(shell printf "\033[36m")
GREEN := $(shell printf "\033[32m")
RESET := $(shell printf "\033[0m")
BOLD  := $(shell printf "\033[1m")

ITERATIONS := 5
SLEEP_TIME := 5
PERF_DURATION := 30
PERF_SAMPLES := 6
SAMPLE_TIME := 5

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
	@meson setup build --buildtype=release -Dstrip=true --optimization=3 -Dc_args="-DNDEBUG"
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

bench-build:
	@rm -rf build
	@meson $(BUILD_CMD) > /dev/null
	@ninja -C build > /dev/null
	@$(MAKE) bench-run RUN_NAME="$(BUILD_NAME)" BIN_PATH="$(BIN_PATH)"

bench: ## Full Benchmark: Size, Startup Speed (Hyperfine), and Resource Usage
	@echo "$(CYAN)--- Starting Full Benchmark ---$(RESET)"
	@pkill -9 yams || true
	@pkill -9 playerctl || true
	
	@echo "$(CYAN)Step 1: Preparing Binaries...$(RESET)"
	@# Build Debug
	@rm -rf build-debug
	@meson setup build-debug --buildtype=debug > /dev/null
	@ninja -C build-debug > /dev/null
	@# Build Release
	@rm -rf build-release
	@meson setup build-release --buildtype=release -Dstrip=true --optimization=s -Dc_args="-DNDEBUG" > /dev/null
	@ninja -C build-release > /dev/null

	@printf "\n$(CYAN)Step 2: Binary Footprint (Disk Usage)$(RESET)\n"
	@printf "  %-15s %s\n" "Debug Build:" "$$(du -h ./build-debug/yams | cut -f1)"
	@printf "  %-15s %s\n" "Release Build:" "$$(du -h ./build-release/yams | cut -f1)"
	@if command -v strip >/dev/null; then \
		cp ./build-release/yams ./yams-stripped; \
		strip ./yams-stripped; \
		printf "  %-15s %s\n" "Fully Stripped:" "$$(du -h ./yams-stripped | cut -f1)"; \
		rm ./yams-stripped; \
	fi

	@printf "\n$(CYAN)Step 3: Cold Start Speed (Hyperfine)$(RESET)\n"
	@if command -v hyperfine >/dev/null; then \
		INSTALLED_CMD=""; \
		if command -v yams >/dev/null; then INSTALLED_CMD="yams"; fi; \
			YAMS_EXIT_IMMEDIATELY=1 hyperfine -N --warmup 20 --runs 200 \
			'./build-debug/yams' \
			'./build-release/yams' \
			$$INSTALLED_CMD; \
	else \
		echo "Skipping: hyperfine not installed."; \
	fi

	@printf "\n$(CYAN)Step 4: Resource Efficiency (Idle Stats)$(RESET)\n"
	@$(MAKE) bench-run RUN_NAME="Debug"   BIN_PATH="./build-debug/yams"
	@$(MAKE) bench-run RUN_NAME="Release" BIN_PATH="./build-release/yams"
	@if command -v yams >/dev/null; then \
		$(MAKE) bench-run RUN_NAME="Installed" BIN_PATH="$$(command -v yams)"; \
	fi

perf: build-release ## Standalone Deep Perf Analysis (30s)
	@$(MAKE) perf-stat BIN_PATH="./build-release/yams" DURATION=$(PERF_DURATION)

perf-stat:
	@if command -v perf >/dev/null; then \
		echo "$(CYAN)--- Starting Perf Analysis ($(PERF_SAMPLES) samples) ---$(RESET)"; \
		pkill -9 yams || true; \
		TERM=xterm setsid $(BIN_PATH) > /dev/null 2>&1 & \
		YPID=$$!; \
		sleep 2; \
		if ps -p $$YPID > /dev/null; then \
			echo "$(GREEN)Tracing PID: $$YPID...$(RESET)"; \
			if [ "$(STRACE)" = "1" ]; then \
				echo "$(CYAN)Aggregating Syscalls in background...$(RESET)"; \
				sudo strace -p $$YPID -o strace_raw.log & STRACE_PID=$$!; \
			fi; \
			> perf_results.log; \
			for i in $$(seq 1 $(PERF_SAMPLES)); do \
				echo "  Sample $$i/$(PERF_SAMPLES)..."; \
				perf stat -p $$YPID -- sleep $(SAMPLE_TIME) 2>&1 | tee -a perf_results.log | grep -E 'insn_per_cycle|tma_frontend_bound|tma_backend_bound|tma_retiring' || true; \
			done; \
			if [ "$(STRACE)" = "1" ]; then \
				sudo kill -INT $$STRACE_PID; \
				echo "\n$(BOLD)--- TOP SYSCALLS ---$(RESET)"; \
				awk '{print $$1}' strace_raw.log | cut -d'(' -f1 | sort | uniq -c | sort -nr | head -n 10; \
				rm strace_raw.log; \
			fi; \
			kill -9 -$$YPID > /dev/null 2>&1 || true; \
			printf "\n$(BOLD)--- OVERALL PERF AVERAGES ---$(RESET)\n"; \
			awk '!/nan|TopdownL1/ { \
				match($$0, /[0-9]+\.[0-9]+/); \
				val = substr($$0, RSTART, RLENGTH); \
				if (val == "") next; \
				if ($$0 ~ /insn_per_cycle/)    { ipc += val; ipcc++ } \
				if ($$0 ~ /tma_frontend_bound/) { fe += val; fec++ } \
				if ($$0 ~ /tma_backend_bound/)  { be += val; bec++ } \
				if ($$0 ~ /tma_retiring/)        { rt += val; rtc++ } \
			} \
			END { \
				if (ipcc>0) printf "  %-20s %.2f\n",  "IPC:", ipc/ipcc; \
				if (fec>0)  printf "  %-20s %.1f%%\n", "Frontend Bound:", fe/fec; \
				if (bec>0)  printf "  %-20s %.1f%%\n", "Backend Bound:", be/bec; \
				if (rtc>0)  printf "  %-20s %.1f%%\n", "Retiring:", rt/rtc; \
			}' perf_results.log; \
			rm perf_results.log; \
		else \
			echo "Error: Yams failed to stay alive."; exit 1; \
		fi; \
	else \
		echo "Skipping: perf not installed."; \
	fi

trace: build-release ## Run yams and attach strace to see syscalls in real-time
	@echo "$(CYAN)--- Starting Syscall Trace ---$(RESET)"
	@pkill -9 yams || true
	@TERM=xterm setsid ./build-release/yams > /dev/null 2>&1 & \
	YPID=$$!; \
	sleep 1; \
	if ps -p $$YPID > /dev/null; then \
		echo "$(GREEN)Attaching strace to PID: $$YPID (Press Ctrl+C to stop)...$(RESET)"; \
		sudo strace -p $$YPID -c -S name; \
		kill -9 -$$YPID > /dev/null 2>&1 || true; \
	else \
		echo "Error: Yams failed to start."; \
	fi

perf-trace: build-release ## Run perf and strace simultaneously
	@$(MAKE) perf-stat BIN_PATH="./build-release/yams" DURATION=$(PERF_DURATION) STRACE=1

bench-run:
	@printf "\n$(CYAN)[%s Version]$(RESET)\n" "$(RUN_NAME)"
	@printf "$(GREEN)Monitoring %d samples (%ds intervals)...$(RESET)\n" $(ITERATIONS) $(SLEEP_TIME)
	@total_cpu=0; total_mem=0; count=0; \
	for i in $$(seq 1 $(ITERATIONS)); do \
		setsid $(BIN_PATH) > /dev/null 2>&1 & \
		MYPID=$$!; \
		sleep 2; \
		STAT1=$$(cat /proc/$$MYPID/stat 2>/dev/null); \
		if [ -z "$$STAT1" ]; then continue; fi; \
		UTIME1=$$(echo $$STAT1 | cut -d' ' -f14); \
		STIME1=$$(echo $$STAT1 | cut -d' ' -f15); \
		UP1=$$(cat /proc/uptime | cut -d' ' -f1); \
		sleep $(SLEEP_TIME); \
		STAT2=$$(cat /proc/$$MYPID/stat 2>/dev/null); \
		if [ -z "$$STAT2" ]; then kill -9 -$$MYPID 2>/dev/null; continue; fi; \
		UTIME2=$$(echo $$STAT2 | cut -d' ' -f14); \
		STIME2=$$(echo $$STAT2 | cut -d' ' -f15); \
		UP2=$$(cat /proc/uptime | cut -d' ' -f1); \
		SAMPLE_MEM=$$(echo $$STAT2 | cut -d' ' -f24); \
		PAGE_SIZE=$$(getconf PAGESIZE); \
		MEM_MB=$$(echo "scale=2; ($$SAMPLE_MEM * $$PAGE_SIZE) / 1048576" | bc); \
		DIFF=$$(echo "scale=4; $$UP2 - $$UP1" | bc); \
		TICKS=$$(( (UTIME2 + STIME2) - (UTIME1 + STIME1) )); \
		HZ=$$(getconf CLK_TCK); \
		SAMPLE_CPU=$$(awk -v t="$$TICKS" -v h="$$HZ" -v d="$$DIFF" 'BEGIN { printf "%.2f", ((t/h)/d)*100 }'); \
		kill -9 -$$MYPID > /dev/null 2>&1 || true; \
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
