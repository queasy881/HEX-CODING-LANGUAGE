#!/bin/bash
# ============================================================================
#  HEX Language Installer for Linux/macOS
#  Downloads latest hex from GitHub, installs to /usr/local/bin,
#  registers .hex file association
#
#  Usage: curl -fsSL https://raw.githubusercontent.com/queasy881/HEX-CODING-LANGUAGE/main/install.sh | bash
#     or: chmod +x install.sh && ./install.sh
# ============================================================================

set -e

REPO="queasy881/HEX-CODING-LANGUAGE"
INSTALL_DIR="/usr/local/bin"
BOLD='\033[1m'
GREEN='\033[32m'
RED='\033[31m'
YELLOW='\033[33m'
CYAN='\033[36m'
RESET='\033[0m'

echo ""
echo -e "${BOLD}  ██╗  ██╗███████╗██╗  ██╗${RESET}"
echo -e "${BOLD}  ██║  ██║██╔════╝╚██╗██╔╝${RESET}"
echo -e "${BOLD}  ███████║█████╗   ╚███╔╝${RESET}"
echo -e "${BOLD}  ██╔══██║██╔══╝   ██╔██╗${RESET}"
echo -e "${BOLD}  ██║  ██║███████╗██╔╝ ██╗${RESET}"
echo -e "${BOLD}  ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝${RESET}"
echo -e "  ${CYAN}Installer for Linux/macOS${RESET}"
echo ""

# ── Check for root/sudo ─────────────────────────────────────────────────
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}[*] Need sudo for /usr/local/bin — re-running with sudo...${RESET}"
    exec sudo bash "$0" "$@"
fi

# ── Detect OS and architecture ──────────────────────────────────────────
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

echo -e "${GREEN}[+]${RESET} OS: $OS"
echo -e "${GREEN}[+]${RESET} Arch: $ARCH"

# ── Check for build tools ───────────────────────────────────────────────
if ! command -v g++ &> /dev/null; then
    echo -e "${YELLOW}[*] g++ not found. Installing build tools...${RESET}"
    if [ "$OS" = "linux" ]; then
        if command -v apt-get &> /dev/null; then
            apt-get update -qq && apt-get install -y -qq g++ make
        elif command -v dnf &> /dev/null; then
            dnf install -y gcc-c++ make
        elif command -v pacman &> /dev/null; then
            pacman -S --noconfirm gcc make
        elif command -v apk &> /dev/null; then
            apk add g++ make
        else
            echo -e "${RED}[!] Cannot install g++. Install it manually and re-run.${RESET}"
            exit 1
        fi
    elif [ "$OS" = "darwin" ]; then
        if command -v brew &> /dev/null; then
            brew install gcc
        else
            echo -e "${RED}[!] Install Xcode Command Line Tools: xcode-select --install${RESET}"
            exit 1
        fi
    fi
fi

echo -e "${GREEN}[+]${RESET} g++ found: $(g++ --version | head -1)"

# ── Download source from GitHub ─────────────────────────────────────────
echo -e "${CYAN}[*] Downloading HEX source from GitHub...${RESET}"

TMPDIR=$(mktemp -d)
cd "$TMPDIR"

# Download all source files
BASE_URL="https://raw.githubusercontent.com/$REPO/main"

for file in token.h value.h environment.h lexer.h error.h interpreter.h hex.cpp; do
    echo -e "  Downloading $file..."
    if command -v curl &> /dev/null; then
        curl -fsSL "$BASE_URL/src/$file" -o "$file" 2>/dev/null || \
        curl -fsSL "$BASE_URL/$file" -o "$file" 2>/dev/null || true
    elif command -v wget &> /dev/null; then
        wget -q "$BASE_URL/src/$file" -O "$file" 2>/dev/null || \
        wget -q "$BASE_URL/$file" -O "$file" 2>/dev/null || true
    fi
done

# Check if we got the files — if not, try downloading the single-file version
if [ ! -f "interpreter.h" ] || [ ! -s "interpreter.h" ]; then
    echo -e "${YELLOW}[*] Source files not found on repo. Downloading single-file version...${RESET}"

    # Create a minimal download script that gets the hex binary directly
    echo -e "${YELLOW}[*] Source not available — downloading pre-built binary is not possible for Linux.${RESET}"
    echo -e "${YELLOW}[*] You need to push the source files to the GitHub repo first.${RESET}"
    echo -e ""
    echo -e "  Add these files to your repo root or a src/ folder:"
    echo -e "    token.h, value.h, environment.h, lexer.h, error.h, interpreter.h, hex.cpp"
    echo -e ""
    echo -e "  Then re-run this installer."

    # Clean up
    rm -rf "$TMPDIR"
    exit 1
