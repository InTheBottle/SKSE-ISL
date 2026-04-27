#pragma once

#include "PCH.h"

namespace isl {

    inline constexpr float K_S            = 1960.0f;
    inline constexpr float CutoffRegular  = 0.05f;
    inline constexpr float CutoffShadow   = 0.022f;

    // Engine snaps DATA\FOV >= 50 to sqrt(2) (treated as "no ISL size set").
    inline constexpr float MaxSize        = 49.99f;

    // Numerical guard for F barely above c.
    inline constexpr float MinFmc         = 0.001f;

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

    [[nodiscard]] constexpr float DefaultCutoff(std::uint32_t flags) noexcept
    {
        return IsShadowCaster(flags) ? CutoffShadow : CutoffRegular;
    }

    [[nodiscard]] bool ComputeISL(float F, float r, float c, ISLParams& out) noexcept;

}
