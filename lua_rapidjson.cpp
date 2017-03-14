extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
    /*  库 open 函数的前置声明   */
    int luaopen_rapidjson(lua_State *L);
    int luaopen_rapidjson_safe(lua_State *L);
}

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"
#include "rapidjson/error/error.h"
#include <iostream>
#include "rapidjson/reader.h"
#include <lua.hpp>
#include <vector>
#include <math.h>
#include <cmath>
using namespace std;
using namespace rapidjson;


#define _DEBUG_TRACE_FLF_ 1
#if 0 != _DEBUG_TRACE_FLF_  
    #include <cstdio>  
#endif
#if 1==_DEBUG_TRACE_FLF_
	#define TRACE_FLF (printf("%s(%d)-<%s>: ",__FILE__, __LINE__, __FUNCTION__), printf)
#else  
    #define TRACE_FLF 
#endif

#define DEFAULT_ENCODE_MAX_DEPTH 1000
#define DEFAULT_DECODE_MAX_DEPTH 128
#define DEFAULT_ENCODE_EMPTY_TABLE_AS_OBJECT 1


static void createSharedMeta(lua_State* L, const char* meta, const char* type)
{
    luaL_newmetatable(L, meta); // [meta]
    lua_pushstring(L, type); // [meta, name]
    lua_setfield(L, -2, "__jsontype"); // [meta]
    lua_pop(L, 1); // []
}

#define JSON_NULL "null"
#define JSON_NULL_LENGTH 4

typedef struct {
    int encode_max_depth;
    int encode_empty_table_as_object;
    int decode_max_depth;
} json_config_t;


/* ===== CONFIGURATION ===== */

static json_config_t *json_fetch_config(lua_State *l)
{
    json_config_t *cfg;

    cfg = (json_config_t *) lua_touserdata(l, lua_upvalueindex(1));
    if (!cfg)
        luaL_error(l, "BUG: Unable to fetch configuration");

    return cfg;
}

/* Ensure the correct number of arguments have been provided.
 * Pad with nil to allow other functions to simply check arg[i]
 * to find whether an argument was provided */
static json_config_t *json_arg_init(lua_State *l, int args)
{
    luaL_argcheck(l, lua_gettop(l) <= args, args + 1,
                  "found too many arguments");

    while (lua_gettop(l) < args)
        lua_pushnil(l);

    return json_fetch_config(l);
}

/* Process integer options for configuration functions */
static int json_integer_option(lua_State *l, int optindex, int *setting,
                               int min, int max)
{
    char errmsg[64];
    int value;

    if (!lua_isnil(l, optindex)) {
        value = luaL_checkinteger(l, optindex);
        snprintf(errmsg, sizeof(errmsg), "expected integer between %d and %d", min, max);
        luaL_argcheck(l, min <= value && value <= max, 1, errmsg);
        *setting = value;
    }

    lua_pushinteger(l, *setting);

    return 1;
}

/* Process enumerated arguments for a configuration function */
static int json_enum_option(lua_State *l, int optindex, int *setting,
                            const char **options, int bool_true)
{
    static const char *bool_options[] = { "off", "on", NULL };

    if (!options) {
        options = bool_options;
        bool_true = 1;
    }

    if (!lua_isnil(l, optindex)) {
        if (bool_true && lua_isboolean(l, optindex))
            *setting = lua_toboolean(l, optindex) * bool_true;
        else
            *setting = luaL_checkoption(l, optindex, NULL, options);
    }

    if (bool_true && (*setting == 0 || *setting == bool_true))
        lua_pushboolean(l, *setting);
    else
        lua_pushstring(l, options[*setting]);

    return 1;
}

/* Configures how to treat empty table when encode lua table */
static int json_cfg_encode_empty_table_as_object(lua_State *l)
{
    json_config_t *cfg = json_arg_init(l, 1);

    return json_enum_option(l, 1, &cfg->encode_empty_table_as_object, NULL, 1);
}

/* Configures the maximum number of nested arrays/objects allowed when
 * encoding */
static int json_cfg_encode_max_depth(lua_State *l)
{
    json_config_t *cfg = json_arg_init(l, 1);

    return json_integer_option(l, 1, &cfg->encode_max_depth, 1, INT_MAX);
}

/* Configures the maximum number of nested arrays/objects allowed when
 * encoding */
static int json_cfg_decode_max_depth(lua_State *l)
{
    json_config_t *cfg = json_arg_init(l, 1);
    //return json_integer_option(l, 1, &cfg->decode_max_depth, 1, INT_MAX);
    return json_integer_option(l, 1, &cfg->decode_max_depth, 1, DEFAULT_DECODE_MAX_DEPTH);
}

