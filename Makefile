# Makefile for Finance City Engine
# Supports Linux, macOS, and Windows

# Detect operating system
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        DETECTED_OS := Linux
    endif
    ifeq ($(UNAME_S),Darwin)
        DETECTED_OS := macOS
    endif
endif

# Set platform-specific variables
ifeq ($(DETECTED_OS),Linux)
    CMAKE_PRESET := linux-default
    VULKAN_SDK := $(HOME)/1.3.296.0/x86_64
    EXPORT_LIB_PATH := export LD_LIBRARY_PATH=$(VULKAN_SDK)/lib:$$LD_LIBRARY_PATH
    VULKAN_LAYER_PATH := $(VULKAN_SDK)/share/vulkan/explicit_layer.d
else ifeq ($(DETECTED_OS),macOS)
    CMAKE_PRESET := mac-default
    # Try to find Homebrew in standard locations (Apple Silicon or Intel)
    HOMEBREW_PREFIX := $(shell /opt/homebrew/bin/brew --prefix 2>/dev/null || /usr/local/bin/brew --prefix 2>/dev/null || echo "/opt/homebrew")
    VULKAN_SDK := $(HOMEBREW_PREFIX)/opt/vulkan-loader
    EXPORT_LIB_PATH := export DYLD_LIBRARY_PATH=$(VULKAN_SDK)/lib:$$DYLD_LIBRARY_PATH
    VULKAN_LAYER_PATH := $(HOMEBREW_PREFIX)/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d
else ifeq ($(DETECTED_OS),Windows)
    CMAKE_PRESET := windows-default
    VULKAN_SDK := C:/VulkanSDK/1.3.296.0
    EXPORT_LIB_PATH :=
    VULKAN_LAYER_PATH := $(VULKAN_SDK)/Bin
else
    $(error Unsupported operating system: $(DETECTED_OS))
endif

# Common environment setup
EXPORT_VULKAN_SDK := export VULKAN_SDK="$(VULKAN_SDK)"
EXPORT_PATH := export PATH="$(VULKAN_SDK)/bin:$$PATH"
EXPORT_VK_LAYER := export VK_LAYER_PATH="$(VULKAN_LAYER_PATH)"

# Combine all environment exports
ifeq ($(DETECTED_OS),Linux)
    ENV_SETUP := $(EXPORT_VULKAN_SDK) && $(EXPORT_PATH) && export LD_LIBRARY_PATH="$(VULKAN_SDK)/lib:$$LD_LIBRARY_PATH" && $(EXPORT_VK_LAYER)
else ifeq ($(DETECTED_OS),macOS)
    ENV_SETUP := export PATH="$(HOMEBREW_PREFIX)/bin:$$PATH" && export VK_LAYER_PATH="$(VULKAN_LAYER_PATH)" && export DYLD_FALLBACK_LIBRARY_PATH="$(HOMEBREW_PREFIX)/opt/vulkan-validationlayers/lib:$(HOMEBREW_PREFIX)/lib:/usr/local/lib:/usr/lib"
else
    ENV_SETUP := $(EXPORT_VULKAN_SDK) && $(EXPORT_PATH) && $(EXPORT_VK_LAYER)
endif

# Build directory
BUILD_DIR := build
EXECUTABLE := $(BUILD_DIR)/FinanceCityEngine

