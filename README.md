# SKSE ISL — Runtime Inverse Square Lighting Converter

Standalone SKSE plugin that performs the conversion at runtime — no patch ESP required.

Designed to pair with the
[Community Shaders](https://github.com/doodlum/skyrim-community-shaders)
**Inverse Square Lighting** feature. The plugin rewrites vanilla `LIGH`
records on data load so their falloff matches ISL, and optionally rewrites
per-placement `XLIG`/`XRDS`/`XSCL` overrides on cell attach.

---

## Requirements

- Skyrim SE / AE / VR (builds a multi-runtime DLL by default)
- [SKSE64](https://skse.silverlock.org/) matching your game version
- [Community Shaders](https://www.nexusmods.com/skyrimspecialedition/mods/86492)
  with the Inverse Square Lighting add-on installed
- Optional: [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352)
  for in-game configuration UI

## Installing (end-user)

1. Drop `SKSE_ISL.dll` into `Data/SKSE/Plugins/`.
2. Launch. A config panel appears under `SKSE ISL → General` in the
   SKSE Menu Framework window if that framework is installed.
3. Settings are persisted to `Data/SKSE/Plugins/SKSE_ISL.ini`.

## Building

```powershell
# Prerequisites
#   - Visual Studio 2022 with Desktop C++ workload
#   - CMake 3.21+
#   - vcpkg, with VCPKG_ROOT set

git clone <this repo> SKSE-ISL
cd SKSE-ISL

# Drop the SKSE Menu Framework header here (optional, but enables UI)
#   include/SKSEMenuFramework.h
# Without it, the plugin still builds and still performs conversion —
# only the config panel is disabled.

cmake --preset ALL
cmake --build build/ALL --config Release
```

The resulting DLL will be at
`build/ALL/Release/SKSE_ISL.dll`.

### Auto-deploy to your game install(s)

Either pass `-DISL_OUTPUT_DIRS=...` on the CMake command line or set it in a
`CMakeUserPresets.json`, using a semicolon-separated list of `Data/`
directories:

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "ALL-deploy",
            "inherits": "ALL",
            "cacheVariables": {
                "ISL_OUTPUT_DIRS": "F:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data;F:/MO2/mods/SKSE_ISL"
            }
        }
    ]
}
```

## How it works

**LIGH pass** (runs on `kDataLoaded`, and on demand from the UI):
Walks every `TESObjectLIGH` in the data handler. For each one not already
flagged Inverse Square, and not excluded as a LightPlacer, magic, projectile,
explosion, hazard, window/glow/fx editor-ID match, or emittance light, it solves

$$
s = \sqrt{\frac{c \cdot r^2}{1960\,(F - c)}},
\qquad
I = \frac{F \cdot s^2}{8}
$$

where `F` = vanilla fade (`FNAM`), `r` = radius (`DATA\Radius`),
`c` = cutoff (0.05 normally, 0.022 for shadow casters), and `I` = the
radius-matched ISL fade. Writes either `I` or the original `F` back to `FNAM`
after the global intensity multiplier, `s` to `DATA\FOV`, `c` to
`DATA\Falloff Exponent`, and sets the Inverse Square flag bit (`0x4000`) in
`DATA\Flags`. Radius matching is enabled by default; changing that mode takes
effect on the next data load.

**REFR pass** (runs lazily on `TESCellAttachDetachEvent`):
For every placed light in a newly-attached cell, reads `XSCL`, `ExtraRadius`
(`XRDS`), and `ExtraLightData` (`XLIG`) and rewrites the `ExtraLightData`
fields under ISL semantics:

- `ExtraLightData::fov` → absolute ISL size override (0 = inherit base)
- `ExtraLightData::fade` → intensity **delta** from the converted base

## Limitations / known caveats

- REFR pass is heuristic because already-saved `XLIG` overrides can be
  ambiguous. Disable `Convert per-placement REFR overrides` in the UI if you
  see artifacts.
- Magic, projectile, explosion, hazard, window/glow/fx editor-ID matches, and
  emittance lights are left untouched because those bases are often spawned
  dynamically, attached to actor spell visuals, or authored as mesh glow.
