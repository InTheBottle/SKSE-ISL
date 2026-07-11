#include "Conversion.h"
#include "ISLMath.h"
#include "MenuIntegration.h"

#include <fstream>
#include <sstream>

namespace isl {

    namespace {
        constexpr std::string_view kConfigPath =
            "Data/SKSE/Plugins/SKSE_ISL.ini";

        Config g_config;
        Stats  g_stats;

        // Solve inputs for a converted REFR, so a cutoff change can re-solve it exactly.
        struct RefRecord {
            float fade;            // effective vanilla F (base F x XLIG multiplier)
            float radius;          // effective radius (override x ref scale)
            bool  radiusOverride;  // whether XLIG fov carries an absolute size
        };

        std::mutex                                    g_refrMutex;
        std::unordered_set<RE::FormID>                g_processedRefs;
        std::unordered_map<RE::FormID, RefRecord>     g_convertedRefs;
        std::unordered_set<RE::FormID>                g_processedCells;
        // Converted LIGH bases -> original vanilla fade, the exact inputs for live re-solves.
        std::unordered_map<RE::FormID, float>         g_convertedLights;
        std::shared_mutex                             g_convertedLightMutex;

        // LIGH base IDs from LP JSONs; mutations on these bases leak into LP-spawned NiLights.
        std::unordered_set<RE::FormID>      g_lpFormIDs;
        std::shared_mutex                   g_lpMutex;

        // Dynamic FX LIGH bases: magic/projectile/explosion/hazard, fx editor IDs, emittance.
        std::unordered_set<RE::FormID>      g_magicFXFormIDs;
        std::shared_mutex                   g_magicFXMutex;

        // Global intensity factor currently baked into converted LIGH fades.
        std::atomic<float>                  g_appliedIntensityScale{ 1.0f };

        // True when converted LIGH fades use radius-matched ISL intensity.
        std::atomic<bool>                   g_appliedRadiusMatchedFade{ true };

        // Shadow-boost factor currently baked into converted LIGH fades.
        std::atomic<float>                  g_appliedShadowBoost{ 1.0f };

        // Regular-light cutoff currently baked into converted LIGH records.
        std::atomic<float>                  g_appliedCutoff{ CutoffRegular };

        // True once the LIGH pass has run; drives a lazy failsafe.
        std::atomic<bool> g_lighPassDone{ false };

        std::atomic<RE::FormID> g_lastProcessedCell{ 0 };

        RE::ExtraLightData* GetOrCreateExtraLightData(RE::TESObjectREFR* refr)
        {
            auto* existing = refr->extraList.GetByType<RE::ExtraLightData>();
            if (existing)
                return existing;

            auto* ld = new RE::ExtraLightData();
            // "Unset" defaults: fov >= 50 => engine uses sqrt(2); fade 0 => base fade.
            ld->data.fov             = 90.0f;
            ld->data.fade            = 0.0f;
            ld->data.endDistanceCap  = 0.0f;
            ld->data.shadowDepthBias = 1.0f;
            refr->extraList.Add(ld);
            return ld;
        }

        bool IsISLAuthored(const RE::ExtraLightData* xlig) noexcept
        {
            return xlig && xlig->data.endDistanceCap == AuthoredEndCap;
        }

        RE::ExtraRadius* GetExtraRadius(RE::TESObjectREFR* refr)
        {
            return refr->extraList.GetByType<RE::ExtraRadius>();
        }

        float GetRefScale(RE::TESObjectREFR* refr) noexcept
        {
            // TESObjectREFR::refScale is uint16 storing scale*100.
            const auto& rd = refr->GetReferenceRuntimeData();
            const float s = static_cast<float>(rd.refScale) / 100.0f;
            return s > 0.0f ? s : 1.0f;
        }

        bool IsRefConvertible(const RE::TESObjectREFR* refr) noexcept
        {
            if (!refr)
                return false;
            if (refr->IsDisabled() || refr->IsDeleted())
                return false;
            return true;
        }

        // Persistent refs serialize ExtraLightData into saves forever; skip them for save safety.
        bool IsPersistent(const RE::TESObjectREFR* refr) noexcept
        {
            return (refr->GetFormFlags() & RE::TESForm::RecordFlags::kPersistent) != 0;
        }

        bool IsLightPlacerExcluded(RE::FormID id) noexcept
        {
            if (id == 0)
                return false;
            std::shared_lock lk(g_lpMutex);
            return g_lpFormIDs.contains(id);
        }

        bool IsMagicFXExcluded(RE::FormID id) noexcept
        {
            if (id == 0)
                return false;
            std::shared_lock lk(g_magicFXMutex);
            return g_magicFXFormIDs.contains(id);
        }

