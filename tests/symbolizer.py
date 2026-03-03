#!/usr/bin/env python3

"""Report all symbols and docs in decoders of rtl_433 as json."""

# from ../include/rtl_433_devices.h
#     DECL(silvercrest) \
#
# static char const *const output_fields_EG53MA4[] = {
#         "model",
#         "type",
#         "id",
#         "flags",
#         "pressure_kPa",
#         "temperature_F",
#         "mic",
#         NULL,
# };
#
# r_device const schraeder = {
#         .name        = "Schrader TPMS",
#         .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
#         .short_width = 120,
#         .long_width  = 0,
#         .sync_width  = 0,
#         .gap_limit   = 0,
#         .reset_limit = 480,
#         .decode_fn   = &schraeder_callback,
#         .disabled    = 0,
#         .fields      = output_fields,
# };

import sys
import os
from os import listdir
from os.path import join, isfile, isdir, getsize
import fnmatch
import json
import datetime
import re

errout = sys.stderr
haserr = False


def log(s):
    print(s, file=errout)


def err(s):
    global haserr
    haserr = True
    print(s, file=errout)


def process_protocols(path):
    """Extract protocol numbers from a decl file."""
    protocols = []

    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f.readlines():
            #    DECL(prologue)
            m = re.match(r'\s*DECL\s*\(\s*([^\)]*)\s*\)', line)
            if m:
                pName = m.group(1)
                protocols.append(pName)

    return protocols


def update_links(links, rName, name, i, key, param):
    if not rName:
        err(f"::error file={name},line={i}::Key without r_device ({key}: {param})")
    links[rName].update({key: param})


