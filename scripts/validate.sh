#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repository_dir"

mode="${1:-all}"
target="${2:-}"

usage() {
  cat <<'EOF'
Usage: ./scripts/validate.sh [all|code-quality|build|build-target <target-id>]

Modes:
  code-quality  Run static checks and protocol tests.
  build         Compile all CI firmware targets.
  build-target  Compile only one CI firmware target.
  all           Run code-quality and build (default).

Build target IDs:
  esp32-idf
  esp32s3-idf
  esp32-arduino
  esp32-multi-idf
EOF
}

require_commands() {
  local missing_commands=()
  local command_name
  for command_name in "$@"; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
      missing_commands+=("$command_name")
    fi
  done
  if ((${#missing_commands[@]} != 0)); then
    echo "Missing required commands: ${missing_commands[*]}" >&2
    return 1
  fi
}

require_pytest() {
  if ! python -c "import pytest" >/dev/null 2>&1; then
    echo "Missing Python module: pytest" >&2
    return 1
  fi
}

cpp_files=(
  components/allpowers_ble/*.h
  components/allpowers_ble/*.cpp
  tests/test_protocol.cpp
)

run_code_quality() {
  require_commands \
    clang-format \
    cpplint \
    flake8 \
    g++ \
    pylint \
    python \
    ruff \
    yamllint
  require_pytest

  echo "==> Python syntax"
  python -m compileall -q components tests

  echo "==> Ruff lint"
  ruff check components tests

  echo "==> Ruff format"
  ruff format --check components tests

  echo "==> Flake8"
  flake8 components tests

  echo "==> Pylint"
  pylint components/allpowers_ble tests/test_protocol.py

  echo "==> ClangFormat"
  clang-format --dry-run --Werror "${cpp_files[@]}"

  echo "==> CppLint"
  cpplint "${cpp_files[@]}"

  echo "==> YAML lint"
  yamllint \
    .github/workflows \
    packages \
    examples \
    tests/*.yaml \
    home_assistant/allpowers_ble_controls.yaml.example

  echo "==> Native C++ tests"
  native_test_binary="$(mktemp /tmp/allpowers-ble-protocol-tests.XXXXXX)"
  trap 'rm -f "$native_test_binary"' EXIT
  g++ \
    -std=c++17 \
    -Wall \
    -Wextra \
    -Werror \
    -pedantic \
    -Icomponents/allpowers_ble \
    components/allpowers_ble/allpowers_ble_protocol.cpp \
    tests/test_protocol.cpp \
    -o "$native_test_binary"
  "$native_test_binary"

  echo "==> Python tests"
  python -m pytest tests
}

compile_firmware() {
  local label="$1"
  local board="$2"
  local device_name="$3"
  local framework="$4"
  local configuration="$5"

  echo "==> ESPHome compile: $label"
  esphome \
    -s test_board "$board" \
    -s test_device_name "$device_name" \
    -s test_framework "$framework" \
    compile "$configuration"
}

run_build() {
  require_commands esphome

  if [[ -n "$1" ]]; then
    case "$1" in
      esp32-idf)
        compile_firmware \
          "ESP32 (ESP-IDF)" \
          esp32dev \
          allpowers-ble-ci-esp32 \
          esp-idf \
          tests/ci.yaml
        ;;
      esp32s3-idf)
        compile_firmware \
          "ESP32-S3 (ESP-IDF)" \
          esp32-s3-devkitc-1 \
          allpowers-ble-ci-esp32-s3 \
          esp-idf \
          tests/ci.yaml
        ;;
      esp32-arduino)
        compile_firmware \
          "ESP32 (Arduino)" \
          esp32dev \
          allpowers-ble-ci-arduino \
          arduino \
          tests/ci.yaml
        ;;
      esp32-multi-idf)
        compile_firmware \
          "ESP32 dual station (ESP-IDF)" \
          esp32dev \
          allpowers-ble-ci-multi \
          esp-idf \
          tests/ci-multi.yaml
        ;;
      *)
        echo "Unknown build target: $1" >&2
        usage >&2
        return 2
        ;;
    esac
    return
  fi

  compile_firmware \
    "ESP32 (ESP-IDF)" \
    esp32dev \
    allpowers-ble-ci-esp32 \
    esp-idf \
    tests/ci.yaml

  compile_firmware \
    "ESP32-S3 (ESP-IDF)" \
    esp32-s3-devkitc-1 \
    allpowers-ble-ci-esp32-s3 \
    esp-idf \
    tests/ci.yaml

  compile_firmware \
    "ESP32 (Arduino)" \
    esp32dev \
    allpowers-ble-ci-arduino \
    arduino \
    tests/ci.yaml

  compile_firmware \
    "ESP32 dual station (ESP-IDF)" \
    esp32dev \
    allpowers-ble-ci-multi \
    esp-idf \
    tests/ci-multi.yaml
}

case "$mode" in
  code-quality)
    run_code_quality
    ;;
  build)
    run_build
    ;;
  build-target)
    if [[ -z "$target" ]]; then
      echo "Missing build target id for 'build-target' mode." >&2
      usage >&2
      exit 2
    fi
    run_build "$target"
    ;;
  all)
    run_code_quality
    run_build
    ;;
  -h|--help|help)
    usage
    exit 0
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

echo "Validation mode '$mode' passed."
