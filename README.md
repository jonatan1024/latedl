# Late Downloads
## What is this?
This is a [SourceMod](http://www.sourcemod.net/) extension that allows file transfers to players that are already in the game.
## How to build this?
Just as any other [AMBuild](https://wiki.alliedmods.net/AMBuild) project:
1. [Install AMBuild](https://wiki.alliedmods.net/AMBuild#Installation)
2. Download [Half-Life 2 SDK](https://github.com/alliedmodders/hl2sdk), [Metamod:Source](https://github.com/alliedmodders/metamod-source/) and [SourceMod](https://github.com/alliedmodders/sourcemod)
3. `mkdir build && cd build`
4. `python ../configure.py --hl2sdk-root=??? --mms-path=??? --sm-path=??? --sdks=csgo`
5. `ambuild`
## How to use this?
Simply copy the extension binary to the `extensions` folder and the include file into the `scripting/include` folder.
Now just create a new plugin and include [`latedl.inc`](include/latedl.inc).
## Sample script
```pawn
...
```
## Additional information
* This was tested only in CS:GO, but any modern Source game (OrangeBox+) should be ok.
* This extension used to be a part of the [Gorme](https://github.com/jonatan1024/gorme) project.
