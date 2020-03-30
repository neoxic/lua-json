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

#pragma once

#include <lauxlib.h>

#ifndef _WIN32
#pragma GCC visibility push(hidden)
#endif

#define MODNAME "lua-json"
#define VERSION "0.1.3"

#define checkStack(L) luaL_checkstack(L, LUA_MINSTACK, "too many nested values")

#if LUA_VERSION_NUM < 502
#define lua_rawlen(L, idx) lua_objlen(L, idx)
#endif

int json__encode(lua_State *L);
int json__decode(lua_State *L);

#ifndef _WIN32
#pragma GCC visibility pop
#endif
