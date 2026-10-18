#!/usr/bin/env bash
# Install/verify the open-source IC toolchain used by CloverNPU.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

need_brew=()
for tool in verilator iverilog yosys; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        need_brew+=("$tool")
    fi
done

if ((${#need_brew[@]})); then
    if ! command -v brew >/dev/null 2>&1; then
        echo "error: missing tools (${need_brew[*]}) and Homebrew is not installed" >&2
        exit 1
    fi
    echo "Installing Homebrew packages: ${need_brew[*]}"
    HOMEBREW_NO_AUTO_UPDATE=1 brew install "${need_brew[@]}"
fi

if [[ ! -d "$ROOT/.venv" ]]; then
    echo "Creating Python virtual environment at .venv"
    python3 -m venv "$ROOT/.venv"
fi

"$ROOT/.venv/bin/python" -m pip install --upgrade pip >/dev/null
"$ROOT/.venv/bin/python" -m pip install -q -r "$ROOT/requirements.txt"

"$ROOT/scripts/check_tools.sh"
