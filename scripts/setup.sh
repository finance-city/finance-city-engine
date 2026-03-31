#!/bin/bash
# Finance City Engine — Development Environment Setup
# Supports: Ubuntu 22.04 / 24.04 (Linux), macOS (Homebrew)
#
# Usage:
#   chmod +x scripts/setup.sh
#   ./scripts/setup.sh

set -e

COLOR_GREEN="\033[0;32m"
COLOR_BLUE="\033[0;34m"
COLOR_YELLOW="\033[0;33m"
COLOR_RED="\033[0;31m"
COLOR_RESET="\033[0m"

VULKAN_SDK_VERSION="1.3.296.0"
VCPKG_DIR="${HOME}/vcpkg"

log_step() { echo -e "\n${COLOR_BLUE}▶ $1${COLOR_RESET}"; }
log_ok()   { echo -e "${COLOR_GREEN}✅ $1${COLOR_RESET}"; }
log_warn() { echo -e "${COLOR_YELLOW}⚠️  $1${COLOR_RESET}"; }
log_err()  { echo -e "${COLOR_RED}❌ $1${COLOR_RESET}"; }

# ==============================================================================
# Detect OS
# ==============================================================================
detect_os() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        if [ -f /etc/os-release ]; then
            . /etc/os-release
            OS="linux"
            DISTRO="${ID}"
        else
            log_err "Cannot detect Linux distribution. /etc/os-release not found."
            exit 1
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
    else
        log_err "Unsupported OS: $OSTYPE"
        log_err "This script supports Ubuntu (22.04/24.04) and macOS only."
        exit 1
    fi
}

# ==============================================================================
# Linux setup
# ==============================================================================
setup_linux() {
    log_step "Checking Linux distribution..."
    if [[ "$DISTRO" != "ubuntu" ]]; then
        log_warn "This script is tested on Ubuntu. Proceeding anyway on: $DISTRO"
    fi
    echo "Distribution: $DISTRO $VERSION_ID"

    log_step "Installing system packages..."
    sudo apt-get update -qq
    sudo apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
        git \
        curl \
        wget \
        pkg-config \
        clang-18 \
        python3 \
        python3-pip \
        libx11-dev \
        libxrandr-dev \
        libxinerama-dev \
        libxcursor-dev \
        libxi-dev \
        libgl-dev
    log_ok "System packages installed"

    # Clang 18 may not be in default apt — add LLVM repo if missing
    if ! command -v clang-18 &>/dev/null; then
        log_step "clang-18 not found — adding LLVM apt repository..."
        wget -qO- https://apt.llvm.org/llvm.sh | sudo bash -s -- 18
        sudo apt-get install -y clang-18
    fi
    log_ok "clang-18: $(clang-18 --version | head -1)"

    # Vulkan SDK
    setup_vulkan_linux

    # vcpkg
    setup_vcpkg

    # Shell profile
    write_profile_linux
}

setup_vulkan_linux() {
    log_step "Installing Vulkan SDK ${VULKAN_SDK_VERSION}..."

    VULKAN_INSTALL_DIR="${HOME}/${VULKAN_SDK_VERSION}"
    VULKAN_SDK_PATH="${VULKAN_INSTALL_DIR}/x86_64"

    if [ -d "${VULKAN_SDK_PATH}" ]; then
        log_ok "Vulkan SDK already installed at ${VULKAN_SDK_PATH}"
        return
    fi

    # Try LunarG apt repo first (Ubuntu only)
    if [[ "$DISTRO" == "ubuntu" ]]; then
        log_warn "Attempting LunarG apt repository install..."
        wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add - 2>/dev/null || true
        UBUNTU_VER="${VERSION_ID//./-}"
        sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-${VULKAN_SDK_VERSION}-${UBUNTU_VER}.list \
            "https://packages.lunarg.com/vulkan/${VULKAN_SDK_VERSION}/lunarg-vulkan-${VULKAN_SDK_VERSION}-${UBUNTU_VER}.list" 2>/dev/null || true
        sudo apt-get update -qq 2>/dev/null || true
        if sudo apt-get install -y vulkan-sdk 2>/dev/null; then
            log_ok "Vulkan SDK installed via apt"
            echo "VULKAN_SDK=/usr" >> /tmp/fce_env_vars
            return
        fi
    fi

    # Fallback: manual download
    log_step "Downloading Vulkan SDK ${VULKAN_SDK_VERSION} (manual install)..."
    TARBALL="vulkansdk-linux-x86_64-${VULKAN_SDK_VERSION}.tar.xz"
    DOWNLOAD_URL="https://sdk.lunarg.com/sdk/download/${VULKAN_SDK_VERSION}/linux/${TARBALL}"

    mkdir -p "${HOME}/${VULKAN_SDK_VERSION}"
    cd "${HOME}/${VULKAN_SDK_VERSION}"
    echo "Downloading from: ${DOWNLOAD_URL}"
    echo "This may take a few minutes (~500 MB)..."
    wget -q --show-progress "${DOWNLOAD_URL}" -O "${TARBALL}"
    tar -xf "${TARBALL}"
    rm -f "${TARBALL}"
    cd - >/dev/null
    log_ok "Vulkan SDK installed at ${VULKAN_SDK_PATH}"
    echo "VULKAN_SDK=${VULKAN_SDK_PATH}" >> /tmp/fce_env_vars
}