fi

# ── Compile ─────────────────────────────────────────────────────────────
echo -e "${CYAN}[*] Compiling HEX...${RESET}"

g++ -std=c++17 -O2 -o hex hex.cpp -lpthread 2>&1

if [ ! -f "hex" ]; then
    echo -e "${RED}[!] Compilation failed.${RESET}"
    rm -rf "$TMPDIR"
    exit 1
fi

echo -e "${GREEN}[+]${RESET} Compiled successfully"

# ── Install binary ──────────────────────────────────────────────────────
echo -e "${CYAN}[*] Installing to $INSTALL_DIR/hex...${RESET}"

cp hex "$INSTALL_DIR/hex"
chmod +x "$INSTALL_DIR/hex"

echo -e "${GREEN}[+]${RESET} Installed: $INSTALL_DIR/hex"

# ── Register .hex file type (Linux desktop) ─────────────────────────────
if [ "$OS" = "linux" ]; then
    # Create MIME type
    MIME_DIR="$HOME/.local/share/mime/packages"
    mkdir -p "$MIME_DIR"
    cat > "$MIME_DIR/hex-script.xml" << 'MIMEEOF'
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="text/x-hex-script">
    <comment>HEX Script</comment>
    <glob pattern="*.hex"/>
  </mime-type>
</mime-info>
MIMEEOF

    # Create desktop entry for running .hex files
    APPS_DIR="$HOME/.local/share/applications"
    mkdir -p "$APPS_DIR"
    cat > "$APPS_DIR/hex-runner.desktop" << DESKTOPEOF
[Desktop Entry]
Type=Application
Name=HEX Script Runner
Exec=bash -c 'hex "%f"; echo ""; echo "Press Enter to close..."; read'
MimeType=text/x-hex-script;
Terminal=true
Icon=utilities-terminal
DESKTOPEOF

    # Update MIME database
    if command -v update-mime-database &> /dev/null; then
        update-mime-database "$HOME/.local/share/mime" 2>/dev/null || true
    fi
    if command -v xdg-mime &> /dev/null; then
        xdg-mime default hex-runner.desktop text/x-hex-script 2>/dev/null || true
    fi

    echo -e "${GREEN}[+]${RESET} Registered .hex file association"
fi

# ── macOS: register file type ───────────────────────────────────────────
if [ "$OS" = "darwin" ]; then
    echo -e "${YELLOW}[*]${RESET} On macOS, right-click a .hex file → Open With → Terminal to run"
fi

# ── Create man page ─────────────────────────────────────────────────────
MAN_DIR="/usr/local/share/man/man1"
if [ -d "/usr/local/share/man" ]; then
    mkdir -p "$MAN_DIR"
    cat > "$MAN_DIR/hex.1" << 'MANEOF'
.TH HEX 1 "2026" "HEX Language" "User Commands"
.SH NAME
hex \- HEX programming language interpreter
.SH SYNOPSIS
.B hex
[\fIscript.hex\fR] [\fIargs...\fR]
.SH DESCRIPTION
HEX is a symbol-heavy, hacker-aesthetic programming language.
Run a .hex script file or start the interactive REPL.
.SH EXAMPLES
.TP
hex script.hex
Run a HEX script
.TP
hex
Start interactive REPL
.SH WEBSITE
https://github.com/queasy881/HEX-CODING-LANGUAGE
MANEOF
    echo -e "${GREEN}[+]${RESET} Installed man page (try: man hex)"
fi

# ── Clean up ────────────────────────────────────────────────────────────
rm -rf "$TMPDIR"

# ── Done ────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}  ========================================${RESET}"
echo -e "${GREEN}${BOLD}  Installation complete!${RESET}"
echo -e "${BOLD}  ========================================${RESET}"
echo ""
echo -e "  You can now:"
echo -e "    ${CYAN}hex${RESET}                     Open REPL"
echo -e "    ${CYAN}hex script.hex${RESET}          Run a script"
echo ""
echo -e "  Try it:"
echo -e "    ${CYAN}echo '>> \"hello from HEX!\"' | hex${RESET}"
echo ""

# Quick test
if command -v hex &> /dev/null; then
    echo -e "  ${GREEN}Verification:${RESET}"
    echo '>> "HEX is installed and working!"' | hex
    echo ""
fi
