# ArmorUnlimited Beta

ArmorUnlimited Plugin for Skyrim AE 1.6.1170
## Patreon Donations
- The following page is 18+ only because Skyrim has a 17+ age restriction on Steam.
- [Patreon Donation Page](https://www.patreon.com/posts/mod-github-repos-122104991?utm_medium=clipboard_copy&utm_source=copyLink&utm_campaign=postshare_creator&utm_content=join_link)
## Requirements

- Any terminal of your choice (e.g., PowerShell)
- [Visual Studio Community 2022](https://visualstudio.microsoft.com/)
  - Desktop development with C++
- [CMake](https://cmake.org/)
  - Edit the `PATH` environment variable and add the cmake.exe install path as a new value
  - Instructions for finding and editing the `PATH` environment variable can be found [here](https://www.java.com/en/download/help/path.html)
- [Git](https://git-scm.com/downloads)
  - Edit the `PATH` environment variable and add the Git.exe install path as a new value
- [Vcpkg](https://github.com/microsoft/vcpkg)
  - Install vcpkg using the directions in vcpkg's [Quick Start Guide](https://github.com/microsoft/vcpkg#quick-start-windows)
  - After install, add a new environment variable named `VCPKG_ROOT` with the value as the path to the folder containing vcpkg

## User Requirements
- [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
  - Needed for SSE/AE
- [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101)
  - Needed for VR

## Register Visual Studio as a Generator

- Open `x64 Native Tools Command Prompt`
- Run `cmake`
- Close the cmd window

## Clone and Build
Open terminal (e.g., PowerShell) and run the following commands:

```
git clone https://github.com/naitro2010/ArmorUnlimited --recursive
cd CommonLibSSE-NG-Template
.\BuildRelease.bat
```

### CMAKE Options (optional)
If you want an example CMakeUserPreset to start off with you can copy the `CMakeUserPresets.json.template` -> `CMakeUserPresets.json`
#### AUTO_PLUGIN_DEPLOYMENT
* This option is default `"OFF"`
* Make sure `"AUTO_PLUGIN_DEPLOYMENT"` is set to `"ON"` in `CMakeUserPresets.json`
* Change the `"SamplePluginOutputDir"` value to match your desired outputs, if you want multiple folders you can separate them by `;` is shown in the template example

When using custom preset you can call BuildRelease.bat with an parameter to specify which preset to configure eg:
`.\BuildRelease.bat ALL-WITH-AUTO-DEPLOYMENT`

When switching between different presets you might need to remove the build folder

## Rework notes

The plugin was rewritten from a single `XSEPlugin.cpp` full of version `#ifdef`s into a few small modules:

- `Offsets.h`: every game address as an Address Library ID. Only Skyrim AE 1.6.1170 is supported.
- `Extras.h/.cpp`: lifetime of the extra `BipedAnim`s that hold conflicting armor.
- `Hooks.cpp`: the game hooks.
- `XSEPlugin.cpp`: plugin entry and the `ArmorUnlimited_VisitExtraParts` export.

Behavior changes:

- The first armor in a slot stays on the actor's normal biped, so the game and other plugins see it. Only conflicting pieces go to an extra biped.
- Extra bipeds are destroyed together with their main biped instead of leaking with dangling pointers.
- The 3D update after an unequip runs on the main thread. Running it on Papyrus and AI threads raced the renderer and crashed in effect shader setup.
- No out-of-bounds reads on NPCs (the player-only field at `Actor+0x8F0`).

SKEE integration:

- skee64.dll is no longer patched. A SKEE build that wants morphs on extra bipeds calls the exported `ArmorUnlimited_VisitExtraParts(biped, visitor, context)` from its biped visitor. Stock SKEE works but doesn't morph extra bipeds.

Known issues:

- NPCs can still only wear one piece per conflicting slot.
- Hidden collision shapes that FSMP uses can become visible.
