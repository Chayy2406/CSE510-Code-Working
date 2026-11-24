#!/usr/bin/env bash
# One-command pipeline (SPEC.md section 12): build (Release) -> run the
# unit tests -> for every config in CONFIG_DIR (generate data, build
# indices/statistics, run every plan, oracle-check, write a CSV, make a
# graph and table) -> a final make_plots.py pass over every CSV
# together. Exits non-zero if any oracle check fails.
#
# Usage: ./scripts/run_all.sh [config_dir]   (default: configs)
#   configs/smoke.json alone runs in well under a minute and is meant
#   for testing this pipeline itself — see PLAN.md Phase 9 and
#   SPEC.md section 12.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

CONFIG_DIR="${1:-configs}"

echo "== Build (Release) =="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

echo
echo "== Unit tests =="
ctest --test-dir build --output-on-failure

# Run configs in a deterministic, sensible order: the fast pipeline
# smoke test first (fails fast), then the Phase 6 showcase, then the
# six experiments, then anything else found.
ORDERED=()
for name in smoke qa_methods e1 e2 e3 e4 e5 e6; do
  f="${CONFIG_DIR}/${name}.json"
  [ -e "$f" ] && ORDERED+=("$f")
done
for f in "${CONFIG_DIR}"/*.json; do
  [[ " ${ORDERED[*]:-} " == *" $f "* ]] || ORDERED+=("$f")
done

FAIL=0
for cfg in "${ORDERED[@]}"; do
  echo
  echo "== Experiment: ${cfg} =="
  if ! python3 scripts/run_experiments.py "${cfg}"; then
    echo "run_all.sh: ${cfg} FAILED" >&2
    FAIL=1
  fi
done

if ls results/*.csv >/dev/null 2>&1; then
  echo
  echo "== make_plots.py summary over all results =="
  python3 scripts/make_plots.py results/*.csv --figures-dir figures || FAIL=1
fi

echo
if [ "$FAIL" -ne 0 ]; then
  echo "run_all.sh: FAILED (see above for which config/oracle check)" >&2
  exit 1
fi
echo "run_all.sh: all configs in ${CONFIG_DIR} passed"
