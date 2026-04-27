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
        // Peak-match: ISL intensity at d=0 equals vanilla fade.
        // Size s already encodes the cutoff distance, so no extra scaling on I.
        out.intensity = F;
        out.size      = s;
        out.cutoff    = c;
        return true;
    }

}  // namespace isl