static void json_create_config(lua_State *l)
{
    json_config_t *cfg = (json_config_t *)lua_newuserdata(l, sizeof(*cfg));

    cfg->encode_max_depth = DEFAULT_ENCODE_MAX_DEPTH;
    cfg->decode_max_depth = DEFAULT_DECODE_MAX_DEPTH;
    cfg->encode_empty_table_as_object = DEFAULT_ENCODE_EMPTY_TABLE_AS_OBJECT;

}

inline bool lua_is_integer(lua_State* L, int idx, int64_t* out = NULL)
{
#if LUA_VERSION_NUM >= 503
	if (lua_isinteger(L, idx)) // but it maybe not detect all integers.
	{
		if (out)
			*out = lua_tointeger(L, idx);
		return true;
	}
#endif
	double intpart;
	if (std::modf(lua_tonumber(L, idx), &intpart) == 0.0)
	{
		if (std::numeric_limits<lua_Integer>::min() <= intpart
			&& intpart <= std::numeric_limits<lua_Integer>::max())
		{
			if (out)
				*out = static_cast<int64_t>(intpart);
			return true;
		}
	}
	return false;
}

typedef void(*DECODEPTRFUNC)();

lua_State* cur_decode_L;
DECODEPTRFUNC decode_funcs[DEFAULT_DECODE_MAX_DEPTH];
int  decode_func_index;
SizeType decode_indexs[DEFAULT_DECODE_MAX_DEPTH];
int  decode_idx_index;   
DECODEPTRFUNC cur_dec_call_func;
SizeType cur_dec_array_index;
int dec_max_depth;
int current_decode_depth;


static inline void object_func()
{
    lua_rawset(cur_decode_L, -3);
}

static inline void array_func()
{
    lua_rawseti(cur_decode_L, -2, ++cur_dec_array_index);
}
static inline void empty_func()
{
}

template<typename Writer>
static void encode_data_to_json(lua_State *l, json_config_t *cfg, Writer* writer, int lindex ,int current_decode_depth );

const bool lua_int_large_uint = sizeof(lua_Integer) > sizeof(unsigned int);
const bool lua_int_large_int64 = sizeof(lua_Integer) >= sizeof(int64_t);
const bool lua_int_large_uint64 = sizeof(lua_Integer) > sizeof(uint64_t);

struct MyHandler : public BaseReaderHandler<UTF8<>, MyHandler> 
{ 
    bool Null() 
    {
    	//TRACE_FLF("\n");
        lua_pushlstring(cur_decode_L, JSON_NULL, JSON_NULL_LENGTH);
        cur_dec_call_func();
        return true; 
    }    
    bool Bool(bool b) 
    { 
    	//TRACE_FLF("\n");
        lua_pushboolean(cur_decode_L, b);
        cur_dec_call_func();
        return true; 
    }    
    bool Int(int i) 
    { 
    	//TRACE_FLF("\n");
        lua_pushinteger(cur_decode_L, i);
        cur_dec_call_func();
        return true; 
    }   
    bool Uint(unsigned u) 
    { 
    	//TRACE_FLF("\n");
        if (lua_int_large_uint || u <= static_cast<unsigned>(std::numeric_limits<lua_Integer>::max()))
        {
            lua_pushinteger(cur_decode_L, static_cast<lua_Integer>(u));
        }
        else
        {
            lua_pushnumber(cur_decode_L, static_cast<lua_Number>(u));
        }

        cur_dec_call_func();
        return true; 
    }    
    bool Int64(int64_t i) 
    { 
    	//TRACE_FLF("\n");
        if (lua_int_large_int64 || (i <= std::numeric_limits<lua_Integer>::max() && i >= std::numeric_limits<lua_Integer>::min()))
        {
            lua_pushinteger(cur_decode_L, static_cast<lua_Integer>(i));
        }
        else
        {
            lua_pushnumber(cur_decode_L, static_cast<lua_Number>(i));
        }
        cur_dec_call_func();
        return true; 
    }    
    
