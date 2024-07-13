--- Built-in methods for the xorg plugin
-- @module xorg

local exports = {
	randr = {}
}

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
