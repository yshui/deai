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
		local name = output.info.name -- Seems like EDID isn't populated until we get output.info.
		                              -- Classic Xorg
		edid = output.props["EDID"]
		if edid ~= nil then
			edid = di.hwinfo.display:from_edid(edid)
		end
		if edid ~= nil then
			info[name] = {
				model = edid.model,
				make = edid.make,
				serial = edid.serial,
				output = output,
			}
		end
	end
	return info
end
local function find_output_impl(monitors, monitor_info)
	if type(monitor_info) == "string" then
		return monitors[monitor_info].output
	end
	for _, m in pairs(monitors) do
		local match = (monitor_info.model == nil or monitor_info.model == m.model) and
			(monitor_info.make == nil or monitor_info.make == m.make) and
			(monitor_info.serial == nil or monitor_info.serial == m.serial)
		if match then
			return m.output
		end
	end
	return nil
end

--- Find the output that corresponds to a monitor.
-- The argument is a :lua:mod:`~deai.plugin.xorg.randr.MonitorInfo` object. But not
-- all of its fields need to be filled in. Only the fields present will be used in
-- the search, and the `output` field will be ignored. If there are multiple outputs
-- that match the information given, any of them may be returned.
-- @function deai.plugin.xorg:RandrExt.find_output
-- @tparam deai.plugin.xorg.randr:MonitorInfo monitor_info The monitor information.
-- This can also be a string, in which case it is treated as the name of the output.
-- @treturn deai.plugin.xorg.randr:Output The output that corresponds to the monitor
function exports.randr:find_output(monitor_info)
	return find_output_impl(self:monitor_info(), monitor_info)
end

if di.misc ~= nil then

