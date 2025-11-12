# Enhance flex decoders with LUA

## TL;DR

You can provide two LUA functions to be used by a flex decoder. These are `validate` which takes a single argument
corresponding to the `bitbuffer` and returns whether it is valid. It might check a CRC for example.

The `encode` function takes the `bitbuffer` argument and adds extra items to the decoded packet. This can be
used when the `get` argument is insufficient.

## Building

This extension is only built if the LUA 5.4 development environment is installed.

## Configuration -- LUA file

The flex decoder definition now takes an extra `lua` argument whose value is a filename containing the LUA code. If there
are multiple flex decoders, each of them can have their own individual files. If the LUA code file contains `require`
statements, then care should be taken as to the working directory of the `rtl_433` daemon. It may make sense to make
this the `/etc/rtl_433` directory.

It may prove easier to develop the LUA code by referencing a file, and then use the `luacode` option if the decoder
definition is to be distributed (i.e. it is all in a single file).

## Configuration -- inline LUA code

The flex decoder definition now takes an extra `luacode` argument whose value is the LUA code to run. Note that some
careful escaping is required. In particular, the `decoder` must be declared using the { } syntax:

```
decoder {
    name=wierdthing,
    <other arguments>,
    luacode=[[
    ]]
}
```

Note that the closing `}` and `]]` must be at the end of lines. The `}` requirement poses some problems for LUA code
as the phrase `local result = {}` which would otherwise close the `{`. However, this line can be modified to read `local result = {} --`
and this solves the quoting issue (by adding an empty LUA comment).
If the LUA code file contains `require`
statements, then care should be taken as to the working directory of the `rtl_433` daemon. It may make sense to make
this the `/etc/rtl_433` directory. In any case, it means that distributing the decoder definition now requires the externally
required file to be distributed as well.


## `bitbuffer` argument

Both functions are passed a single table containing the main parts of the `bitbuffer`. In particular, the value passed
is an array of tables. There is one inner table for each row in the signal received. Each inner table contains two keys:

* `len` -- the bit length of the message
* `data` -- the actual message. Note that this is binary characters and NOT hex.

## `validate` details

This function is passed the `bitbuffer` argument and returns one of two options:

* `bool` indicating whether the data is valid (true) or not (false).
* `table` containing the valid components of the signal in the same format as the `bitbuffer` argument

If there are no rows in the returned table, or the boolean is false, then the validation is treated as
failed.

### Example

Say we have a device which transmits 16 bits where the xor of the first and second bytes is 0xff.

```
function validate(data)
  local result = {}
  for _, value in ipairs(data) do
    if value.len >= 16 and value.len <= 18 then
      if string.byte(value.data, 1) ^ string.byte(value.data, 2) == 255 then
        value.len = 16
        table.insert(result, value)
      end
    end
  end
  return result
end
```

## `encode` details

This function is passed the `bitbuffer` argument and returns a table with key/values that are added
to the data object that represents the signal detection. This allows specific decoding that cannot
be performed by the `get` decoder argument.

### Example

Say we have a device that transmits the 16 bits above, where the first byte is mapped to a function on the remote control:

```
local keyMap = {
  [0x32] = "off",
  [0x33] = "+",
  [0x34] = "up",
  [0x35] = "on",
  [0x36] = "-",
  [0x37] = "down"
}

function encode(data)
  res = {}
  res.key = keyMap[string.byte(data[1].data)] or string.byte(data[1].data)
  return res
end
```

## Debugging

When writing these scripts, you can use the `print` command to put diagnostic information out to the console.

If the script fails to parse, then the error message will be printed, and the `rtl_433` program will not start.
