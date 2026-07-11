#pragma once

#include "PCH.h"

namespace isl {

    struct Stats {
        std::atomic<std::uint32_t> lighConverted{ 0 };
        std::atomic<std::uint32_t> lighSkippedAlreadyISL{ 0 };
        std::atomic<std::uint32_t> lighSkippedMath{ 0 };
        std::atomic<std::uint32_t> lighSkippedLightPlacer{ 0 };
        std::atomic<std::uint32_t> lighSkippedMagicFX{ 0 };
        std::atomic<std::uint32_t> lighSkippedSpot{ 0 };

        std::atomic<std::uint32_t> refrCellsProcessed{ 0 };
        std::atomic<std::uint32_t> refrConverted{ 0 };
        std::atomic<std::uint32_t> refrSkippedMath{ 0 };
        std::atomic<std::uint32_t> refrSkippedLightPlacer{ 0 };
        std::atomic<std::uint32_t> refrSkippedMagicFX{ 0 };
        std::atomic<std::uint32_t> refrSkippedSpot{ 0 };
        std::atomic<std::uint32_t> refrSkippedPersistent{ 0 };
        std::atomic<std::uint32_t> refrReverted{ 0 };

        void Reset() noexcept
        {
            lighConverted = 0;
            lighSkippedAlreadyISL = 0;
            lighSkippedMath = 0;
            lighSkippedLightPlacer = 0;
            lighSkippedMagicFX = 0;
            lighSkippedSpot = 0;
            refrCellsProcessed = 0;
            refrConverted = 0;
            refrSkippedMath = 0;
            refrSkippedLightPlacer = 0;
            refrSkippedMagicFX = 0;
            refrSkippedSpot = 0;
            refrSkippedPersistent = 0;
            refrReverted = 0;
        }
    };

    // User config, persisted ini-style.
    struct Config {
        bool  enabled             = true;
        bool  convertRefrs        = true;
        bool  radiusMatchedFade   = true;
        // Legacy toggle; Load() migrates a saved "off" to shadowBoost = 1.0.
        bool  boostShadowCasters  = true;
        bool  excludeLightPlacer  = true;
        float intensityScale      = 1.0f;
        float shadowBoost         = 8.0f;
        // Regular-light falloff cutoff; shadow casters track at the default 0.022/0.05 ratio.
        float cutoff              = 0.05f;

        void Load();
        void Save() const;
    };

    Config& GetConfig();
    Stats&  GetStats();

    // Live-scales every converted shadow-caster LIGH fade by newBoost / oldBoost.
    void SetShadowBoost(float newBoost);

    // Live-scales every converted LIGH fade by newScale / oldScale.
    void SetIntensityScale(float newScale);

    // Re-solves every converted LIGH and REFR delta for a new falloff cutoff.
    void SetCutoff(float newCutoff);

    // Scans Data/LightPlacer/**/*.json for light EditorIDs; resolved LIGH bases are skipped everywhere.
    void LoadLightPlacerExclusions();

    // Collects magic/projectile/explosion/hazard lights and conservative editor-ID/emittance skips.
    void LoadMagicFXExclusions();

    // Rewrites every TESObjectLIGH to ISL. Idempotent via the ISL flag bit.
    void ConvertAllLights();

    // Converts LIGH-based REFRs in cell. Called from the cell-attach sink.
    void ConvertCellRefs(RE::TESObjectCELL* cell);

    // Wires SKSE messaging + event sinks. Call once from SKSEPlugin_Load.
    void Install();

}  // namespace isl