--- Output configuration.
-- Note all fields except `match` are optional.
-- @table deai.plugin.xorg.randr:OutputConfig
-- @tfield deai.plugin.xorg.randr:MonitorInfo match The monitor or output this configuration applies to.
-- Monitor are matched the same way as described in :lua:mod:`~deai.plugin.xorg.RandrExt.find_output`
-- @tfield [deai.plugin.xorg.randr:MonitorInfo] left The monitor to the left of the matched monitor.
-- An array of `MonitorInfo`. They are used the same way as `match. The same applies to `right`, `up`, and `down`.
-- @tfield [deai.plugin.xorg.randr:MonitorInfo] right The monitor to the right of the matched monitor.
-- @tfield [deai.plugin.xorg.randr:MonitorInfo] up The monitor above the matched monitor.
-- @tfield [deai.plugin.xorg.randr:MonitorInfo] down The monitor below the matched monitor.
-- @tfield :integer width The width of the output in pixels.
-- When either `width` or `height` is not specified, the preferred mode is used.
-- @tfield :integer height The height of the output in pixels.
-- When either `width` or `height` is not specified, the preferred mode is used.
-- @tfield :integer x The x coordinate of the top-left corner of the output in pixels.
-- @tfield :integer y The y coordinate of the top-left corner of the output in pixels.
output_config = {}

--- Configure the outputs.
-- Set the position and size of the outputs according to configuration. The outputs you
-- want to configure must ALL appear in the configuration list, even if you don't place any
-- positional constraints on them. The positional constraints MUST NOT refer to any outputs
-- that are not in the configuration list. If the constraints are impossible to satisfy,
-- then an error will be raised.
-- @function deai.plugin.xorg:RandrExt.configure_outputs
-- @tparam [deai.plugin.xorg.randr:OutputConfig] configs The configuration
-- @treturn :void No return value
function exports.randr:configure_outputs(configs)
	local output_to_index = {}
	local constraints_list = {}
	local monitors = self:monitor_info()
	local outputs = {}
	local output_sizes = {}
	for i, config in ipairs(configs) do
		local output = find_output_impl(monitors, config.match)
		outputs[i] = output
		output_to_index[output.id] = i
		-- Each output has 4 variables: x, y, x_mm, y_mm, in that order
		constraints_list[4 * i - 3] = {}
		constraints_list[4 * i - 2] = {}
		constraints_list[4 * i - 1] = {}
		constraints_list[4 * i] = {}
	end

	-- Step 1: select modes for each output in the configuration
	for i, config in ipairs(configs) do
		if config.width == nil or config.height == nil then
			local mode = outputs[i].info:preferred_mode()
			output_sizes[i] = {mode.width, mode.height, nil, nil, mode}
		else
			local best_score = -1
			for _, mode in ipairs(outputs[i].info.modes) do
				if mode.width == config.width and mode.height == config.height then
					local score = 0
					if not mode.interlaced then
						score = score + 1
					end
					if not mode.double_scan then
						score = score + 1
					end
					if score > best_score then
						best_score = score
						output_sizes[i] = {mode.width, mode.height, nil, nil, mode}
					end
					break
				end
			end
			if best_score == -1 then
				-- error("No mode with width " .. config.width .. " and height " .. config.height .. " found for output " .. outputs[i].info.name)
			end
		end
		output_sizes[i][3] = outputs[i].info.mm_width
		output_sizes[i][4] = outputs[i].info.mm_height
	end

	-- Step 2: set up constraints based on the configuration
	for i, config in ipairs(configs) do
		local dirs = {
			left  = {-1, 0}, -- -1 for x, 0 to the left
			right = {-1, 1}, -- -1 for x, 1 to the right
			up    = {0, 0}, -- 0 for y, 0 up
			down  = {0, 1}, -- 0 for y, 1 down
		}
		for k,v in pairs(dirs) do
			if config[k] ~= nil then
				for _, other in pairs(config[k]) do
					local other_output = find_output_impl(monitors, other)
					local other_index = output_to_index[other_output.id]
					local diff1 = output_sizes[i][v[1] + 2] * v[2] + output_sizes[other_index][v[1] + 2] * (1 - v[2])
					local diff2 = output_sizes[i][v[1] + 4] * v[2] + output_sizes[other_index][v[1] + 4] * (1 - v[2])
					if v[2] == 0 then
						table.insert(constraints_list[4 * i + v[1] - 2], 4 * other_index + v[1] - 2 - 1)
						table.insert(constraints_list[4 * i + v[1] - 2], -diff1)
						table.insert(constraints_list[4 * i + v[1]]    , 4 * other_index + v[1] - 1)
						table.insert(constraints_list[4 * i + v[1]]    , -diff2)
					else
						table.insert(constraints_list[4 * other_index + v[1] - 2],  4 * i + v[1] - 2 - 1)
						table.insert(constraints_list[4 * other_index + v[1] - 2], -diff1)
						table.insert(constraints_list[4 * other_index + v[1]]    ,  4 * i + v[1] - 1)
						table.insert(constraints_list[4 * other_index + v[1]]    , -diff2)
					end
				end
			end
		end
	end

	-- Step 2.5: solve for screen positions
	local solution = di.misc:difference_constraints(constraints_list)

	-- Step 3: allocate views to outputs
	-- We do this with a bipartite match.
	local view_to_index = {}
	local screen_resources = self.screen_resources
	local all_views = screen_resources.views
	for i, view in ipairs(all_views) do
		view_to_index[view.id] = i
	end
	local possible_views = {}
	for i, output in ipairs(outputs) do
		possible_views[i] = {}
		for _, view in ipairs(output.views) do
			table.insert(possible_views[i], view_to_index[view.id] - 1)
		end
	end
	local selected_views = di.misc:bipartite_match(possible_views)
	local output_xid_to_view = {}
	local view_to_output = {}
	for i, view in ipairs(selected_views) do
		if view == -1 then
			error("Unable to allocate view for output " .. outputs[i].info.name)
		else
			selected_views[i] = all_views[view + 1]
			output_xid_to_view[outputs[i].id] = selected_views[i]
			-- print("Selected view " .. selected_views[i].id .. " for output " .. outputs[i].info.name)
		end
	end

	-- Step 4: calcuate and set up total screen size
	local min = {}
	local max = {}
	for i, config in ipairs(configs) do
		for k = 0, 3 do
			if min[4 - k] == nil or solution[4 * i - k] < min[4 - k] then
				min[4 - k] = solution[4 * i - k]
			end
			if max[4 - k] == nil or solution[4 * i - k] + output_sizes[i][4 - k] > max[4 - k] then
				max[4 - k] = solution[4 * i - k] + output_sizes[i][4 - k]
			end
		end
	end
	-- print("Total output size: " .. (max[1] - min[1]) .. "x" .. (max[2] - min[2]))
	-- print("Total output size in mm: " .. (max[3] - min[3]) .. "x" .. (max[4] - min[4]))

	-- Before setting the screen size, we need to turn off all current outputs that doesn't fit
	-- in the target screen size
	for _, output in ipairs(screen_resources.outputs) do
		local view = output.current_view
		local view_config = view.config
		if view_config ~= nil then
			local x = view_config.x
			local y = view_config.y
			local width = view_config.width
			local height = view_config.height
			if x < 0 or x + width > max[1] - min[1] or y < 0 or y + height > max[2] - min[2] or
			   output_xid_to_view[output.id].id ~= view.id  then
				-- print("Turning off output " .. output.info.name)
				-- print("Output current view: " .. view.id .. ", want: " .. output_xid_to_view[output.id].id)
				view.config = {
					x = 0,
					y = 0,
					mode = 0,
					reflection = 0,
					rotation = 0,
					outputs = {}
				}
			end
		end
	end

	self.screen_size = {
		width = max[1] - min[1],
		height = max[2] - min[2],
		mm_width = max[3] - min[3],
		mm_height = max[4] - min[4],
	}
	for i, config in ipairs(configs) do
		local x = solution[4 * i - 3] - min[1]
		local y = solution[4 * i - 2] - min[2]
		local width = output_sizes[i][1]
		local height = output_sizes[i][2]
		-- print("Setting output " .. outputs[i].info.name .. " to +" .. x .. "+" .. y .. "+" .. width .. "x" .. height)
		local x_mm = solution[4 * i - 1] - min[3]
		local y_mm = solution[4 * i] - min[4]
		local width_mm = output_sizes[i][3]
		local height_mm = output_sizes[i][4]
		-- print("Output physical dimensions: +" .. x_mm .. "+" .. y_mm .. "+" .. width_mm .. "x" .. height_mm)
		selected_views[i].config = {
			x = x,
			y = y,
			mode = output_sizes[i][5].id,
			reflection = 0,
			rotation = 0,
			outputs = {outputs[i].id},
		}
	end
end

end -- if di.misc ~= nil

end -- if di.hwinfo ~= nil

return exports
