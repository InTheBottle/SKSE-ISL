#include "ISLMath.h"

namespace isl {

    bool ComputeISL(float F, float r, float c, ISLParams& out) noexcept
    {
        if (r <= 0.0f || c <= 0.0f || F <= 0.0f)
            return false;
        const float fmc = F - c;
        if (fmc < MinFmc)
            return false;
        const float s2 = (c * r * r) / (K_S * fmc);
        if (s2 <= 0.0f)
            return false;
        const float s = std::sqrt(s2);
        if (s >= MaxSize)
            return false;
        const float I = (F * c * r * r) / (K_I * fmc);
        out.intensity = I;
        out.size      = s;
        out.cutoff    = c;
        return true;
    }

    float BaseISLIntensity(float F, float r, float c) noexcept
    {
        if (r <= 0.0f || c <= 0.0f || F <= 0.0f)
            return 0.0f;
        const float fmc = F - c;
        if (fmc < MinFmc)
            return 0.0f;
        return (F * c * r * r) / (K_I * fmc);
    }

}  // namespace isl
