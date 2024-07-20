xc = di.xorg:connect()

-- Monitor models chosen randomly

local output_configs = {
	{
		match = { model = "PHILIPS 241V8LA" },
		-- Put Dell to the left of Philips
		left = { { model = "DELL U2515H" } },
	},
	{
		match = { model = "DELL U2515H" }
	}
}

ret = xc.randr:configure_outputs(output_configs)