    bool Uint64(uint64_t u) 
    { 
    	//TRACE_FLF("\n");
        if (lua_int_large_uint64 || u <= static_cast<uint64_t>(std::numeric_limits<lua_Integer>::max()))
        {
            lua_pushinteger(cur_decode_L, static_cast<lua_Integer>(u));
        }
        else
        {
            lua_pushnumber(cur_decode_L, static_cast<lua_Number>(u));
        }
        cur_dec_call_func();
        return true; 
    }  
    bool Double(double d) 
    { 
    	//TRACE_FLF("\n");
        lua_pushnumber(cur_decode_L, static_cast<lua_Number>(d));
        cur_dec_call_func();
        return true; 
    }    
    bool String(const char* str, SizeType length, bool copy) 
    { 
    	//TRACE_FLF("\n");
        lua_pushlstring(cur_decode_L, str, length);
        cur_dec_call_func();     
        return true; 
    }
    bool RawNumber(const char* str, rapidjson::SizeType length, bool copy) 
    {
    	//TRACE_FLF("\n");
        lua_getglobal(cur_decode_L, "tonumber");
        lua_pushlstring(cur_decode_L, str, length);
        lua_call(cur_decode_L, 1, 1);
        cur_dec_call_func();
        return true;
    }  
    bool StartObject() 
    { 
    	//TRACE_FLF("\n");
    	++current_decode_depth;
    	if(current_decode_depth > dec_max_depth)
    	{
    		luaL_error(cur_decode_L, "Found too many nested data structures (%d)", current_decode_depth);
    	}
        lua_createtable(cur_decode_L, 0, 0);  // [..., object]
        luaL_getmetatable(cur_decode_L, "json.object");    //[..., object, json.object]
        lua_setmetatable(cur_decode_L, -2);   // [..., object]

        decode_funcs[decode_func_index++] = cur_dec_call_func;
        cur_dec_call_func = object_func;
        return true; 
    }   
    bool Key(const char* str, SizeType length, bool copy) 
    { 
    	//TRACE_FLF("\n");
        lua_pushlstring(cur_decode_L, str, length);
        return true; 
    }    
    bool EndObject(SizeType memberCount) 
    { 
    	//TRACE_FLF("\n");
    	--current_decode_depth;
        cur_dec_call_func = decode_funcs[--decode_func_index];
        cur_dec_call_func();

        return true; 
    }    
    bool StartArray() 
    { 
    	//TRACE_FLF("\n");
    	++current_decode_depth;
    	if(current_decode_depth > dec_max_depth)
    	{
    		luaL_error(cur_decode_L, "Found too many nested data structures (%d)", current_decode_depth);
    	}
        lua_createtable(cur_decode_L, 0, 0);
        // mark as array.
        luaL_getmetatable(cur_decode_L, "json.array");  //[..., array, json.array]
        lua_setmetatable(cur_decode_L, -2); // [..., array]

        decode_funcs[decode_func_index++] = cur_dec_call_func;
        cur_dec_call_func = array_func;

        decode_indexs[decode_idx_index++] = cur_dec_array_index;
        cur_dec_array_index = 0;

        return true; 
    }    
    bool EndArray(SizeType elementCount) 
    { 
    	//TRACE_FLF("\n");
    	--current_decode_depth;
        //assert(elementCount == cur_dec_array_index);
        if (elementCount != cur_dec_array_index)
        {
        	luaL_error(cur_decode_L, "elementCount != context_.index_");
        }

        cur_dec_call_func = decode_funcs[--decode_func_index];

        cur_dec_array_index = decode_indexs[--decode_idx_index];
        cur_dec_call_func();
        return true; 
    }
private:


};

static void json_encode_exception(lua_State *l, int lindex,
                                  const char *reason)
{
    luaL_error(l, "Cannot serialise %s: %s",
                  lua_typename(l, lua_type(l, lindex)), reason);
}

static int json_decode(lua_State *l)
{
	json_config_t *cfg = json_fetch_config(l);
    //size_t json_len;
    luaL_argcheck(l, lua_gettop(l) == 1, 1, "expected 1 argument");
    //const char * input_json = luaL_checklstring(l, 1, NULL);
    const char * input_json = lua_tostring(l, 1);

    /*初使化*/
    cur_decode_L = l;
    dec_max_depth = cfg->decode_max_depth;   
    current_decode_depth = 0;
    cur_dec_call_func = empty_func;
    cur_dec_array_index = 0;
    decode_func_index = 0;
    decode_idx_index = 0;

	Reader reader;
	MyHandler handler;
    StringStream ss(input_json);    
    ParseResult r = reader.Parse(ss, handler);
    if (!r) {
    	int top = lua_gettop(l);
		lua_settop(l, top);
		lua_pushnil(l);
		lua_pushfstring(l, "%s (%d)", GetParseError_En(r.Code()), r.Offset());
		return 2;
	}
    return 1;
}

/* ===== INITIALISATION ===== */

#if !defined(LUA_VERSION_NUM) || LUA_VERSION_NUM < 502
/* Compatibility for Lua 5.1.
 *
 * luaL_setfuncs() is used to create a module table where the functions have
 * json_config_t as their first upvalue. Code borrowed from Lua 5.2 source. */
