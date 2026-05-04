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
        constexpr float kSliderWidth = 260.0f;
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

            ImGuiMCP::SeparatorText("Conversion");

            DrawSavedCheckbox("Enable conversion next load", cfg.enabled);
            DrawSavedCheckbox("Convert placed-light overrides", cfg.convertRefrs);
            DrawSavedCheckbox("Match vanilla radius (next game launch)", cfg.radiusMatchedFade);

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText("Intensity");

            float intensityValue = cfg.intensityScale;
            ImGuiMCP::SetNextItemWidth(kSliderWidth);
            if (ImGuiMCP::SliderFloat("Global light intensity", &intensityValue, 0.25f, 8.0f, "%.2fx")) {
                cfg.intensityScale = intensityValue;  // stash live; apply on release
            }
            if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
                cfg.Save();
                SetIntensityScale(cfg.intensityScale);
            }

            float boostValue = cfg.shadowBoost;
            ImGuiMCP::SetNextItemWidth(kSliderWidth);
            if (ImGuiMCP::SliderFloat("Shadow-caster boost", &boostValue, 0.1f, 32.0f, "%.2fx")) {
                cfg.shadowBoost = boostValue;  // stash live; apply on release
            }
            if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
                cfg.Save();
                if (cfg.boostShadowCasters)
                    SetShadowBoost(cfg.shadowBoost);
            }

            if (DrawSavedCheckbox("Enable shadow-caster boost", cfg.boostShadowCasters)) {
                SetShadowBoost(cfg.shadowBoost);
            }

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText("Exclusions");

            DrawSavedCheckbox("Exclude LightPlacer lights", cfg.excludeLightPlacer);
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Rescan LP JSONs")) {
                LoadLightPlacerExclusions();
            }

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
                DrawStatRow("Base converted", stats.lighConverted);
                DrawStatRow("Base already ISL", stats.lighSkippedAlreadyISL);
                DrawStatRow("Base math skipped", stats.lighSkippedMath);
                DrawStatRow("Base skipped LightPlacer", stats.lighSkippedLightPlacer);
                DrawStatRow("Base skipped magic/FX", stats.lighSkippedMagicFX);
                DrawStatRow("Base skipped spot", stats.lighSkippedSpot);
                DrawStatRow("Cells processed", stats.refrCellsProcessed);
                DrawStatRow("Placed converted", stats.refrConverted);
                DrawStatRow("Placed math skipped", stats.refrSkippedMath);
                DrawStatRow("Placed skipped LightPlacer", stats.refrSkippedLightPlacer);
                DrawStatRow("Placed skipped magic/FX", stats.refrSkippedMagicFX);
                DrawStatRow("Placed skipped spot", stats.refrSkippedSpot);
                DrawStatRow("Placed skipped persistent", stats.refrSkippedPersistent);
                ImGuiMCP::EndTable();
            }

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText("Actions");

            if (ImGuiMCP::Button("Convert remaining bases")) {
                ConvertAllLights();
            }
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Reset counters")) {
                stats.Reset();
            }
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
