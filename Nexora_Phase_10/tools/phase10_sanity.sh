#!/usr/bin/env sh
set -eu
make clean
make crosscheck
printf '%s\n' 'Phase 10 sanity suite: PASS'
