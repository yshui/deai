local exports = {
	randr = {}
}

if di.hwinfo ~= nil then
-- These methods are only available if the hwinfo plugin is loaded

function exports.randr:monitor_info()
	local info = {}
	for _, output in pairs(self.outputs) do
		edid = output.props["EDID"]
		if edid ~= nil then
			edid = di.hwinfo.display:from_edid(edid)
		end
		if edid ~= nil then
			info[output.info.name] = {
				model = edid.model,
				make = edid.make,
				serial = edid.serial,
				x = output,
			}
		end
	end
	return info
end

end -- if di.hwinfo ~= nil

return exports
