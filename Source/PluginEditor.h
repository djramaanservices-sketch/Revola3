#pragma once

#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// The editor is a thin resizable shell. All controls live in a fixed-size
// design-space view (780 x 480) that is scaled to fit the window.
class RevolaAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit RevolaAudioProcessorEditor (RevolaAudioProcessor&);
    ~RevolaAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::unique_ptr<juce::Component> view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RevolaAudioProcessorEditor)
};
