#!/usr/bin/env bash
# Print tool versions and fail if a required tool is missing.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="$ROOT/.venv/bin/python"
fail=0

check() {
    local name="$1"
    if command -v "$name" >/dev/null 2>&1; then
        printf "  %-14s %s\n" "$name" "$(command -v "$name")"
    else
        printf "  %-14s MISSING\n" "$name"
        fail=1
    fi
}

echo "CloverNPU toolchain:"
check verilator
check iverilog
check yosys
check cmake
check ninja
check make
check git

if [[ -x "$PY" ]]; then
    printf "  %-14s %s\n" "python" "$("$PY" --version 2>&1)"
else
    printf "  %-14s MISSING (%s)\n" "python" "$PY"
    fail=1
fi

echo
echo "Versions:"
if command -v verilator >/dev/null 2>&1; then
    verilator --version | sed 's/^/  /'
fi
if command -v iverilog >/dev/null 2>&1; then
    iverilog -V 2>&1 | sed -n '1p' | sed 's/^/  /'
fi
if command -v yosys >/dev/null 2>&1; then
    yosys -V 2>&1 | sed -n '1p' | sed 's/^/  /'
fi
if command -v cmake >/dev/null 2>&1; then
    cmake --version | sed -n '1p' | sed 's/^/  /'
fi

if [[ -x "$PY" ]]; then
    "$PY" - <<'PY'
import sys
try:
    import cocotb
    print(f"  cocotb {cocotb.__version__}")
except Exception as exc:  # pragma: no cover
    print(f"  cocotb MISSING: {exc}")
    sys.exit(1)
try:
    import numpy
    print(f"  numpy {numpy.__version__}")
except Exception as exc:  # pragma: no cover
    print(f"  numpy MISSING: {exc}")
    sys.exit(1)
PY
else
    fail=1
fi

if ((fail)); then
    echo
    echo "Run ./scripts/setup.sh to install missing dependencies." >&2
    exit 1
fi

echo
echo "All required tools are present."
