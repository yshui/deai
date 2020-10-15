if di.os.env.EXECED == nil then
    di.os.env.EXECED = "1"
    di:exec(di.argv)
    assert(false)
end
