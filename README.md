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
   SKSE Menu Framework window if that framework is installed. The main view
   holds the master toggle and three sliders (falloff curve, intensity,
   shadow-caster boost); everything else lives in the collapsed `Advanced`
   and `Statistics` sections. Hover any control for an explanation.
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
`c` = cutoff (0.05 by default, 0.022 for shadow casters), and `I` = the
radius-matched ISL fade. Writes either `I` or the original `F` back to `FNAM`
after the global intensity multiplier, `s` to `DATA\FOV`, `c` to
`DATA\Falloff Exponent`, and sets the Inverse Square flag bit (`0x4000`) in
`DATA\Flags`. Radius matching is enabled by default; changing that mode takes
effect on the next data load.

**Falloff curve slider:** the cutoff `c` is the light level at which a
light's reach ends, and it is what shapes the ISL curve — lower values give a
flatter, more realistic curve that carries further; higher values give tight
light pools with less bleed. The `Falloff curve` slider sets `c` for regular
lights (shadow casters track proportionally at the tuned 0.022/0.05 ratio)
and re-solves every converted light from its stored original fade, so `I`,
`s`, and `c` stay consistent with each other. Community Shaders clamps the
per-light cutoff to `[0.01, 1.0]` and treats exactly `1.0` as "unset", so the
slider range is capped to `[0.01, 0.30]`. Fade changes are visible
immediately; the new cutoff/size are cached per light at creation, so a scene
shows the full new curve after it reloads (save/load or cell transition).

**REFR pass** (runs lazily on `TESCellAttachDetachEvent`):
For every placed light in a newly-attached cell, reads `XSCL`, `ExtraRadius`
(`XRDS`), and `ExtraLightData` (`XLIG`) and rewrites the `ExtraLightData`
fields under ISL semantics:

- `ExtraLightData::fov` → absolute ISL size override (0 = inherit base)
- `ExtraLightData::fade` → intensity **delta** from the converted base

Refs that carry this plugin's authored `ExtraLightData` but are excluded
under current rules (spot lights, non-converted/foreign bases, persistent,
emittance-driven) are automatically reverted to unset defaults when their
cell attaches. This heals saves written by older builds whose exclusion
rules were narrower — e.g. spotlight refs whose beam angle was clobbered by
an ISL size, which made spot lights look like they were no longer excluded.

## Particle lights (Community Shaders)

Community Shaders' Light Limit Fix can turn configured particle effects
(candle flames, embers, torches, magic) into dynamic lights, driven by
texture-matched configs in `Data\ParticleLights\*.ini`. Those lights are
built from mesh geometry — no `LIGH` records are involved — so this plugin
neither converts nor breaks them:

- Particle lights keep their own simple falloff and brightness sliders in the
  Community Shaders menu; this plugin's sliders only affect converted `LIGH`
  lights. Use the two sets of sliders together to balance fixtures that have
  both a real placed light and a particle flame.
- Cutoffs written by this plugin always stay inside Community Shaders' clamp
  range (`[0.01, 1.0)`), so the value we solve against is the value the
  renderer actually uses.
- Decorative glow/window/fx bulbs (which particle-light packs typically cover
  visually) are excluded from conversion by editor-ID heuristics, and
  emittance-driven placements are skipped in the REFR pass.

## Limitations / known caveats

- REFR pass is heuristic because already-saved `XLIG` overrides can be
  ambiguous. Disable `Convert per-placement REFR overrides` in the UI if you
  see artifacts.
- Magic, projectile, explosion, hazard, window/glow/fx editor-ID matches, and
  emittance lights are left untouched because those bases are often spawned
  dynamically, attached to actor spell visuals, or authored as mesh glow.