def process_source(path, name):
    """Extract symbols and documentation from a decoder file."""
    links = {}
    links[name] = {"src": name, "line": 1, "type": "file"}
    with open(join(path, name), encoding='utf-8', errors='replace') as f:
        fName = None
        fLine = None
        rName = None
        captureDoc = False
        fileDoc = False
        dLine = None
        dSee = None
        dSee2 = None
        doc = None
        for i, line in enumerate(f):
            # look for documentation comments:
            # /** @file ... */
            # /** @fn ... */
            m = re.match(r'\s*\*/', line)
            if captureDoc and m:
                captureDoc = False
                if fileDoc:
                    links[name].update({"doc_line": dLine, "doc": doc})
                    fileDoc = False
                    doc = None
                if fName:
                    if fName not in links:
                        links[fName] = {"src": name, "type": "func"}
                    if dSee:
                        links[fName].update({"doc_line": dLine, "doc_see": dSee})
                        dSee = None
                    links[fName].update({"doc_line": dLine, "doc": doc})
                    doc = None
                    fName = None
                continue
            if captureDoc:
                doc += line
                # TODO: we should use a list
                m = re.match(r'\s*\@sa\s+(.*?)\(\)\s*(?:(.*?)\(\))?', line)
                if m:
                    dSee = m.group(1)
                    if m.group(2):
                        dSee2 = m.group(2)
                continue
            # inline link /** @sa func() */
            m = re.match(r'\s*/\*\*\s*\@sa\s+(.*?)\(\)\s*\*/', line)
            if m:
                dLine = i + 1
                dSee = m.group(1)
                continue
            # inline /** ... */
            m = re.match(r'\s*/\*\*\s*(.*?)\s*\*/', line)
            if m:
                dLine = i + 1
                doc = m.group(1)
                continue
            # copyright /** @file ... */
            m = re.match(r'\s*/\*\*\s*@file', line)
            if m:
                captureDoc = True
                fileDoc = True
                dLine = i + 1
                doc = ''
                continue
            # /** @fn ... */
            m = re.match(
                r'\s*/\*\*\s*@fn\s+(?:\s*static\s*)?(?:\s*int\s*)?([a-zA-Z0-9_]+)\(\s*r_device\s+\*\s*[a-z]+\s*,\s*bitbuffer_t\s+\*\s*[a-z]+', line)
            if m:
                fName = m.group(1)
                captureDoc = True
                dLine = i + 1
                doc = ''
                continue
            m = re.match(r'\s*/\*\*', line)
            if m:
                fName = None
                captureDoc = True
                dLine = i + 1
                doc = ''
                continue

            # look for char * variable = "" declarations that should really be char const *
            m = re.match(r'(?:static)?\s?char\s?\*\s?[^\*\s]+\s?=\s?(?:{|")', line)
            if m:
                err(f"::error file={name},line={i}::char * variable should be char const * when being given a constant value")

            # look for output fields declarations that must be static char const *const
            m = re.match(r'static\s?char\s+const\s*\*\s*[^\*\s]+\[\]\s*=\s*\{', line)
            if m:
                err(f"::error file={name},line={i}::output fields static variable should be 'static char const *const'")

            # look for r_device with decode_fn
            m = re.match(r'\s*r_device\s+const\s+([^\*]*?)\s*=', line)
            if m:
                rName = m.group(1)
                if rName in links:
                    err(f"::error file={name},line={i}::Duplicate r_device ({rName})")
                links[rName] = {"src": name, "line": i + 1, "type": "r_device"}
                if dSee:
                    links[rName].update({"doc_line": dLine, "doc_see": dSee})
                    dSee = None
                if doc:
                    links[rName].update({"doc_line": dLine, "doc": doc})
                    doc = None
                continue
            # .name        = "The Name",
            m = re.match(r'\s*\.name\s*=\s*"([^"]*)', line)
            if m:
                update_links(links, rName, name, i, 'name', m.group(1))
                continue
            # .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
            m = re.match(r'\s*\.modulation\s*=\s*([^,\s]*)', line)
            if m:
                update_links(links, rName, name, i, 'modulation', m.group(1))
                continue
            # .short_width = 120,
            m = re.match(r'\s*\.short_width\s*=\s*([^,\s]*)', line)
            if m:
                update_links(links, rName, name, i, 'short_width', m.group(1))
                continue
            # .long_width  = 0,
            m = re.match(r'\s*\.long_width\s*=\s*([^,\s]*)', line)
            if m:
                update_links(links, rName, name, i, 'long_width', m.group(1))
                continue
            # .sync_width  = 0,
            m = re.match(r'\s*\.sync_width\s*=\s*([^,\s]*)', line)
            if m:
                update_links(links, rName, name, i, 'sync_width', m.group(1))
                continue
            # .gap_limit   = 0,
            m = re.match(r'\s*\.gap_limit\s*=\s*([^,\s]*)', line)
            if m:
                update_links(links, rName, name, i, 'gap_limit', m.group(1))
                continue
            # .reset_limit = 480,
            m = re.match(r'\s*\.reset_limit\s*=\s*([^,\s]*)', line)
            if m:
                update_links(links, rName, name, i, 'reset_limit', m.group(1))
                continue
            # .decode_fn   = &the_callback,
            m = re.match(r'\s*\.decode_fn\s*=\s*&([^,\s]*)', line)
            if m:
                update_links(links, rName, name, i, 'decode_fn', m.group(1))
                continue
            # .disabled    = 0,
            m = re.match(r'\s*\.disabled\s*=\s*([^,\s]*)', line)
            if m:
                update_links(links, rName, name, i, 'disabled', m.group(1))
                continue

            # static int foo_callback(r_device *decoder, bitbuffer_t *bitbuffer)
            # static int foo_callback(r_device *decoder, bitbuffer_t *bitbuffer, ...
            # static int
            # foo_callback(r_device *decoder, bitbuffer_t *bitbuffer)
            m = re.match(
                r'(?:\s*static\s*int\s*)?([a-zA-Z0-9_]+)\(\s*r_device\s+\*\s*[a-z]+\s*,\s*bitbuffer_t\s+\*\s*[a-z]+', line)
            if m:
                # print(m.group(1))
                fName = m.group(1)
                fLine = i + 1
                if fName not in links:
                    links[fName] = {}
                links[fName].update({"src": name, "line": fLine, "type": "func"})
                if dSee:
                    links[fName].update({"doc_line": dLine, "doc_see": dSee})
                    dSee = None
                if dSee2:
                    links[fName].update({"doc_line": dLine, "doc_see2": dSee2})
                    dSee2 = None
                if doc:
                    links[fName].update({"doc_line": dLine, "doc": doc})
                    doc = None
                continue

            # Match the data outputs
            # "volume_m3", "Volume", DATA_COND, foo, DATA_FORMAT, "%.1f m3", DATA_DOUBLE, wread,
            # DATA_DATA | DATA_INT | DATA_DOUBLE | DATA_STRING | DATA_ARRAY
            # data_int | data_dbl | data_str | data_ary | data_dat | data_hex
            if re.match(r'\s*//*', line):
                # skip comment
                pass
            elif fName and re.match(r'.*\b(?:data_int|data_dbl|data_str|data_ary|data_dat|data_hex)\(.*', line):
                m = re.match(r'.*\bdata_(int|dbl|str|ary|dat|hex)\(.*?,\s*"(.*?)"\s*,\s*"(.*?)"\s*,\s*(?:NULL|"(.*?)")\s*,\s*(?:"(.*?)")?.*\);.*', line)
                if m:
                    # type, subtype, and model should be fixed string values
                    d_type = m.group(1)
                    d_key = m.group(2)
                    d_pretty = m.group(3)
                    d_cond = None
                    d_format = m.group(4)
                    d_value = m.group(5)
                    if d_key == 'type' or d_key == 'model':
                        if d_type != 'str' or d_value is None:
                            log(f"::notice file={name},line={i + 1}::DATA line bad format")

                    if "outputs" not in links[fName]:
                        links[fName]["outputs"] = []

                    links[fName]["outputs"].append({
                        "type": d_type,
                        "key": d_key,
                        "pretty": d_pretty,
                        "cond": d_cond,
                        "format": d_format,
                        "value": d_value, # capture value for "model"
                    })
                else:
                    # Complain if a line could not be parsed
                    log(f"::notice file={name},line={i + 1}::DATA line did not parse")
            elif re.match(r'.*\b(DATA_COND|DATA_FORMAT|DATA_DATA|DATA_INT|DATA_DOUBLE|DATA_STRING|DATA_ARRAY)\b.*', line):
                typemap = {
                    "DATA_DATA": "dat",
                    "DATA_INT": "int",
                    "DATA_DOUBLE": "dbl",
                    "DATA_STRING": "str",
                    "DATA_ARRAY": "ary",
                }
                m = re.match(r'\s*"(.*?)"\s*,\s*"(.*?)"\s*,\s*(?:DATA_COND\s*,\s*(.*?)\s*,\s*)?(?:DATA_FORMAT\s*,\s*(.*?)\s*,\s*)?(DATA_DATA|DATA_INT|DATA_DOUBLE|DATA_STRING|DATA_ARRAY)\s*,\s*(?:"(.*?)")?.*', line)
                if m:
                    # type, subtype, and model should be fixed string values
                    d_key = m.group(1)
                    d_pretty = m.group(2)
                    d_cond = m.group(3)
                    d_format = m.group(4)
                    d_type = typemap[m.group(5)]
                    d_value = m.group(6)
                    if d_key == 'type' or d_key == 'model':
                        if d_type != 'str' or d_value is None:
                            log(f"::notice file={name},line={i + 1}::DATA line bad format")

                    if "outputs" not in links[fName]:
                        links[fName]["outputs"] = []

                    links[fName]["outputs"].append({
                        "key": d_key,
                        "pretty": d_pretty,
                        "cond": d_cond,
                        "format": d_format,
                        "type": d_type,
                        "value": d_value, # capture value for "model"
                    })
                else:
                    # Complain if a line could not be parsed
                    log(f"::notice file={name},line={i + 1}::DATA line did not parse")

            # Match the prefix string up to "TheModelName"
            # "model", "", DATA_STRING, "Schrader",
            m = re.match(r'\s*"model"\s*,.*DATA_STRING', line)
            if not m:
                # data_t *data = data_str(NULL, "model", "", NULL, "Honeywell-CM921");
                m = re.match(r'.*data_str\([^,]*\s*,\s*"model"\s*,[^,]*,[^,]*,', line)
            if m:
                prefix = m.group(0)
                s = line[len(prefix):]
                models = re.findall(r'"([^"]+)"', s)
                if len(models) == 0:
                    err(f"::error file={name},line={i + 1}::No models")
                if not fName:
                    err(f"::error file={name},line={i + 1}::No func")
                for model in models:
                    if not re.match(r'^[A-Za-z][0-9A-Za-z"]+(-[0-9A-Za-z"]+)?$', model):
                        log(f"::error file={name},line={i + 1}::Bad model name \"{model}\"")
                    if model in links and links[model]["func"] != fName:
                        log(f"::notice file={name},line={i + 1}::Reused model")
                    elif model in links:
                        log(f"::notice file={name},line={i + 1}::Duplicate model")
                    links[model] = {"src": name, "line": i + 1, "type": "model", "func": fName}

    if captureDoc:
        err(f"::error file={name},line={dLine}::Unclosed doc comment")
    if dSee:
        err(f"::error file={name},line={dLine}::Unattached doc sa")
    if doc:
        err(f"::error file={name},line={dLine}::Unattached doc comment")

    return links


