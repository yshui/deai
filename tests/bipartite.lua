-- Test for utils.bipartite_match

local result = di.misc:bipartite_match({
	{1, 2},
	{},
	{0, 3},
	{2},
	{2, 3},
	{5}
})
local matched = 0
for _, v in pairs(result) do
	if v ~= -1 then
		matched = matched + 1
	end
end
if matched ~= 5 then
	di:exit(1)
end