# Colors for output
COLOR_GREEN := \033[0;32m
COLOR_BLUE := \033[0;34m
COLOR_YELLOW := \033[0;33m
COLOR_RESET := \033[0m

.PHONY: all build run run-only clean re help info demo-smoke release wasm configure-wasm build-wasm serve-wasm clean-wasm re-wasm setup-emscripten check-emscripten build-wasm-only

# Default target
all: build

# Display build information
info:
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)  Finance City Engine Build System$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "Operating System: $(COLOR_GREEN)$(DETECTED_OS)$(COLOR_RESET)"
	@echo "CMake Preset:     $(COLOR_GREEN)$(CMAKE_PRESET)$(COLOR_RESET)"
	@echo "Vulkan SDK:       $(COLOR_GREEN)$(VULKAN_SDK)$(COLOR_RESET)"
	@echo "Build Directory:  $(COLOR_GREEN)$(BUILD_DIR)$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"

# Configure the project with CMake presets
configure: info
	@echo "$(COLOR_YELLOW)Configuring project...$(COLOR_RESET)"
	@$(ENV_SETUP) && cmake --preset $(CMAKE_PRESET) -DBUILD_TESTS=ON
	@echo "$(COLOR_GREEN)Configuration complete!$(COLOR_RESET)"

# Build the project
build: configure
	@echo "$(COLOR_YELLOW)Building project...$(COLOR_RESET)"
	@$(ENV_SETUP) && cmake --build $(BUILD_DIR)
	@echo "$(COLOR_GREEN)Build complete!$(COLOR_RESET)"

# Run the main application
run: build
	@echo "$(COLOR_YELLOW)Running Finance City Engine...$(COLOR_RESET)"
	@$(ENV_SETUP) && ./$(EXECUTABLE)

# Run without building
run-only:
	@echo "$(COLOR_YELLOW)Running Finance City Engine...$(COLOR_RESET)"
	@$(ENV_SETUP) && ./$(EXECUTABLE)

# Clean build artifacts
clean:
	@echo "$(COLOR_YELLOW)Cleaning build directory...$(COLOR_RESET)"
	@rm -rf $(BUILD_DIR)
	@rm -rf vcpkg_installed
	@echo "$(COLOR_GREEN)Clean complete!$(COLOR_RESET)"

# Rebuild from scratch
re: clean build

# =============================================================================
# Production Build (without test executables)
# =============================================================================
release: info
	@echo "$(COLOR_YELLOW)Building release (no tests)...$(COLOR_RESET)"
	@$(ENV_SETUP) && cmake --preset $(CMAKE_PRESET) -DBUILD_TESTS=OFF
	@$(ENV_SETUP) && cmake --build $(BUILD_DIR)
	@echo "$(COLOR_GREEN)Release build complete!$(COLOR_RESET)"

# =============================================================================
# Test Executables
# =============================================================================
demo-smoke: build
	@echo "$(COLOR_YELLOW)Running RHI smoke test...$(COLOR_RESET)"
	@$(ENV_SETUP) && ./$(BUILD_DIR)/rhi_smoke_test

# Display help
help:
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)  Finance City Engine Build System$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_BLUE)Build & Run:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)make$(COLOR_RESET)                    - Build project"
	@echo "  $(COLOR_GREEN)make run$(COLOR_RESET)                - Build and run"
	@echo "  $(COLOR_GREEN)make run-only$(COLOR_RESET)           - Run without building"
	@echo "  $(COLOR_GREEN)make release$(COLOR_RESET)            - Build without tests (production)"
	@echo ""
	@echo "$(COLOR_BLUE)Tests:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)make demo-smoke$(COLOR_RESET)         - Run RHI smoke test"
	@echo ""
	@echo "$(COLOR_BLUE)Maintenance:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)make clean$(COLOR_RESET)              - Remove all build artifacts"
	@echo "  $(COLOR_GREEN)make re$(COLOR_RESET)                 - Clean and rebuild"
	@echo "  $(COLOR_GREEN)make info$(COLOR_RESET)               - Display build configuration"
	@echo ""
	@echo "$(COLOR_BLUE)WebAssembly (WebGPU):$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)make setup-emscripten$(COLOR_RESET)   - Install Emscripten SDK (one-time)"
	@echo "  $(COLOR_GREEN)make wasm$(COLOR_RESET)               - Build WebAssembly version"
	@echo "  $(COLOR_GREEN)make serve-wasm$(COLOR_RESET)         - Build and serve on localhost:8000"
	@echo "  $(COLOR_GREEN)make clean-wasm$(COLOR_RESET)         - Remove WASM build artifacts"
	@echo "  $(COLOR_GREEN)make re-wasm$(COLOR_RESET)            - Clean and rebuild WASM"
	@echo ""
	@echo "$(COLOR_BLUE)Environment:$(COLOR_RESET)"
	@echo "  VULKAN_SDK=$(VULKAN_SDK)"
	@echo ""

# =============================================================================
# WebAssembly (WebGPU) Build Targets
# =============================================================================

WASM_BUILD_DIR := build_wasm
WASM_EXECUTABLE := $(WASM_BUILD_DIR)/FinanceCityEngine.html

# Emscripten environment setup
EMSDK_PATH := $(HOME)/emsdk
EMSCRIPTEN_ENV := $(EMSDK_PATH)/emsdk_env.sh

# Install Emscripten SDK (one-time setup)
setup-emscripten:
	@echo "$(COLOR_BLUE)Running Emscripten setup script...$(COLOR_RESET)"
	@./scripts/setup_emscripten.sh

