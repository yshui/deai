<!-- markdown-toc start - Don't edit this section. Run M-x markdown-toc-refresh-toc -->
**Table of Contents**

- [deai](#deai)
    - [Documentation](#documentation)
    - [Build and Run](#build-and-run)
        - [Build Dependencies](#build-dependencies)
        - [Usage](#usage)
    - [Current features](#current-features)
    - [Planned features](#planned-features)
    - [Contact](#contact)

<!-- markdown-toc end -->
# deai

[![Codecov](https://img.shields.io/codecov/c/github/yshui/deai.svg)](https://codecov.io/gh/yshui/deai) [![CircleCI](https://circleci.com/gh/yshui/deai.svg?style=shield&circle-token=75b416f5709a1179e1a817e7fb5568c5814d51ee)](https://circleci.com/gh/yshui/deai) [![Documentation Status](https://readthedocs.org/projects/deai/badge/?version=latest)](https://deai.readthedocs.io/en/latest/?badge=latest)

**deai** is a tool to automate your Linux desktop. It tries to expose common events and interfaces of a Linux system to scripting languages, to enable users to automate tasks with event-driven scripts. Example could be changing screen brightness with time-of-day, or automatically mounting/unmounting removable storage.

Compared unlike using shell scripts, deai is a single tool, rather than a collection of different commands created by different people, so it's more consistent. And handling events with deai's interface is much nicer than reading and parsing text output from commands.

**!!!Warning!!!** deai is currently under heavy development. Things might break or might not work correctly. If you are thinking about creating plugins for deai, please consider contribute directly to this repository, or wait until deai is stable. This is because neither the API nor the ABI of deai has been finalized. New changes to deai could break your plugins.

## Documentation

Most of deai is documented [here](https://deai.readthedocs.io/en/latest/)

There are also a few examples given here. If you need more information, you can [ask me](#contact)

## Build and Run

### Build Dependencies

* libev
* libudev (optional, for the `udev` plugin)
* dbus-libs (optional, for the `dbus` plugin)
* xorg (optional, for the `xorg` plugin)
    * xcb
    * xcb-randr
    * xcb-xinput
    * xcb-xkb
    * libxkbcommon
    * xcb-util-keysyms
* lua (optional, for the `lua` plugin)
* libinotify (optional, for the `file` plugin)

### Usage

```sh
/path/to/deai module.method arguments...
```

A more detailed explanation of how the command line arguments works can be found
[here](https://deai.readthedocs.io/en/latest/how_to_run.html)

## Current features

Right now the only supported scripting language is Lua, so the examples will be give in Lua.

* Launching programs

  ```lua
  -- "di" is how you access deai functionality in lua
  -- "di.spawn" refers to the "spawn" module
  -- "run" is the method that executes program
  p = di.spawn:run({"ls", "-lh"})
  p:on("stdout_line", function(line)
      print("output: ", line)
  end)
  p:on("exit", function()
      -- This tells deai to exit
      di:quit()
  end)
  ```

* Set timer

  ```lua
  di.event:timer(10):on("elapsed", function()
      print("Time flies!")
  end)
  ```

* Change/set environment variables

  ```lua
  di.os.env["PATH"] = "/usr"
  ```

* Watch file changes

  (See [this](https://deai.readthedocs.io/en/latest/generated/deai.plugin.file%3AWatch.html#signals) for all possible signals)
  ```lua
  watcher = di.file:watch({"."})
  watcher:on("open", function(dir, filepath)
      print(dir, filepath)
  end)
  ```

* Connect to Xorg

  ```lua
  -- Connect to Xorg is the first step to get X events
  xc = di.xorg:connect()
  -- You can also use :connect_to(DISPLAY)
  ```

* Set xrdb

  ```lua
  -- Assuming you have connected to X
  xc.xrdb = "Xft.dpi:\t192\n"
  ```

* X Key bindings

  (See [this](https://deai.readthedocs.io/en/latest/generated/deai.plugin.xorg%3AKey.html) for more information)

  ```lua
  -- Map ctrl-a
  xc.key:new({"ctrl"}, "a", false):on("pressed", function()
      -- do something
  end)
  ```

* Get notified for new input devices

  ```lua
  xc.xinput:on("new-device", function(dev)
      print(dev.type, dev.use, dev.name, dev.id)
      -- do something about the device
  end)
  ```

* Change input device properties

  (See [this](https://deai.readthedocs.io/en/latest/generated/deai.plugin.xorg.xi%3ADevice.html#module-deai.plugin.xorg.xi.Device) for more information)

  ```lua
  -- Assuming you get a dev from an "new-device" event
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

  (See [this](https://deai.readthedocs.io/en/latest/generated/deai.plugin.xorg%3ARandrExt.html) for more information)

  ```lua
  -- Note: RandR support is not quite done
  xc.randr:on("view-change", function(v)
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

* **dbus support:** Lots of the interfaces are now exposed via dbus, such as **UDisks** to manage removable storage, **UPower** for power management. So obviously dbus support is a must have.

* **Audio:** Support adjust volumes, etc., via **ALSA** or **Pulseaudio**

* **Network:** Support for network events and react to them. For example, automatically connect to VPN after switching to an open WiFi.

* **Power management:** Reacts to power supply condition changes, etc.

* **UI components:** Allows you to create tray icons, menus, etc. So you can interact with **deai** using a GUI.

* **More languages:** Support everyone's favourite scripting languages!

* **And more...** If you want something, just open an issue.

## Contact

* Email: yshuiv7 at gmail dot com

