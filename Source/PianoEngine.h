#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

// Small additive piano: inharmonic partials, per-partial decay, hammer thump.
class PianoEngine
{
public:
    void prepare (double sr) { sampleRate = sr; reset(); }
    void reset() { for (auto& v : voices) v.active = false; }

    void noteOn (int note, int velocity)
    {
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) { v = &voices[0]; for (auto& c : voices) if (c.age < v->age) v = &c; }
        v->start (note, velocity, sampleRate, ++counter);
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
            if (v.active && v.held && v.note == note) v.release (sampleRate);
    }

    float render()
    {
        float s = 0.f;
        for (auto& v : voices) if (v.active) s += v.render();
        return std::tanh (s);
    }

private:
    struct Voice
    {
        static constexpr int NH = 10;
        bool active = false, held = false;
        int note = 0, nh = 0;
        uint64_t age = 0;
        float amp[NH] {}, dec[NH] {};
        double ph[NH] {}, inc[NH] {};
        float att = 0, attInc = 0, rel = 1, relMul = 1, hammer = 0, hammerMul = 0, lp = 0;
        uint32_t rng = 12345;

        void start (int n, int vel, double sr, uint64_t a)
        {
            active = true; held = true; note = n; age = a; rel = 1.f; att = 0.f; lp = 0.f;
            attInc = 1.f / (0.002f * (float) sr);
            const float vn = (float) std::max (1, vel) / 127.f;
            const double f0 = 440.0 * std::pow (2.0, (n - 69) / 12.0);
            const double B  = 8e-5 * std::exp ((n - 21) * 0.04);
            const double t60 = 6.0 * std::exp (-(n - 21) * 0.025) + 0.4;

            nh = 0;
            for (int h = 1; h <= NH; ++h)
            {
                const double fh = h * f0 * std::sqrt (1.0 + B * h * h);
                if (fh > 0.45 * sr) break;
                amp[nh] = 0.28f * std::pow (vn, 0.6f) * std::pow ((float) h, -1.1f)
                                * std::pow (vn, 0.12f * (float) (h - 1));
                dec[nh] = (float) std::exp (-6.9078 / ((t60 / (1.0 + 0.8 * (h - 1))) * sr));
                inc[nh] = fh / sr;
                ph[nh]  = 0.0;
                ++nh;
            }
            hammer    = 0.12f * vn;
            hammerMul = (float) std::exp (-6.9078 / (0.005 * sr));
        }

        void release (double sr)
        {
            held = false;
            relMul = (float) std::exp (-6.9078 / (0.12 * sr));
        }

        float render()
        {
            float s = 0.f;
            for (int k = 0; k < nh; ++k)
            {
                ph[k] += inc[k];
                if (ph[k] >= 1.0) ph[k] -= 1.0;
                s += amp[k] * std::sin (6.2831853f * (float) ph[k]);
                amp[k] *= dec[k];
            }
            rng = rng * 1664525u + 1013904223u;
            const float noise = (float) (rng >> 8) * (1.f / 8388608.f) - 1.f;
            lp += 0.25f * (noise - lp);
            s += lp * hammer;
            hammer *= hammerMul;

            if (att < 1.f) att += attInc;
            s *= std::min (att, 1.f) * rel;
            if (! held) rel *= relMul;
            if (nh == 0 || amp[0] * rel < 1e-4f) active = false;
            return s;
        }
    };

    std::array<Voice, 12> voices;
    double sampleRate = 44100.0;
    uint64_t counter = 0;
};