def check_symbols(symbols):
    """Check link integrity."""
    models_by_func = {}
    for f in symbols:
        d = symbols[f]

        if f == "protocols":
            continue

        if d["type"] == "file":
            if "doc" not in d:
                log(f"::notice file={f},line=1::file doc missing")
                pass

        if d["type"] == "r_device":
            if "decode_fn" not in d:
                err(f"::error file={d['src']},line={d['line']}::device missing ({json.dumps(d)}) for {f}")
            elif d["decode_fn"] not in symbols:
                err(f"::error file={d['src']},line={d['line']}::decoder missing ({d['decode_fn']}) for {f}")

        if d["type"] == "func":
            if "line" not in d:
                err(f"::error file={d['src']}::func missing ({f})")
            if "doc" not in d or not d["doc"]:
                if "doc_see" not in d or not d["doc_see"]:
                    err(f"::warning file={d['src']},line={d['line']}::doc missing for {f}")
                pass

        if d["type"] == "model":
            func = d["func"]
            if func not in models_by_func:
                models_by_func[func] = []
            models_by_func[func].append(f)

    for f in symbols:
        d = symbols[f]

        if f == "protocols":
            continue

        if d["type"] == "r_device":
            if "decode_fn" not in d:
                err(f"::error file={d['src']},line={d['line']}::no decode_fn found for {f}")
                continue
            decode_fn = d["decode_fn"]
            func = {}
            if decode_fn in symbols:
                func = symbols[decode_fn]
            else:
                err(f"::error file={d['src']},line={d['line']}::decode_fn not found ({decode_fn}) for {f}")
            see = None
            if "doc_see" in func:
                see = func["doc_see"]
                if see not in symbols:
                    err(f"::error file={d['src']},line={d['line']}::broken link @sa ({see}) for {f}")

            if see and see in models_by_func:
                # err(f"::error file={d['src']},line={d['line']}::models on sa link ({see}) for {f}")
                pass
            elif decode_fn not in models_by_func:
                err(f"::error file={d['src']},line={d['line']}::models not found for {f}")
                if see:
                    err(f"::error file={d['src']},line={d['line']}::but @sa ({func['doc_see']})")


def main(args):
    """Scan basedir for all groups, devices, sets, and content."""

    # ../include/rtl_433_devices.h
    #    DECL(prologue)

    check = "check" in args
    if check:
        args.remove("check")
        errout = sys.stdout
    root = (['.'] + args)[-1]
    basedir = root + '/src/devices/'
    declpath = root + '/include/rtl_433_devices.h'

    symbols = {}

    symbols['protocols'] = process_protocols(declpath)

    for f in listdir(basedir):
        if f.endswith('.c'):
            symbols.update(process_source(basedir, f))

    check_symbols(symbols)
    if check:
        return haserr
    else:
        # print(symbols)
        # print(json.dumps(symbols, indent=2))
        print(json.dumps(symbols))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
