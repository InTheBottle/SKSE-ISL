#pragma once

#include "PCH.h"

#include <numbers>

namespace isl {

    inline constexpr float K_S            = 1960.0f;
    inline constexpr float CutoffRegular  = 0.05f;
    inline constexpr float CutoffShadow   = 0.022f;

    // SCS-ISL clamps DATA\Falloff Exponent to [0.01, 1.0]; exactly 1.0 means "unset".
    inline constexpr float MinCutoff      = 0.01f;
    inline constexpr float MaxCutoff      = 0.30f;

    // Shadow casters keep the tuned default ratio as the cutoff moves.
    inline constexpr float ShadowCutoffRatio = CutoffShadow / CutoffRegular;

    // DATA\FOV >= 50 means "no ISL size set" (size sqrt(2)); sizes are clamped to at least 0.01.
    inline constexpr float UnsetSizeFov   = 50.0f;
    inline constexpr float MaxSize        = UnsetSizeFov - 0.01f;
    inline constexpr float MinSize        = 0.01f;

    // Numerical guard for F barely above c.
    inline constexpr float MinFmc         = 0.001f;

    inline constexpr float AuthoredEndCap = -7771.337f;

    // LIGH DATA flag bits (xEdit names).
    inline constexpr std::uint32_t FlagInverseSquare = 0x00004000;  // "Unknown 14"
    inline constexpr std::uint32_t FlagShadowMask    = 0x00001C00;  // Spotlight/Hemisphere/Omni shadow
    inline constexpr std::uint32_t FlagSpotMask      = 0x00000600;  // kSpotlight | kSpotShadow

    struct ISLParams {
        float intensity;  // FNAM to write
        float size;       // DATA\FOV to write
        float cutoff;     // DATA\Falloff Exponent to write
    };

    [[nodiscard]] constexpr bool IsShadowCaster(std::uint32_t flags) noexcept
    {
        return (flags & FlagShadowMask) != 0;
    }

    [[nodiscard]] constexpr bool IsSpotLight(std::uint32_t flags) noexcept
    {
        return (flags & FlagSpotMask) != 0;
    }

    [[nodiscard]] constexpr bool IsAlreadyISL(std::uint32_t flags) noexcept
    {
        return (flags & FlagInverseSquare) != 0;
    }

    // Per-light cutoff; shadow casters track proportionally, floored at the runtime clamp.
    [[nodiscard]] constexpr float EffectiveCutoff(std::uint32_t flags, float regularCutoff) noexcept
    {
        const float c = IsShadowCaster(flags) ? regularCutoff * ShadowCutoffRatio : regularCutoff;
        return c < MinCutoff ? MinCutoff : c;
    }

    [[nodiscard]] constexpr float BaseSize(float fov) noexcept
    {
        const float s = fov >= UnsetSizeFov ? std::numbers::sqrt2_v<float> : fov;
        return s < MinSize ? MinSize : s;
    }

    [[nodiscard]] constexpr float IntensityForReach(float r, float c, float s) noexcept
    {
        return c * (r * r / K_S + s * s) / 8.0f;
    }

    [[nodiscard]] bool ComputeISL(float F, float r, float c, ISLParams& out) noexcept;

}
