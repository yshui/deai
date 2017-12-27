-- Multiply two matrices
-- @param m1 first matrix
-- @param m2 second matrix
-- @return the result matrix
function MatMul( m1, m2 )
    if #m1[1] ~= #m2 then       -- inner matrix-dimensions must agree
        return nil
    end
    local res = {}
    for i = 1, #m1 do
        res[i] = {}
        for j = 1, #m2[1] do
            res[i][j] = 0
            for k = 1, #m2 do
                res[i][j] = res[i][j] + m1[i][k] * m2[k][j]
            end
        end
    end
    return res
end

-- Get transformation matrix for touchscreen
-- @param cfg view configuration (width, height, rotation, etc.)
-- @param s the xorg screen
-- @return an array of float with length 9, the transformation matrix
function get_transform_matrix(cfg, s)
    -- move the origin to (0.5, 0.5)
    m1 = {{1, 0, -0.5},
        {0, 1, -0.5},
        {0, 0, 1}}
    -- rotation matrix
    r = cfg.rotation*math.pi/2.0
    m2 = {{math.cos(r), -math.sin(r), 0},
          {math.sin(r), math.cos(r), 0},
          {0, 0, 1}}
    -- move back
    m3 = {{1, 0, 0.5},
          {0, 1, 0.5},
          {0, 0, 1}}

    mrot = MatMul(m3, MatMul(m2, m1))

    mref = {{1,0,0},{0,1,0},{0,0,1}}
    if (cfg.reflection&1) == 1 then
        -- reflect x
        t = {{-1,0,1},{0,1,0},{0,0,1}}
        mref = MatMul(t, mref)
    end
    if (cfg.reflection&2) == 2 then
        -- reflect y
        t = {{1,0,0},{0,-1,1},{0,0,1}}
        mref = MatMul(t, mref)
    end

    mtr = {{cfg.width/s.width, 0, cfg.x/s.width},
        {0, cfg.height/s.height, cfg.y/s.height},
        {0, 0, 1}}

    M = MatMul(mtr, MatMul(mref, mrot))
    ret = {}
    for i = 1,3 do
        for j = 1,3 do
            ret[(i-1)*3+j] = M[i][j]
        end
    end
    return ret
end

function get_output(name)
    os = xc.randr.outputs
    for _, v in pairs(os) do
        if v.name == name then
            return v
        end
    end
    return nil
end
-- connect to xorg
xc = di.xorg.connect()
-- alternatively:
-- xc = di.xorg.connect_to(<display>)

-- Apply settings to xinput device
-- @param dev the device
function apply_xi_settings(dev)
    p = dev.props
    if dev.type == "touchpad" then
        p["libinput Tapping Enabled"] = {1}
        p["libinput Tapping Button Mapping Enabled"] = {0,1}
    end
    if dev.use == "pointer" then
        p["libinput Natural Scrolling Enabled"] = {1}
    end

    if dev.name == "ELAN Touchscreen" then
        -- assuming the touchscreen is eDP1
        o = get_output("eDP1")
        s = xc.screen

        tr = get_transform_matrix(o.view.config, s)
        print(unpack(tr))
        -- apply the transformation matrix to touchscreen
        p["Coordinate Transformation Matrix"] = tr
    end
end

xi = xc.xinput

-- add listener for new-device event
xi.on("new-device", function(dev)
    print("new device", dev.type, dev.use, dev.name, dev.id)
    -- apply settings to new device
    apply_xi_settings(dev)
end)


-- apply settings to existing devices
devs = xi.devices;
for i, v in ipairs(devs) do
    print(i, v.id, v.name, v.type, v.use)
    apply_xi_settings(v)

    if v.type == "touchscreen" then
    end
end
for _, v in pairs(xc.randr.outputs) do
    print("\t",v.name, v.view)
end

xc.randr.on("view-change", function(v)
    print("affected output: ")
    for _, v in pairs(v.outputs) do
        print("\t",v.name)
    end
end)
o = get_output("HDMI-A-0")
o.backlight = o.max_backlight
o = nil
