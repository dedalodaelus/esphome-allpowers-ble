#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repository_dir"

required_commands=(
  clang-format
  cpplint
  esphome
  flake8
  g++
  pylint
  python
  ruff
  yamllint
)

missing_commands=()
for command_name in "${required_commands[@]}"; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    missing_commands+=("$command_name")
  fi
done

if ((${#missing_commands[@]} != 0)); then
  echo "Missing validation commands: ${missing_commands[*]}" >&2
  echo "Install requirements-lint.txt and requirements-ci.txt, then retry." >&2
  exit 2
fi

if ! python -c "import pytest" >/dev/null 2>&1; then
  echo "Missing Python module: pytest" >&2
  echo "Install requirements-lint.txt, then retry." >&2
  exit 2
fi

cpp_files=(
  components/allpowers_ble/*.h
  components/allpowers_ble/*.cpp
  tests/test_protocol.cpp
)

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

echo "All validation checks passed."
