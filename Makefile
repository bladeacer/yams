# --- Yet Another Audio Script (yaas) Makefile ---

CC      := gcc
CFLAGS  := -Wall -Wextra -O2
LIBS    := -lncurses
TARGET  := yaas
SRC     := yaas.c

# Default target
.DEFAULT_GOAL := help

.PHONY: help build clean install uninstall

help: ## Show this help message
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'

build: ## Compile the yaas binary
	@echo "Compiling $(TARGET)..."
	@$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)
	@echo "Done. Run ./$(TARGET) to start."

clean: ## Remove compiled binary and objects
	@echo "Cleaning up..."
	@rm -f $(TARGET)

install: build ## Install yaas to /usr/local/bin
	@echo "Installing to /usr/local/bin..."
	@sudo install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

uninstall: ## Remove yaas from /usr/local/bin
	@echo "Removing from /usr/local/bin..."
	@sudo rm -f /usr/local/bin/$(TARGET)
