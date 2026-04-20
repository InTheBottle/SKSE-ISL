#include "PCH.h"
#include "MenuIntegration.h"
#include "Conversion.h"

#if __has_include(<SKSEMenuFramework.h>)
#    include <SKSEMenuFramework.h>
#    define ISL_HAS_MENU_FRAMEWORK 1
#else
#    define ISL_HAS_MENU_FRAMEWORK 0
#endif

namespace isl {

#if ISL_HAS_MENU_FRAMEWORK

    namespace {
        void __stdcall RenderPanel()
        {
            auto& cfg   = GetConfig();
            auto& stats = GetStats();

            ImGuiMCP::SeparatorText("Inverse Square Lighting — Runtime Conversion");
            ImGuiMCP::TextWrapped(
                "Rewrites vanilla LIGH records (and per-placement overrides) "
                "on data load so Community Shaders' Inverse Square Lighting "
                "falloff matches vanilla peak brightness and radius.");

            ImGuiMCP::Spacing();

            bool enabled = cfg.enabled;
            if (ImGuiMCP::Checkbox("Enabled (takes effect next load)", &enabled)) {
                cfg.enabled = enabled;
                cfg.Save();
            }

            bool refrs = cfg.convertRefrs;
            if (ImGuiMCP::Checkbox("Convert per-placement REFR overrides", &refrs)) {
                cfg.convertRefrs = refrs;
                cfg.Save();
            }

            bool boost = cfg.boostShadowCasters;
            if (ImGuiMCP::Checkbox("Boost shadow-caster intensity", &boost)) {
                cfg.boostShadowCasters = boost;
                cfg.Save();
                SetShadowBoost(cfg.shadowBoost);
            }

            float boostValue = cfg.shadowBoost;
            if (ImGuiMCP::SliderFloat("Shadow boost multiplier",
                                       &boostValue, 0.1f, 32.0f, "%.2fx"))
            {
                cfg.shadowBoost = boostValue;  // stash live; apply on release
            }
            if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
                cfg.Save();
                if (cfg.boostShadowCasters)
                    SetShadowBoost(cfg.shadowBoost);
            }

            bool excludeLP = cfg.excludeLightPlacer;
            if (ImGuiMCP::Checkbox("Exclude LightPlacer-managed light bases", &excludeLP)) {
                cfg.excludeLightPlacer = excludeLP;
                cfg.Save();
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Rescan LP JSONs")) {
                LoadLightPlacerExclusions();
            }

            bool excludeSpot = cfg.excludeSpotLights;
            if (ImGuiMCP::Checkbox("Exclude spotlights entirely", &excludeSpot)) {
                cfg.excludeSpotLights = excludeSpot;
                cfg.Save();
            }

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText("Session stats");

            ImGuiMCP::Text("LIGH converted        : %u", stats.lighConverted.load());
            ImGuiMCP::Text("LIGH already ISL      : %u", stats.lighSkippedAlreadyISL.load());
            ImGuiMCP::Text("LIGH math out-of-range: %u", stats.lighSkippedMath.load());
            ImGuiMCP::Text("LIGH skipped (LP)     : %u", stats.lighSkippedLightPlacer.load());
            ImGuiMCP::Text("LIGH skipped (spot)   : %u", stats.lighSkippedSpot.load());
            ImGuiMCP::Text("REFR cells processed  : %u", stats.refrCellsProcessed.load());
            ImGuiMCP::Text("REFR converted        : %u", stats.refrConverted.load());
            ImGuiMCP::Text("REFR math skipped     : %u", stats.refrSkippedMath.load());
            ImGuiMCP::Text("REFR skipped (LP)     : %u", stats.refrSkippedLightPlacer.load());
            ImGuiMCP::Text("REFR skipped (spot)   : %u", stats.refrSkippedSpot.load());
            ImGuiMCP::Text("REFR skipped (persist): %u", stats.refrSkippedPersistent.load());

            ImGuiMCP::Spacing();
            if (ImGuiMCP::Button("Re-run LIGH pass now")) {
                ConvertAllLights();
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Reset stats")) {
                stats.Reset();
            }

            ImGuiMCP::Spacing();
            ImGuiMCP::TextDisabled("Tip: toggling 'Enabled' only takes effect on next data load.");
        }
    }  // namespace

    void RegisterMenuFramework()
    {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::info("[ISL] SKSE Menu Framework not installed — skipping UI.");
            return;
        }
        SKSEMenuFramework::SetSection("SKSE ISL");
        SKSEMenuFramework::AddSectionItem("General", RenderPanel);
        logger::info("[ISL] SKSE Menu Framework panel registered.");
    }

#else  // ISL_HAS_MENU_FRAMEWORK

    void RegisterMenuFramework()
    {
        logger::info("[ISL] Built without SKSEMenuFramework.h — UI disabled. "
                     "Drop the header into include/ and rebuild to enable.");
    }

#endif

}  // namespace isl
