#pragma once
// Revola DSP core. Plain C++17, no JUCE dependency, so it can be tested on its own.
//
// Signal flow (modelled on the hardware reverb + tremolo box design):
//
//   input --> [send gate] --+--> low-cut --> reverb --> tremolo --> level --+--> output
//                           |                                               |
//   noise source -----------+                                               |
//                                                                           |
//   input (dry) --------------------------------- (MIX mode only) ----------+
//
// The send gate is what the FX button controls: releasing it stops feeding the
// reverb, but the tail keeps ringing out through the tremolo.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace revola
{
constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

template <typename T>
inline T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

//==============================================================================
class Smoothed
{
public:
    void prepare (double sampleRate, float seconds, float initial)
    {
        coeff = 1.0f - std::exp (-1.0f / (float (sampleRate) * seconds));
        value = target = initial;
    }
    void setTarget (float t) { target = t; }
    void snap (float v)      { value = target = v; }
    float next()
    {
        value += coeff * (target - value);
        return value;
    }
    float current() const   { return value; }
    float getTarget() const { return target; }

private:
    float value = 0.0f, target = 0.0f, coeff = 1.0f;
};

//==============================================================================
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setHighPass (double sr, double f, double q = 0.70710678)
    {
        f = clampv (f, 5.0, sr * 0.45);
        const double w = kTwoPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double alpha = s / (2.0 * q), a0 = 1.0 + alpha;
        b0 = float ((1.0 + c) * 0.5 / a0);
        b1 = float (-(1.0 + c) / a0);
        b2 = b0;
        a1 = float (-2.0 * c / a0);
        a2 = float ((1.0 - alpha) / a0);
    }

    // RBJ high shelf, slope S = 1
    void setHighShelf (double sr, double f, double gainDb)
    {
        f = clampv (f, 20.0, sr * 0.45);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = kTwoPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double alpha = s / 2.0 * std::sqrt (2.0);
        const double tsa = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1) - (A - 1) * c + tsa;
        b0 = float (A * ((A + 1) + (A - 1) * c + tsa) / a0);
        b1 = float (-2.0 * A * ((A - 1) + (A + 1) * c) / a0);
        b2 = float (A * ((A + 1) + (A - 1) * c - tsa) / a0);
        a1 = float (2.0 * ((A - 1) - (A + 1) * c) / a0);
        a2 = float (((A + 1) - (A - 1) * c - tsa) / a0);
    }
};

//==============================================================================
// Circular delay line, fractional (linear) read. Read BEFORE push each sample;
// read (d) returns the sample pushed d samples ago, d >= 1.
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read (float d) const
    {
        const int di = (int) d;
        const float fr = d - (float) di;
        const int i0 = (w - di) & mask;
        const int i1 = (i0 - 1) & mask;
        return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

class Allpass
{
public:
    void prepare (int lengthSamples, float gain)
    {
        len = lengthSamples;
        g = gain;
        line.prepare (len + 4);
    }
    void clear() { line.clear(); }
    float process (float x)
    {
        const float d = line.read ((float) len);
        const float v = x + g * d;
        line.push (v);
        return d - g * v;
    }

private:
    DelayLine line;
    int len = 1;
    float g = 0.6f;
};

class NoiseGen
{
public:
    explicit NoiseGen (uint32_t seed = 22222u) : s (seed) {}
    float next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float ((int32_t) s) * 4.656612875e-10f; // -1..1
    }

private:
    uint32_t s;
};

//==============================================================================
// 8-line feedback delay network with Hadamard mixing, in-loop damping,
// slow delay modulation, input diffusion and a multi-tap early-reflection stage.
//   character -> room size (delay lengths) and early-reflection prominence
//   rt60      -> decay time in seconds
//   air       -> high-frequency damping in the loop + output high shelf
class Reverb
{
public:
    static constexpr int N = 8;
    static constexpr int kTaps = 6;
    static constexpr float kMaxSize = 1.5f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const float k = float (sr / 48000.0);
        static const int baseSamples[N] = { 1117, 1327, 1559, 1709, 1913, 2099, 2297, 2477 };
        static const float modRates[N]  = { 0.11f, 0.17f, 0.23f, 0.29f, 0.13f, 0.19f, 0.31f, 0.37f };

