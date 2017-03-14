
# lua-rapidjson

A json module for LuaJIT and Lua, based on the very fast RapidJSON C++ library.
See project homepage for more informations, bug report and feature request.

# Usage

```
cd lua-rapidjson

make
```

# Synopsis

```
local rapidjson = require "rapidjson.safe"

rapidjson.encode_empty_table_as_object(false)
rapidjson.encode_max_depth(4)
rapidjson.decode_max_depth(4)

local strjson = [[{"name":"rose","degree":200,"college":{"name":"信息工程学院 ","id":12023002},"lat":23.61024865925,"gender":1,"mobile":""}]]

local dec_json = rapidjson.decode(strjson)


local enc_str = rapidjson.encode(dec_json)
print(enc_str)
```
