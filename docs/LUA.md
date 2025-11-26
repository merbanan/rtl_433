# Enhance flex decoders with LUA

## TL;DR

You can provide two LUA functions to be used by a flex decoder. These are `validate` which takes a single argument
corresponding to the `bitbuffer` and returns whether it is valid. It might check a CRC for example.

The `decode` function takes the `bitbuffer` argument and adds extra items to the decoded packet. This can be
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
is an array of tables. There is one BitBuffer object for each row in the signal received. The BitBuffer object behaves like
a String -- all the String methods work on a BitBuffer. It has the following additional methods:

* `bitlen()` -- returns the bit length of the message
* `little_endian_buffer(le)` -- sets the byte ordering of the buffer (default big-endian)
* `little_endian_value(le)` -- sets the bit ordering of the returned values (default big-endian)
* `signed(sgn)` -- sets if the returned value should be a signed integer (default unsigned)

It also supports bit index to extract values.

```lua
local s = BitBuffer.new(string.char(0xc5, 0x6a), 16);
assert(s[{4, 8}] == 0x56, "basic check")
assert(s[8] == 0, "single bit check")
```

The syntax above is a field extraction operator -- the first value is the bit offset from the start of the message, and the second
value is the number of bits to extract. You can also include values of `little_endian_buffer`, `little_endian_value`, `signed` as other
entries in the index table if you want to override the current settings for a single call. To access a single bit, you can
just provide the offset. This will be interpreted according to the current setting of `:little_endian_buffer(<true/false>)`. 

## Bit ordering

There are two ways to think about numbering bits in a sequence of bytes:

* big-endian-buffer: the first bit (#0) is the *most* significant bit in the first byte. Bit #8 is the *most* significant bit in the second byte.
* little-endian-buffer: the first bit (#0) is the *least* significant bit in the first byte. Bit #8 is the *least* significant bit in the second byte.

This has implications when cross byte boundaries. The field extraction operator always returns a consecutive sequence of bits according to the endianness above. 

Once the sequence of bits has been extracted, there are two ways to turn it into an integer:

* big-endian-value: the first extracted bit is the *most* significant bit in the returned integer.
* little-endian-value: the first extracted bit is the *least* significant bit in the returned integer.



## `validate` details

This function is passed the array of `BitBuffer` arguments and returns one of two options:

* `bool` indicating whether the data is valid (true) or not (false).
* `table` containing the valid components of the signal in the same format as the `BitBuffer` argument

If there are no rows in the returned table, or the boolean is false, then the validation is treated as
failed. If you want to construct a new set of `BitBuffer` objects for the return table, then you can call the `BitBuffer.new` function
which takes a string and a bit length as arguments.

### Example

Say we have a device which transmits 16 bits where the xor of the first and second bytes is 0xff.

```
function validate(data)
  local result = {}
  for _, value in ipairs(data) do
    if value:bitlen() >= 16 and value:bitlen() <= 18 then
      if value[{0, 8}] ^ value[{8,8}] == 255 then
        table.insert(result, value)
      end
    end
  end
  return result
end
```

## `decode` details

This function is passed the `BitBuffer` argument and returns a table with key/values that are added
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

function decode(data)
  res = {}
  res.key = keyMap[data[1][{0,8}] or data[1][{0, 8}]
  return res
end
```

This indexing is actually just syntactic sugar over the `getbits` call which is described below.

## :getbits method

The `getbits` method is available on a `BitBuffer`. This takes a table as an argument and 
returns an integer which is the value of the selected, consecutive, bits. The table keys are:

* `offset`: an integer which is the offset into the packet of the start of the bitfield
* `width`: the number of consecutive bits to return (no more than will fit into an integer)
* `little_endian_buffer`: a boolean set to true if the buffer is to be treated as little endian
* `little_endian_value`: a boolesn set to true if the result is to be treated as little endian.
* `signed`: a boolean to indicate if the returned value should be signed.

The `getbits` method treats the supplied bitbuffer as a sequence of bits according to the `little_endian_buffer` setting. 
Once the required number of consecutive bits have been extracted, they are converted to an integer according to the `little_endian_value` setting.

## BitBuffer.new

This function creates a new `BitBuffer`. It is called with two arguments:

* A string containing the bytes of the packet or another BitBuffer object.
* An integer indicating the number of bits in the packet.

It is an error if the number of bits exceeds the size of the string.

Two `BitBuffer` objects compare equal if they both have the same data string and bit length.

The `tostring` function converts a `BitBuffer` to the string that it was created with. 

## Debugging

When writing these scripts, you can use the `print` command to put diagnostic information out to the console.

If the script fails to parse, then the error message will be printed, and the `rtl_433` program will not start.

If you want to test your `validate` or `decode` functions, then you can just call them after defining them with specially constructed `BitBuffer` objects.

## Complete Example


```
decoder {
    n=remote_rc1,m=OOK_PWM,s=312,l=1072,r=6820,g=6000,t=304,y=0,unique,bits=16,
    luacode=[[
print("Loading validate && decode")
function validate(data)
  local result = {} --
  for _, buffer in ipairs(data) do
    if buffer:bitlen() >= 16 and buffer:bitlen() <= 18 then
      if buffer[{0, 8}] + buffer[{8, 8}] == 255 then
	table.insert(result, BitBuffer.new(buffer, 16))
      end
    end
  end
  return result
end

local keyMap = {
  [0x32] = "off",
  [0x33] = "+",
  [0x34] = "up",
  [0x35] = "on",
  [0x36] = "-",
  [0x37] = "down"
} --

function decode(data)
  res = {} --
  res.key = keyMap[data[1][{8, 8}]] or data[1][{8, 8}]
  return res
end
]]
}
```

There is the slight hassle that a `}` at the end of the line in the lua block causes parsing problems, so you can just put a `--` after it to start a comment. If
you wanted to test your functions, you could add the following code 

```
local packet = BitBuffer.new(string.char(0xCB, 0x34), 16)
assert(validate({packet})[1] == packet, "Incorrect packet validation")
assert(decode({packet}).key == "up", "Key value not 'up'")
```
