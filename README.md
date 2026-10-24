# Revola

A reverb + tremolo effect for afro house, built as a VST3 (plus AU and standalone) for **Windows and macOS** (Apple Silicon and Intel).

It follows the layout of the TEILE TEIL2 "Revolo" hardware box (reverb into tremolo, noise source, low-cut on the send, tap tempo, inline vs send/return, latch/momentary FX button). It is an original implementation with its own name and artwork. It does not use TEILE's name, logo, or any of their code or circuit.

## Controls

| Control | What it does |
|---|---|
| **Level** | Reverb return level. |
| **Character** | Room size and early-reflection balance (small and tight to large and diffuse). |
| **Length** | Decay time, 0.2 to 12 s. |
| **Air** | Brightness of the tail (damping inside the loop plus a high shelf). |
| **Low cut** | High-pass on the reverb send, 20 Hz to 1 kHz, keeps kick and bass out of the wash. |
| **Noise** | White noise fed into the reverb (not the dry signal). Sweep Low cut with Noise up for risers. |
| **Tremolo** (Depth, Rate, Shape, Spread) | Sits **after** the reverb and only affects the wet signal. Shape morphs sine to soft square (dub chop). Spread offsets left/right phase for auto-pan. |
| **Free / Host sync** | Free-running Rate in Hz, or locked to host tempo and bar position with a division (2/1 to 1/32, triplets, dotted). |
| **Tap tempo** | Tap the beat; Rate follows (free mode). |
| **Mix / Wet** | Mix = inline (dry plus reverb). Wet = reverb only, for aux sends and return buses. |
| **FX button** | Gates the send. **Latch** toggles. **Momentary** engages only while held; on release the tail rings out. Both are automatable. |
| **Output** | Output trim, plus a stereo peak meter. |

The window is resizable (75% to 200%). 11 presets are included (Clap Splash, Conga Room, Pulse Wash, Dub Chop, Noise Riser, Cathedral Send, ...).

## Getting the binaries

### Option A: GitHub Actions (no local toolchain)
1. Create a GitHub repo and push this folder.
2. Open the **Actions** tab. The **Build Revola** workflow runs the DSP tests, then builds on Windows and macOS runners.
3. Download the `Revola-Windows` and `Revola-macOS` artifacts.

### Option B: Build locally
Requirements: CMake 3.22+, and Visual Studio 2022 (Windows) or Xcode command line tools (macOS). Needs internet on first configure (fetches JUCE 8.0.6).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```
Add `-DREVOLA_COPY_AFTER_BUILD=ON` to install straight into your plug-in folders.

## Installing

| | VST3 location |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3\Revola.vst3` |
| macOS | `/Library/Audio/Plug-Ins/VST3/` or `~/Library/Audio/Plug-Ins/VST3/` |
| macOS AU (Logic, GarageBand) | `~/Library/Audio/Plug-Ins/Components/Revola.component` |

Rescan plug-ins in your DAW. Works in any VST3 host: FL Studio, Ableton Live 11+, Reaper, Bitwig, Cubase, Studio One, and so on. VST2 is not supported (Steinberg ended VST2 licensing); Live 10 and older only load VST2.

**macOS Gatekeeper:** unsigned downloads get quarantined. Either sign and notarize (needs an Apple Developer account) or run `xattr -cr /path/to/Revola.vst3` after copying.

## Tests

The DSP engine (`Source/RevolaDSP.h`) has no JUCE dependency and has its own test:

```
g++ -O2 -std=c++17 tests/dsp_test.cpp -o dsp_test && ./dsp_test
```
It checks stability at 44.1 to 192 kHz with extreme settings, decay time vs Length, tremolo depth and rate, FX-gate tail behaviour, WET mode, noise routing, low-cut, startup state and CPU load (about 1% of one core).

## Make it yours
- Rename the company, manufacturer code, plugin code and bundle ID in `CMakeLists.txt`. The 4-character codes must be unique to you.
- Presets live in `Source/PluginProcessor.cpp` (`kPresets`).
- Colours live in the `pal` namespace at the top of `Source/PluginEditor.cpp`.

## Licensing
JUCE is dual-licensed: AGPLv3 (your plugin must then be open source) or a JUCE commercial licence for closed-source/commercial distribution. Check juce.com/juce-licence before you sell or ship it.
