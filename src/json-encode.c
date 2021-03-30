/*
** Copyright (C) 2019-2020 Arseny Vakhrushev <arseny.vakhrushev@me.com>
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
** THE SOFTWARE.
*/

#include <string.h>
#include <locale.h>
#include "json.h"

#define MAXSTACK 1000 /* Arbitrary stack size limit to check for recursion */

typedef struct {
	char *buf;
	size_t pos, size;
} Box;

static char *resizeBox(lua_State *L, Box *box, size_t size) {
	void *ud;
	lua_Alloc allocf = lua_getallocf(L, &ud);
	char *buf = allocf(ud, box->buf, box->size, size);
	if (!size) return 0;
	if (!buf) luaL_error(L, "cannot allocate buffer");
	box->buf = buf;
	box->size = size;
	return buf;
}

static int freeBox(lua_State *L) {
	resizeBox(L, lua_touserdata(L, 1), 0);
	return 0;
}

static Box *newBox(lua_State *L) {
	Box *box = lua_newuserdata(L, sizeof *box);
	box->buf = 0;
	box->pos = 0;
	box->size = 0;
	if (luaL_newmetatable(L, MODNAME)) {
		lua_pushcfunction(L, freeBox);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);
	resizeBox(L, box, 100);
	return box;
}

static char *appendData(lua_State *L, Box *box, size_t size) {
	char *buf = box->buf;
	size_t pos = box->pos;
	size_t old = box->size;
	size_t new = pos + size;
	if (new > old) { /* Expand buffer */
		old <<= 1; /* At least twice the old size */
		buf = resizeBox(L, box, new > old ? new : old);
	}
	box->pos = new;
	return buf + pos;
}

static void encodeData(lua_State *L, Box *box, const char *data, size_t size) {
	memcpy(appendData(L, box, size), data, size);
}

static void encodeByte(lua_State *L, Box *box, char val) {
	*appendData(L, box, 1) = val;
}

static void encodeString(lua_State *L, Box *box, int idx) {
	size_t len, i;
	const char *str = lua_tolstring(L, idx, &len);
	encodeByte(L, box, '"');
	for (i = 0; i < len; ++i) {
		unsigned char c = str[i];
		switch (c) {
			case '"':
			case '\\':
			case '/':
				break;
			case '\b':
				c = 'b';
				break;
			case '\f':
				c = 'f';
				break;
			case '\n':
				c = 'n';
				break;
			case '\r':
				c = 'r';
				break;
			case '\t':
				c = 't';
				break;
			default:
				if (c < 0x20) { /* Control character */
					char buf[7];
					encodeData(L, box, buf, sprintf(buf, "\\u%04x", c));
					continue;
				}
				goto next;
		}
		encodeByte(L, box, '\\');
	next:
		encodeByte(L, box, c);
	}
	encodeByte(L, box, '"');
}

#define encodeLiteral(L, box, str) encodeData(L, box, str, sizeof(str) - 1)

static int isInteger(lua_State *L, int idx, lua_Integer *val) {
	lua_Integer i;
#if LUA_VERSION_NUM < 503
	lua_Number n;
	if (!lua_isnumber(L, idx)) return 0;
	n = lua_tonumber(L, idx);
	i = (lua_Integer)n;
	if (i != n) return 0;
#else
	int res;
	i = lua_tointegerx(L, idx, &res);
	if (!res) return 0;
#endif
	*val = i;
	return 1;
}

static int error(lua_State *L, int *nerr, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
#if LUA_VERSION_NUM >= 504
	luaL_checkstack(L, 1, 0);
#endif
	lua_pushvfstring(L, fmt, ap);
	va_end(ap);
	lua_insert(L, -(++(*nerr)));
	return 0;
}

