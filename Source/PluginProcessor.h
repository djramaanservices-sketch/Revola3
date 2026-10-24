#pragma once

#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "RevolaDSP.h"

namespace ids
{
inline constexpr const char* level      = "level";
inline constexpr const char* character  = "character";
inline constexpr const char* length     = "length";
inline constexpr const char* air        = "air";
inline constexpr const char* lowcut     = "lowcut";
inline constexpr const char* noise      = "noise";
inline constexpr const char* tremDepth  = "trem_depth";
inline constexpr const char* tremRate   = "trem_rate";
inline constexpr const char* tremSync   = "trem_sync";
inline constexpr const char* tremDiv    = "trem_div";
inline constexpr const char* tremShape  = "trem_shape";
inline constexpr const char* tremSpread = "trem_spread";
inline constexpr const char* mode       = "mode";        // 0 = MIX (inline), 1 = WET (send/return)
inline constexpr const char* fx         = "fx";          // FX button
inline constexpr const char* fxMomentary = "fx_momentary"; // 0 = latch, 1 = momentary
inline constexpr const char* output     = "output";
} // namespace ids

class RevolaAudioProcessor : public juce::AudioProcessor
{
public:
    RevolaAudioProcessor();
    ~RevolaAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Revola"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 13.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Message-thread helper: sets the tremolo rate from the interval between taps.
    void tapTempo();

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& divisionNames();

    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor (message thread)
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };   // peak since last read
    std::atomic<float> lfoPhaseUI { 0.0f };

private:
    revola::Params readParams() const;

    revola::Engine engine;
    std::array<float, 512> monoScratch {};
    static constexpr int kChunk = 512;

    struct Cache
    {
        std::atomic<float>* level = nullptr; std::atomic<float>* character = nullptr;
        std::atomic<float>* length = nullptr; std::atomic<float>* air = nullptr;
        std::atomic<float>* lowcut = nullptr; std::atomic<float>* noise = nullptr;
        std::atomic<float>* depth = nullptr; std::atomic<float>* rate = nullptr;
        std::atomic<float>* sync = nullptr; std::atomic<float>* div = nullptr;
        std::atomic<float>* shape = nullptr; std::atomic<float>* spread = nullptr;
        std::atomic<float>* mode = nullptr; std::atomic<float>* fx = nullptr;
        std::atomic<float>* output = nullptr;
    } p;

    int currentProgram = 0;
    double lastTapMs = 0.0;
    std::array<double, 4> tapIntervals {};
    int numTapIntervals = 0, tapWrite = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RevolaAudioProcessor)
};
