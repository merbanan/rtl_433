#!/usr/bin/env node
/** @file
    Source code style checks.

    Copyright (C) 2020 by Christian Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

const path = require('path')
const fs = require('fs')
const log = console.error
const MAX_LEN = 550 // this should be around 120, but idm.c breaks this.

// console.log(process.cwd()) // /home/runner/work/rtl_433/rtl_433
// console.log(__dirname) // /home/runner/work/rtl_433/rtl_433/.github/actions/style-check

process.exitCode = [
  'include',
  'src',
  'src/devices',
].flatMap(glob_dir).concat([
  'CMakeLists.txt',
  'conf/CMakeLists.txt',
  'tests/CMakeLists.txt',
])
  .filter(discard_vendor)
  .reduce((e, f) => e + style_check(f), 0) > 0

function discard_vendor(filename) {
  return filename.indexOf('/mongoose.') < 0
}

function glob_dir(dir) {
  return fs.readdirSync(dir)
    .filter(filename =>
      filename.endsWith('.h') ||
      filename.endsWith('.c') ||
      filename.endsWith('.txt')
    )
    .map(filename =>
      path.join(dir, filename)
    )
}

function style_check(filename) {
  const strict = filename.indexOf('/devices/') >= 0

  let read_errors = 0
  let long_errors = 0
  let crlf_errors = 0
  let tabs_errors = 0
  let trailing_errors = 0
  let memc_errors = 0
  let funbrace_errors = 0

  let leading_tabs = 0
  let leading_spcs = 0

  let use_stdout = 0
  let use_printf = 0

  let need_cond = 0
  let line_number = 0

  fs.readFileSync(filename, 'ascii')
    .split('\n')
    .map(line => {
      line_number++
      const len = line.length
      if (len < 0) {
        log(`::error file=${filename},line=${line_number},col=${len-1}::READ error`)
        read_errors++
      }
      if (len >= MAX_LEN - 1) {
        log(`::error file=${filename},line=${line_number},col=${len-1}::LONG line error`)
        long_errors++
      }
      if (line[len - 1] == '\r' || (len > 1 && line[len - 2] == '\r')) {
        log(`::error file=${filename},line=${line_number},col=${len-1}::CRLF error`)
        crlf_errors++
      }

      if (line[0] == '\t') {
        log(`::error file=${filename},line=${line_number},col=${len - 1}::TAB indented line`)
        leading_tabs++
      }
      if (len >= 4 && line[0] == ' ' && line[1] == ' ' && line[2] == ' ' && line[3] == ' ') {
        leading_spcs++
      }
      if (line[len - 1] == ' ' || line[len - 1] == '\t') {
        log(`::error file=${filename},line=${line_number},col=${len-1}::TRAILING whitespace error`)
        trailing_errors++
      }
      if (line.indexOf('(r_device *decoder') >= 0 && line[len - 1] == '{') {
        log(`::error file=${filename},line=${line_number},col=${len-1}::BRACE function on newline error`)
        funbrace_errors++
      }

      if (strict && line.indexOf('stdout') >= 0) {
        log(`::error file=${filename},line=${line_number},col=${len - 1}::STDOUT line`)
        use_stdout++
      }
      const p = line.indexOf('printf')
      if (strict && p >= 0) {
        if (p == 0 || line[p-1] < '_' || line[p-1] > 'z') {
          log(`::error file=${filename},line=${line_number},col=${len - 1}::PRINTF line`)
          use_printf++
        }
      }
      if (need_cond && line.indexOf('if (!') < 0) {
        // we had an alloc but no check on the following line
        log(`::error file=${filename},line=${line_number},col=${len - 1}::ALLOC check error`)
        memc_errors++
      }
      need_cond = 0
      if (line.indexOf('alloc(') >= 0 && line.indexOf('alloc()') < 0) {
        need_cond++
      }
      if (line.indexOf('strdup(') >= 0 && line.indexOf('strdup()') < 0) {
        need_cond++
      }

    })

  if (leading_tabs && leading_spcs) {
    log(`::error file=${filename}::${tabs_errors} MIXED tab/spaces errors.`)
    tabs_errors = leading_tabs > leading_spcs ? leading_spcs : leading_tabs
  }

  return read_errors + long_errors + crlf_errors + tabs_errors + leading_tabs + trailing_errors
    + funbrace_errors + use_stdout + use_printf + memc_errors
}
