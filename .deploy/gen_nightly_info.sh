#!/bin/bash

git log $(git describe --tags --abbrev=0 HEAD^)..HEAD --oneline |egrep -v 'minor:|build:|docs:' |sed -e 's/[^ ]*/-/' >RELEASEINFO.md
