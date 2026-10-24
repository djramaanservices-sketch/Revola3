#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <iterator>

namespace
{
// Tremolo cycle length in beats for each division choice (index matches divisionNames()).
constexpr double kDivBeats[] = { 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
                                 1.5, 0.75, 0.375 };

struct Preset
{
    const char* name;
    float level, character, length, air, lowcut, noise, depth, rate;
    bool sync;
    int div;
    float shape, spread;
    int mode;
};

// Division indices: 0 2/1, 1 1/1, 2 1/2, 3 1/4, 4 1/8, 5 1/16, 6 1/32, 7 1/4T, 8 1/8T, 9 1/16T, 10 1/4., 11 1/8., 12 1/16.
const Preset kPresets[] =
{
    //  name              lvl   chr   len    air   lowcut noise depth rate  sync div  shape spread mode
    { "Init",             0.50f, 0.50f, 2.8f,  0.60f, 120.f, 0.00f, 0.00f, 2.07f, false, 4, 0.0f, 0.0f, 0 },
    { "Clap Splash",      0.55f, 0.25f, 1.2f,  0.75f, 220.f, 0.00f, 0.00f, 2.07f, false, 4, 0.0f, 0.0f, 0 },
    { "Conga Room",       0.50f, 0.35f, 1.8f,  0.55f, 180.f, 0.00f, 0.25f, 2.07f, true,  4, 0.3f, 0.2f, 0 },
    { "Tribal Tail",      0.60f, 0.60f, 3.6f,  0.50f, 140.f, 0.00f, 0.35f, 2.07f, true,  4, 0.2f, 0.4f, 0 },
    { "Pulse Wash",       0.60f, 0.70f, 5.5f,  0.60f, 200.f, 0.10f, 0.80f, 2.07f, true,  4, 0.6f, 0.6f, 0 },
    { "Vocal Throw",      0.55f, 0.75f, 4.2f,  0.70f, 260.f, 0.00f, 0.20f, 2.07f, true,  3, 0.0f, 0.3f, 0 },
    { "Dub Chop",         0.65f, 0.55f, 3.0f,  0.45f, 300.f, 0.00f, 0.95f, 2.07f, true,  5, 1.0f, 0.0f, 0 },
    { "Noise Riser",      0.70f, 0.80f, 7.5f,  0.80f, 480.f, 0.75f, 0.50f, 0.40f, false, 4, 0.0f, 0.3f, 0 },
    { "Sunset Hall",      0.50f, 0.90f, 9.0f,  0.55f,  90.f, 0.00f, 0.30f, 0.25f, false, 4, 0.0f, 0.8f, 0 },
    { "Tape Flutter",     0.45f, 0.30f, 2.2f,  0.40f, 160.f, 0.00f, 0.55f, 5.50f, false, 4, 0.0f, 0.5f, 0 },
    { "Cathedral Send",   0.70f, 0.95f, 10.0f, 0.50f, 150.f, 0.00f, 0.20f, 2.07f, true,  3, 0.0f, 0.5f, 1 },
};

void setPlain (juce::AudioProcessorValueTreeState& s, const char* id, float plain)
{
    if (auto* prm = s.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    }
}
} // namespace