static void luaL_setfuncs (lua_State *l, const luaL_Reg *reg, int nup)
{
    int i;

    luaL_checkstack(l, nup, "too many upvalues");
    for (; reg->name != NULL; reg++) {  /* fill the table with given functions */
        for (i = 0; i < nup; i++)  /* copy upvalues to the top */
            lua_pushvalue(l, -nup);
        lua_pushcclosure(l, reg->func, nup);  /* closure with those upvalues */
        lua_setfield(l, -(nup + 2), reg->name);
    }
    lua_pop(l, nup);  /* remove upvalues */
}
#endif

static int lua_array_length(lua_State *l)
{
    double k;
    int max;
    int items;

    max = 0;
    items = 0;

    lua_pushnil(l);
    /* table, startkey */
    while (lua_next(l, -2) != 0) {
        /* table, key, value */
        if (lua_type(l, -2) == LUA_TNUMBER &&
            (k = lua_tonumber(l, -2))) {
            /* Integer >= 1 ? */
            if (floor(k) == k && k >= 1) {
                if (k > max)
                    max = (int)k;
                items++;
                lua_pop(l, 1);
                continue;
            }
        }

        /* Must not be an array (non integer key) */
        lua_pop(l, 2);
        return -1;
    }

    /* Encode excessively sparse arrays as objects (if enabled) */
    /*
    if (cfg->encode_sparse_ratio > 0 &&
        max > items * cfg->encode_sparse_ratio &&
        max > cfg->encode_sparse_safe) {
        if (!cfg->encode_sparse_convert)
            json_encode_exception(l, cfg, json, -1, "excessively sparse array");

        return -1;
    }
	*/
    return max;
}

template<typename Writer>
static void encode_array_to_json(lua_State *l, json_config_t *cfg, Writer* writer, int current_depth, int array_length )
{
	writer->StartArray();
	for (int n = 1; n <= array_length; ++n)
	{
		lua_rawgeti(l, -1, n); 
		encode_data_to_json(l, cfg, writer, -1, current_depth);
		lua_pop(l, 1); 
	}
	writer->EndArray();
}

#define BUF_SIZE 32
template<typename Writer>
static void encode_object_to_json(lua_State *l, json_config_t *cfg, Writer* writer, int current_depth )
{
    static char buf[BUF_SIZE];
    static int64_t int_val;
    static int keytype;
    static size_t len;
    static const char * key;
    static double num;

	writer->StartObject();
	lua_pushnil(l);
	while (lua_next(l, -2) != 0) {
        /* table, key, value */
        keytype = lua_type(l, -2);
        if (keytype == LUA_TNUMBER) 
        {
        	
            if (lua_is_integer(l, -2, &int_val))
        	{
				len = snprintf(buf, BUF_SIZE, "%lld", (long long)int_val);
			}
			else {
				num = lua_tonumber(l, -2);
				len = snprintf(buf, BUF_SIZE, "%.14g", num);
			}
			key = buf;
			writer->Key(key, static_cast<SizeType>(len));
        } 
        else if (keytype == LUA_TSTRING) 
        {
			key = lua_tolstring(l, -2, &len);
			writer->Key(key, static_cast<SizeType>(len));
        } 
        else 
        {
            json_encode_exception(l, -2, "table key must be a number or string");
            /* never returns */
        }
        /* table, key, value */
        encode_data_to_json(l, cfg, writer, -1, current_depth);
        lua_pop(l, 1);
        /* table, key */
    }
    writer->EndObject();
}

static void json_check_encode_depth(lua_State *l, json_config_t *cfg,
                                    int current_depth)
{
    if (current_depth <= cfg->encode_max_depth && lua_checkstack(l, 3))
        return;

    luaL_error(l, "Cannot serialise, excessive nesting (%d)",
               current_depth);
}


