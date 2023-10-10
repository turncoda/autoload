# Autoload: automatically hot-reload paks on change

**Warning: This is alpha quality software. Filesystem corruption is possible,
so back up your files. Use at your own risk. Only tested with UE 5.1.**

Autoload is a C++ plugin for UE4SS. It works by watching for changes to a
folder of paks and hot-reloading pak files that have changed.

The selling point of this plugin is that you don't have to restart the game to
update assets.

Hot-reloading paks comes with some caveats. You should first understand how pak
patching works in Unreal Engine. Hot-reloading paks adds a bit more nuance on
top of that. For example, any existing instances of assets will not be updated
automatically. You'd have to destroy and re-instantiate them to see the updated
asset.

### Installation

1. Download the [latest
   release](https://github.com/turncoda/autoload/releases/latest) from this
   repository. I can only guarantee the mod will work if you use my bundled
   build of UE4SS; the official releases don't support C++ mods out of the box
   yet, so you can't just drop the mod into your installation of UE4SS and expect
   it to work.
1. If you have a pre-existing installation of UE4SS, back it up or remove it.
1. Install the version of UE4SS you just downloaded (i.e. extract all of the
   files into `<game>/Binaries/Win64`)

### Usage

1. Start with some pak files in `<game>\Content\Paks\Autoload`
1. When you want to update a pak file, create a new file in the directory with
   the same name but with '.staged' tacked on the end. At this point, Autoload
   should detect the new file and performs the following filesystem operations:
     1. Unmount the original pak
     1. Delete the original pak
     1. Rename the .staged pak to take its place
     1. Mount the new pak

**Warning: Your success with this will vary. Sometimes it will get stuck and
you just have to quit and try again. I'm working on making this more
reliable.**

### Tips

- Check the UE4SS logs to make sure Autoload is working properly.
- Autoload requires that you create a folder at `<game>\Content\Paks\Autoload`. This isn't created automatically.

### Building

1. `git clone --recursive <this repo>` or if already cloned without
`--recursive`, cd in and do `git submodule update --init --recursive`
    1. If you get an error about `RE-UE4SS/deps/first/Unreal` failing to clone,
    you can fix this by joining the Epic Games organization on Github.
1. `cmake -S . -B Output`
    1. This may fail with `CMake Error: Error required internal CMake variable
    not set, cmake may not be built correctly.` and `CMake Generate step
    failed. Build files cannot be regenerated correctly.` This is probably
    okay and you can safely ignore it.
1. Open `Output/Autoload.sln` in Visual Studio (tested with VS 2022)
1. Build > Build Solution (or Ctrl+Shift+B)
1. To deploy: Copy `Output/AutoloadMod/<build-config>/AutoloadMod.dll` to
   `<game>/Binaries/Win64/Mods/AutoloadMod/dlls/main.dll`

Source:
https://github.com/UE4SS-RE/RE-UE4SS/blob/852c937ac6445f7e865f0438e05faf2ae76d8e7d/docs/guides/creating-a-c%2B%2B-mod.md#creating-a-c-mod
