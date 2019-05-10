/*
** Copyright (C) 2019 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <math.h>
#include "json.h"

static size_t decodeWhitespace(const char *buf, size_t pos, size_t size) {
	while (pos < size && strchr(" \t\n\r", buf[pos])) ++pos;
	return pos;
}

static size_t decodeBoundary(const char *buf, size_t pos, size_t size, char c, int *res) {
	return pos + (*res = pos < size && buf[pos] == c);
}

static size_t decodeDelimiter(lua_State *L, const char *buf, size_t pos, size_t size, char c, int *res) {
	int res_;
	pos = decodeWhitespace(buf, pos, size);
	pos = decodeBoundary(buf, pos, size, c, &res_);
	if (res) *res = res_;
	else if (!res_) luaL_error(L, "delimiter '%c' expected at position %d", c, pos + 1);
	return pos;
}

static size_t decodeCharacter(lua_State *L, const char *buf, size_t pos, size_t size, luaL_Buffer *sb, int *val) {
	int len = 0, x = 0;
	buf += pos;
	for (;;) {
		unsigned char c;
		if (pos + len >= size) luaL_error(L, "character expected at position %d", pos + len + 1);
		c = buf[len];
		if (c < 0x20) luaL_error(L, "control character at position %d", pos + len + 1);
		++len;
		if (len == 1) {
			if (c == '\\') continue;
			luaL_addchar(sb, c);
			break;
		}
		if (len == 2) {
			switch (c) {
				case '"':
				case '\\':
				case '/':
					break;
				case 'b':
					c = '\b';
					break;
				case 'f':
					c = '\f';
					break;
				case 'n':
					c = '\n';
					break;
				case 'r':
					c = '\r';
					break;
				case 't':
					c = '\t';
					break;
				case 'u':
					continue;
				default:
					goto error;
			}
			luaL_addchar(sb, c);
			break;
		}
		x <<= 4;
		if ((c >= '0') && (c <= '9')) x |= c - '0';
		else if ((c >= 'a') && (c <= 'f')) x |= c - 'a' + 10;
		else if ((c >= 'A') && (c <= 'F')) x |= c - 'A' + 10;
		else goto error;
		if (len == 6) {
			*val = x;
			break;
		}
	}
	return pos + len;
error:
	luaL_error(L, "invalid escape sequence at position %d", pos + 1);
	return 0;
}

static void addUTF8(luaL_Buffer *sb, int val) {
	char buf[4];
	int len;
	if (val <= 0x7f) {
		buf[0] = val;
		len = 1;
	} else if (val <= 0x7ff) {
		buf[0] = ((val >> 6) & 0x1f) | 0xc0;
		buf[1] = (val & 0x3f) | 0x80;
		len = 2;
	} else if (val <= 0xffff) {
		buf[0] = ((val >> 12) & 0x0f) | 0xe0;
		buf[1] = ((val >> 6) & 0x3f) | 0x80;
		buf[2] = (val & 0x3f) | 0x80;
		len = 3;
	} else {
		buf[0] = ((val >> 18) & 0x07) | 0xf0;
		buf[1] = ((val >> 12) & 0x3f) | 0x80;
		buf[2] = ((val >> 6) & 0x3f) | 0x80;
		buf[3] = (val & 0x3f) | 0x80;
		len = 4;
	}
	luaL_addlstring(sb, buf, len);
}

static size_t decodeString(lua_State *L, const char *buf, size_t pos, size_t size) {
	luaL_Buffer sb;
	pos = decodeDelimiter(L, buf, pos, size, '"', 0);
	luaL_buffinit(L, &sb);
	for (;;) {
		int res, val = -1;
		pos = decodeBoundary(buf, pos, size, '"', &res);
		if (res) break;
		pos = decodeCharacter(L, buf, pos, size, &sb, &val);
		if (val == -1) continue;
		if (val >= 0xd800 && val <= 0xdbff) { /* Surrogate pair */
			int val_ = -1;
			size_t pos_ = decodeCharacter(L, buf, pos, size, &sb, &val_);
			if (val_ < 0xdc00 || val_ > 0xdfff) luaL_error(L, "invalid UTF-16 surrogate at position %d", pos + 1);
			val = ((val - 0xd800) << 10) + (val_ - 0xdc00) + 0x10000;
			pos = pos_;
		}
		addUTF8(&sb, val);
	}
	luaL_pushresult(&sb);
	return pos;
}

static size_t decodeValue(lua_State *L, const char *buf, size_t pos, size_t size, int hidx);

static size_t decodeArray(lua_State *L, const char *buf, size_t pos, size_t size, int hidx) {
	int res;
	lua_Integer len = 0;
	pos = decodeDelimiter(L, buf, pos, size, '[', 0);
	pos = decodeDelimiter(L, buf, pos, size, ']', &res);
	lua_newtable(L);
	if (res) goto done;
	checkStack(L);
	do {
		pos = decodeValue(L, buf, pos, size, hidx);
		lua_rawseti(L, -2, ++len);
		pos = decodeDelimiter(L, buf, pos, size, ',', &res);
	} while (res);
	pos = decodeDelimiter(L, buf, pos, size, ']', 0);
done:
	lua_pushinteger(L, len);
	lua_setfield(L, -2, "__array");
	return pos;
}

