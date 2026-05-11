#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SOURCE_DIRS=(include src tests examples benchmarks)
EXISTING_DIRS=()

for dir in "${SOURCE_DIRS[@]}"; do
  if [[ -d "${ROOT_DIR}/${dir}" ]]; then
    EXISTING_DIRS+=("${ROOT_DIR}/${dir}")
  fi
done

if [[ "${#EXISTING_DIRS[@]}" -eq 0 ]]; then
  echo "No source directories found."
  exit 0
fi

mapfile -t FILES < <(
  find "${EXISTING_DIRS[@]}" -type f \
    \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
    -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) \
    | sort
)

if [[ "${#FILES[@]}" -eq 0 ]]; then
  echo "No source files found."
  exit 0
fi

CLANG_FORMAT_BIN="${CLANG_FORMAT:-clang-format}"

if ! command -v "${CLANG_FORMAT_BIN}" >/dev/null 2>&1; then
  echo "clang-format not found. Install clang-format or set CLANG_FORMAT." >&2
  exit 127
fi

"${CLANG_FORMAT_BIN}" -i --style=Google "${FILES[@]}"

echo "Formatted ${#FILES[@]} source files with Google Style."
