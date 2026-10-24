// Build & run:  g++ -O2 -std=c++17 tests/dsp_test.cpp -o dsp_test && ./dsp_test
#include "../Source/RevolaDSP.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#if defined(__SSE__)
 #include <xmmintrin.h>
#endif

using namespace revola;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static double rms (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b && i < v.size(); ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / double (b - a));
}
static bool allFinite (const std::vector<float>& v) { for (float x : v) if (! std::isfinite (x) || std::fabs (x) > 50.f) return false; return true; }

static void render (Engine& e, std::vector<float>& L, std::vector<float>& R)
{
    const int block = 256;
    for (size_t i = 0; i < L.size(); i += block)
        e.process (L.data() + i, R.data() + i, int (std::min<size_t> (block, L.size() - i)));
}

int main()
{
#if defined(__SSE__)
    _mm_setcsr (_mm_getcsr() | 0x8040); // flush denormals, like juce::ScopedNoDenormals
#endif
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

    std::printf ("1. Stability across sample rates and extreme settings\n");
    for (double sr : rates)
    {
        Engine e; e.prepare (sr);
        Params p; p.level = 1; p.lengthSec = 12; p.character = 1; p.air = 1; p.noise = 1; p.tremDepth = 1; p.tremShape = 1; p.tremSpread = 1; p.lowCutHz = 20; p.tremRateHz = 20;
        e.setParams (p); e.snapToParams();
        std::vector<float> L ((size_t) sr * 3), R (L.size());
        for (size_t i = 0; i < L.size(); ++i) { L[i] = ((rand() / (float) RAND_MAX) * 2 - 1) * 0.9f; R[i] = L[i]; }
        render (e, L, R);
        CHECK (allFinite (L) && allFinite (R), "no NaN/inf/blow-up at %.0f Hz (max settings, loud noise)", sr);
    }

    std::printf ("2. Decay time follows Length (impulse response, 48 kHz)\n");
    {
        const double sr = 48000.0;
        for (float len : { 1.0f, 2.8f, 6.0f })
        {
            Engine e; e.prepare (sr);
            Params p; p.lengthSec = len; p.level = 1; p.air = 1.0f; p.lowCutHz = 20; e.setParams (p);
            // let smoothers settle
            std::vector<float> w (4800), w2 (4800); render (e, w, w2);
            e.reset();
            std::vector<float> L ((size_t) (sr * (len * 2.0 + 1))), R (L.size());
            L[0] = R[0] = 1.0f; render (e, L, R);
            // energy decay: compare windowed RMS early vs at len seconds
            const size_t w0 = (size_t) (0.15 * sr), wl = (size_t) (len * sr);
            const double early = rms (L, w0, w0 + 4800), late = rms (L, wl, wl + 4800);
            const double dB = 20.0 * std::log10 (late / early);
            // RT60 means -60 dB at 'len' seconds after the start; damping makes it a bit faster, accept -35..-90
            CHECK (dB < -35.0 && dB > -95.0, "Length %.1fs: level at %.1fs is %.1f dB vs early tail", len, len, dB);
        }
    }

    std::printf ("3. Tremolo depth/shape\n");
    {
        for (float depth : { 0.25f, 0.5f, 1.0f })
        {
            float mn = 10, mx = -10;
            for (int i = 0; i < 1000; ++i) { float g = tremGain (i / 1000.0f, depth, 0.0f); mn = std::min (mn, g); mx = std::max (mx, g); }
            CHECK (std::fabs (mx - 1.0f) < 1e-3f && std::fabs (mn - (1.0f - depth)) < 2e-3f, "depth %.2f -> gain range %.3f..%.3f", depth, mn, mx);
        }
        float sq = tremGain (0.0f, 1.0f, 1.0f), sqLow = tremGain (0.5f, 1.0f, 1.0f);
        CHECK (sq > 0.99f && sqLow < 0.01f, "square shape reaches full on/off (%.3f / %.3f)", sq, sqLow);
    }

    std::printf ("4. Tremolo modulates the reverb tail at the set rate\n");
    {
        const double sr = 48000.0;
        Engine e; e.prepare (sr);
        Params p; p.tremDepth = 1.0f; p.tremRateHz = 4.0f; p.level = 1.0f; p.lengthSec = 8.0f; p.noise = 1.0f; e.setParams (p);
        std::vector<float> L ((size_t) (sr * 4)), R (L.size()); render (e, L, R);
        // Envelope in 10ms windows over the last 2 s; count peaks (~4 Hz -> ~8 peaks in 2 s)
        std::vector<double> env; for (size_t i = (size_t) (2 * sr); i + 480 < L.size(); i += 480) env.push_back (rms (L, i, i + 480));
        int peaks = 0; for (size_t i = 2; i + 2 < env.size(); ++i) if (env[i] > env[i-1] && env[i] > env[i+1] && env[i] > env[i-2] && env[i] > env[i+2]) ++peaks;
        CHECK (peaks >= 7 && peaks <= 9, "counted %d envelope peaks in 2 s at 4 Hz (expect ~8)", peaks);
    }

    std::printf ("5. FX gate: releasing lets the tail ring out; MIX keeps dry\n");
    {
        const double sr = 48000.0;
        Engine e; e.prepare (sr);
        Params p; p.level = 1.0f; p.lengthSec = 3.0f; p.engaged = true; e.setParams (p); e.snapToParams();
        std::vector<float> L ((size_t) sr), R (L.size());
        for (size_t i = 0; i < L.size(); ++i) L[i] = R[i] = 0.5f * std::sin (float (kTwoPi * 220.0 * i / sr));
        render (e, L, R);
        p.engaged = false; e.setParams (p);
        std::vector<float> L2 ((size_t) (sr * 6)), R2 (L2.size());
        for (size_t i = 0; i < L2.size(); ++i) L2[i] = R2[i] = 0.5f * std::sin (float (kTwoPi * 220.0 * i / sr)); // dry keeps playing
        render (e, L2, R2);
        // wet-only to isolate the tail
        Engine e2; e2.prepare (sr);
        Params q = p; q.engaged = true; q.wetOnly = true; e2.setParams (q); e2.snapToParams();
        std::vector<float> A ((size_t) sr), B (A.size());
        for (size_t i = 0; i < A.size(); ++i) A[i] = B[i] = 0.5f * std::sin (float (kTwoPi * 220.0 * i / sr));
        render (e2, A, B);
        q.engaged = false; e2.setParams (q);
        std::vector<float> T ((size_t) (sr * 6)), U (T.size()); // silent input after release
        render (e2, T, U);
        const double t0 = rms (T, (size_t) (0.1 * sr), (size_t) (0.3 * sr));
        const double t2 = rms (T, (size_t) (2.5 * sr), (size_t) (2.7 * sr));
        const double t5 = rms (T, (size_t) (5.5 * sr), (size_t) (5.7 * sr));
        CHECK (t0 > 1e-3, "tail present after release (rms %.4f)", t0);
        CHECK (t2 < t0 && t5 < t2, "tail decays monotonically (%.4f -> %.4f -> %.5f)", t0, t2, t5);
        CHECK (rms (L2, (size_t) (4 * sr), (size_t) (5 * sr)) > 0.3, "MIX mode: dry still passes after release");
    }

    std::printf ("6. WET mode removes dry; noise feeds the reverb only while engaged\n");
    {
        const double sr = 48000.0;
        Engine e; e.prepare (sr);
        Params p; p.wetOnly = true; p.level = 1; p.engaged = false; p.noise = 0.0f; e.setParams (p); e.snapToParams();
        std::vector<float> L ((size_t) sr, 0.5f), R (L.size(), 0.5f); render (e, L, R);
        CHECK (rms (L, (size_t) (0.5 * sr), (size_t) sr) < 1e-3, "WET + FX off + dry input -> silence (rms %.6f)", rms (L, (size_t) (0.5 * sr), (size_t) sr));

        Engine n; n.prepare (sr);
        Params q; q.wetOnly = true; q.level = 1; q.noise = 0.8f; q.engaged = true; q.lengthSec = 4; n.setParams (q);
        std::vector<float> a ((size_t) (sr * 2)), b (a.size()); render (n, a, b);
        const double on = rms (a, (size_t) sr, (size_t) (2 * sr));
        q.engaged = false; n.setParams (q);
        std::vector<float> c ((size_t) (sr * 6)), d (c.size()); render (n, c, d);
        const double ring = rms (c, (size_t) (0.1 * sr), (size_t) (0.3 * sr)), gone = rms (c, (size_t) (5.5 * sr), (size_t) (5.8 * sr));
        CHECK (on > 0.01, "noise-only wash while engaged (rms %.4f)", on);
        CHECK (ring > 0.5 * on && gone < 0.25 * ring, "noise wash rings out then fades (%.4f -> %.4f)", ring, gone);
    }

    std::printf ("7. Low-cut thins the send\n");
    {
        const double sr = 48000.0;
        auto lowEnergy = [&] (float lc)
        {
            Engine e; e.prepare (sr);
            Params p; p.wetOnly = true; p.level = 1; p.lowCutHz = lc; p.lengthSec = 2; p.air = 1; e.setParams (p);
            std::vector<float> L ((size_t) (sr * 2)), R (L.size());
            for (size_t i = 0; i < L.size(); ++i) L[i] = R[i] = 0.5f * std::sin (float (kTwoPi * 60.0 * i / sr));
            render (e, L, R); return rms (L, (size_t) sr, (size_t) (2 * sr));
        };
        const double open = lowEnergy (20), cut = lowEnergy (400);
        CHECK (cut < open * 0.2, "60 Hz tone into reverb: low-cut 400 Hz is %.1f dB below 20 Hz", 20 * std::log10 (open / cut));
    }

    std::printf ("8. Level calibration (wet RMS vs dry RMS, white noise, wet-only)\n");
    for (float len : { 1.0f, 2.8f, 8.0f })
    {
        const double sr = 48000.0;
        Engine e; e.prepare (sr);
        Params p; p.wetOnly = true; p.level = 0.707f; p.lengthSec = len; e.setParams (p);  // level^2*2 = 1.0 gain
        std::vector<float> L ((size_t) (sr * 8)), R (L.size());
        for (size_t i = 0; i < L.size(); ++i) { L[i] = R[i] = ((rand() / (float) RAND_MAX) * 2 - 1) * 0.3f; }
        const double dry = rms (L, 0, L.size());
        render (e, L, R);
        std::printf ("  info: Length %.1fs -> wet is %+.1f dB relative to dry\n", len, 20 * std::log10 (rms (L, (size_t) (4 * sr), L.size()) / dry));
    }

    std::printf ("8b. Startup state matches parameters (no stale ramp)\n");
    {
        const double sr = 48000.0;
        Engine e; e.prepare (sr);
        Params p; p.wetOnly = true; p.engaged = false; p.level = 1.0f; e.setParams (p); e.snapToParams();
        std::vector<float> L (4800, 0.5f), R (4800, 0.5f); render (e, L, R);
        CHECK (rms (L, 0, L.size()) < 1e-4, "first 100 ms with FX off + WET is silent from sample 0 (rms %.7f)", rms (L, 0, L.size()));
    }

    std::printf ("9. CPU\n");
    {
        const double sr = 48000.0;
        Engine e; e.prepare (sr);
        Params p; p.tremDepth = 0.6f; p.noise = 0.3f; e.setParams (p);
        std::vector<float> L ((size_t) (sr * 20)), R (L.size());
        for (size_t i = 0; i < L.size(); ++i) L[i] = R[i] = ((rand() / (float) RAND_MAX) * 2 - 1) * 0.3f;
        const auto t0 = std::chrono::steady_clock::now();
        render (e, L, R);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double load = secs / 20.0 * 100.0;
        CHECK (load < 5.0, "20 s of stereo audio rendered in %.3f s  (~%.2f%% of one core)", secs, load);
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
