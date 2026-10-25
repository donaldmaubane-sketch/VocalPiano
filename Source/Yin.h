#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// YIN pitch detector (de Cheveigné & Kawahara, 2002), monophonic voice range ~70Hz-1.1kHz.
class Yin
{
public:
    struct Result { float freq = 0.f; float prob = 0.f; };

    void prepare (double sr, int windowSize)
    {
        sampleRate = sr;
        half   = windowSize / 2;
        tauMin = std::max (2, (int) (sr / 1100.0));
        tauMax = std::min (half - 1, (int) (sr / 70.0));
        d.assign ((size_t) half + 1, 0.f);
        cm.assign ((size_t) half + 1, 1.f);
    }

    // x must hold at least 2*half samples
    Result detect (const float* x)
    {
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.f;
            for (int j = 0; j < half; ++j)
            {
                const float diff = x[j] - x[j + tau];
                sum += diff * diff;
            }
            d[(size_t) tau] = sum;
        }

        cm[0] = 1.f;
        float running = 0.f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += d[(size_t) tau];
            cm[(size_t) tau] = running > 0.f ? d[(size_t) tau] * (float) tau / running : 1.f;
        }

        int tau = tauMin;
        bool found = false;
        while (tau <= tauMax)
        {
            if (cm[(size_t) tau] < threshold)
            {
                while (tau + 1 <= tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                found = true;
                break;
            }
            ++tau;
        }
        if (! found) return {};

        float better = (float) tau;
        if (tau > 1 && tau < tauMax)
        {
            const float s0 = cm[(size_t) tau - 1], s1 = cm[(size_t) tau], s2 = cm[(size_t) tau + 1];
            const float den = 2.f * (2.f * s1 - s2 - s0);
            if (std::fabs (den) > 1e-9f) better += (s2 - s0) / den;
        }
        return { (float) (sampleRate / better), 1.f - cm[(size_t) tau] };
    }

private:
    double sampleRate = 44100.0;
    int half = 1024, tauMin = 40, tauMax = 600;
    float threshold = 0.15f;
    std::vector<float> d, cm;
};
