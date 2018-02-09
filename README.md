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
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <latedl>

public Plugin myinfo = 
{
	name = "My First Plugin", 
	author = "Me", 
	description = "My first plugin ever", 
	version = "1.0", 
	url = "http://www.sourcemod.net/"
};

public void OnPluginStart()
{
	RegAdminCmd("testdl", Command_TestDL, ADMFLAG_SLAY);
}

public void OnDownloadSuccess(int iClient, char[] filename) {
	if (iClient > 0)
		return;
	PrintToServer("All players successfully downloaded file '%s'!", filename);
}

public Action Command_TestDL(int client, int args)
{
	//create arbitrary file
	int time = GetTime();
	char tstr[64];
	FormatEx(tstr, 64, "%d.txt", time);
	File file = OpenFile(tstr, "wt", true, NULL_STRING);
	WriteFileString(file, tstr, false);
	CloseHandle(file);
	
	//send
	AddLateDownload(tstr);
	return Plugin_Handled;
}
```
## Extension configuration
The extension exposes following cvars:
* `latedl_minimalbandwidth` (default = 64) - Kick clients with lower bandwidth (in kbps). Zero to disable.
* `latedl_maximaldelay` (default = 500) - Acceptable additional delay (in ms) when sending files.
* `latedl_requireupload` (default = 1) - Kick clients with "sv_allowupload" = 0. Zero to disable.

The first two cvars limit the maximal time that the download can take. The maximal duration (in seconds) is computed using following formula: `maximalDelay / 1000 + (fileSizeInBytes * 8) / (minimalBandwidth * 1000)`

If the player fails to download the file in time, he's kicked.

The last cvar kicks any player that rejects incoming files.
## Additional information
* This was tested only in CS:GO, but any modern Source game (OrangeBox+) should be ok.
* This extension used to be a part of the [Gorme](https://github.com/jonatan1024/gorme) project.
* 8. 2. 2018 Valve torpedoed this extension by [defaulting sv_allowupload to zero](http://blog.counter-strike.net/index.php/2018/02/20051/).
