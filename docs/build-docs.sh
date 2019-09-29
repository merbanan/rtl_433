#!/bin/sh

# abort on errors
set -e

# navigate to the docs directory
cd ${0%/*}

# copy other docs
sed 's/docs\///' ../README.md >README.md
cp ../CHANGELOG.md .
cp ../rtl_433_tests/README.md TESTS.md
cp ../vs15/README.md VS15.md

# build
yarn install
yarn docs:build