        bool IsPluginConvertedLight(RE::FormID id) noexcept
        {
            if (id == 0)
                return false;
            std::shared_lock lk(g_convertedLightMutex);
            return g_convertedLights.contains(id);
        }

        float ClampIntensityScale(float scale) noexcept
        {
            if (scale < 0.25f) return 0.25f;
            if (scale > 8.0f) return 8.0f;
            return scale;
        }

        float ClampCutoff(float cutoff) noexcept
        {
            if (cutoff < MinCutoff) return MinCutoff;
            if (cutoff > MaxCutoff) return MaxCutoff;
            return cutoff;
        }

        float ClampShadowBoost(float boost) noexcept
        {
            if (boost < 0.1f) return 0.1f;
            if (boost > 32.0f) return 32.0f;
            return boost;
        }

        enum class LiveScaleTarget
        {
            GlobalIntensity,
            ShadowBoost
        };

        bool IsConvertedLightEligible(const RE::TESObjectLIGH* ligh, std::uint32_t flags) noexcept
        {

            if (!ligh || !IsAlreadyISL(flags) || !IsPluginConvertedLight(ligh->formID))
                return false;
            if (IsSpotLight(flags))
                return false;
            return true;
        }

        bool ShouldScaleLight(const RE::TESObjectLIGH* ligh, LiveScaleTarget target) noexcept
        {
            if (!ligh)
                return false;

            const auto flags = ligh->data.flags.underlying();
            if (!IsConvertedLightEligible(ligh, flags))
                return false;

            const bool isShadow = IsShadowCaster(flags);
            return target == LiveScaleTarget::ShadowBoost ? isShadow : !isShadow;
        }

        std::uint32_t ScaleConvertedRefDeltas(float ratio, LiveScaleTarget target)
        {
            std::vector<RE::FormID> cellIDs;
            std::unordered_map<RE::FormID, RefRecord> refIDs;
            {
                std::scoped_lock lk(g_refrMutex);
                cellIDs.assign(g_processedCells.begin(), g_processedCells.end());
                refIDs = g_convertedRefs;
            }

            std::uint32_t touched = 0;
            for (const auto cellID : cellIDs) {
                auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellID);
                if (!cell)
                    continue;

                std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
                {
                    auto& rd = cell->GetRuntimeData();
                    RE::BSSpinLockGuard lock(rd.spinLock);
                    refs.reserve(rd.references.size());
                    for (const auto& handle : rd.references)
                        refs.emplace_back(handle);
                }

                for (const auto& handle : refs) {
                    auto* refr = handle.get();
                    if (!refr || !refIDs.contains(refr->formID))
                        continue;

                    auto* base = refr->GetBaseObject();
                    auto* ligh = base ? base->As<RE::TESObjectLIGH>() : nullptr;
                    if (!ShouldScaleLight(ligh, target))
                        continue;

                    auto* xlig = refr->extraList.GetByType<RE::ExtraLightData>();
                    if (!IsISLAuthored(xlig))
                        continue;

                    xlig->data.fade *= ratio;
                    ++touched;
                }
            }

            return touched;
        }

        // Re-solves converted REFR deltas for a new cutoff; refs whose base failed stay untouched.
        std::uint32_t RecomputeConvertedRefDeltas(
            float newCutoff, bool radiusMatched, float scale, float boost,
            const std::unordered_set<RE::FormID>& failedBases)
        {
            std::vector<RE::FormID> cellIDs;
            std::unordered_map<RE::FormID, RefRecord> records;
            {
                std::scoped_lock lk(g_refrMutex);
                cellIDs.assign(g_processedCells.begin(), g_processedCells.end());
                records = g_convertedRefs;
            }

            std::uint32_t touched = 0;
            for (const auto cellID : cellIDs) {
                auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(cellID);
                if (!cell)
                    continue;

                std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
                {
                    auto& rd = cell->GetRuntimeData();
                    RE::BSSpinLockGuard lock(rd.spinLock);
                    refs.reserve(rd.references.size());
                    for (const auto& handle : rd.references)
                        refs.emplace_back(handle);
                }

                for (const auto& handle : refs) {
                    auto* refr = handle.get();
                    if (!refr)
                        continue;
                    const auto it = records.find(refr->formID);
                    if (it == records.end())
                        continue;

                    auto* base = refr->GetBaseObject();
                    auto* ligh = base ? base->As<RE::TESObjectLIGH>() : nullptr;
                    if (!ligh)
                        continue;
                    const auto flags = ligh->data.flags.underlying();
                    if (!IsConvertedLightEligible(ligh, flags) ||
                        failedBases.contains(ligh->formID))
                        continue;

                    auto* xlig = refr->extraList.GetByType<RE::ExtraLightData>();
                    if (!IsISLAuthored(xlig))
                        continue;

                    const auto& rec = it->second;
                    const float c = EffectiveCutoff(flags, newCutoff);
                    ISLParams p{};
                    if (!ComputeISL(rec.fade, rec.radius, c, p))
                        continue;
                    if (!radiusMatched)
                        p.intensity = rec.fade;

                    const float desiredI =
                        p.intensity * (IsShadowCaster(flags) ? boost : scale);
                    const float delta = desiredI - ligh->fade;
                    if (!std::isfinite(delta) || std::fabs(delta) > 1.0e6f)
                        continue;

                    xlig->data.fade = delta;
                    if (rec.radiusOverride)
                        xlig->data.fov = p.size;
                    ++touched;
                }
            }

            return touched;
        }

