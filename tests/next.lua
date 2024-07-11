a = di.lua:as_di_object({
  a = 1, b = "asdf", c = true
})

expected = {
  a = 1, b = "asdf", c = true
}

local count = 0
for k, v in pairs(a) do
	if v ~= expected[k] then
		print("Key:", k, "| expected:", expected[k], "| got:", v)
		di:exit(1)
	end
	count = count + 1
end

local expected_count = 0;
for k, v in pairs(expected) do
	expected_count = expected_count + 1
end
if count ~= expected_count then
	print("Missing elements, expected:", expected_count, "| got:", count)
end
di:exit(0)
