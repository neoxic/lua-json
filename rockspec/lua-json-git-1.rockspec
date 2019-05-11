package = 'lua-json'
version = 'git-1'
source = {
	url = 'git://github.com/neoxic/lua-json.git',
}
description = {
	summary = 'JSON encoding/decoding library for Lua',
	detailed = [[
		lua-json provides fast JSON encoding/decoding routines for Lua:
		- Support for inline data transformation/filtering via metamethods/handlers.
		- Properly protected against memory allocation errors.
		- No external dependencies.
		- Written in C.
	]],
	license = 'MIT',
	homepage = 'https://github.com/neoxic/lua-json',
	maintainer = 'Arseny Vakhrushev <arseny.vakhrushev@gmail.com>',
}
dependencies = {
	'lua >= 5.1',
}
build = {
	type = 'builtin',
	modules = {
		json = {
			sources = {
				'src/json.c',
				'src/json-encode.c',
				'src/json-decode.c',
			},
		},
	},
}