        modDepth = 5.0f * k;
        for (int i = 0; i < N; ++i)
        {
            baseLen[i] = (float) baseSamples[i] * k;
            lines[i].prepare (int (baseLen[i] * kMaxSize + modDepth) + 32);
            modInc[i]   = float (kTwoPi * modRates[i] / sr);
            modPhase[i] = (float) i * 0.7853982f;
            lp[i] = 0.0f;
            gain[i] = 0.0f;
        }

        apL[0].prepare (int (142 * k), 0.62f);
        apL[1].prepare (int (379 * k), 0.62f);
        apR[0].prepare (int (107 * k), 0.62f);
        apR[1].prepare (int (277 * k), 0.62f);

        const int erMax = int (0.09 * sr) + 8;
        erL.prepare (erMax);
        erR.prepare (erMax);

        sizeSm.prepare (sr, 0.15f, 0.5f);
        rtSm.prepare (sr, 0.05f, 2.5f);
        airSm.prepare (sr, 0.05f, 0.6f);
        charSm.prepare (sr, 0.05f, 0.5f);
        sizeSm.snap (sizeFromCharacter (0.5f));
        rtSm.snap (2.5f);
        airSm.snap (0.6f);
        charSm.snap (0.5f);

        reset();
        ctrl = kCtrl; // force an update on the first sample
    }

    void reset()
    {
        for (int i = 0; i < N; ++i) { lines[i].clear(); lp[i] = 0.0f; }
        for (auto& a : apL) a.clear();
        for (auto& a : apR) a.clear();
        erL.clear();
        erR.clear();
        shelfL.reset();
        shelfR.reset();
    }

    void setParams (float character, float rt60Seconds, float air)
    {
        sizeSm.setTarget (sizeFromCharacter (clampv (character, 0.0f, 1.0f)));
        charSm.setTarget (clampv (character, 0.0f, 1.0f));
        rtSm.setTarget (clampv (rt60Seconds, 0.1f, 30.0f));
        airSm.setTarget (clampv (air, 0.0f, 1.0f));
    }

    // Jump all smoothed values to their targets (call after setParams on prepare/reset)
    void snapToParams()
    {
        sizeSm.snap (sizeSm.getTarget());
        rtSm.snap (rtSm.getTarget());
        airSm.snap (airSm.getTarget());
        charSm.snap (charSm.getTarget());
        ctrl = kCtrl;
    }

    void process (float inL, float inR, float& outL, float& outR)
    {
        sizeCur = sizeSm.next();
        rtCur   = rtSm.next();
        airCur  = airSm.next();
        charCur = charSm.next();
        if (++ctrl >= kCtrl) { ctrl = 0; updateControl(); }

        // Early reflections (read before push)
        static const float tapMsL[kTaps] = { 7.3f, 13.1f, 19.7f, 28.3f, 37.9f, 49.1f };
        static const float tapMsR[kTaps] = { 9.1f, 15.7f, 23.3f, 31.1f, 42.7f, 53.3f };
        static const float tapG[kTaps]   = { 0.85f, 0.70f, 0.58f, 0.46f, 0.36f, 0.27f };
        static const float tapS[kTaps]   = { 1.f, -1.f, 1.f, 1.f, -1.f, 1.f };
        float erOutL = 0.0f, erOutR = 0.0f;
        const float msToSamp = float (sr) * 0.001f * sizeCur;
        for (int t = 0; t < kTaps; ++t)
        {
            erOutL += tapS[t] * tapG[t] * erL.read (tapMsL[t] * msToSamp);
            erOutR += tapS[t] * tapG[t] * erR.read (tapMsR[t] * msToSamp);
        }
        erL.push (inL);
        erR.push (inR);

        // Input diffusion
        const float dl = apL[1].process (apL[0].process (inL));
        const float dr = apR[1].process (apR[0].process (inR));

        // FDN
        float ys[N], yd[N];
        for (int i = 0; i < N; ++i)
        {
            const float mod = modDepth * std::sin (modPhase[i]);
            modPhase[i] += modInc[i];
            if (modPhase[i] > (float) kTwoPi) modPhase[i] -= (float) kTwoPi;

            const float x = lines[i].read (baseLen[i] * sizeCur + mod);
            lp[i] += dampCoef * (x - lp[i]);
            yd[i] = lp[i];
            ys[i] = lp[i] * gain[i];
        }
        hadamard8 (ys);

        static const float inSign[N] = { 1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, -1.f };
        for (int i = 0; i < N; ++i)
            lines[i].push (kInGain * inSign[i] * ((i & 1) ? dr : dl) + ys[i]);

        const float fdnL = yd[0] - yd[2] + yd[4] - yd[6];
        const float fdnR = yd[1] + yd[3] - yd[5] - yd[7];

        const float erLevel = 0.6f - 0.35f * charCur;
        float wl = kOutScale * fdnL + erLevel * 0.25f * erOutL;
        float wr = kOutScale * fdnR + erLevel * 0.25f * erOutR;

        outL = shelfL.process (wl);
        outR = shelfR.process (wr);
    }

