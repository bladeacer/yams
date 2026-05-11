# Wrapper Makefile for yams (Meson/Ninja backend)

.DEFAULT_GOAL := help

CYAN  := $(shell printf "\033[36m")
GREEN := $(shell printf "\033[32m")
RESET := $(shell printf "\033[0m")
BOLD  := $(shell printf "\033[1m")

ITERATIONS := 30
SLEEP_TIME := 1
PERF_DURATION := 10
PERF_SAMPLES := 50
SAMPLE_TIME  := 2

CPU_CORE := $(shell lscpu -p=CPU,CORE | grep -v '^#' | head -n 1 | cut -d',' -f1)

ARCH := $(shell uname -m)
OS   := $(shell uname -s)

# Base Flags
COMMON_FLAGS := -DNDEBUG -O3 -fno-plt

VENDOR = $(shell grep -m1 "vendor_id" /proc/cpuinfo | awk '{print $$3}')

ifeq ($(ARCH), x86_64)
    # Intel/AMD Specific
    OPTIM_FLAGS := -march=native -mtune=native
else ifeq ($(ARCH), aarch64)
    # ARM Linux (Raspberry Pi, etc)
    OPTIM_FLAGS := -march=native
else ifeq ($(OS), Darwin)
    # Apple Silicon (M1/M2/M3)
    OPTIM_FLAGS := -mcpu=apple-m1 
endif

FULL_CFLAGS := $(COMMON_FLAGS) $(OPTIM_FLAGS)

kill_yams = pkill -9 yams || true; pkill -9 playerctl || true

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

diff: ## Writes output of git diff HEAD~0 to diff.txt
	git diff HEAD~0 > diff.txt

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

debug-flags: ## Debug compiler flags set based on CPU arch
	@echo "Detected Arch: $(ARCH)"
	@echo "Detected OS:   $(OS)"
	@echo "Flags sent:    $(FULL_CFLAGS)"

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
	@meson setup build-release --buildtype=release -Dstrip=true --optimization=s -Dc_args="$(FULL_CFLAGS)" > /dev/null
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

perf: release ## Standalone Deep Perf Analysis (Pinned to Core)
	@$(MAKE) perf-stat BIN_PATH="./build-release/yams"

perf-stat:
	@if ! command -v perf >/dev/null; then echo "Error: perf not found."; exit 1; fi
	@echo "$(CYAN)--- Deep Analysis (Core: $(CPU_CORE)) ---$(RESET)"
	@$(kill_yams)
	@TERM=xterm setsid taskset -c $(CPU_CORE) $(BIN_PATH) > /dev/null 2>&1 & \
	YPID=$$!; \
	sleep 2; \
	if ! ps -p $$YPID > /dev/null; then echo "Error: Binary failed to start."; exit 1; fi; \
	rm -f perf_results.log; \
	echo "Warming up CPU..."; \
	sudo perf stat -p $$YPID -- sleep 3 > /dev/null 2>&1; \
	for i in $$(seq 1 $(PERF_SAMPLES)); do \
		echo "  Sample $$i/$(PERF_SAMPLES)..."; \
		sudo perf stat -p $$YPID -ddd -- sleep $(SAMPLE_TIME) >> perf_results.log 2>&1; \
	done; \
	kill -9 -$$YPID > /dev/null 2>&1 || true; \
	$(kill_yams); \
	printf "\n$(BOLD)--- OVERALL PERF AVERAGES ---$(RESET)\n"; \
	awk '{ \
		gsub(/,/, ""); \
		if ($$0 ~ /insn_per_cycle/) { for(i=1; i<=NF; i++) { if ($$i ~ /^[0-9]+\.[0-9]+$$/) { ipc += $$i; ipcc++; break } } } \
		if ($$0 ~ /tma_/) { \
			val = 0; for(i=1; i<=NF; i++) { if ($$i ~ /^[0-9]+\.[0-9]+$$/) { val = $$i; break } } \
			if ($$0 ~ /tma_frontend_bound/) fe += val; \
			if ($$0 ~ /tma_backend_bound/)  be += val; \
			if ($$0 ~ /tma_bad_speculation/) bs += val; \
			if ($$0 ~ /tma_retiring/)        rt += val; \
			fec++; \
		} \
		if ($$0 ~ /L1-dcache-loads/) l1_ld += $$1; \
		if ($$0 ~ /L1-dcache-load-misses/) l1_m += $$1; \
		if ($$0 ~ /branch-instructions/) br_ins += $$1; \
		if ($$0 ~ /branch-misses/) br_m += $$1; \
		if ($$0 ~ /llc_miss_rate/) { for(i=1; i<=NF; i++) { if ($$i ~ /^[0-9]+\.[0-9]+$$/) { llc += $$i; llcc++; break } } } \
	} \
	END { \
		if (fec > 0 || ipcc > 0) { \
			if (ipcc > 0) printf "  %-20s %.2f\n", "Avg IPC:", ipc/ipcc; \
			if (l1_ld > 0) printf "  %-20s %.2f%%\n", "L1-D Miss Rate:", (l1_m/l1_ld)*100; \
			if (llcc > 0)  printf "  %-20s %.2f%%\n", "Avg LLC Miss Rate:", llc/llcc; \
			if (br_ins > 0) printf "  %-20s %.2f%%\n", "Branch Miss Rate:", (br_m/br_ins)*100; \
			print "---------------------------------"; \
			if (fec > 0) { \
				printf "  %-20s %.1f%%\n", "Frontend Bound:", fe/(fec/4); \
				printf "  %-20s %.1f%%\n", "Backend Bound:", be/(fec/4); \
				printf "  %-20s %.1f%%\n", "Bad Speculation:", bs/(fec/4); \
				printf "  %-20s %.1f%%\n", "Retiring:", rt/(fec/4); \
			} \
		} \
	}' perf_results.log; \
	rm -f perf_results.log

