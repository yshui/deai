x = di.xorg:connect()
for _, o in pairs(x.randr.outputs) do
	local edid = o.props["EDID"]
	if edid ~= nil then
		edid = di.hwinfo.display:from_edid(edid)
		print(edid.model)
	end
end
