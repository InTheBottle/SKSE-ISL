#pragma once

#include "PCH.h"

namespace isl {

    struct Stats {
        std::atomic<std::uint32_t> lighConverted{ 0 };
        std::atomic<std::uint32_t> lighSkippedAlreadyISL{ 0 };
        std::atomic<std::uint32_t> lighSkippedMath{ 0 };
        std::atomic<std::uint32_t> lighSkippedLightPlacer{ 0 };
        std::atomic<std::uint32_t> lighSkippedSpot{ 0 };

        std::atomic<std::uint32_t> refrCellsProcessed{ 0 };
        std::atomic<std::uint32_t> refrConverted{ 0 };
        std::atomic<std::uint32_t> refrSkippedMath{ 0 };
        std::atomic<std::uint32_t> refrSkippedLightPlacer{ 0 };
        std::atomic<std::uint32_t> refrSkippedSpot{ 0 };
        std::atomic<std::uint32_t> refrSkippedPersistent{ 0 };

        void Reset() noexcept
        {
            lighConverted = 0;
            lighSkippedAlreadyISL = 0;
            lighSkippedMath = 0;
            lighSkippedLightPlacer = 0;
            lighSkippedSpot = 0;
            refrCellsProcessed = 0;
            refrConverted = 0;
            refrSkippedMath = 0;
            refrSkippedLightPlacer = 0;
            refrSkippedSpot = 0;
            refrSkippedPersistent = 0;
        }
    };

    // Global feature toggle (persisted as ini-style config).
    struct Config {
        bool  enabled             = true;
        bool  convertRefrs        = true;
        bool  boostShadowCasters  = true;
        bool  excludeLightPlacer  = true;
        bool  excludeSpotLights   = true;
        float shadowBoost         = 8.0f;

        void Load();
        void Save() const;
    };

    Config& GetConfig();
    Stats&  GetStats();

    // Live-scales every converted shadow-caster LIGH fade by newBoost / oldBoost.
    void SetShadowBoost(float newBoost);

    // Scans Data/LightPlacer/**/*.json for "light" EditorIDs; resolved LIGH
    // bases are skipped everywhere (LIGH pass, REFR pass, live boost).
    void LoadLightPlacerExclusions();

    // Rewrites every TESObjectLIGH to ISL. Idempotent via the ISL flag bit.
    void ConvertAllLights();

    // Converts LIGH-based REFRs in cell. Called from the cell-attach sink.
    void ConvertCellRefs(RE::TESObjectCELL* cell);

    // Wires SKSE messaging + event sinks. Call once from SKSEPlugin_Load.
    void Install();

}  // namespace isl
