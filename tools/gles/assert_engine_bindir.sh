#!/usr/bin/env bash
# Shared guard: GLES smokes must use build-gles/bin, not legacy debug/bin artifacts.
assert_gles_engine_bindir() {
  local build_dir="$1"
  local bindir="$2"
  local stale="$build_dir/debug/bin/engine"

  if [[ ! -x "$bindir/engine" ]]; then
    echo "Missing $bindir/engine — run: cmake --build $build_dir --target engine shaderpack" >&2
    exit 1
  fi

  if [[ -x "$stale" ]]; then
    echo "WARN: legacy $stale exists (often stale)" >&2
    echo "      Smokes use $bindir/engine only — remove debug/bin or ignore if unused." >&2
  fi
}