# ==============================================================================
# macOS setup
# ==============================================================================
setup_macos() {
    log_step "Setting up macOS environment..."

    # Homebrew
    if ! command -v brew &>/dev/null; then
        log_step "Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    log_ok "Homebrew: $(brew --version | head -1)"

    log_step "Installing packages via Homebrew..."
    brew install cmake ninja llvm git python3
    brew install vulkan-loader vulkan-headers vulkan-validationlayers molten-vk
    log_ok "Homebrew packages installed"

    # vcpkg
    setup_vcpkg

    # Shell profile
    write_profile_macos
}

# ==============================================================================
# vcpkg (common)
# ==============================================================================
setup_vcpkg() {
    log_step "Setting up vcpkg..."

    if [ -d "${VCPKG_DIR}" ]; then
        log_ok "vcpkg already exists at ${VCPKG_DIR}"
        # Pull latest to be safe
        git -C "${VCPKG_DIR}" pull --quiet
    else
        git clone https://github.com/microsoft/vcpkg.git "${VCPKG_DIR}"
    fi

    if [ ! -f "${VCPKG_DIR}/vcpkg" ]; then
        "${VCPKG_DIR}/bootstrap-vcpkg.sh" -disableMetrics
    fi

    log_ok "vcpkg ready at ${VCPKG_DIR}"
    echo "VCPKG_ROOT=${VCPKG_DIR}" >> /tmp/fce_env_vars
}

# ==============================================================================
# Write environment variables to shell profile
# ==============================================================================
write_profile_linux() {
    local profile="${HOME}/.bashrc"
    if [ -n "$ZSH_VERSION" ] || [ "$SHELL" = "/bin/zsh" ] || [ "$SHELL" = "/usr/bin/zsh" ]; then
        profile="${HOME}/.zshrc"
    fi

    # Read collected env vars
    local vulkan_sdk_val=""
    local vcpkg_root_val=""
    if [ -f /tmp/fce_env_vars ]; then
        vulkan_sdk_val=$(grep "VULKAN_SDK=" /tmp/fce_env_vars | tail -1 | cut -d= -f2-)
        vcpkg_root_val=$(grep "VCPKG_ROOT=" /tmp/fce_env_vars | tail -1 | cut -d= -f2-)
        rm -f /tmp/fce_env_vars
    fi

    log_step "Writing environment variables to ${profile}..."

    # Remove old block if present
    sed -i '/# Finance City Engine/,/# end Finance City Engine/d' "${profile}" 2>/dev/null || true

    {
        echo ""
        echo "# Finance City Engine"
        if [ -n "${vcpkg_root_val}" ]; then
            echo "export VCPKG_ROOT=${vcpkg_root_val}"
        fi
        if [ -n "${vulkan_sdk_val}" ]; then
            echo "export VULKAN_SDK=${vulkan_sdk_val}"
            echo "export PATH=\"\${VULKAN_SDK}/bin:\${PATH}\""
            echo "export LD_LIBRARY_PATH=\"\${VULKAN_SDK}/lib:\${LD_LIBRARY_PATH}\""
        fi
        echo "# end Finance City Engine"
    } >> "${profile}"

    log_ok "Environment variables written to ${profile}"
}

write_profile_macos() {
    local profile="${HOME}/.zshrc"
    if [ "$SHELL" = "/bin/bash" ] || [ "$SHELL" = "/usr/bin/bash" ]; then
        profile="${HOME}/.bash_profile"
    fi

    HOMEBREW_PREFIX=$(brew --prefix)

    log_step "Writing environment variables to ${profile}..."

    sed -i '' '/# Finance City Engine/,/# end Finance City Engine/d' "${profile}" 2>/dev/null || true

    {
        echo ""
        echo "# Finance City Engine"
        echo "export VCPKG_ROOT=${VCPKG_DIR}"
        echo "export VULKAN_SDK=${HOMEBREW_PREFIX}/opt/vulkan-loader"
        echo "export PATH=\"${HOMEBREW_PREFIX}/opt/llvm/bin:\${PATH}\""
        echo "# end Finance City Engine"
    } >> "${profile}"

    log_ok "Environment variables written to ${profile}"
}

# ==============================================================================
# Summary
# ==============================================================================
print_summary() {
    echo ""
    echo -e "${COLOR_GREEN}========================================"
    echo -e "  Setup Complete!"
    echo -e "========================================${COLOR_RESET}"
    echo ""
    echo -e "${COLOR_YELLOW}IMPORTANT: Reload your shell before building:${COLOR_RESET}"
    echo ""
    echo "    source ~/.bashrc    # bash"
    echo "    source ~/.zshrc     # zsh"
    echo ""
    echo -e "${COLOR_BLUE}Then build and run:${COLOR_RESET}"
    echo ""
    echo "    make build"
    echo "    ./build/FinanceCityEngine"
    echo ""
    echo -e "${COLOR_BLUE}For WebAssembly (optional):${COLOR_RESET}"
    echo ""
    echo "    make setup-emscripten"
    echo "    make wasm"
    echo "    make serve-wasm"
    echo ""
}

# ==============================================================================
# Main
# ==============================================================================
main() {
    echo -e "${COLOR_BLUE}"
    echo "========================================"
    echo "  Finance City Engine — Setup"
    echo "========================================"
    echo -e "${COLOR_RESET}"

    detect_os

    if [ "$OS" = "linux" ]; then
        setup_linux
    elif [ "$OS" = "macos" ]; then
        setup_macos
    fi

    print_summary
}

main "$@"
