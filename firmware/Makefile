# ESP32-C6 Touch LCD Makefile
# Code formatting and project management

# Paths
CLANG_FORMAT := /home/myos/.espressif/tools/esp-clang/16.0.1-fe4f10a809/esp-clang/bin/clang-format
CLANG_TIDY := /home/myos/.espressif/tools/esp-clang/16.0.1-fe4f10a809/esp-clang/bin/clang-tidy
PROJECT_ROOT := $(shell pwd)

# Find all C/C++ source and header files (exclude external/third-party code)
SRC_DIRS := main components
EXCLUDE_PATHS := -not -path "*/managed_components/*" -not -path "*/build/*" \
                 -not -path "*/esp_bsp/*" -not -path "*/esp_lcd_jd9853/*" -not -path "*/esp_lcd_touch_axs5106/*"
C_FILES := $(shell find $(SRC_DIRS) -type f \( -name "*.c" -o -name "*.cpp" \) $(EXCLUDE_PATHS))
H_FILES := $(shell find $(SRC_DIRS) -type f \( -name "*.h" -o -name "*.hpp" \) $(EXCLUDE_PATHS))
ALL_FILES := $(C_FILES) $(H_FILES)

# Colors for output
COLOR_RESET := \033[0m
COLOR_GREEN := \033[32m
COLOR_YELLOW := \033[33m
COLOR_BLUE := \033[34m

.PHONY: help format format-check format-changed tidy tidy-fix tidy-all tidy-all-fix clean-format

# Default target
help:
	@echo "$(COLOR_BLUE)ESP32-C6 Touch LCD - Makefile Commands$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_GREEN)Formatting Commands:$(COLOR_RESET)"
	@echo "  make format         - Format all C/C++ files in the project"
	@echo "  make format-check   - Check if files need formatting (dry-run)"
	@echo "  make format-changed - Format only git-changed files"
	@echo "  make format-file    - Format a specific file (usage: make format-file FILE=path/to/file.c)"
	@echo "  make tidy           - Check naming conventions with clang-tidy"
	@echo "  make tidy-fix       - Auto-fix naming conventions with clang-tidy"
	@echo "  make tidy-all       - Run full code quality checks (SonarQube-like)"
	@echo "  make tidy-all-fix   - Auto-fix all code quality issues"
	@echo ""
	@echo "$(COLOR_GREEN)ESP-IDF Commands:$(COLOR_RESET)"
	@echo "  make build          - Build the project (idf.py build)"
	@echo "  make flash          - Flash the project (idf.py flash)"
	@echo "  make monitor        - Open serial monitor (idf.py monitor)"
	@echo "  make clean          - Clean build files (idf.py fullclean)"
	@echo "  make menuconfig     - Open menuconfig (idf.py menuconfig)"
	@echo ""

# Format all files
format:
	@echo "$(COLOR_YELLOW)Formatting all C/C++ files...$(COLOR_RESET)"
	@for file in $(ALL_FILES); do \
		echo "Formatting: $$file"; \
		$(CLANG_FORMAT) -i $$file; \
	done
	@echo "$(COLOR_GREEN)✓ Formatting complete! ($(words $(ALL_FILES)) files)$(COLOR_RESET)"

# Check formatting without modifying files
format-check:
	@echo "$(COLOR_YELLOW)Checking formatting...$(COLOR_RESET)"
	@FAIL=0; \
	for file in $(ALL_FILES); do \
		if ! $(CLANG_FORMAT) --dry-run --Werror $$file 2>/dev/null; then \
			echo "$(COLOR_YELLOW)Needs formatting: $$file$(COLOR_RESET)"; \
			FAIL=1; \
		fi; \
	done; \
	if [ $$FAIL -eq 0 ]; then \
		echo "$(COLOR_GREEN)✓ All files are properly formatted!$(COLOR_RESET)"; \
	else \
		echo "$(COLOR_YELLOW)⚠ Some files need formatting. Run 'make format'$(COLOR_RESET)"; \
		exit 1; \
	fi

# Format only git-changed files (staged + unstaged)
format-changed:
	@echo "$(COLOR_YELLOW)Formatting git-changed files...$(COLOR_RESET)"
	@git diff --name-only --diff-filter=ACMR | grep -E '\.(c|cpp|h|hpp)$$' | while read file; do \
		if [ -f "$$file" ]; then \
			echo "Formatting: $$file"; \
			$(CLANG_FORMAT) -i "$$file"; \
		fi; \
	done
	@git diff --staged --name-only --diff-filter=ACMR | grep -E '\.(c|cpp|h|hpp)$$' | while read file; do \
		if [ -f "$$file" ]; then \
			echo "Formatting: $$file"; \
			$(CLANG_FORMAT) -i "$$file"; \
		fi; \
	done
	@echo "$(COLOR_GREEN)✓ Git-changed files formatted!$(COLOR_RESET)"

# Format a specific file
format-file:
	@if [ -z "$(FILE)" ]; then \
		echo "$(COLOR_YELLOW)Usage: make format-file FILE=path/to/file.c$(COLOR_RESET)"; \
		exit 1; \
	fi
	@if [ ! -f "$(FILE)" ]; then \
		echo "$(COLOR_YELLOW)Error: File '$(FILE)' not found$(COLOR_RESET)"; \
		exit 1; \
	fi
	@echo "$(COLOR_YELLOW)Formatting: $(FILE)$(COLOR_RESET)"
	@$(CLANG_FORMAT) -i "$(FILE)"
	@echo "$(COLOR_GREEN)✓ File formatted!$(COLOR_RESET)"

# ESP-IDF integration
build:
	@idf.py build

flash:
	@idf.py flash

monitor:
	@idf.py monitor

flash-monitor: flash monitor

clean:
	@idf.py fullclean

menuconfig:
	@idf.py menuconfig

# Check naming conventions with clang-tidy
tidy:
	@echo "$(COLOR_YELLOW)Running clang-tidy for naming convention checks...$(COLOR_RESET)"
	@if [ ! -f build/compile_commands.json ]; then \
		echo "$(COLOR_YELLOW)Warning: compile_commands.json not found!$(COLOR_RESET)"; \
		echo "$(COLOR_YELLOW)Building project first...$(COLOR_RESET)"; \
		idf.py build; \
	fi
	@FAIL=0; \
	for file in $(filter %.cpp,$(C_FILES)); do \
		echo "Checking: $$file"; \
		OUTPUT=$$($(CLANG_TIDY) "$$file" -p build 2>&1 | \
			grep -v "clang-diagnostic-error" | \
			grep -v "invalid arch name" | \
			grep -v "unknown argument" | \
			grep -v "Suppressed.*warnings" | \
			grep -v "Use -header-filter" | \
			grep -v "Found compiler error"); \
		if echo "$$OUTPUT" | grep -q "readability-identifier-naming"; then \
			echo "$$OUTPUT"; \
			FAIL=1; \
		else \
			echo "$(COLOR_GREEN)✓ $$file$(COLOR_RESET)"; \
		fi; \
	done; \
	if [ $$FAIL -eq 0 ]; then \
		echo "$(COLOR_GREEN)✓ All naming conventions are correct!$(COLOR_RESET)"; \
	else \
		echo "$(COLOR_YELLOW)⚠ Some naming convention issues found.$(COLOR_RESET)"; \
	fi

# Full code quality check (SonarQube-like with all checks)
tidy-all:
	@echo "$(COLOR_YELLOW)Running full code quality analysis...$(COLOR_RESET)"
	@if [ ! -f build/compile_commands.json ]; then \
		echo "$(COLOR_YELLOW)Warning: compile_commands.json not found!$(COLOR_RESET)"; \
		echo "$(COLOR_YELLOW)Building project first...$(COLOR_RESET)"; \
		idf.py build; \
	fi
	@echo "$(COLOR_BLUE)Checking:$(COLOR_RESET)"
	@echo "  - Code readability"
	@echo "  - Modern C++ best practices"
	@echo "  - Performance issues"
	@echo "  - Potential bugs"
	@echo "  - C++ Core Guidelines compliance"
	@echo ""
	@FAIL=0; \
	for file in $(filter %.cpp,$(C_FILES)); do \
		echo "Analyzing: $$file"; \
		OUTPUT=$$($(CLANG_TIDY) "$$file" -p build 2>&1 | \
			grep -v "clang-diagnostic-error" | \
			grep -v "invalid arch name" | \
			grep -v "unknown argument" | \
			grep -v "Suppressed.*warnings" | \
			grep -v "Use -header-filter"); \
		if echo "$$OUTPUT" | grep -q "warning:"; then \
			echo "$$OUTPUT" | grep "warning:" | head -20; \
			FAIL=1; \
		fi; \
	done; \
	if [ $$FAIL -eq 0 ]; then \
		echo "$(COLOR_GREEN)✓ All code quality checks passed!$(COLOR_RESET)"; \
	else \
		echo "$(COLOR_YELLOW)⚠ Some code quality issues found. Run 'make tidy-all-fix' to auto-fix.$(COLOR_RESET)"; \
	fi

# Auto-fix all code quality issues
tidy-all-fix:
	@echo "$(COLOR_YELLOW)Auto-fixing all code quality issues...$(COLOR_RESET)"
	@if [ ! -f build/compile_commands.json ]; then \
		echo "$(COLOR_YELLOW)Warning: compile_commands.json not found!$(COLOR_RESET)"; \
		echo "$(COLOR_YELLOW)Building project first...$(COLOR_RESET)"; \
		idf.py build; \
	fi
	@for file in $(filter %.cpp,$(C_FILES)); do \
		echo "Fixing: $$file"; \
		$(CLANG_TIDY) "$$file" -p build --fix --fix-errors --format-style=file 2>&1 | \
			grep -v "clang-diagnostic-error" | \
			grep -v "invalid arch name" | \
			grep -v "unknown argument" | \
			grep -v "Suppressed.*warnings" | \
			grep -v "Use -header-filter" | \
			grep "FIX-IT applied" || true; \
	done
	@echo "$(COLOR_GREEN)✓ Auto-fix complete!$(COLOR_RESET)"
	@echo "$(COLOR_YELLOW)Note: Review changes before committing.$(COLOR_RESET)"

# Auto-fix naming conventions with clang-tidy (only identifier naming)
tidy-fix:
	@echo "$(COLOR_YELLOW)Auto-fixing naming conventions with clang-tidy...$(COLOR_RESET)"
	@if [ ! -f build/compile_commands.json ]; then \
		echo "$(COLOR_YELLOW)Warning: compile_commands.json not found!$(COLOR_RESET)"; \
		echo "$(COLOR_YELLOW)Building project first...$(COLOR_RESET)"; \
		idf.py build; \
	fi
	@echo "$(COLOR_BLUE)Processing C++ source files...$(COLOR_RESET)"
	@for file in $(filter %.cpp,$(C_FILES)); do \
		echo "  Fixing: $$file"; \
		$(CLANG_TIDY) "$$file" -p build \
			--checks='-*,readability-identifier-naming' \
			--header-filter='^(?!.*(managed_components|build|esp-idf|esp_bsp|esp_lcd_jd9853|esp_lcd_touch_axs5106)).*' \
			--fix \
			--fix-errors \
			--format-style=file \
			2>&1 | grep "FIX-IT applied" || true; \
	done
	@echo "$(COLOR_BLUE)Processing C++ header files...$(COLOR_RESET)"
	@for file in $(filter %.hpp,$(H_FILES)); do \
		echo "  Fixing: $$file"; \
		$(CLANG_TIDY) "$$file" -p build \
			--checks='-*,readability-identifier-naming' \
			--header-filter='^(?!.*(managed_components|build|esp-idf|esp_bsp|esp_lcd_jd9853|esp_lcd_touch_axs5106)).*' \
			--fix \
			--fix-errors \
			--format-style=file \
			2>&1 | grep "FIX-IT applied" || true; \
	done
	@echo "$(COLOR_BLUE)Processing C header files...$(COLOR_RESET)"
	@for file in $(filter %.h,$(H_FILES)); do \
		echo "  Fixing: $$file"; \
		$(CLANG_TIDY) "$$file" -p build \
			--checks='-*,readability-identifier-naming' \
			--header-filter='^(?!.*(managed_components|build|esp-idf|esp_bsp|esp_lcd_jd9853|esp_lcd_touch_axs5106)).*' \
			--fix \
			--fix-errors \
			--format-style=file \
			2>&1 | grep "FIX-IT applied" || true; \
	done
	@echo "$(COLOR_GREEN)✓ Auto-fix complete!$(COLOR_RESET)"
	@echo "$(COLOR_YELLOW)📝 Review changes: git diff$(COLOR_RESET)"
	@echo "$(COLOR_YELLOW)💾 Revert if needed: git checkout .$(COLOR_RESET)"

# Format before building
build-format: format build
	@echo "$(COLOR_GREEN)✓ Formatted and built successfully!$(COLOR_RESET)"