        void AddLightID(std::unordered_set<RE::FormID>& ids, const RE::TESObjectLIGH* ligh)
        {
            if (ligh)
                ids.insert(ligh->formID);
        }

        void AddExplosionLightID(std::unordered_set<RE::FormID>& ids, const RE::BGSExplosion* expl)
        {
            if (expl)
                AddLightID(ids, expl->data.light);
        }

        void AddProjectileLightIDs(std::unordered_set<RE::FormID>& ids, const RE::BGSProjectile* proj)
        {
            if (!proj)
                return;

            AddLightID(ids, proj->data.light);
            AddLightID(ids, proj->data.muzzleFlashLight);
            AddExplosionLightID(ids, proj->data.explosionType);
        }

        bool StartsWithNoCase(std::string_view text, std::string_view prefix) noexcept
        {
            if (text.size() < prefix.size())
                return false;

            for (std::size_t i = 0; i < prefix.size(); ++i) {
                char a = text[i];
                char b = prefix[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                if (a != b)
                    return false;
            }
            return true;
        }

        bool ContainsNoCase(std::string_view text, std::string_view needle) noexcept
        {
            if (needle.empty())
                return true;
            if (text.size() < needle.size())
                return false;

            for (std::size_t i = 0; i <= text.size() - needle.size(); ++i) {
                if (StartsWithNoCase(text.substr(i), needle))
                    return true;
            }
            return false;
        }

        bool IsExcludedLightEditorID(std::string_view id) noexcept
        {
            return ContainsNoCase(id, "glowfill") ||
                   ContainsNoCase(id, "window") ||
                   ContainsNoCase(id, "sun") ||
                   (StartsWithNoCase(id, "fx") && ContainsNoCase(id, "light")) ||
                   // SCS's light editor overrides these bulbs per-frame; stay out of their way.
                   ContainsNoCase(id, "defaultgreen");
        }

        void AddEditorIDExcludedLightIDs(
            std::unordered_set<RE::FormID>& ids,
            std::size_t& count)
        {
            const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
            [[maybe_unused]] const RE::BSReadLockGuard guard{ lock };
            if (!map)
                return;

            for (const auto& [editorID, form] : *map) {
                auto* ligh = form ? form->As<RE::TESObjectLIGH>() : nullptr;
                if (!ligh || !IsExcludedLightEditorID(editorID))
                    continue;

                if (ids.insert(ligh->formID).second)
                    ++count;
            }
        }

        void LoadAllExclusions()
        {
            LoadLightPlacerExclusions();
            LoadMagicFXExclusions();
        }
    }  // namespace

    Config& GetConfig() { return g_config; }
    Stats&  GetStats()  { return g_stats; }

    void Config::Load()
    {
        std::ifstream in(std::string{ kConfigPath });
        if (!in)
            return;
        std::string line;
        while (std::getline(in, line)) {
            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const auto key = line.substr(0, eq);
            const auto val = line.substr(eq + 1);
            if (key == "enabled")                 enabled             = (val == "1" || val == "true");
            else if (key == "convertRefrs")       convertRefrs        = (val == "1" || val == "true");
            else if (key == "radiusMatchedFade")  radiusMatchedFade   = (val == "1" || val == "true");
            else if (key == "boostShadow")        boostShadowCasters  = (val == "1" || val == "true");
            else if (key == "excludeLightPlacer") excludeLightPlacer  = (val == "1" || val == "true");
            else if (key == "excludeSpotLights")  excludeSpotLights   = (val == "1" || val == "true");
            else if (key == "intensityScale") {
                try { intensityScale = ClampIntensityScale(std::stof(val)); } catch (...) {}
            }
            else if (key == "shadowBoost") {
                try { shadowBoost = ClampShadowBoost(std::stof(val)); } catch (...) {}
            }
            else if (key == "cutoff") {
                try { cutoff = ClampCutoff(std::stof(val)); } catch (...) {}
            }
        }

        // The boost checkbox is gone from the UI; fold a saved "off" into a neutral 1.0x.
        if (!boostShadowCasters) {
            boostShadowCasters = true;
            shadowBoost = 1.0f;
        }
    }

