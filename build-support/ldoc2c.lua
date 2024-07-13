local exports = {}
local function gen_args(item)
	local args = ""
	for _, arg in ipairs(item.params) do
		if args ~= "" then
			args = args..", "
		end
		args = args..arg..": "..item.modifiers.param[arg].type
	end
	return args
end
local function_count = 0
local function print_function(item)
	local s
	local lines = {}
	print("/// "..item.summary)
	print("///")
	print("/// EXPORT: "..item.name.."("..gen_args(item).."): "..item.modifiers["return"][1].type)
	print("///")
	for s in item.description:gmatch("[^\r\n]+") do
		s = s:gsub("^%s+", "")
		print("/// "..s)
	end
	if #item.params > 0 then
		print("///")
		print("/// Arguments:")
		print("///")
		for _, arg in ipairs(item.params) do
			s = item.params.map[arg]:gsub("^%s+", "")
			print("/// - "..arg.." "..s)
		end
	end
	print("void func"..function_count.."();")
end

local p = require("pl.pretty")
local function print_type(item)
	local s
	print("/// "..item.summary)
	print("///")
	print("/// TYPE: "..item.name)
	print("///")
	for s in item.description:gmatch("[^\r\n]+") do
		s = s:gsub("^%s+", "")
		print("/// "..s)
	end
	print("struct Type"..function_count)
	print("{")
	for _, param in ipairs(item.params) do
		s = item.params.map[param]:gsub("^%s+", "")
		print("\t/// "..s)
		print("\t///")
		print("\t/// EXPORT: "..item.name.."."..param..": "..item.modifiers.field[param].type)
		print("\tvoid * "..param..";")
	end
	print("};")
end

function exports.dump(t)
	for _, mod in ipairs(t) do
		for _, item in pairs(mod.items) do
			if item.type == "function" then
				print_function(item)
			elseif item.type == "table" then
				print_type(item)
			end
			function_count = function_count + 1
		end
	end
end

return exports
