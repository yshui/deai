--- Built-in methods for the xorg plugin
-- @module xorg

local exports = {
	randr = {},
	randr_output_info = {},
}

--- Find the preferred mode of an output.
-- Mode is chosen first based on the preferred modes returned by the X server,
-- any modes that aren't preferred are ignored. From what remains, we choose the
-- mode with highest number of pixels. If there are multiple modes with the same
-- number of pixels, we prefer the one that isn't interlaced or double scanned.
-- @function deai.plugin.xorg.randr:OutputInfo.preferred_mode
-- @treturn deai.plugin.xorg.randr:Mode The preferred mode
function exports.randr_output_info:preferred_mode()
	local num_preferred = self.num_preferred
	local max_score = 0
	local preferred_mode
	for i, m in ipairs(self.modes) do
		if i > num_preferred then
			break
		end
		local score = m.width * m.height * 3
		if not m.interlaced then
			score = score + 1
		end
		if not m.double_scan then
			score = score + 1
		end
		if score > max_score then
			max_score = score
			preferred_mode = m
		end
	end
	return preferred_mode
end

if di.hwinfo ~= nil then
-- These methods are only available if the hwinfo plugin is loaded

--- Infotrmation about a monitor
-- @table deai.plugin.xorg.randr:MonitorInfo
-- @tfield :string model The model of the monitor
-- @tfield :string make The make of the monitor
-- @tfield :string serial The serial number of the monitor
-- @tfield deai.plugin.xorg.randr:Output output The the output this monitor is connected to
MonitorInfo = {}

--- Get the monitor information for each connected monitor.
-- Returns a table of monitor information, indexed by the name of the output,
-- each value is of type :lua:mod:`~deai.plugin.xorg.randr.MonitorInfo`.
-- @function deai.plugin.xorg:RandrExt.monitor_info
-- @treturn :object A table of monitor information
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
				output = output,
			}
		end
	end
	return info
end

end -- if di.hwinfo ~= nil

return exports
