#!/bin/sh

# execute command and set exit code 1 if there is output on stdout or stderr
out=$($@ 2>&1)
echo "$out"
[ -z "$out" ]
