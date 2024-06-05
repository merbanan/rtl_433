#!/bin/bash

# This script is for internal CI use only

set -e
set -x

# prefer GNU commands
realpath=$(command -v grealpath || :)
realpath="${realpath:-realpath}"

# remove this script name and two dir levels to get the source root
source_dir=$(dirname $(dirname $(dirname $($realpath -s $0))))

cd "${source_dir}/.."
[ -e rtl_433_tests ] || git clone --depth 1 https://github.com/merbanan/rtl_433_tests.git
cd rtl_433_tests
export PATH="${source_dir}/build/src:$PATH"
test -f "${source_dir}/build/src/rtl_433"

# python3 -m venv .venv
# source .venv/bin/activate
# pip install deepdiff
make test
