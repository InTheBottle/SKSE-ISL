#include "PCH.h"
#include "MenuIntegration.h"
#include "Conversion.h"
#include "ISLMath.h"

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

        template <typename Fn>
        void DeferToGame(Fn&& fn)
        {
            if (auto* task = SKSE::GetTaskInterface())
                task->AddTask(std::forward<Fn>(fn));
        }

        struct SliderCache {
            float value       = 0.0f;
            bool  initialized = false;

            void EnsureInit(float source)
            {
                if (!initialized) {
                    value = source;
                    initialized = true;
                }
            }
        };

        bool DrawSavedCheckbox(const char* label, bool& setting)
        {
            bool value = setting;
            if (!ImGuiMCP::Checkbox(label, &value))
                return false;

            DeferToGame([&setting, value]{
                setting = value;
                GetConfig().Save();
            });
            return true;
        }

        // One stat row; pass nullptr where a counter doesn't apply.
        void DrawStatRow(const char* label,
                         const std::atomic<std::uint32_t>* base,
                         const std::atomic<std::uint32_t>* placed)
        {
            ImGuiMCP::TableNextRow();
            ImGuiMCP::TableNextColumn();
            ImGuiMCP::TextUnformatted(label);
            ImGuiMCP::TableNextColumn();
            if (base)
                ImGuiMCP::Text("%u", base->load());
            else
                ImGuiMCP::TextDisabled("-");
            ImGuiMCP::TableNextColumn();
            if (placed)
                ImGuiMCP::Text("%u", placed->load());
            else
                ImGuiMCP::TextDisabled("-");
        }

        void __stdcall RenderPanel()
        {
            auto& cfg   = GetConfig();
            auto& stats = GetStats();

            DrawSavedCheckbox("Enable conversion", cfg.enabled);
            ImGuiMCP::SetItemTooltip("Master toggle. Takes effect on the next game launch.");

            ImGuiMCP::Spacing();
            ImGuiMCP::SeparatorText("Lighting");

            static SliderCache cutoffCache;
            cutoffCache.EnsureInit(cfg.cutoff);
            ImGuiMCP::SetNextItemWidth(kSliderWidth);
            ImGuiMCP::SliderFloat("Falloff curve", &cutoffCache.value,
                MinCutoff, MaxCutoff, "%.3f",
                ImGuiMCP::ImGuiSliderFlags_Logarithmic |
                ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
            if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
                DeferToGame([v = cutoffCache.value]{
                    SetCutoff(v);
                    GetConfig().Save();
                });
            }
            ImGuiMCP::SetItemTooltip(
                "Cutoff: the light level where a light's reach ends.\n"
                "Lower = more realistic falloff that carries further.\n"
                "Higher = tighter light pools with less bleed.\n"
                "0.050 matches the Community Shaders default; shadow-caster\n"
                "lights track proportionally. Fully applies once a scene reloads.");

            static SliderCache intensityCache;
            intensityCache.EnsureInit(cfg.intensityScale);
            ImGuiMCP::SetNextItemWidth(kSliderWidth);
            ImGuiMCP::SliderFloat("Intensity", &intensityCache.value, 0.25f, 8.0f, "%.2fx");
            if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
                DeferToGame([v = intensityCache.value]{
                    GetConfig().intensityScale = v;
                    GetConfig().Save();
                    SetIntensityScale(v);
                });
            }
            ImGuiMCP::SetItemTooltip("Global fade multiplier for converted non-shadow lights.");

            static SliderCache boostCache;
            boostCache.EnsureInit(cfg.shadowBoost);
            ImGuiMCP::SetNextItemWidth(kSliderWidth);
            ImGuiMCP::SliderFloat("Shadow-caster boost", &boostCache.value, 0.1f, 32.0f, "%.2fx");
            if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
                DeferToGame([v = boostCache.value]{
                    GetConfig().shadowBoost = v;
                    GetConfig().Save();
                    SetShadowBoost(v);
                });
            }
            ImGuiMCP::SetItemTooltip(
                "Fade multiplier for converted shadow-casting lights.\n1.00x disables the boost.");

            ImGuiMCP::Spacing();

            if (ImGuiMCP::CollapsingHeader("Advanced")) {
                DrawSavedCheckbox("Convert placed-light overrides", cfg.convertRefrs);
                ImGuiMCP::SetItemTooltip(
                    "Also rewrite per-placement XLIG/XRDS/scale overrides on cell attach.\n"
                    "Disable if placed lights show artifacts.");

                DrawSavedCheckbox("Match vanilla radius", cfg.radiusMatchedFade);
                ImGuiMCP::SetItemTooltip(
                    "Solve each light so its ISL reach matches its vanilla radius.\n"
                    "Takes effect on the next game launch.");

                DrawSavedCheckbox("Exclude LightPlacer lights", cfg.excludeLightPlacer);
                ImGuiMCP::SetItemTooltip(
                    "Skip LIGH bases referenced by Data/LightPlacer configs.");
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Rescan")) {
                    DeferToGame([]{ LoadLightPlacerExclusions(); });
                }

                if (ImGuiMCP::Button("Convert remaining bases")) {
                    DeferToGame([]{ ConvertAllLights(); });
                }
                ImGuiMCP::SetItemTooltip(
                    "Re-run the LIGH pass for lights added since data load.");
            }

            if (ImGuiMCP::CollapsingHeader("Statistics")) {
                ImGuiMCP::Text("Cells processed: %u", stats.refrCellsProcessed.load());

                if (ImGuiMCP::BeginTable("##isl-stats", 3,
                        ImGuiMCP::ImGuiTableFlags_BordersInnerH |
                        ImGuiMCP::ImGuiTableFlags_RowBg |
                        ImGuiMCP::ImGuiTableFlags_SizingStretchProp))
                {
                    ImGuiMCP::TableSetupColumn("Metric", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
                    ImGuiMCP::TableSetupColumn("Base", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, kStatsCountColumnWidth);
                    ImGuiMCP::TableSetupColumn("Placed", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, kStatsCountColumnWidth);
                    ImGuiMCP::TableHeadersRow();
                    DrawStatRow("Converted", &stats.lighConverted, &stats.refrConverted);
                    DrawStatRow("Already ISL", &stats.lighSkippedAlreadyISL, nullptr);
                    DrawStatRow("Math skipped", &stats.lighSkippedMath, &stats.refrSkippedMath);
                    DrawStatRow("LightPlacer", &stats.lighSkippedLightPlacer, &stats.refrSkippedLightPlacer);
                    DrawStatRow("Magic / FX", &stats.lighSkippedMagicFX, &stats.refrSkippedMagicFX);
                    DrawStatRow("Spot lights", &stats.lighSkippedSpot, &stats.refrSkippedSpot);
                    DrawStatRow("Persistent", nullptr, &stats.refrSkippedPersistent);
                    DrawStatRow("Reverted stale", nullptr, &stats.refrReverted);
                    ImGuiMCP::EndTable();
                }

                if (ImGuiMCP::Button("Reset counters")) {
                    stats.Reset();
                }
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
