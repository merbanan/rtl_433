#!/bin/sh
set -e
mkdir -p build
cd build
cmake ../
make
# make install
cd ..
set -x
git clone https://github.com/merbanan/rtl_433_tests.git
cd rtl_433_tests
export PATH=../build/src:$PATH
test -f ../build/src/rtl_433

# virtualenv --system-site-packages .venv
# source .venv/bin/activate
# pip install deepdiff
make test