private:
    static constexpr int kCtrl = 32;
    static constexpr float kInGain = 0.5f;
    static constexpr float kOutScale = 0.5f;

    static float sizeFromCharacter (float c) { return 0.55f + 0.95f * c; }

    static void hadamard8 (float* x)
    {
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += h * 2)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }
        for (int i = 0; i < 8; ++i) x[i] *= 0.35355339f;
    }

    void updateControl()
    {
        const float srf = (float) sr;
        for (int i = 0; i < N; ++i)
        {
            const float len = baseLen[i] * sizeCur;
            gain[i] = std::min (std::pow (10.0f, -3.0f * len / (srf * std::max (0.1f, rtCur))), 0.99995f);
        }
        float fc = 1500.0f * std::pow (2.0f, 3.6f * airCur);
        fc = std::min (fc, 0.45f * srf);
        dampCoef = 1.0f - std::exp (-(float) kTwoPi * fc / srf);

        shelfL.setHighShelf (sr, 6500.0, -4.0 + 9.0 * (double) airCur);
        shelfR.b0 = shelfL.b0; shelfR.b1 = shelfL.b1; shelfR.b2 = shelfL.b2;
        shelfR.a1 = shelfL.a1; shelfR.a2 = shelfL.a2;
    }

    double sr = 48000.0;
    DelayLine lines[N];
    float baseLen[N] {}, modInc[N] {}, modPhase[N] {}, lp[N] {}, gain[N] {};
    float modDepth = 5.0f, dampCoef = 0.5f;
    Allpass apL[2], apR[2];
    DelayLine erL, erR;
    Biquad shelfL, shelfR;
    Smoothed sizeSm, rtSm, airSm, charSm;
    float sizeCur = 1.0f, rtCur = 2.5f, airCur = 0.6f, charCur = 0.5f;
    int ctrl = 0;
};

//==============================================================================
// Tremolo helpers shared by the audio engine and the UI scope.
inline float lfoUnipolar (float phase, float shape)
{
    float s = std::cos ((float) kTwoPi * phase);   // peak at phase 0 (the downbeat when synced)
    if (shape > 0.001f)
    {
        const float k = 1.0f + 9.0f * shape * shape;  // sine -> soft square
        s = std::tanh (k * s) / std::tanh (k);
    }
    return 0.5f + 0.5f * s;
}

inline float tremGain (float phase, float depth, float shape)
{
    return 1.0f - depth * (1.0f - lfoUnipolar (phase, shape));
}

//==============================================================================
struct Params
{
    float level = 0.5f;        // 0..1   reverb return level
    float character = 0.5f;    // 0..1   room size / early reflections
    float lengthSec = 2.8f;    // RT60 in seconds
    float air = 0.6f;          // 0..1   brightness
    float lowCutHz = 120.0f;   // reverb send high-pass
    float noise = 0.0f;        // 0..1   white noise into the reverb
    float tremDepth = 0.0f;    // 0..1
    float tremRateHz = 2.0f;
    float tremShape = 0.0f;    // 0 sine .. 1 soft square
    float tremSpread = 0.0f;   // 0 mono .. 1 L/R in antiphase
    float outputDb = 0.0f;
    bool  wetOnly = false;     // false = inline MIX, true = send/return WET
    bool  engaged = true;      // FX button
};