//==============================================================================
const juce::StringArray& RevolaAudioProcessor::divisionNames()
{
    static const juce::StringArray names { "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                                           "1/4T", "1/8T", "1/16T", "1/4.", "1/8.", "1/16." };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout RevolaAudioProcessor::createLayout()
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    using Attr = juce::AudioParameterFloatAttributes;
    using juce::NormalisableRange;
    using juce::String;
    using PID = juce::ParameterID;

    const auto pct  = [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + " %"; };
    const auto pctV = [] (const String& t) { return t.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; };
    const auto numV = [] (const String& t) { return t.retainCharacters ("0123456789.-").getFloatValue(); };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<APF> (PID { ids::level, 1 }, "Level", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                       Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctV)));
    layout.add (std::make_unique<APF> (PID { ids::character, 1 }, "Character", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                       Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctV)));

    NormalisableRange<float> lengthRange (0.2f, 12.0f);
    lengthRange.setSkewForCentre (2.5f);
    layout.add (std::make_unique<APF> (PID { ids::length, 1 }, "Length", lengthRange, 2.8f,
                                       Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " s"; })
                                             .withValueFromStringFunction (numV)));

    layout.add (std::make_unique<APF> (PID { ids::air, 1 }, "Air", NormalisableRange<float> (0.0f, 1.0f), 0.6f,
                                       Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctV)));

    NormalisableRange<float> lowcutRange (20.0f, 1000.0f);
    lowcutRange.setSkewForCentre (150.0f);
    layout.add (std::make_unique<APF> (PID { ids::lowcut, 1 }, "Low Cut", lowcutRange, 120.0f,
                                       Attr().withStringFromValueFunction ([] (float v, int) { return v <= 22.0f ? String ("Off") : String (juce::roundToInt (v)) + " Hz"; })
                                             .withValueFromStringFunction (numV)));

    layout.add (std::make_unique<APF> (PID { ids::noise, 1 }, "Noise", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                       Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctV)));

    layout.add (std::make_unique<APF> (PID { ids::tremDepth, 1 }, "Tremolo Depth", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                       Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctV)));

    NormalisableRange<float> rateRange (0.05f, 20.0f);
    rateRange.setSkewForCentre (2.0f);
    layout.add (std::make_unique<APF> (PID { ids::tremRate, 1 }, "Tremolo Rate", rateRange, 2.07f,
                                       Attr().withStringFromValueFunction ([] (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; })
                                             .withValueFromStringFunction (numV)));

    layout.add (std::make_unique<APB> (PID { ids::tremSync, 1 }, "Tremolo Sync", false));
    layout.add (std::make_unique<APC> (PID { ids::tremDiv, 1 }, "Tremolo Division", divisionNames(), 4));

    layout.add (std::make_unique<APF> (PID { ids::tremShape, 1 }, "Tremolo Shape", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                       Attr().withStringFromValueFunction ([] (float v, int) { return v < 0.02f ? String ("Sine") : (v > 0.98f ? String ("Square") : String (juce::roundToInt (v * 100.0f)) + " %"); })
                                             .withValueFromStringFunction (pctV)));
    layout.add (std::make_unique<APF> (PID { ids::tremSpread, 1 }, "Tremolo Spread", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                       Attr().withStringFromValueFunction (pct).withValueFromStringFunction (pctV)));

    layout.add (std::make_unique<APC> (PID { ids::mode, 1 }, "Mix / Wet", juce::StringArray { "Mix", "Wet" }, 0));
    layout.add (std::make_unique<APB> (PID { ids::fx, 1 }, "FX", true));
    layout.add (std::make_unique<APB> (PID { ids::fxMomentary, 1 }, "FX Momentary", false));

    layout.add (std::make_unique<APF> (PID { ids::output, 1 }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                       Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })
                                             .withValueFromStringFunction (numV)));
    return layout;
}

//==============================================================================
RevolaAudioProcessor::RevolaAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "REVOLA", createLayout())
{
    p.level     = apvts.getRawParameterValue (ids::level);
    p.character = apvts.getRawParameterValue (ids::character);
    p.length    = apvts.getRawParameterValue (ids::length);
    p.air       = apvts.getRawParameterValue (ids::air);
    p.lowcut    = apvts.getRawParameterValue (ids::lowcut);
    p.noise     = apvts.getRawParameterValue (ids::noise);
    p.depth     = apvts.getRawParameterValue (ids::tremDepth);
    p.rate      = apvts.getRawParameterValue (ids::tremRate);
    p.sync      = apvts.getRawParameterValue (ids::tremSync);
    p.div       = apvts.getRawParameterValue (ids::tremDiv);
    p.shape     = apvts.getRawParameterValue (ids::tremShape);
    p.spread    = apvts.getRawParameterValue (ids::tremSpread);
    p.mode      = apvts.getRawParameterValue (ids::mode);
    p.fx        = apvts.getRawParameterValue (ids::fx);
    p.output    = apvts.getRawParameterValue (ids::output);
}

revola::Params RevolaAudioProcessor::readParams() const
{
    revola::Params q;
    q.level      = p.level->load();
    q.character  = p.character->load();
    q.lengthSec  = p.length->load();
    q.air        = p.air->load();
    q.lowCutHz   = p.lowcut->load();
    q.noise      = p.noise->load();
    q.tremDepth  = p.depth->load();
    q.tremRateHz = p.rate->load();
    q.tremShape  = p.shape->load();
    q.tremSpread = p.spread->load();
    q.outputDb   = p.output->load();
    q.wetOnly    = p.mode->load() > 0.5f;
    q.engaged    = p.fx->load() > 0.5f;
    return q;
}

void RevolaAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    engine.setParams (readParams());
    engine.snapToParams();
}

bool RevolaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void RevolaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int total = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, total);
    if (numCh < 1 || total < 1)
        return;

    auto params = readParams();

    // Host tempo / position for the synced tremolo
    const bool sync = p.sync->load() > 0.5f;
    const int divIdx = juce::jlimit (0, (int) std::size (kDivBeats) - 1, (int) std::lround (p.div->load()));
    const double beats = kDivBeats[divIdx];
    const double sr = getSampleRate();

    double bpm = 120.0, ppq = 0.0;
    bool playing = false, havePpq = false;
    if (sync)
    {
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) bpm = *b;
                playing = pos->getIsPlaying();
                if (auto q = pos->getPpqPosition()) { ppq = *q; havePpq = true; }
            }
        params.tremRateHz = (float) (bpm / 60.0 / beats);
    }
    engine.setParams (params);

    float* left  = buffer.getWritePointer (0);
    float* right = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    int done = 0;
    while (done < total)
    {
        const int n = std::min (kChunk, total - done);

        if (sync && playing && havePpq)
            engine.setTremoloPhase (std::fmod (ppq / beats, 1.0));

        float* l = left + done;
        float* r = nullptr;
        if (right != nullptr)
            r = right + done;
        else
        {
            std::copy (l, l + n, monoScratch.begin());
            r = monoScratch.data();
        }

        engine.process (l, r, n);
        done += n;
        if (havePpq)
            ppq += (double) n * bpm / (60.0 * sr);
    }

    // Meters + scope
    const float magL = buffer.getMagnitude (0, 0, total);
    const float magR = numCh > 1 ? buffer.getMagnitude (1, 0, total) : magL;
    meterL.store (std::max (meterL.load(), magL));
    meterR.store (std::max (meterR.load(), magR));
    lfoPhaseUI.store ((float) engine.getTremoloPhase());
}

//==============================================================================
void RevolaAudioProcessor::tapTempo()
{
    if (p.sync->load() > 0.5f)
        return; // in sync mode the host tempo rules

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (lastTapMs > 0.0 && now - lastTapMs < 2000.0)
    {
        tapIntervals[(size_t) tapWrite] = now - lastTapMs;
        tapWrite = (tapWrite + 1) % (int) tapIntervals.size();
        numTapIntervals = std::min (numTapIntervals + 1, (int) tapIntervals.size());
    }
    else
    {
        numTapIntervals = 0;
        tapWrite = 0;
    }
    lastTapMs = now;

    if (numTapIntervals > 0)
    {
        double sum = 0.0;
        for (int i = 0; i < numTapIntervals; ++i)
            sum += tapIntervals[(size_t) i];
        const float hz = juce::jlimit (0.05f, 20.0f, (float) (1000.0 / (sum / numTapIntervals)));
        setPlain (apvts, ids::tremRate, hz);
    }
}

//==============================================================================
int RevolaAudioProcessor::getNumPrograms() { return (int) std::size (kPresets); }

const juce::String RevolaAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) std::size (kPresets)) ? kPresets[index].name : "";
}

void RevolaAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) std::size (kPresets)))
        return;

    currentProgram = index;
    const auto& s = kPresets[index];
    setPlain (apvts, ids::level, s.level);
    setPlain (apvts, ids::character, s.character);
    setPlain (apvts, ids::length, s.length);
    setPlain (apvts, ids::air, s.air);
    setPlain (apvts, ids::lowcut, s.lowcut);
    setPlain (apvts, ids::noise, s.noise);
    setPlain (apvts, ids::tremDepth, s.depth);
    setPlain (apvts, ids::tremRate, s.rate);
    setPlain (apvts, ids::tremSync, s.sync ? 1.0f : 0.0f);
    setPlain (apvts, ids::tremDiv, (float) s.div);
    setPlain (apvts, ids::tremShape, s.shape);
    setPlain (apvts, ids::tremSpread, s.spread);
    setPlain (apvts, ids::mode, (float) s.mode);
}

void RevolaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RevolaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentProgram = juce::jlimit (0, (int) std::size (kPresets) - 1,
                                           (int) apvts.state.getProperty ("program", 0));
        }
}

juce::AudioProcessorEditor* RevolaAudioProcessor::createEditor()
{
    return new RevolaAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RevolaAudioProcessor();
}
