#!/usr/bin/env bash
# Test driver for clang_tidy_test; see clang_tidy_test.bzl for the semantics.
#
# Usage: clang_tidy_test_runner.sh CLANG_TIDY CONFIG CHECK (match|no-match) SRC [COMPILER_ARGS...]

set -uo pipefail

if [[ $# -lt 5 ]]; then
  echo "usage: $0 CLANG_TIDY CONFIG CHECK (match|no-match) SRC [COMPILER_ARGS...]" >&2
  exit 2
fi

clang_tidy=$1
config=$2
check=$3
expect=$4
src=$5
shift 5

marker="CHECK-MATCH"
tidy_args=(--quiet --experimental-custom-checks --config-file="$config" --checks="-*,$check")

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

if ! command -v "$clang_tidy" > /dev/null; then
  fail "clang-tidy binary '$clang_tidy' not found (PATH=$PATH)"
fi

# Make sure the check exists, otherwise a negative test would pass vacuously.
if ! "$clang_tidy" "${tidy_args[@]}" --list-checks | grep -qxF "    $check"; then
  "$clang_tidy" "${tidy_args[@]}" --list-checks >&2
  fail "check '$check' is not enabled by $config"
fi

output=$("$clang_tidy" "${tidy_args[@]}" "$src" -- "$@" 2>&1)

if grep -qE '\[clang-diagnostic-error\]$|^Error while processing' <<< "$output"; then
  echo "$output" >&2
  fail "$src does not compile"
fi

# Lines of $src that the check reported, one per line, sorted and unique. Diagnostics in other
# files (e.g. headers) are ignored.
actual=$(
  while IFS= read -r line; do
    [[ $line =~ ^(.*):([0-9]+):[0-9]+:\ (warning|error):\ .*\ \[([^]]*)\]$ ]] || continue
    file=${BASH_REMATCH[1]}
    [[ $file == "$src" || $file == */"$src" ]] || continue
    [[ ",${BASH_REMATCH[4]}," == *",$check,"* ]] || continue
    echo "${BASH_REMATCH[2]}"
  done <<< "$output" | sort -nu
)

expected=$(grep -nE -- "//[[:space:]]*$marker\b" "$src" | cut -d: -f1 | sort -nu)

case "$expect" in
  match)
    [[ -n $expected ]] || fail "positive test $src has no '// $marker' markers"
    ;;
  no-match)
    [[ -z $expected ]] || fail "negative test $src must not contain '// $marker' markers"
    ;;
  *)
    fail "unknown expectation '$expect' (want 'match' or 'no-match')"
    ;;
esac

if [[ $actual != "$expected" ]]; then
  echo "$output" >&2
  echo >&2
  missing=$(comm -23 <(sort <<< "$expected") <(sort <<< "$actual") | grep . | sort -n |
            sed "s|^|  $src:|")
  unexpected=$(comm -13 <(sort <<< "$expected") <(sort <<< "$actual") | grep . | sort -n |
               sed "s|^|  $src:|")
  [[ -z $missing ]] || echo -e "expected $check diagnostics missing at:\n$missing" >&2
  [[ -z $unexpected ]] || echo -e "unexpected $check diagnostics at:\n$unexpected" >&2
  fail "$check diagnostics don't match expectations in $src"
fi

count=$(grep -c . <<< "$actual")
echo "PASS: $check: $expect ($count matching lines) in $src"