bench-run:
	@printf "\n$(CYAN)[%s Version]$(RESET)\n" "$(RUN_NAME)"
	@printf "$(GREEN)Pinned to Core $(CPU_CORE) | %d samples (%ds intervals)$(RESET)\n" $(ITERATIONS) $(SLEEP_TIME)
	@total_cpu=0; total_mem=0; count=0; \
	setsid taskset -c $(CPU_CORE) $(BIN_PATH) > /dev/null 2>&1 & \
	MYPID=$$!; \
	sleep 2; \
	for i in $$(seq 1 $(ITERATIONS)); do \
		$(kill_yams) ; \
		STAT1=$$(cat /proc/$$MYPID/stat 2>/dev/null); \
		if [ -z "$$STAT1" ]; then continue; fi; \
		UTIME1=$$(echo $$STAT1 | cut -d' ' -f14); \
		STIME1=$$(echo $$STAT1 | cut -d' ' -f15); \
		UP1=$$(cat /proc/uptime | cut -d' ' -f1); \
		sleep $(SLEEP_TIME); \
		STAT2=$$(cat /proc/$$MYPID/stat 2>/dev/null); \
		if [ -z "$$STAT2" ]; then $(kill_yams) ; continue; fi; \
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
		echo "  Sample $$i: CPU $$SAMPLE_CPU%, MEM $$MEM_MB MB"; \
		total_cpu=$$(echo "$$total_cpu + $$SAMPLE_CPU" | bc); \
		total_mem=$$(echo "$$total_mem + $$MEM_MB" | bc); \
		count=$$((count + 1)); \
	done; \
	if [ $$count -gt 0 ]; then \
		avg_cpu=$$(echo "scale=2; $$total_cpu / $$count" | bc); \
		avg_mem=$$(echo "scale=2; $$total_mem / $$count" | bc); \
		printf "\n$(BOLD)AVERAGE STATS (Core $(CPU_CORE), n=$$count):$(RESET)\n"; \
		printf "  %-15s %s%%\n" "CPU Usage:" "$$avg_cpu"; \
		printf "  %-15s %s MB\n" "Memory:" "$$avg_mem"; \
	fi; \
	kill -9 -$$MYPID > /dev/null 2>&1 || true; \
	$(kill_yams) ; \