# Check if Emscripten is available
check-emscripten:
	@if [ ! -f "$(EMSCRIPTEN_ENV)" ]; then \
		echo "$(COLOR_YELLOW)========================================$(COLOR_RESET)"; \
		echo "$(COLOR_YELLOW)  Emscripten SDK not found!$(COLOR_RESET)"; \
		echo "$(COLOR_YELLOW)========================================$(COLOR_RESET)"; \
		echo ""; \
		echo "$(COLOR_BLUE)WebAssembly builds require Emscripten SDK.$(COLOR_RESET)"; \
		echo ""; \
		echo "$(COLOR_GREEN)Quick Setup (Recommended):$(COLOR_RESET)"; \
		echo "  $(COLOR_YELLOW)./scripts/setup_emscripten.sh$(COLOR_RESET)"; \
		echo ""; \
		echo "$(COLOR_GREEN)Manual Setup:$(COLOR_RESET)"; \
		echo "  git clone https://github.com/emscripten-core/emsdk.git ~/emsdk"; \
		echo "  cd ~/emsdk"; \
		echo "  ./emsdk install 3.1.71"; \
		echo "  ./emsdk activate 3.1.71"; \
		echo "  source ~/emsdk/emsdk_env.sh"; \
		echo ""; \
		echo "$(COLOR_BLUE)After installation, run 'make wasm' again.$(COLOR_RESET)"; \
		echo ""; \
		exit 1; \
	fi

# Configure WASM build
configure-wasm: check-emscripten
	@if [ ! -f $(WASM_BUILD_DIR)/CMakeCache.txt ]; then \
		echo "$(COLOR_YELLOW)Configuring WebAssembly build...$(COLOR_RESET)"; \
		mkdir -p $(WASM_BUILD_DIR); \
		bash -c "export PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin && source $(EMSCRIPTEN_ENV) && cd $(WASM_BUILD_DIR) && emcmake cmake .. \
			-DCMAKE_TOOLCHAIN_FILE=../cmake/EmscriptenToolchain.cmake \
			-DCMAKE_BUILD_TYPE=Release"; \
		echo "$(COLOR_GREEN)WASM configuration complete!$(COLOR_RESET)"; \
	else \
		echo "$(COLOR_BLUE)WASM already configured (use 'make clean-wasm' and retry to reconfigure)$(COLOR_RESET)"; \
	fi

# Build WASM
build-wasm: configure-wasm
	@echo "$(COLOR_YELLOW)Building WebAssembly version...$(COLOR_RESET)"
ifeq ($(DETECTED_OS),macOS)
	@bash -c "export PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin && source $(EMSCRIPTEN_ENV) && cd $(WASM_BUILD_DIR) && emmake make -j$$(sysctl -n hw.ncpu)"
else
	@bash -c "export PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin && source $(EMSCRIPTEN_ENV) && cd $(WASM_BUILD_DIR) && emmake make -j$$(nproc)"
endif
	@echo "$(COLOR_GREEN)WASM build complete!$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_BLUE)Build artifacts:$(COLOR_RESET)"
	@ls -lh $(WASM_BUILD_DIR)/FinanceCityEngine.html 2>/dev/null || echo "  No .html file found"
	@ls -lh $(WASM_BUILD_DIR)/FinanceCityEngine.js 2>/dev/null || echo "  No .js file found"
	@ls -lh $(WASM_BUILD_DIR)/FinanceCityEngine.wasm 2>/dev/null || echo "  No .wasm file found"

# Build WASM without reconfiguring
build-wasm-only: check-emscripten
	@echo "$(COLOR_YELLOW)Building WebAssembly (no reconfigure)...$(COLOR_RESET)"
ifeq ($(DETECTED_OS),macOS)
	@bash -c "export PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin && source $(EMSCRIPTEN_ENV) && cd $(WASM_BUILD_DIR) && emmake make -j$$(sysctl -n hw.ncpu)"
else
	@bash -c "export PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin && source $(EMSCRIPTEN_ENV) && cd $(WASM_BUILD_DIR) && emmake make -j$$(nproc)"
endif
	@echo "$(COLOR_GREEN)WASM build complete!$(COLOR_RESET)"

# Full WASM build (alias for build-wasm)
wasm: build-wasm

# Serve WASM build on local web server
serve-wasm: wasm
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)Starting web server...$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@echo "URL: $(COLOR_GREEN)http://localhost:8000/FinanceCityEngine.html$(COLOR_RESET)"
	@echo "Press Ctrl+C to stop"
	@echo "$(COLOR_BLUE)========================================$(COLOR_RESET)"
	@cd $(WASM_BUILD_DIR) && python3 -m http.server 8000

# Clean WASM build
clean-wasm:
	@echo "$(COLOR_YELLOW)Cleaning WebAssembly build...$(COLOR_RESET)"
	@rm -rf $(WASM_BUILD_DIR)
	@echo "$(COLOR_GREEN)WASM build cleaned!$(COLOR_RESET)"

# Rebuild WASM from scratch
re-wasm: clean-wasm wasm