    void Config::Save() const
    {
        const std::filesystem::path configPath{ std::string{ kConfigPath } };
        std::error_code ec;
        std::filesystem::create_directories(configPath.parent_path(), ec);
        if (ec) {
            logger::warn("[ISL] Failed to create config directory: {}", ec.message());
            return;
        }

        std::ofstream out(configPath, std::ios::trunc);
        if (!out) {
            logger::warn("[ISL] Failed to save config to {}", kConfigPath);
            return;
        }
        out << "enabled="            << (enabled            ? "1" : "0") << '\n';
        out << "convertRefrs="       << (convertRefrs       ? "1" : "0") << '\n';
        out << "radiusMatchedFade="  << (radiusMatchedFade  ? "1" : "0") << '\n';
        out << "boostShadow="        << (boostShadowCasters ? "1" : "0") << '\n';
        out << "excludeLightPlacer=" << (excludeLightPlacer ? "1" : "0") << '\n';
        out << "excludeSpotLights="  << (excludeSpotLights  ? "1" : "0") << '\n';
        out << "intensityScale="     << intensityScale                   << '\n';
        out << "shadowBoost="        << shadowBoost                      << '\n';
        out << "cutoff="             << cutoff                           << '\n';
    }

    void LoadLightPlacerExclusions()
    {
        std::unordered_set<RE::FormID> collected;

        namespace fs = std::filesystem;
        const fs::path root{ "Data/LightPlacer" };
        std::error_code ec;
        if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
            // A missing folder is more likely a user mistake than a real "no exclusions" signal.
            logger::info("[ISL] LightPlacer folder not found - keeping existing exclusion set.");
            return;
        }

        static const std::regex kLightEdidRe{
            R"REGEX("light"\s*:\s*"([^"]+)")REGEX" };

        std::size_t filesSeen = 0, tokens = 0, edids = 0, resolved = 0;

        for (auto it = fs::recursive_directory_iterator(root, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".json") continue;

            ++filesSeen;
            std::ifstream f(it->path());
            if (!f) continue;
            std::stringstream ss;
            ss << f.rdbuf();
            const std::string buf = ss.str();

            for (auto m = std::sregex_iterator(buf.begin(), buf.end(), kLightEdidRe),
                      e = std::sregex_iterator();
                 m != e; ++m)
            {
                ++tokens;
                const std::string value = (*m)[1].str();

                std::size_t start = 0;
                while (start <= value.size()) {
                    const auto bar  = value.find('|', start);
                    const auto stop = (bar == std::string::npos) ? value.size() : bar;
                    const std::string edid = value.substr(start, stop - start);
                    start = (bar == std::string::npos) ? value.size() + 1 : bar + 1;
                    if (edid.empty()) continue;
                    ++edids;
                    if (auto* form = RE::TESForm::LookupByEditorID(edid)) {
                        if (form->Is(RE::FormType::Light)) {
                            collected.insert(form->GetFormID());
                            ++resolved;
                        }
                    }
                }
            }
        }

        {
            std::unique_lock lk(g_lpMutex);
            g_lpFormIDs = std::move(collected);
            logger::info(
                "[ISL] LightPlacer scan: files={} lightTokens={} edids={} resolvedLIGH={} excludedBases={}",
                filesSeen, tokens, edids, resolved, g_lpFormIDs.size());
        }
    }

    void LoadMagicFXExclusions()
    {
        std::unordered_set<RE::FormID> collected;

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            std::unique_lock lk(g_magicFXMutex);
            g_magicFXFormIDs.clear();
            return;
        }

        std::size_t mgefLights = 0, projLights = 0, explLights = 0,
                    hazardLights = 0, nameLights = 0;

        for (auto* mgef : dh->GetFormArray<RE::EffectSetting>()) {
            if (!mgef)
                continue;

            const auto before = collected.size();
            AddLightID(collected, mgef->data.light);
            AddProjectileLightIDs(collected, mgef->data.projectileBase);
            AddExplosionLightID(collected, mgef->data.explosion);
            mgefLights += collected.size() - before;
        }

        for (auto* proj : dh->GetFormArray<RE::BGSProjectile>()) {
            const auto before = collected.size();
            AddProjectileLightIDs(collected, proj);
            projLights += collected.size() - before;
        }

        for (auto* expl : dh->GetFormArray<RE::BGSExplosion>()) {
            const auto before = collected.size();
            AddExplosionLightID(collected, expl);
            explLights += collected.size() - before;
        }

        for (auto* hazard : dh->GetFormArray<RE::BGSHazard>()) {
            if (!hazard)
                continue;

            const auto before = collected.size();
            AddLightID(collected, hazard->data.light);
            hazardLights += collected.size() - before;
        }

        AddEditorIDExcludedLightIDs(collected, nameLights);

        {
            std::unique_lock lk(g_magicFXMutex);
            g_magicFXFormIDs = std::move(collected);
            logger::info(
                "[ISL] Magic/FX light scan: mgef={} projectile={} explosion={} hazard={} nameMatch={} excludedBases={}",
                mgefLights, projLights, explLights, hazardLights, nameLights, g_magicFXFormIDs.size());
        }
    }

