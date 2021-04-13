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
const MAX_LEN = 300 // this should likely be around 160
const replacement_character = '\uFFFD'

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
  return filename.indexOf('/jsmn.') < 0 && filename.indexOf('/mongoose.') < 0
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
  const txt = filename.endsWith('.txt')

  let errors = 0

  let leading_tabs = 0
  let leading_spcs = 0
  let mixed_ws = 0
  let need_cond = 0
  let in_comment = false

  let line_number = 0

  fs.readFileSync(filename, 'utf8')
    .split('\n')
    .map(line => {
      line_number++
      const len = line.length
      if (len < 0) {
        log(`::error file=${filename},line=${line_number}::READ error`)
        errors++
      }
      if (len >= MAX_LEN - 1) {
        log(`::error file=${filename},line=${line_number},col=${MAX_LEN}::LONG line error`)
        errors++
      }
      if (line[len - 1] == '\r' || (len > 1 && line[len - 2] == '\r')) {
        log(`::error file=${filename},line=${line_number},col=${len - 1}::CRLF error`)
        errors++
      }
      if (line.indexOf('/*')) {
        in_comment = true
      }

      if (line[0] == '\t') {
        log(`::error file=${filename},line=${line_number},col=0::TAB indented line`)
        leading_tabs++
      }
      if (len >= 4 && line[0] == ' ' && line[1] == ' ' && line[2] == ' ' && line[3] == ' ') {
        leading_spcs++
      }
      if (line[len - 1] == ' ' || line[len - 1] == '\t') {
        log(`::error file=${filename},line=${line_number},col=${len - 1}::TRAILING whitespace error`)
        errors++
      }

      const invchr = line.indexOf(replacement_character)
      if (invchr >= 0) {
        log(`::error file=${filename},line=${line_number},col=${invchr + 1}::INVALID-UTF8 character error`)
        errors++
      }
      const nonasc = line.search(/[^ -~]/)
      if (!in_comment && nonasc >= 0) {
        log(`::error file=${filename},line=${line_number},col=${nonasc + 1}::NON-ASCII character error`)
        errors++
      }
      else if (nonasc >= 0) {
        //log(`::warning file=${filename},line=${line_number},col=${nonasc + 1}::NON-ASCII character`)
      }
      if (line.indexOf('(r_device *decoder') >= 0 && line[len - 1] == '{') {
        log(`::error file=${filename},line=${line_number},col=${len - 1}::BRACE function on newline error`)
        errors++
      }

      if (line.indexOf('){') >= 0 && line.indexOf('}') < 0) {
        log(`::error file=${filename},line=${line_number},col=${len - 1}::STICKY-BRACE error`)
        errors++
      }
      if (!txt && (line.indexOf('if(') >= 0 || line.indexOf('for(') >= 0 || line.indexOf('while(') >= 0)) {
        log(`::error file=${filename},line=${line_number},col=${len - 1}::STICKY-PAREN error`)
        errors++
      }

      if (strict && line.indexOf('stdout') >= 0) {
        log(`::error file=${filename},line=${line_number},col=${len - 1}::STDOUT line`)
        errors++
      }
      const p = line.indexOf('printf')
      if (strict && p >= 0) {
        if (p == 0 || line[p - 1] < '_' || line[p - 1] > 'z') {
          log(`::error file=${filename},line=${line_number},col=${len - 1}::PRINTF line`)
          errors++
        }
      }
      if (need_cond && line.indexOf('if (!') < 0) {
        // we had an alloc but no check on the following line
        log(`::error file=${filename},line=${line_number},col=${len - 1}::ALLOC check error`)
        errors++
      }
      need_cond = 0
      if (line.indexOf('alloc(') >= 0 && line.indexOf('alloc()') < 0) {
        need_cond++
      }
      if (line.indexOf('strdup(') >= 0 && line.indexOf('strdup()') < 0) {
        need_cond++
      }

      if (line.indexOf('*/')) {
        in_comment = false
      }
    })

  if (leading_tabs && leading_spcs) {
    mixed_ws = leading_tabs > leading_spcs ? leading_spcs : leading_tabs
    log(`::error file=${filename}::${mixed_ws} MIXED tab/spaces errors.`)
  }

  return errors + mixed_ws + leading_tabs
}