class Engine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reverb.prepare (sr);
        sendEnv.prepare (sr, 0.004f, 1.0f);
        levelSm.prepare (sr, 0.02f, 0.5f);
        drySm.prepare (sr, 0.02f, 1.0f);
        outSm.prepare (sr, 0.02f, 1.0f);
        noiseSm.prepare (sr, 0.02f, 0.0f);
        depthSm.prepare (sr, 0.02f, 0.0f);
        lowCutSm.prepare (sr, 0.03f, 120.0f);
        noiseL = NoiseGen (12345u);
        noiseR = NoiseGen (67890u);
        reset();
        setParams (Params {});
        snapToParams();
    }

    void reset()
    {
        reverb.reset();
        hpL.reset();
        hpR.reset();
        trem.phase = 0.0;
    }

    void setParams (const Params& p)
    {
        params = p;
        reverb.setParams (p.character, p.lengthSec, p.air);
        sendEnv.setTarget (p.engaged ? 1.0f : 0.0f);
        levelSm.setTarget (levelToGain (p.level));
        drySm.setTarget (p.wetOnly ? 0.0f : 1.0f);
        outSm.setTarget (dbToGain (p.outputDb));
        noiseSm.setTarget (p.noise * p.noise * 0.35f);
        depthSm.setTarget (p.tremDepth);
        lowCutSm.setTarget (p.lowCutHz);
        trem.rateHz = p.tremRateHz;
    }

    // Jump all smoothed values to the current parameters (call after setParams in prepareToPlay)
    void snapToParams()
    {
        reverb.snapToParams();
        for (Smoothed* sm : { &sendEnv, &levelSm, &drySm, &outSm, &noiseSm, &depthSm, &lowCutSm })
            sm->snap (sm->getTarget());
        ctrl = kCtrl;
    }

    void setTremoloPhase (double p) { trem.phase = p - std::floor (p); }
    double getTremoloPhase() const  { return trem.phase; }

    void process (float* l, float* r, int n)
    {
        const float shape = params.tremShape;
        const float spread = params.tremSpread;
        const double inc = trem.rateHz / sr;

        for (int i = 0; i < n; ++i)
        {
            if (++ctrl >= kCtrl)
            {
                ctrl = 0;
                hpL.setHighPass (sr, lowCutSm.current());
                hpR.b0 = hpL.b0; hpR.b1 = hpL.b1; hpR.b2 = hpL.b2; hpR.a1 = hpL.a1; hpR.a2 = hpL.a2;
            }
            lowCutSm.next();

            const float send = sendEnv.next();
            const float dryL = l[i], dryR = r[i];
            const float ng = noiseSm.next() * send;

            float inL = dryL * send + noiseL.next() * ng;
            float inR = dryR * send + noiseR.next() * ng;
            inL = hpL.process (inL);
            inR = hpR.process (inR);

            float wl, wr;
            reverb.process (inL, inR, wl, wr);

            // Tremolo sits AFTER the reverb, on the wet signal only
            const float depth = depthSm.next();
            const float pL = (float) trem.phase;
            float pR = pL + 0.5f * spread;
            pR -= std::floor (pR);
            wl *= tremGain (pL, depth, shape);
            wr *= tremGain (pR, depth, shape);
            trem.phase += inc;
            if (trem.phase >= 1.0) trem.phase -= 1.0;

            const float lg = levelSm.next();
            const float dg = drySm.next();
            const float og = outSm.next();
            l[i] = (dryL * dg + wl * lg) * og;
            r[i] = (dryR * dg + wr * lg) * og;
        }
    }

private:
    static constexpr int kCtrl = 32;
    static float levelToGain (float level) { return 2.0f * level * level; } // 0..+6 dB, audio taper

    struct Trem { double phase = 0.0; float rateHz = 2.0f; };

    double sr = 48000.0;
    Params params;
    Reverb reverb;
    Biquad hpL, hpR;
    NoiseGen noiseL, noiseR;
    Smoothed sendEnv, levelSm, drySm, outSm, noiseSm, depthSm, lowCutSm;
    Trem trem;
    int ctrl = 0;
};

} // namespace revola