    void ConvertAllLights()
    {
        if (!g_config.enabled)
            return;

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh)
            return;

        const auto& lights = dh->GetFormArray<RE::TESObjectLIGH>();
        logger::info("[ISL] Converting {} LIGH forms...", lights.size());

        const bool firstPass = !g_lighPassDone.load(std::memory_order_acquire);
        if (firstPass) {
            g_config.intensityScale = ClampIntensityScale(g_config.intensityScale);
            g_config.shadowBoost = ClampShadowBoost(g_config.shadowBoost);
            g_config.cutoff = ClampCutoff(g_config.cutoff);
        }

        bool foreignISLDominant = false;
        if (firstPass && !lights.empty()) {
            std::size_t alreadyISL = 0;
            for (auto* l : lights) {
                if (l && IsAlreadyISL(l->data.flags.underlying()))
                    ++alreadyISL;
            }

            if (alreadyISL * 10 >= lights.size()) {
                foreignISLDominant = true;
                logger::info(
                    "[ISL] Pre-existing ISL coverage detected ({}/{} lights). "
                    "Forcing session boost/scale to 1.0 to avoid stacking on third-party tuning. "
                    "Use the slider to override.",
                    alreadyISL, lights.size());
            }
        }

        const bool radiusMatched = firstPass
                                       ? g_config.radiusMatchedFade
                                       : g_appliedRadiusMatchedFade.load(std::memory_order_acquire);
        const float intensityScale = firstPass
                                         ? (foreignISLDominant ? 1.0f : g_config.intensityScale)
                                         : g_appliedIntensityScale.load(std::memory_order_acquire);
        const float boost = firstPass
                                ? (foreignISLDominant
                                       ? 1.0f
                                       : (g_config.boostShadowCasters ? g_config.shadowBoost : 1.0f))
                                : g_appliedShadowBoost.load(std::memory_order_acquire);
        const float cutoff = firstPass
                                 ? g_config.cutoff
                                 : g_appliedCutoff.load(std::memory_order_acquire);

        std::uint32_t converted = 0, skippedISL = 0, skippedMath = 0,
                      skippedLP = 0, skippedMagicFX = 0, skippedSpot = 0;

        std::vector<std::pair<RE::FormID, float>> newlyConverted;
        newlyConverted.reserve(lights.size() / 4);

        for (auto* ligh : lights) {
            if (!ligh)
                continue;

            const auto flagsRaw = ligh->data.flags.underlying();

            if (IsSpotLight(flagsRaw)) {
                ++skippedSpot;
                continue;
            }

            if (g_config.excludeLightPlacer &&
                IsLightPlacerExcluded(ligh->formID))
            {
                ++skippedLP;
                continue;
            }

            if (IsMagicFXExcluded(ligh->formID)) {
                ++skippedMagicFX;
                continue;
            }

            if (IsAlreadyISL(flagsRaw)) {
                ++skippedISL;
                continue;
            }

            const float F = ligh->fade;
            const float r = static_cast<float>(ligh->data.radius);
            const float c = EffectiveCutoff(flagsRaw, cutoff);

            ISLParams p{};
            if (!ComputeISL(F, r, c, p)) {
                ++skippedMath;
                continue;
            }

            if (!radiusMatched)
                p.intensity = F;

            const bool isShadow = IsShadowCaster(flagsRaw);
            if (!isShadow)
                p.intensity *= intensityScale;
            if (isShadow)
                p.intensity *= boost;

            ligh->fade                = p.intensity;
            ligh->data.fov            = p.size;
            ligh->data.fallofExponent = p.cutoff;
            ligh->data.flags.set(
                static_cast<RE::TES_LIGHT_FLAGS>(FlagInverseSquare));
            newlyConverted.emplace_back(ligh->formID, F);

            ++converted;
        }

        if (!newlyConverted.empty()) {
            std::unique_lock lk(g_convertedLightMutex);
            g_convertedLights.insert(newlyConverted.begin(), newlyConverted.end());
        }

