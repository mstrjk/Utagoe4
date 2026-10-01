# Utagoe 4.0
Reimagination of Utagoe by TODAKEN.


Utagoe pulls the vocal out of a song by subtracting the song's instrumental (off vocal / karaoke) version from the original mix.

## Download and run

1. Download the newest version of `Utagoe.exe` from the release page.
2. On first start it installs itself and adds a Start menu shortcut. It can be removed from Windows "Apps & features".

Needs 64-bit Windows 10 or 11. If the free Microsoft .NET Desktop Runtime 10 is missing, Utagoe downloads it and installs it first.

The exe is not code signed, so Windows SmartScreen may show an "unknown publisher" warning. Choose "More info", then "Run anyway".

Hover over any blue, underlined heading in Settings to see what it does.

## Build from source

Needs:

- 64-bit MinGW-w64 `g++`, CMake 3.21 or newer and Ninja, all on `PATH`
- .NET 10 SDK
- An internet connection the first time only

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

The script:

1. downloads the pinned codec sources into `third_party\` (`deps.ps1` checks each download's SHA-256);
2. builds the C++ core and runs its tests;
3. builds the app.

It produces:

- `dist\Utagoe.exe`: the installer.
- `utagoe-ui\bin\Release\net10.0-windows\win-x64\Utagoe.exe`: a development build.

## Layout

```
utagoe-core/   C++ signal processing core (utagoe_core.dll with a C API) and its tests
  setup/       the small native installer that wraps the app into dist\Utagoe.exe
utagoe-ui/     C# WinForms app (net10.0-windows, x64)
licenses/      licence texts bundled with the app
build.ps1      builds everything
deps.ps1       fetches third party sources
```

## Licences

Utagoe bundles libogg, libvorbis, Opus, opusfile, libopusenc, FLAC, dr_libs and minimp3. Their licence texts ship inside the app and are listed, with links, under About > Acknowledgements.

Have a blast.
