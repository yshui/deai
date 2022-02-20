<!-- markdown-toc start - Don't edit this section. Run M-x markdown-toc-refresh-toc -->
**Table of Contents**

- [deai](#deai)
    - [Background](#background)
    - [Documentation](#documentation)
    - [Design](#design)
    - [Current features](#current-features)
    - [Planned features](#planned-features)
    - [Build and Run](#build-and-run)
        - [Build Dependencies](#build-dependencies)
        - [Usage](#usage)
    - [Contact](#contact)

<!-- markdown-toc end -->
# deai

[![Codecov](https://img.shields.io/codecov/c/github/yshui/deai.svg)](https://codecov.io/gh/yshui/deai) [![CircleCI](https://circleci.com/gh/yshui/deai.svg?style=shield&circle-token=75b416f5709a1179e1a817e7fb5568c5814d51ee)](https://circleci.com/gh/yshui/deai) [![Gitter](https://img.shields.io/gitter/room/nwjs/nw.js.svg)](https://gitter.im/deai/Lobby?utm_source=share-link&utm_medium=link&utm_campaign=share-link)

**deai** is a tool to automate your Linux desktop. It has similar goals as [hammerspoon](https://github.com/Hammerspoon/hammerspoon**. But it aims to be more extensible and support more languages.

**!!!Warning!!!** deai is currently under heavy development. Things might break or might not work correctly. If you are thinking about creating plugins for deai, please consider contribute directly to this repository, or wait until deai is stable. This is because neither the API nor the ABI of deai has been finalized. Your plugin could break after every new commit.

## Background

Desktop Environments (DE) are great, as long as your way of using your computer is the same as the designer of the DE. Otherwise you will have a hard time bending the DE to your will, and the end result might still be unsatisfying. That's why a lot of people choose to carefully hand pick all the tools they use, and build their own "Desktop Environments".

However, with Desktop Environments come great integration. The various tools come with the DE are usually designed to work together. So you can do things like dimming the screen when you unplug, applying settings when you plug in a new device, etc. Building a customized "DE" means sacrificing that integration. Sure, one can try to glue things together by writing a bunch of shell scripts, but that will not be officially supported, and a headache to maintain.

So I decided to make **deai**. **deai** tried to expose common desktop events and interfaces to scripting languages, so users can write scripts to react to those events. This way the users will be able to implement a lot of the "DE features" with a scripting language they like. And unlike using shell scripts, the users don't need to trust a gazillion different command line tools anymore, which will leads to easier maintenance.

## Documentation

The documentation is WIP. Currently I want to spend more time on implementing the features, but I understand documentation is very important. I just don't have enough time.

I hope the few examples given here are self explanatory enough, if not, you can always find ways to ask me in [Contact](#contact)

## Design

**deai** is designed to be really extensible. Except those features that depends on **libev**, all other features are implemented in plugins, and exposed in **deai** as modules. If you don't need certain feature, you can simply remove the plugin. This also means you don't need approval to add new features to **deai**. You can do it completely independently, and ship it as a plugin.

In order to interface with scripting languages nicely, **deai** uses a dynamic type system that mimics the "object" concept found in languages like JavaScript, Lua, or Python. This does make developing for **deai** in C slightly harder, but it also means I only need to develop a feature once, and have it seamleesly shared between all supported languages.

## Current features

Right now the only supported scripting language is Lua, so the examples will be give in Lua.

* Launch programs

```lua
-- "di" is how you access deai functionality in lua
-- "di.spawn" refers to the "spawn" module
-- "run" is the method that executes program
p = di.spawn.run({"ls", "-lh"})
p.on("stdout_line", function(_, line)
    print("output: ", line)
end)
p.on("exit", function()
    -- This cause deai to exit
    di.quit()
end)

```

* Set timer

```lua
di.event.timer(10).on("elapsed", function()
    print("Time flies!")
end)
```

* Set environment variables

```lua
di.env["PATH"] = "/usr"
```

* Watch file changes

```lua
watcher = di.file.watch({"."})
watcher.on("open", function(_, dir, filepath)
    print(dir, filepath)
end)
watcher.on("modify"), -- similar
)
```

* Connect to Xorg

```lua
-- Connect to Xorg is the first step to get X events
xc = di.xorg.connect()

-- You can also use .connect_to(DISPLAY)
```

* Set xrdb

```lua
-- Assume you have connected to X
xc.xrdb = "Xft.dpi:\t192\n"
```

* X Key bindings

```lua
xc.key.new({"ctrl"}, "a", false -- false meaning don't replay the key
).on("pressed", function()
    -- do something
end)
```

* Get notified for new input devices

```lua
xc.xinput.on("new-device", function(_, dev)
    print(dev.type, dev.use, dev.name, dev.id)
    -- do something about the device
end)
```

* Change input device properties

```lua
-- Assume you get a dev from an "new-device" event
if dev.type == "touchpad" then
    -- For property names, see libinput(4)
    dev.props["libinput Tapping Enabled"] = {1}
end

if dev.name == "<<<Some touchscreen device name here>>>" then
    -- Map your touchscreen to an output, if you use multiple
    -- monitors, you will understand the problem.
    M = compute_transformation_matrix(touchscreen_output)
    dev.props["Coordinate Transformation Matrix"] = M
end
```

* Get notified when resolution change, or when new monitor is connected, etc.

```lua
-- Note: RandR support is not quite done
xc.randr.on("view-change", function(_, v)
    -- A "view" is a rectangular section of the X screen
    -- Each output (or monitor) is connected to one view
    for _, o in pairs(v.outputs) do
        -- But each view might be used by multiple outputs
        print(o.name)
    end
end)
```

* Adjust backlight

```lua
for _, o in pairs(xc.randr.outputs) do
    -- Backlight must be set with an integer, math.floor is required here
    o.backlight = math.floor(o.max_backlight/2)
end
```

## Planned features

I use the issue tracker to track the progress of these feature, so if you are interested, you can find more info there.

* **dbus support:** Lots of the interfaces are now exposed via dbus, such as **UDisks** to manage removable storage, **UPower** for power management. So obviously dbus support is a must have.

* **Audio:** Support adjust volumes, etc., via **ALSA** or **Pulseaudio**

* **Network:** Support for network events and react to them. For example, automatically connect to VPN after switching to an open WiFi.

* **Power management:** Reacts to power supply condition changes, etc.

* **UI components:** Allows you to create tray icons, menus, etc. So you can interact with **deai** using a GUI.

* **More languages:** Support everyone's favourite scripting languages!

* **And more...** If you want something, just open an issue.

## Build and Run

### Build Dependencies

* libev
* libudev
* dbus-libs
* xorg
    * xcb
    * xcb-randr
    * xcb-xinput
    * xcb-xkb
    * libxkbcommon
    * xcb-util-keysyms
* lua

In the future, you will be able to build only the plugins you want.

### Usage

```sh
/path/to/deai module.method arguments...
```

A more detailed explanation of how the command line arguments works can be found
[in the wiki](https://github.com/yshui/deai/wiki/Command-line)

## Contact

* IRC: #deai at freenode.com
* Email: yshuiv7 at gmail dot com

