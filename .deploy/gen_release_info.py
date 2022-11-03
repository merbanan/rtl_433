#!/usr/bin/env python

with open('CHANGELOG.md') as r, open('RELEASEINFO.md', 'w') as w:
    # skip over possible 'Unreleased'
    for line in r:
        if line.startswith('## Release'):
            # w.write(line[1:])
            break
    for line in r:
        if line.startswith('## '):
            break
        # if line.startswith('#'):
        #     line = line[1:]
        w.write(line)