/* encode Lua data  */
template<typename Writer>
static void encode_data_to_json(lua_State *l, json_config_t *cfg, Writer* writer, int lindex ,int current_depth )
{
    static int len;
    static size_t size_len;
    static int64_t int_val;
	static const char* t_s;

	int type_val = lua_type(l, lindex);
    switch (type_val) {
    case LUA_TSTRING:
        t_s = lua_tolstring(l, lindex, &size_len);
        writer->String(t_s, size_len);  
        break;
    case LUA_TNUMBER:
        if (lua_is_integer(l, lindex, &int_val))
        {
			writer->Int64(int_val);
		}
		else {
			if (!writer->Double(lua_tonumber(l, lindex)))
			{
				json_encode_exception(l, lindex, "error encode number value.");
			}
		}
        break;
    case LUA_TBOOLEAN:
    	writer->Bool(lua_toboolean(l, lindex) != 0);
        break;
    case LUA_TTABLE:
        current_depth++;
        json_check_encode_depth(l, cfg, current_depth);
        len = lua_array_length(l);
        if (len > 0 || (len == 0 && !cfg->encode_empty_table_as_object))
        {
            encode_array_to_json(l, cfg, writer, current_depth, len);
        }
        else 
        {
            encode_object_to_json(l, cfg, writer, current_depth);
        }
        break;
    case LUA_TNIL:
        writer->Null();
        break;
    case LUA_TLIGHTUSERDATA:
        //if (lua_touserdata(l, -1) == NULL) {
        //    strbuf_append_mem(json, "null", 4);
        //} else if (lua_touserdata(l, -1) == &json_empty_array) {
        //    json_append_array(l, cfg, current_depth, json, 0);
        //}
        //break;
    default:
        /* Remaining types (LUA_TFUNCTION, LUA_TUSERDATA, LUA_TTHREAD,
         * and LUA_TLIGHTUSERDATA) cannot be serialised */
        json_encode_exception(l, -1, "type not supported");
        /* never returns */
    }
}

static int json_encode(lua_State *l)
{
	json_config_t *cfg = json_fetch_config(l); 
    luaL_argcheck(l, lua_gettop(l) == 1, 1, "expected 1 argument");  
    StringBuffer s;
    Writer<StringBuffer> writer(s);
    encode_data_to_json(l, cfg, &writer, -1, 0);
    lua_pushlstring(l, s.GetString(), s.GetSize());
    return 1;
}

/* Return module table */
static int lua_rapidjson_new(lua_State *L)
{
    luaL_Reg reg_funcs[] = {
        { "encode", json_encode },
        { "decode", json_decode },
        { "encode_empty_table_as_object", json_cfg_encode_empty_table_as_object },
        { "encode_max_depth", json_cfg_encode_max_depth },
        { "decode_max_depth", json_cfg_decode_max_depth },
        { "new", lua_rapidjson_new },
        { NULL, NULL }
    };
    /*  module table */
    lua_newtable(L);

    json_create_config(L);

//#if LUA_VERSION_NUM >= 502 // LUA 5.2 or above
		luaL_setfuncs(L, reg_funcs, 1);
//#else
//		luaL_register(L, "rapidjson", reg_funcs);
//#endif

    createSharedMeta(L, "json.object", "object");
    createSharedMeta(L, "json.array", "array");

    /* Set module name / version fields */
    lua_pushliteral(L, "rapidjson");
    lua_setfield(L, -2, "_NAME");
    lua_pushliteral(L, "0.1devel");
    lua_setfield(L, -2, "_VERSION");

    return 1;
}

static int json_protect_conversion(lua_State *l)
{
    int err;

    /* Deliberately throw an error for invalid arguments */
    luaL_argcheck(l, lua_gettop(l) == 1, 1, "expected 1 argument");

    /* pcall() the function stored as upvalue(1) */
    lua_pushvalue(l, lua_upvalueindex(1));
    lua_insert(l, 1);
    err = lua_pcall(l, 1, 1, 0);
    if (!err)
        return 1;

    if (err == LUA_ERRRUN) {
        lua_pushnil(l);
        lua_insert(l, -2);
        return 2;
    }

    /* Since we are not using a custom error handler, the only remaining
     * errors are memory related */
    return luaL_error(l, "Memory allocation error in protected call");
}

static int lua_rapidjson_safe_new(lua_State *l)
{
    const char *func[] = { "decode", "encode", NULL };
    int i;

    lua_rapidjson_new(l);

    /* Fix new() method */
    lua_pushcfunction(l, lua_rapidjson_safe_new);
    lua_setfield(l, -2, "new");

    for (i = 0; func[i]; i++) {
        lua_getfield(l, -1, func[i]);
        lua_pushcclosure(l, json_protect_conversion, 1);
        lua_setfield(l, -2, func[i]);
    }

    return 1;
}


/*  库打开时的执行函数（相当于这个库的 main 函数），执行完这个函数后， lua 中就可以加载这个 so 库了   */
int luaopen_rapidjson(lua_State *L)
{
	/*  把那个结构体数组注册到 sig （名字可自己取）库中去 */
	lua_rapidjson_new(L);
	return 1;
}


int luaopen_rapidjson_safe(lua_State *l)
{
    lua_rapidjson_safe_new(l);
    return 1;
}
