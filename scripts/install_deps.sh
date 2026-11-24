#!/usr/bin/env bash
# Checks for the tools this project needs and installs the one
# dependency that's actually optional-but-missing (matplotlib). Does
# not install a C++ compiler, CMake, or Python 3 itself — those are
# expected to already be present (Xcode Command Line Tools on macOS,
# build-essential + cmake on Linux).
set -euo pipefail

missing=0

check() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "MISSING: $1 — $2" >&2
    missing=1
  else
    echo "OK: $1 ($("$1" --version 2>&1 | head -1))"
  fi
}

check cmake "install via Homebrew (macOS): brew install cmake; or apt: sudo apt install cmake"
check python3 "install via Homebrew (macOS): brew install python3; or apt: sudo apt install python3"

if command -v c++ >/dev/null 2>&1; then
  echo "OK: c++ ($(c++ --version 2>&1 | head -1))"
elif command -v g++ >/dev/null 2>&1; then
  echo "OK: g++ ($(g++ --version 2>&1 | head -1))"
else
  echo "MISSING: a C++17 compiler — install Xcode Command Line Tools (macOS: xcode-select --install) or g++ (apt: sudo apt install g++)" >&2
  missing=1
fi

if python3 -c "import matplotlib" >/dev/null 2>&1; then
  echo "OK: matplotlib ($(python3 -c 'import matplotlib; print(matplotlib.__version__)'))"
else
  echo "Installing matplotlib..."
  python3 -m pip install --user matplotlib
fi

if [ "$missing" -ne 0 ]; then
  echo "install_deps.sh: one or more required tools are missing (see MISSING lines above)" >&2
  exit 1
fi
echo "install_deps.sh: all dependencies present"
