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
        constexpr float kShadowSliderWidth = 240.0f;
        constexpr float kStatsCountColumnWidth = 96.0f;

        bool DrawSavedCheckbox(const char* label, bool& setting)
        {
            bool value = setting;
            if (!ImGuiMCP::Checkbox(label, &value))
                return false;

            setting = value;
            GetConfig().Save();
            return true;
        }

        void DrawStatRow(const char* label, const std::atomic<std::uint32_t>& value)
        {
            ImGuiMCP::TableNextRow();
            ImGuiMCP::TableNextColumn();
            ImGuiMCP::TextUnformatted(label);
            ImGuiMCP::TableNextColumn();
            ImGuiMCP::Text("%u", value.load());
        }

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
            ImGuiMCP::SeparatorText("Conversion");

            DrawSavedCheckbox("Enabled (takes effect next load)", cfg.enabled);
            DrawSavedCheckbox("Convert per-placement REFR overrides", cfg.convertRefrs);

            if (DrawSavedCheckbox("Boost shadow-caster intensity", cfg.boostShadowCasters)) {
                SetShadowBoost(cfg.shadowBoost);
            }

            float boostValue = cfg.shadowBoost;
            ImGuiMCP::SetNextItemWidth(kShadowSliderWidth);
            if (ImGuiMCP::SliderFloat("Shadow boost multiplier", &boostValue, 0.1f, 32.0f, "%.2fx")) {
                cfg.shadowBoost = boostValue;  // stash live; apply on release
            }
            if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
                cfg.Save();
                if (cfg.boostShadowCasters)
                    SetShadowBoost(cfg.shadowBoost);
            }

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText("Exclusions");

            DrawSavedCheckbox("Exclude LightPlacer-managed light bases", cfg.excludeLightPlacer);
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Rescan LP JSONs")) {
                LoadLightPlacerExclusions();
            }

            DrawSavedCheckbox("Exclude spotlights entirely", cfg.excludeSpotLights);

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText("Session stats");

            if (ImGuiMCP::BeginTable("##isl-stats", 2,
                    ImGuiMCP::ImGuiTableFlags_BordersInnerH |
                    ImGuiMCP::ImGuiTableFlags_RowBg |
                    ImGuiMCP::ImGuiTableFlags_SizingStretchProp))
            {
                ImGuiMCP::TableSetupColumn("Metric", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                ImGuiMCP::TableSetupColumn("Count", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, kStatsCountColumnWidth);
                ImGuiMCP::TableHeadersRow();
                DrawStatRow("LIGH converted", stats.lighConverted);
                DrawStatRow("LIGH already ISL", stats.lighSkippedAlreadyISL);
                DrawStatRow("LIGH math out-of-range", stats.lighSkippedMath);
                DrawStatRow("LIGH skipped (LP)", stats.lighSkippedLightPlacer);
                DrawStatRow("LIGH skipped (magic/FX)", stats.lighSkippedMagicFX);
                DrawStatRow("LIGH skipped (spot)", stats.lighSkippedSpot);
                DrawStatRow("REFR cells processed", stats.refrCellsProcessed);
                DrawStatRow("REFR converted", stats.refrConverted);
                DrawStatRow("REFR math skipped", stats.refrSkippedMath);
                DrawStatRow("REFR skipped (LP)", stats.refrSkippedLightPlacer);
                DrawStatRow("REFR skipped (magic/FX)", stats.refrSkippedMagicFX);
                DrawStatRow("REFR skipped (spot)", stats.refrSkippedSpot);
                DrawStatRow("REFR skipped (persist)", stats.refrSkippedPersistent);
                ImGuiMCP::EndTable();
            }

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText("Actions");

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