static int encodeValue(lua_State *L, Box *box, int idx, const char *ev, int ridx, int *nerr) {
	if (luaL_callmeta(L, idx, ev)) lua_replace(L, idx); /* Transform value */
	switch (lua_type(L, idx)) {
		case LUA_TNIL:
			encodeLiteral(L, box, "null");
			break;
		case LUA_TBOOLEAN:
			if (lua_toboolean(L, idx)) encodeLiteral(L, box, "true");
			else encodeLiteral(L, box, "false");
			break;
		case LUA_TNUMBER: {
			char buf[64], *s, p = localeconv()->decimal_point[0];
			int len;
#if LUA_VERSION_NUM < 503
			len = lua_number2str(buf, lua_tonumber(L, idx));
#else
			lua_Integer i;
			len = isInteger(L, idx, &i) ?
				lua_integer2str(buf, sizeof buf, i):
				lua_number2str(buf, sizeof buf, lua_tonumber(L, idx));
#endif
			if (p != '.' && (s = strchr(buf, p))) *s = '.'; /* "Unlocalize" decimal point */
			encodeData(L, box, buf, len);
			break;
		}
		case LUA_TSTRING:
			encodeString(L, box, idx);
			break;
		case LUA_TTABLE: {
			lua_Integer i, len = -1;
			int top = lua_gettop(L);
			if (top >= MAXSTACK) return error(L, nerr, "recursion detected");
			if (lua_getmetatable(L, idx)) return error(L, nerr, "table with metatable unexpected");
			lua_pushvalue(L, idx);
			lua_rawget(L, ridx);
			if (lua_toboolean(L, -1)) return error(L, nerr, "circular reference detected");
			lua_pushvalue(L, idx);
			lua_pushboolean(L, 1);
			lua_rawset(L, ridx);
			lua_getfield(L, idx, "__array");
			if (lua_toboolean(L, -1)) {
				if (!isInteger(L, -1, &len)) len = lua_rawlen(L, idx);
				if (len < 0) len = 0;
			}
			lua_settop(L, top);
			checkStack(L);
			if (len != -1) {
				encodeByte(L, box, '[');
				for (i = 0; i < len; ++i) {
					if (i) encodeByte(L, box, ',');
					lua_rawgeti(L, idx, i + 1);
					if (!encodeValue(L, box, top + 1, ev, ridx, nerr)) return error(L, nerr, "[%d] => ", i + 1);
					lua_pop(L, 1);
				}
				encodeByte(L, box, ']');
			} else {
				encodeByte(L, box, '{');
				for (i = 0, lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1), ++i) {
					if (i) encodeByte(L, box, ',');
					if (lua_type(L, top + 1) != LUA_TSTRING) return error(L, nerr, "string index expected, got %s", luaL_typename(L, top + 1));
					encodeString(L, box, top + 1);
					encodeByte(L, box, ':');
					if (!encodeValue(L, box, top + 2, ev, ridx, nerr)) return error(L, nerr, "[\"%s\"] => ", lua_tostring(L, top + 1));
				}
				encodeByte(L, box, '}');
			}
			lua_pushvalue(L, idx);
			lua_pushnil(L);
			lua_rawset(L, ridx);
			break;
		}
		case LUA_TLIGHTUSERDATA:
			if (!lua_touserdata(L, idx)) {
				encodeLiteral(L, box, "null");
				break;
			} /* Fall through */
		default:
			return error(L, nerr, "%s unexpected", luaL_typename(L, idx));
	}
	return 1;
}

int json__encode(lua_State *L) {
	Box *box;
	int nerr = 0;
	const char *ev = luaL_optstring(L, 2, "__toJSON");
	luaL_checkany(L, 1);
	lua_settop(L, 2);
	lua_newtable(L);
	if (!encodeValue(L, box = newBox(L), 1, ev, 3, &nerr)) {
		lua_concat(L, nerr);
		return luaL_argerror(L, 1, lua_tostring(L, -1));
	}
	lua_pushlstring(L, box->buf, box->pos);
	return 1;
}