static size_t decodeObject(lua_State *L, const char *buf, size_t pos, size_t size, int hidx) {
	int res;
	pos = decodeDelimiter(L, buf, pos, size, '{', 0);
	pos = decodeDelimiter(L, buf, pos, size, '}', &res);
	lua_newtable(L);
	if (res) return pos;
	checkStack(L);
	do {
		pos = decodeString(L, buf, pos, size);
		pos = decodeDelimiter(L, buf, pos, size, ':', 0);
		pos = decodeValue(L, buf, pos, size, hidx);
		lua_rawset(L, -3);
		pos = decodeDelimiter(L, buf, pos, size, ',', &res);
	} while (res);
	pos = decodeDelimiter(L, buf, pos, size, '}', 0);
	return pos;
}

static const char *const literals[] = {"null", "false", "true", "nan", "NaN", "-nan", "-NaN", "inf", "Infinity", "-inf", "-Infinity", 0};

#ifndef lua_str2number /* LuaJIT fails to define this macro */
#define lua_str2number(s, p) strtod(s, p)
#endif

static size_t decodeLiteral(lua_State *L, const char *buf, size_t pos, size_t size) {
	int len, num, frn;
	char str[64];
	pos = decodeWhitespace(buf, pos, size);
	buf += pos;
	for (len = 0, num = 0, frn = 0; pos + len < size; ++len) {
		unsigned char c = buf[len];
		if ((c < '0' || c > '9') && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && !strchr(".-+", c)) break;
		if (len == sizeof str - 1) goto error;
		if (!num && c >= '0' && c <= '9') num = 1;
		if (!frn && num && strchr(".eE", c)) {
			frn = 1;
			if (c == '.') c = localeconv()->decimal_point[0]; /* "Localize" decimal point */
		}
		str[len] = c;
	}
	if (!len) luaL_error(L, "literal expected at position %d", pos + 1);
	str[len] = 0;
	if (num) {
		char *end;
#if LUA_VERSION_NUM >= 503
		if (!frn) {
			lua_Integer i = strtoll(str, &end, 0);
			if (*end) goto error;
			lua_pushinteger(L, i);
		} else
#endif
		{
			lua_Number n = lua_str2number(str, &end);
			if (*end) goto error;
			lua_pushnumber(L, n);
		}
	} else {
		int i;
		const char *lit;
		for (i = 0; (lit = literals[i]) && strcmp(lit, str); ++i);
		switch (i) {
			case 0: /* null */
				lua_pushlightuserdata(L, 0);
				break;
			case 1: /* false */
				lua_pushboolean(L, 0);
				break;
			case 2: /* true */
				lua_pushboolean(L, 1);
				break;
#ifdef NAN
			case 3: /* nan */
			case 4: /* NaN */
				lua_pushnumber(L, NAN);
				break;
			case 5: /* -nan */
			case 6: /* -NaN */
				lua_pushnumber(L, -NAN);
				break;
#endif
#ifdef INFINITY
			case 7: /* inf */
			case 8: /* Infinity */
				lua_pushnumber(L, INFINITY);
				break;
			case 9: /* -inf */
			case 10: /* -Infinity */
				lua_pushnumber(L, -INFINITY);
				break;
#endif
			default:
				goto error;
		}
	}
	return pos + len;
error:
	luaL_error(L, "invalid literal at position %d", pos + 1);
	return 0;
}

static size_t decodeValue(lua_State *L, const char *buf, size_t pos, size_t size, int hidx) {
	pos = decodeWhitespace(buf, pos, size);
	if (pos >= size) luaL_error(L, "value expected at position %d", pos + 1);
	switch (buf[pos]) {
		case '"':
			pos = decodeString(L, buf, pos, size);
			break;
		case '[':
			pos = decodeArray(L, buf, pos, size, hidx);
			break;
		case '{':
			pos = decodeObject(L, buf, pos, size, hidx);
			break;
		default:
			pos = decodeLiteral(L, buf, pos, size);
			break;
	}
	if (!lua_istable(L, -1) || lua_isnil(L, hidx)) return pos;
	lua_pushvalue(L, hidx);
	lua_insert(L, -2);
	lua_call(L, 1, 1); /* Call handler */
	return pos;
}

int json__decode(lua_State *L) {
	size_t size;
	const char *buf = luaL_checklstring(L, 1, &size);
	size_t pos = luaL_optinteger(L, 2, 1) - 1;
	luaL_argcheck(L, pos <= size, 2, "value out of range");
	lua_settop(L, 3);
	lua_pushinteger(L, decodeValue(L, buf, pos, size, 3) + 1);
	return 2;
}