        g_stats.lighConverted         += converted;
        g_stats.lighSkippedAlreadyISL += skippedISL;
        g_stats.lighSkippedMath       += skippedMath;
        g_stats.lighSkippedLightPlacer += skippedLP;
        g_stats.lighSkippedMagicFX    += skippedMagicFX;
        g_stats.lighSkippedSpot       += skippedSpot;

        if (firstPass) {
            g_appliedIntensityScale.store(intensityScale, std::memory_order_release);
            g_appliedRadiusMatchedFade.store(radiusMatched, std::memory_order_release);
            g_appliedShadowBoost.store(boost, std::memory_order_release);
            g_appliedCutoff.store(cutoff, std::memory_order_release);
        }
        g_lighPassDone.store(true, std::memory_order_release);

        logger::info("[ISL]   converted={} alreadyISL={} mathSkipped={} lpSkipped={} magicFXSkipped={} spotSkipped={} radiusMatched={} intensity={:.2f} boost={:.2f} cutoff={:.3f}",
            converted, skippedISL, skippedMath, skippedLP, skippedMagicFX, skippedSpot, radiusMatched, intensityScale, boost, cutoff);
    }

    void SetIntensityScale(float newScale)
    {
        newScale = ClampIntensityScale(newScale);

        if (!g_lighPassDone.load(std::memory_order_acquire)) {
            g_config.intensityScale = newScale;
            return;
        }

        const float oldScale = g_appliedIntensityScale.load(std::memory_order_acquire);
        if (std::fabs(newScale - oldScale) < 1e-4f) {
            g_config.intensityScale = newScale;
            return;
        }

        const float ratio = newScale / oldScale;

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::warn("[ISL] SetIntensityScale: no TESDataHandler.");
            return;
        }

        std::uint32_t touched = 0;
        for (auto* ligh : dh->GetFormArray<RE::TESObjectLIGH>()) {
            if (!ShouldScaleLight(ligh, LiveScaleTarget::GlobalIntensity))
                continue;

            ligh->fade *= ratio;
            ++touched;
        }
        const auto touchedRefs = ScaleConvertedRefDeltas(ratio, LiveScaleTarget::GlobalIntensity);

        g_appliedIntensityScale.store(newScale, std::memory_order_release);
        g_config.intensityScale = newScale;

        logger::info(
            "[ISL] SetIntensityScale: {:.2f} -> {:.2f} (ratio={:.3f}, ligh={} refr={})",
            oldScale, newScale, ratio, touched, touchedRefs);
    }

    void SetShadowBoost(float newBoost)
    {
        newBoost = ClampShadowBoost(newBoost);

        // LIGH pass hasn't run yet: just stash; it'll pick up on first run.
        if (!g_lighPassDone.load(std::memory_order_acquire)) {
            g_config.shadowBoost = newBoost;
            return;
        }

        const float oldBoost = g_appliedShadowBoost.load(std::memory_order_acquire);
        const float effectiveNew = g_config.boostShadowCasters ? newBoost : 1.0f;
        if (std::fabs(effectiveNew - oldBoost) < 1e-4f) {
            g_config.shadowBoost = newBoost;
            return;
        }

        const float ratio = effectiveNew / oldBoost;

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::warn("[ISL] SetShadowBoost: no TESDataHandler.");
            return;
        }

        std::uint32_t touched = 0;
        for (auto* ligh : dh->GetFormArray<RE::TESObjectLIGH>()) {
            if (!ShouldScaleLight(ligh, LiveScaleTarget::ShadowBoost))
                continue;

            ligh->fade *= ratio;
            ++touched;
        }
        const auto touchedRefs = ScaleConvertedRefDeltas(ratio, LiveScaleTarget::ShadowBoost);

        g_appliedShadowBoost.store(effectiveNew, std::memory_order_release);
        g_config.shadowBoost = newBoost;

        logger::info(
            "[ISL] SetShadowBoost: {:.2f} -> {:.2f} (ratio={:.3f}, ligh={} refr={})",
            oldBoost, effectiveNew, ratio, touched, touchedRefs);
    }

    // Live falloff-curve adjustment; fades update live, cutoff/size land when a scene reloads.
    void SetCutoff(float newCutoff)
    {
        newCutoff = ClampCutoff(newCutoff);

        if (!g_lighPassDone.load(std::memory_order_acquire)) {
            g_config.cutoff = newCutoff;
            return;
        }

        const float oldCutoff = g_appliedCutoff.load(std::memory_order_acquire);
        if (std::fabs(newCutoff - oldCutoff) < 1e-5f) {
            g_config.cutoff = newCutoff;
            return;
        }

        const bool radiusMatched =
            g_appliedRadiusMatchedFade.load(std::memory_order_acquire);
        const float scale = g_appliedIntensityScale.load(std::memory_order_acquire);
        const float boost = g_appliedShadowBoost.load(std::memory_order_acquire);

        std::vector<std::pair<RE::FormID, float>> lights;
        {
            std::shared_lock lk(g_convertedLightMutex);
            lights.assign(g_convertedLights.begin(), g_convertedLights.end());
        }

        std::unordered_set<RE::FormID> failed;
        std::uint32_t touched = 0;
        for (const auto& [id, origFade] : lights) {
            auto* ligh = RE::TESForm::LookupByID<RE::TESObjectLIGH>(id);
            if (!ligh)
                continue;
            const auto flags = ligh->data.flags.underlying();
            if (!IsConvertedLightEligible(ligh, flags))
                continue;

            const float c = EffectiveCutoff(flags, newCutoff);
            ISLParams p{};
            if (!ComputeISL(origFade, static_cast<float>(ligh->data.radius), c, p)) {
                // Unsolvable at the new cutoff (F too close to c); keep the previous curve.
                failed.insert(id);
                continue;
            }
            if (!radiusMatched)
                p.intensity = origFade;

            p.intensity *= IsShadowCaster(flags) ? boost : scale;

            ligh->fade                = p.intensity;
            ligh->data.fov            = p.size;
            ligh->data.fallofExponent = p.cutoff;
            ++touched;
        }

        const auto touchedRefs =
            RecomputeConvertedRefDeltas(newCutoff, radiusMatched, scale, boost, failed);

        g_appliedCutoff.store(newCutoff, std::memory_order_release);
        g_config.cutoff = newCutoff;

        logger::info(
            "[ISL] SetCutoff: {:.3f} -> {:.3f} (ligh={} unsolvable={} refr={})",
            oldCutoff, newCutoff, touched, failed.size(), touchedRefs);
    }

    namespace {
        void ConvertOneRef(RE::TESObjectREFR* refr)
        {
            if (!IsRefConvertible(refr))
                return;

            auto* base = refr->GetBaseObject();
            if (!base || base->GetFormType() != RE::FormType::Light)
                return;
            auto* ligh = base->As<RE::TESObjectLIGH>();
            if (!ligh)
                return;

            const auto baseFlags = ligh->data.flags.underlying();

            if (IsSpotLight(baseFlags)) {
                ++g_stats.refrSkippedSpot;
                return;
            }
            if (!IsAlreadyISL(baseFlags))
                return;

            // Base must be one of ours; its stored original fade is the exact vanilla F.
            float origF = 0.0f;
            {
                std::shared_lock lk(g_convertedLightMutex);
                const auto it = g_convertedLights.find(ligh->formID);
                if (it == g_convertedLights.end())
                    return;
                origF = it->second;
            }

            // Save-safety: a persistent ref's ChangeForm would keep our delta forever post-uninstall.
            if (IsPersistent(refr)) {
                ++g_stats.refrSkippedPersistent;
                return;
            }

            if (refr->extraList.HasType<RE::ExtraEmittanceSource>()) {
                ++g_stats.refrSkippedMagicFX;
                return;
            }

            const float scale = GetRefScale(refr);
            auto* xrds = GetExtraRadius(refr);
            auto* xlig = refr->extraList.GetByType<RE::ExtraLightData>();

            if (IsISLAuthored(xlig))
                return;

            if (xlig && xlig->data.fov < MaxSize)
                return;

            const bool hasRadiusOverride =
                (xrds && xrds->radius > 0.0f) || scale != 1.0f;
            const bool hasFadeOverride = xlig && xlig->data.fade != 0.0f;

            if (!hasRadiusOverride && !hasFadeOverride)
                return;  // pure base inheritance

            const float c = EffectiveCutoff(
                baseFlags, g_appliedCutoff.load(std::memory_order_acquire));
            const float baseI   = ligh->fade;         // post-factor ISL intensity
            const float fadeOff = xlig ? xlig->data.fade : 0.0f;
            const float factor  = IsShadowCaster(baseFlags)
                                      ? g_appliedShadowBoost.load(std::memory_order_acquire)
                                      : g_appliedIntensityScale.load(std::memory_order_acquire);
            const bool radiusMatched =
                g_appliedRadiusMatchedFade.load(std::memory_order_acquire);
            if (!std::isfinite(baseI)) {
                ++g_stats.refrSkippedMath;
                return;
            }

            const float rBase = static_cast<float>(ligh->data.radius);
            const float rOver = (xrds && xrds->radius > 0.0f) ? xrds->radius : rBase;
            const float rEff  = rOver * scale;
            // Vanilla XLIG.fade is a multiplier on FNAM; fade <= 0 means "no override".
            const float Feff  = (fadeOff > 0.0f) ? origF * fadeOff : origF;

            ISLParams p{};
            if (!ComputeISL(Feff, rEff, c, p)) {
                ++g_stats.refrSkippedMath;
                return;
            }
            if (!radiusMatched)
                p.intensity = Feff;

            const float desiredI = p.intensity * factor;

            // Sanity: reject non-finite or absurd values rather than bake them into the save.
            const float delta = desiredI - baseI;
            if (!std::isfinite(delta) || std::fabs(delta) > 1.0e6f) {
                ++g_stats.refrSkippedMath;
                return;
            }

            auto* ld = GetOrCreateExtraLightData(refr);

            if (hasRadiusOverride)
                ld->data.fov = p.size;
            ld->data.fade           = delta;
            ld->data.endDistanceCap = AuthoredEndCap;

            {
                std::scoped_lock rl(g_refrMutex);
                g_convertedRefs.insert_or_assign(
                    refr->formID, RefRecord{ Feff, rEff, hasRadiusOverride });
            }

            ++g_stats.refrConverted;
        }
    }

    void ConvertCellRefs(RE::TESObjectCELL* cell)
    {
        if (!g_config.enabled || !g_config.convertRefrs || !cell)
            return;

        if (g_lastProcessedCell.load(std::memory_order_relaxed) == cell->formID)
            return;

        // Failsafe: if kDataLoaded was missed, run the LIGH pass once on first cell attach.
        if (!g_lighPassDone.load(std::memory_order_acquire)) {
            static std::once_flag s_lighOnce;
            std::call_once(s_lighOnce, [] {
                logger::warn("[ISL] LIGH pass had not run by first cell attach - running now.");
                LoadAllExclusions();
                ConvertAllLights();
            });
        }

        {
            std::scoped_lock rl(g_refrMutex);
            if (!g_processedCells.insert(cell->formID).second) {
                g_lastProcessedCell.store(cell->formID, std::memory_order_relaxed);
                return;
            }
        }
        g_lastProcessedCell.store(cell->formID, std::memory_order_relaxed);
        ++g_stats.refrCellsProcessed;

        std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
        {
            auto& rd = cell->GetRuntimeData();
            RE::BSSpinLockGuard lock(rd.spinLock);
            refs.reserve(rd.references.size());
            for (const auto& handle : rd.references)
                refs.emplace_back(handle);
        }

        std::vector<RE::TESObjectREFR*> toProcess;
        toProcess.reserve(refs.size());
        {
            std::scoped_lock rl(g_refrMutex);
            for (const auto& handle : refs) {
                auto* refr = handle.get();
                if (!refr)
                    continue;
                if (g_processedRefs.insert(refr->formID).second)
                    toProcess.push_back(refr);
            }
        }

        for (auto* refr : toProcess)
            ConvertOneRef(refr);
    }

    class CellAttachSink :
        public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
    {
    public:
        static CellAttachSink* GetSingleton()
        {
            static CellAttachSink s;
            return &s;
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESCellAttachDetachEvent* evt,
            RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
        {
            if (evt && evt->attached && evt->reference) {
                if (auto* parent = evt->reference->GetParentCell())
                    ConvertCellRefs(parent);
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    static void OnMessage(SKSE::MessagingInterface::Message* msg)
    {
        if (!msg)
            return;

        logger::debug("[ISL] SKSE message type={}", msg->type);

        switch (msg->type) {
        case SKSE::MessagingInterface::kPostLoad:
            RegisterMenuFramework();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            logger::info("[ISL] kDataLoaded - scanning exclusions, running LIGH pass");
            LoadAllExclusions();
            ConvertAllLights();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            logger::info("[ISL] save/new game message received");

            {
                std::scoped_lock rl(g_refrMutex);
                g_processedCells.clear();
                g_processedRefs.clear();
                g_convertedRefs.clear();
            }
            g_lastProcessedCell.store(0, std::memory_order_relaxed);
            if (!g_lighPassDone.load(std::memory_order_acquire)) {
                logger::warn("[ISL] LIGH pass not yet run at save load; running now.");
                LoadAllExclusions();
                ConvertAllLights();
            }
            break;
        default:
            break;
        }
    }

    void Install()
    {
        g_config.Load();

        if (auto* msg = SKSE::GetMessagingInterface())
            msg->RegisterListener(OnMessage);

        if (auto* source = RE::ScriptEventSourceHolder::GetSingleton())
            source->AddEventSink<RE::TESCellAttachDetachEvent>(
                CellAttachSink::GetSingleton());

        logger::info("[ISL] Install complete (enabled={}, refrs={}, boost={})",
            g_config.enabled, g_config.convertRefrs, g_config.boostShadowCasters);
    }

}  // namespace isl
