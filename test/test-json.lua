local json = require 'json'

local function copy(t)
	local r = {}
	for k, v in pairs(t) do
		r[k] = v
	end
	return r
end

local mt = {__toJSON = function (t) return copy(t) end}
local nums = {
	0.110001,
	0.12345678910111,
	0.412454033640,
	2.6651441426902,
	2.718281828459,
	3.1415926535898,
	2.1406926327793,
}
local vals = {
	function () return json.null end, -- Null
	function () return math.random() < 0.5 end, -- Boolean
	function () return math.random(-2147483648, 2147483647) end, -- Integer
	function () return nums[math.random(#nums)] end, -- Double
	function () -- String
		local t = {}
		for i = 1, math.random(0, 10) do
			t[i] = string.char(math.random(0, 255))
		end
		return table.concat(t)
	end,
}
local refs, any
local objs = {
	function () -- Reference
		local n = #refs
		return n > 0 and refs[math.random(n)] or json.null
	end,
	function (d) -- Array
		local n = math.random(0, 10)
		local t = setmetatable({__array = n}, mt)
		for i = 1, n do
			t[i] = any(d + 1)
		end
		table.insert(refs, t)
		return t
	end,
	function (d) -- Object
		local t = setmetatable({}, mt)
		for i = 1, math.random(0, 10) do
			local k = vals[5]() -- Random string key
			if #k > 0 then
				t[k] = any(d + 1)
			end
		end
		table.insert(refs, t)
		return t
	end,
}

function any(d)
	if d < 4 and math.random() < 0.7 then
		return objs[math.random(#objs)](d)
	end
	return vals[math.random(#vals)]()
end

local function spawn()
	refs = {}
	return any(0)
end

local function compare(x1, x2)
	local r = {}
	local function compare(x1, x2)
		if type(x1) ~= 'table' or type(x2) ~= 'table' then
			return x1 == x2
		end
		if x1 == x2  then
			return true
		end
		if not compare(getmetatable(x1), getmetatable(x2)) then
			return false
		end
		if r[x1] and r[x2] then
			return true
		end
		r[x1] = true
		r[x2] = true
		local function find(t, k_, v_)
			if t[k_] == v_ then
				return true
			end
			for k, v in pairs(t) do
				if compare(k, k_) and compare(v, v_) then
					return true
				end
			end
		end
		for k, v in pairs(x1) do
			if not find(x2, k, v) then
				return false
			end
		end
		for k, v in pairs(x2) do
			if not find(x1, k, v) then
				return false
			end
		end
		r[x1] = nil
		r[x2] = nil
		return true
	end
	return compare(x1, x2)
end

local function handler(t)
	return setmetatable(t, mt)
end

math.randomseed(os.time())

-----------------
-- Stress test --
-----------------

for i = 1, 1000 do
	local obj = spawn()
	local str = json.encode(obj)
	local obj_, pos = json.decode(str, nil, handler)
	assert(compare(obj, obj_))
	assert(pos == #str + 1)

	-- Extra robustness test
	for pos = 2, pos do
		pcall(json.decode, str, pos)
	end
end

---------------------
-- Compliance test --
---------------------

local strs = {
	'{\t"a"\n:\r1, \t"a"\n:\r2}', -- Whitespace + double key
	'"\\ud800\\uDC00"', -- UTF-16 surrogate pair
}

local objs = {
	{a = 2},
	'𐀀',
}

for i = 1, #strs do
	local str, obj = strs[i], objs[i]
	local obj_, pos = json.decode(str)
	assert(compare(obj, obj_))
	assert(pos == #str + 1)
end

-- Errors
assert(not pcall(json.encode, setmetatable({}, {}))) -- Table with metatable
assert(not pcall(json.encode, setmetatable({}, {__toJSON = function (t) return {t = t} end}))) -- Recursion
assert(not pcall(json.encode, setmetatable({}, {__toJSON = function (t) t() end}))) -- Run-time error
assert(not pcall(json.encode, {a = print})) -- Invalid value
assert(not pcall(json.encode, {[print] = 1})) -- Invalid key
assert(not pcall(json.decode, '"\\ud800\\uDBFF"')) -- Invalid UTF-16 surrogate
