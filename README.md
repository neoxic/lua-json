JSON encoding/decoding module for Lua
=====================================

[lua-json] provides fast JSON encoding/decoding routines for Lua:
- Support for inline data transformation/filtering via metamethods/handlers.
- Written in C with 32/64-bit awareness.
- No external dependencies.


### json.encode(value, [event])
Returns a text string containing a JSON representation of `value`. Optional `event` may be used
to specify a metamethod name (default is `__toJSON`) that is called for every processed value. The
value returned by the metamethod is used instead of the original value.

A table (root or nested) is encoded into a dense array if it has a field `__array` whose value is
_true_. The length of the resulting array can be adjusted by storing an integer value in that field.
Otherwise, it is assumed to be equal to the raw length of the table.

### json.decode(data, [pos], [handler])
Returns the value encoded in `data` along with the index of the first unread byte. Optional `pos`
marks where to start reading in `data` (default is 1). Optional `handler` is called for each new
table (root or nested), and its return value is used instead of the original table.

When an array is decoded, its length is stored in a field `__array`.

### json.null
A Lua value that represents JSON null.


Building and installing with LuaRocks
-------------------------------------

To build and install, run:

    luarocks make

To install the latest release using [luarocks.org], run:

    luarocks install lua-json


Building and installing with CMake
----------------------------------

To build and install, run:

    cmake .
    make
    make install

To build for a specific Lua version, set `USE_LUA_VERSION`. For example:

    cmake -D USE_LUA_VERSION=5.1 .

or for LuaJIT:

    cmake -D USE_LUA_VERSION=jit .

To build in a separate directory, replace `.` with a path to the source.


Getting started
---------------

```Lua
local json = require 'json'

-- Helpers
local function encode_decode(val, ev, h)
    return json.decode(json.encode(val, ev), nil, h)
end

-- Primitive types
assert(encode_decode(nil) == json.null)
assert(encode_decode(json.null) == json.null)
assert(encode_decode(false) == false)
assert(encode_decode(true) == true)
assert(encode_decode(123) == 123)
assert(encode_decode(123.456) == 123.456)
assert(encode_decode('abc') == 'abc')

-- Complex types
local data = {
    obj = { -- A table with only string keys translates into an object
        str = 'abc',
        len = 3,
        val = -10.2,
        null = json.null,
    },
    arr1 = {__array = true, 1, 2, 3}, -- A table with a field '__array' translates into an array
    arr2 = {__array = 5, nil, 2, nil, 4, nil}, -- Array length can be adjusted to form a sparse array
}

local out = encode_decode(data)
assert(out.obj.null == json.null) -- 'null' as a field value
assert(out.arr1.__array == #out.arr1) -- Array length is restored
assert(out.arr2.__array == 5) -- Access to the number of items in a sparse array

-- Serialization metamethods can be used to produce multiple JSON representations of the same object.
-- Deserialization handlers can be used to restore Lua objects from complex JSON types on the way back.
-- This is helpful, for example, when objects are exchanged with both trusted and untrusted parties.
-- Various custom filters/wrappers can also be implemented using this API.

local mt = {
    __tostring = function (t) return (t.a or '') .. (t.b or '') end,
    __toA = function (t) return {A = t.a} end, -- [a -> A]
    __toB = function (t) return {B = t.b} end, -- [b -> B]
}

local function new(t) return setmetatable(t, mt) end
local function fromA(t) return new{a = t.A} end -- [A -> a]
local function fromB(t) return new{b = t.B} end -- [B -> b]

local obj = new{a = 'a', b = 'b'}
assert(tostring(obj) == 'ab')
assert(tostring(encode_decode(obj, '__toA', fromA)) == 'a')
assert(tostring(encode_decode(obj, '__toB', fromB)) == 'b')
```


Non-standard JSON values
------------------------

[lua-json] recognizes numbers prefixed with `0x` as hexadecimal. It also supports the following values:
`[-]nan`, `[-]NaN`, `[-]inf`, `[-]Infinity`.

If strictly compliant JSON generation is preferred, the following technique may be used to filter out
these values:

```Lua
local json = require 'json'

local function filter(t)
    for k, v in pairs(t) do
        if v ~= v or v == 1/0 or v == -1/0 then
            error(("non-standard value '%f' at index '%s'"):format(v, k))
        end
    end
    return t
end

local mt = {
    __toJSON = function (t)
        return setmetatable(filter(t), nil)
    end,
}

local function check(t)
    return setmetatable(t, mt)
end

local t = {
    str = 'abc',
    val = 1.234,
    nan = 0/0,
    inf = 1/0,
    ninf = -1/0,
}

local s = [[{
    "str": "abc",
    "val": 1.234,
    "nan": nan,
    "inf": inf,
    "ninf": -inf
}]]

-- Strict encoding
print(json.encode(check(t)))

-- Strict decoding
print(json.decode(s, nil, filter))
```


[lua-json]: https://github.com/neoxic/lua-json
[luarocks.org]: https://luarocks.org
