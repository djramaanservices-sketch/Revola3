#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr int kDesignW = 780;
constexpr int kDesignH = 480;

// Colour carries meaning: ochre = reverb, teal = tremolo, coral = engage / peaks.
namespace pal
{
const juce::Colour bgTop     { 0xff1b1621 };
const juce::Colour bgBottom  { 0xff100d14 };
const juce::Colour panel     { 0xff231d2b };
const juce::Colour panelEdge { 0xff372f42 };
const juce::Colour well      { 0xff17131c };
const juce::Colour track     { 0xff3b3347 };
const juce::Colour ink       { 0xfff0e9dd };
const juce::Colour dim       { 0xff9189a0 };
const juce::Colour ochre     { 0xffe8ab52 };
const juce::Colour teal      { 0xff4cc7b4 };
const juce::Colour coral     { 0xfff0694c };
} // namespace pal

juce::Font F (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions().withHeight (height).withStyle (bold ? "Bold" : "Regular"));
}

//==============================================================================
class RevolaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RevolaLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, pal::ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, pal::ochre.withAlpha (0.35f));
        setColour (juce::Slider::rotarySliderOutlineColourId, pal::track);

        setColour (juce::ComboBox::textColourId, pal::ink);
        setColour (juce::ComboBox::backgroundColourId, pal::well);
        setColour (juce::ComboBox::outlineColourId, pal::panelEdge);
        setColour (juce::ComboBox::arrowColourId, pal::dim);
        setColour (juce::PopupMenu::backgroundColourId, pal::panel);
        setColour (juce::PopupMenu::textColourId, pal::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::ochre.withAlpha (0.28f));
        setColour (juce::PopupMenu::highlightedTextColourId, pal::ink);
        setColour (juce::TextButton::textColourOffId, pal::ink);
        setColour (juce::TextButton::textColourOnId, pal::ink);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
        const float dia = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto c = bounds.getCentre();
        const float r = dia * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        const bool hot = s.isMouseOverOrDragging();

        const float arcR = r - 2.5f;
        const juce::PathStrokeType thin (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const juce::PathStrokeType wide (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (pal::track);
        g.strokePath (track, thin);

        if (pos > 0.002f)
        {
            juce::Path v;
            v.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (accent.withAlpha (hot ? 0.34f : 0.18f));
            g.strokePath (v, wide);
            g.setColour (accent);
            g.strokePath (v, thin);
        }

        const float capR = arcR - 7.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff40374c), c.x, c.y - capR,
                                                 juce::Colour (0xff1d1823), c.x, c.y + capR, false));
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        g.setColour (juce::Colour (0xff50465e));
        g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.75f, -capR + 4.0f, 3.5f, capR * 0.45f, 1.5f);
        g.setColour (accent);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    juce::Font getLabelFont (juce::Label&) override { return F (12.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return F (14.0f); }
    juce::Font getPopupMenuFont() override { return F (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return F (16.0f, true); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (pal::well);
        g.fillRoundedRectangle (b, 9.0f);
        g.setColour (box.hasKeyboardFocus (true) ? pal::ochre : pal::panelEdge);
        g.drawRoundedRectangle (b, 9.0f, 1.0f);

        juce::Path arrow;
        const float cx = (float) w - 18.0f, cy = (float) h * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (pal::dim);
        g.fillPath (arrow);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& bt, const juce::Colour&, bool highlighted, bool down) override
    {
        const auto b = bt.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? pal::panelEdge : pal::well);
        g.fillRoundedRectangle (b, 9.0f);
        g.setColour (highlighted ? pal::ochre : pal::panelEdge);
        g.drawRoundedRectangle (b, 9.0f, 1.0f);
    }
};

//==============================================================================
class PanelBg : public juce::Component
{
public:
    PanelBg (juce::String t, juce::Colour a) : title (std::move (t)), accent (a)
    {
        setInterceptsMouseClicks (false, false);
        setBufferedToImage (true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (pal::panel);
        g.fillRoundedRectangle (b, 14.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.045f), 0.0f, b.getY(),
                                                 juce::Colours::transparentWhite, 0.0f, b.getY() + 60.0f, false));
        g.fillRoundedRectangle (b, 14.0f);
        g.setColour (pal::panelEdge);
        g.drawRoundedRectangle (b, 14.0f, 1.0f);

        g.setColour (accent);
        g.fillRoundedRectangle (16.0f, 15.0f, 4.0f, 14.0f, 2.0f);
        g.setColour (pal::ink);
        g.setFont (F (15.0f, true));
        g.drawText (title, 28, 11, getWidth() - 40, 22, juce::Justification::centredLeft);
    }

private:
    juce::String title;
    juce::Colour accent;
};

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const char* paramID, juce::String captionText, juce::Colour accent)
        : caption (std::move (captionText))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (220);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        addAndMakeVisible (slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop (18);
        slider.setBounds (b);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (pal::dim);
        g.setFont (F (13.0f, true));
        g.drawText (caption, getLocalBounds().removeFromTop (18), juce::Justification::centred);
    }

private:
    juce::String caption;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// Segmented switch bound to a choice or bool parameter.
class Segmented : public juce::Component
{
public:
    Segmented (juce::AudioProcessorValueTreeState& apvts, const char* paramID,
               juce::StringArray labelsIn, juce::StringArray subtitlesIn, bool isVertical, juce::Colour accentColour)
        : labels (std::move (labelsIn)), subtitles (std::move (subtitlesIn)), vertical (isVertical), accent (accentColour)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (paramID),
                                                                  [this] (float v) { index = juce::roundToInt (v); repaint(); });
        attachment->sendInitialUpdate();
    }

    std::function<void (int)> onUserChange;

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (pal::well);
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (pal::panelEdge);
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

        const int n = labels.size();
        for (int i = 0; i < n; ++i)
        {
            auto cell = vertical ? juce::Rectangle<float> (b.getX(), b.getY() + b.getHeight() * (float) i / (float) n, b.getWidth(), b.getHeight() / (float) n)
                                 : juce::Rectangle<float> (b.getX() + b.getWidth() * (float) i / (float) n, b.getY(), b.getWidth() / (float) n, b.getHeight());
            cell = cell.reduced (3.0f);
            const bool on = (i == index);
            if (on)
            {
                g.setColour (accent);
                g.fillRoundedRectangle (cell, 8.0f);
            }

            const auto fg = on ? pal::well : pal::dim;
            if (subtitles.size() == n)
            {
                auto inner = cell.reduced (14.0f, 4.0f);
                g.setColour (fg);
                g.setFont (F (16.0f, true));
                g.drawText (labels[i], inner.removeFromTop (inner.getHeight() * 0.56f), juce::Justification::bottomLeft);
                g.setFont (F (12.0f));
                g.setColour (on ? pal::well.withAlpha (0.8f) : pal::dim.withAlpha (0.8f));
                g.drawText (subtitles[i], inner, juce::Justification::topLeft);
            }
            else
            {
                g.setColour (fg);
                g.setFont (F (13.0f, true));
                g.drawText (labels[i], cell, juce::Justification::centred);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int n = labels.size();
        const int idx = vertical ? juce::jlimit (0, n - 1, (int) (e.position.y / ((float) getHeight() / (float) n)))
                                 : juce::jlimit (0, n - 1, (int) (e.position.x / ((float) getWidth() / (float) n)));
        if (idx == index)
            return;
        index = idx;
        repaint();
        attachment->setValueAsCompleteGesture ((float) idx);
        if (onUserChange)
            onUserChange (idx);
    }

private:
    juce::StringArray labels, subtitles;
    bool vertical;
    juce::Colour accent;
    int index = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
// The big FX button. Latch mode toggles; momentary mode is engaged only while held.
class FxButton : public juce::Component
{
public:
    explicit FxButton (juce::AudioProcessorValueTreeState& apvts)
        : momentary (apvts.getRawParameterValue (ids::fxMomentary))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (ids::fx),
                                                                  [this] (float v) { on = v > 0.5f; repaint(); });
        attachment->sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void forceOff()
    {
        on = false;
        attachment->setValueAsCompleteGesture (0.0f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        auto caption = area.removeFromBottom (20.0f);
        const auto b = area.reduced (8.0f);
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        const auto c = b.getCentre();
        const float r = d * 0.5f;

        if (on)
        {
            g.setGradientFill (juce::ColourGradient (pal::coral.withAlpha (0.38f), c.x, c.y,
                                                     pal::coral.withAlpha (0.0f), c.x, c.y - r * 1.35f, true));
            g.fillEllipse (c.x - r * 1.35f, c.y - r * 1.35f, r * 2.7f, r * 2.7f);
        }

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff40374c), c.x, c.y - r,
                                                 juce::Colour (0xff1d1823), c.x, c.y + r, false));
        g.fillEllipse (c.x - r, c.y - r, d, d);
        g.setColour (on ? pal::coral : pal::track);
        g.drawEllipse (c.x - r, c.y - r, d, d, 3.0f);

        const float ir = r * 0.34f;
        juce::Path icon;
        icon.addCentredArc (c.x, c.y + 1.0f, ir, ir, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        icon.startNewSubPath (c.x, c.y - ir - 3.0f);
        icon.lineTo (c.x, c.y - 1.0f);
        g.setColour (on ? pal::coral : pal::dim);
        g.strokePath (icon, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (on ? pal::ink : pal::dim);
        g.setFont (F (13.0f, true));
        const bool mom = momentary->load() > 0.5f;
        g.drawText (on ? "Engaged" : (mom ? "Hold to engage" : "Bypassed"), caption, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;
        if (momentary->load() > 0.5f)
        {
            held = true;
            on = true;
            attachment->beginGesture();
            attachment->setValueAsPartOfGesture (1.0f);
        }
        else
        {
            on = ! on;
            attachment->setValueAsCompleteGesture (on ? 1.0f : 0.0f);
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (held)
        {
            held = false;
            on = false;
            attachment->setValueAsPartOfGesture (0.0f);
            attachment->endGesture();
            repaint();
        }
    }

private:
    std::atomic<float>* momentary;
    bool on = false, held = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
class TapButton : public juce::Button
{
public:
    TapButton() : juce::Button ("Tap") {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        const bool en = isEnabled();
        g.setColour (down && en ? pal::teal : pal::well);
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (en ? (over ? pal::teal : pal::panelEdge) : pal::panelEdge.withAlpha (0.5f));
        g.drawRoundedRectangle (b, 10.0f, 1.0f);
        g.setColour (down && en ? pal::well : (en ? pal::ink : pal::dim.withAlpha (0.45f)));
        g.setFont (F (14.0f, true));
        g.drawText ("Tap tempo", getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
class LfoScope : public juce::Component
{
public:
    explicit LfoScope (RevolaAudioProcessor& p) : proc (p)
    {
        depth  = p.apvts.getRawParameterValue (ids::tremDepth);
        shape  = p.apvts.getRawParameterValue (ids::tremShape);
        spread = p.apvts.getRawParameterValue (ids::tremSpread);
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (pal::well);
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (pal::panelEdge);
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

        const auto plot = b.reduced (12.0f, 14.0f);
        const float d = depth->load(), sh = shape->load(), sp = spread->load();
        const auto yFor = [&] (float gain) { return plot.getBottom() - gain * plot.getHeight(); };

        const auto buildPath = [&] (float phaseOffset)
        {
            juce::Path p;
            const int steps = 140;
            for (int i = 0; i <= steps; ++i)
            {
                float ph = (float) i / (float) steps + phaseOffset;
                ph -= std::floor (ph);
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) steps;
                const float y = yFor (revola::tremGain (ph, d, sh));
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };

        auto left = buildPath (0.0f);
        juce::Path fill (left);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (pal::teal.withAlpha (0.30f), 0.0f, plot.getY(),
                                                 pal::teal.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);

        if (sp > 0.02f)
        {
            g.setColour (pal::teal.withAlpha (0.45f));
            g.strokePath (buildPath (0.5f * sp), juce::PathStrokeType (1.4f));
        }
        g.setColour (pal::teal);
        g.strokePath (left, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float ph = proc.lfoPhaseUI.load();
        const float px = plot.getX() + plot.getWidth() * ph;
        const float py = yFor (revola::tremGain (ph, d, sh));
        g.setColour (pal::ink.withAlpha (0.22f));
        g.drawLine (px, plot.getY(), px, plot.getBottom(), 1.0f);
        g.setColour (pal::ink);
        g.fillEllipse (px - 4.5f, py - 4.5f, 9.0f, 9.0f);
        g.setColour (pal::teal);
        g.fillEllipse (px - 2.5f, py - 2.5f, 5.0f, 5.0f);
    }

private:
    RevolaAudioProcessor& proc;
    std::atomic<float>* depth = nullptr;
    std::atomic<float>* shape = nullptr;
    std::atomic<float>* spread = nullptr;
};

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    void setLevels (float l, float r)
    {
        if (l != levelL || r != levelR)
        {
            levelL = l;
            levelR = r;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float h = (b.getHeight() - 4.0f) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const juce::Rectangle<float> bar (b.getX(), b.getY() + (float) ch * (h + 4.0f), b.getWidth(), h);
            g.setColour (pal::well);
            g.fillRoundedRectangle (bar, h * 0.5f);

            const float lvl = ch == 0 ? levelL : levelR;
            const float frac = juce::jmap (juce::Decibels::gainToDecibels (lvl, -60.0f), -60.0f, 0.0f, 0.0f, 1.0f);
            if (frac > 0.001f)
            {
                auto fillRect = bar.withWidth (bar.getWidth() * frac);
                juce::ColourGradient grad (pal::ochre, bar.getX(), 0.0f, pal::coral, bar.getRight(), 0.0f, false);
                grad.addColour (0.78, pal::ochre);
                g.saveState();
                g.reduceClipRegion (fillRect.toNearestInt());
                g.setGradientFill (grad);
                g.fillRoundedRectangle (bar, h * 0.5f);
                g.restoreState();
            }
        }
    }

private:
    float levelL = 0.0f, levelR = 0.0f;
};

//==============================================================================
class MainView : public juce::Component, private juce::Timer
{
public:
    explicit MainView (RevolaAudioProcessor& p)
        : proc (p),
          reverbBg ("Reverb", pal::ochre), tremBg ("Tremolo", pal::teal), engageBg ("Engage", pal::coral),
          level (p.apvts, ids::level, "Level", pal::ochre),
          character (p.apvts, ids::character, "Character", pal::ochre),
          length (p.apvts, ids::length, "Length", pal::ochre),
          air (p.apvts, ids::air, "Air", pal::ochre),
          lowcut (p.apvts, ids::lowcut, "Low cut", pal::ochre),
          noise (p.apvts, ids::noise, "Noise", pal::ochre),
          depth (p.apvts, ids::tremDepth, "Depth", pal::teal),
          rate (p.apvts, ids::tremRate, "Rate", pal::teal),
          division (p.apvts, ids::tremDiv, "Division", pal::teal),
          shape (p.apvts, ids::tremShape, "Shape", pal::teal),
          spread (p.apvts, ids::tremSpread, "Spread", pal::teal),
          outputKnob (p.apvts, ids::output, "Output", pal::coral),
          scope (p),
          syncSwitch (p.apvts, ids::tremSync, { "Free", "Host sync" }, {}, false, pal::teal),
          modeSwitch (p.apvts, ids::mode, { "Mix", "Wet" }, { "Inline: dry plus reverb", "Send/return: reverb only" }, true, pal::coral),
          fxModeSwitch (p.apvts, ids::fxMomentary, { "Latch", "Momentary" }, {}, false, pal::coral),
          fxButton (p.apvts)
    {
        setLookAndFeel (&lnf);

        for (auto* c : std::initializer_list<juce::Component*> { &reverbBg, &tremBg, &engageBg })
            addAndMakeVisible (c);
        for (auto* c : std::initializer_list<juce::Component*> { &level, &character, &length, &air, &lowcut, &noise,
                                                                 &depth, &rate, &division, &shape, &spread, &outputKnob,
                                                                 &scope, &syncSwitch, &tapButton, &modeSwitch,
                                                                 &fxModeSwitch, &fxButton, &meter,
                                                                 &presetBox, &prevButton, &nextButton })
            addAndMakeVisible (c);

        for (int i = 0; i < proc.getNumPrograms(); ++i)
            presetBox.addItem (proc.getProgramName (i), i + 1);
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
        presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };

        prevButton.setButtonText ("<");
        nextButton.setButtonText (">");
        prevButton.onClick = [this] { stepPreset (-1); };
        nextButton.onClick = [this] { stepPreset (+1); };

        tapButton.onClick = [this] { proc.tapTempo(); };
        fxModeSwitch.onUserChange = [this] (int idx)
        {
            if (idx == 1)
                fxButton.forceOff(); // entering momentary mode: start from "off"
            fxButton.repaint();
        };

        syncParam = proc.apvts.getRawParameterValue (ids::tremSync);
        lastSync = syncParam->load() > 0.5f;
        applySyncVisibility (lastSync);

        setSize (kDesignW, kDesignH);
        startTimerHz (30);
    }

    ~MainView() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.setGradientFill (juce::ColourGradient (pal::bgTop, 0.0f, 0.0f, pal::bgBottom, 0.0f, (float) getHeight(), false));
        g.fillAll();

        g.setColour (pal::ink);
        g.setFont (F (32.0f, true));
        g.drawText ("Revola", 28, 10, 220, 38, juce::Justification::centredLeft);
        g.setColour (pal::dim);
        g.setFont (F (13.0f));
        g.drawText ("Reverb and tremolo for afro house", 30, 44, 300, 18, juce::Justification::centredLeft);
    }

    void resized() override
    {
        presetBox.setBounds (478, 18, 220, 34);
        prevButton.setBounds (442, 18, 30, 34);
        nextButton.setBounds (704, 18, 30, 34);

        // Reverb panel: one row of six knobs, Level a little wider
        const juce::Rectangle<int> reverbArea (20, 80, 480, 172);
        reverbBg.setBounds (reverbArea);
        {
            int x = reverbArea.getX() + 12;
            const int y = reverbArea.getY() + 38;
            const int h = 126;
            auto place = [&] (Knob& k, int w) { k.setBounds (x, y, w, h); x += w; };
            place (level, 96);
            place (character, 72);
            place (length, 72);
            place (air, 72);
            place (lowcut, 72);
            place (noise, 72);
        }

        // Tremolo panel
        const juce::Rectangle<int> tremArea (516, 80, 244, 380);
        tremBg.setBounds (tremArea);
        scope.setBounds (tremArea.getX() + 12, tremArea.getY() + 40, tremArea.getWidth() - 24, 88);
        {
            const int cw = 110, ch = 112;
            const int x0 = tremArea.getX() + 12, y0 = tremArea.getY() + 136;
            depth.setBounds (x0, y0, cw, ch);
            rate.setBounds (x0 + cw, y0, cw, ch);
            division.setBounds (x0 + cw, y0, cw, ch);
            shape.setBounds (x0, y0 + ch, cw, ch);
            spread.setBounds (x0 + cw, y0 + ch, cw, ch);
        }
        syncSwitch.setBounds (tremArea.getX() + 14, tremArea.getBottom() - 14 - 34 - 40, tremArea.getWidth() - 28, 34);
        tapButton.setBounds (tremArea.getX() + 14, tremArea.getBottom() - 14 - 34, tremArea.getWidth() - 28, 34);

        // Engage panel
        const juce::Rectangle<int> engageArea (20, 264, 480, 196);
        engageBg.setBounds (engageArea);
        fxButton.setBounds (engageArea.getX() + 14, engageArea.getY() + 40, 150, 118);
        fxModeSwitch.setBounds (engageArea.getX() + 14, engageArea.getBottom() - 14 - 30, 150, 30);
        modeSwitch.setBounds (engageArea.getX() + 178, engageArea.getY() + 46, 168, 112);
        outputKnob.setBounds (engageArea.getX() + 358, engageArea.getY() + 34, 110, 116);
        meter.setBounds (engageArea.getX() + 362, engageArea.getBottom() - 14 - 20, 102, 20);
    }

private:
    void stepPreset (int delta)
    {
        const int n = proc.getNumPrograms();
        const int cur = presetBox.getSelectedId() - 1;
        presetBox.setSelectedId (((cur + delta) % n + n) % n + 1, juce::sendNotificationSync);
    }

    void applySyncVisibility (bool sync)
    {
        rate.setVisible (! sync);
        division.setVisible (sync);
        tapButton.setEnabled (! sync);
        tapButton.repaint();
    }

    void timerCallback() override
    {
        const float l = proc.meterL.exchange (0.0f);
        const float r = proc.meterR.exchange (0.0f);
        dispL = std::max (l, dispL * 0.86f);
        dispR = std::max (r, dispR * 0.86f);
        meter.setLevels (dispL, dispR);
        scope.repaint();

        const bool sync = syncParam->load() > 0.5f;
        if (sync != lastSync)
        {
            lastSync = sync;
            applySyncVisibility (sync);
        }

        const int prog = proc.getCurrentProgram();
        if (presetBox.getSelectedId() != prog + 1)
            presetBox.setSelectedId (prog + 1, juce::dontSendNotification);
    }

    RevolaLookAndFeel lnf; // declared first: outlives every child
    RevolaAudioProcessor& proc;

    PanelBg reverbBg, tremBg, engageBg;
    Knob level, character, length, air, lowcut, noise;
    Knob depth, rate, division, shape, spread;
    Knob outputKnob;
    LfoScope scope;
    Segmented syncSwitch, modeSwitch, fxModeSwitch;
    FxButton fxButton;
    TapButton tapButton;
    LevelMeter meter;
    juce::ComboBox presetBox;
    juce::TextButton prevButton, nextButton;

    std::atomic<float>* syncParam = nullptr;
    bool lastSync = false;
    float dispL = 0.0f, dispR = 0.0f;
};
} // namespace

//==============================================================================
RevolaAudioProcessorEditor::RevolaAudioProcessorEditor (RevolaAudioProcessor& p)
    : juce::AudioProcessorEditor (&p)
{
    view = std::make_unique<MainView> (p);
    addAndMakeVisible (*view);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    setResizeLimits (kDesignW * 3 / 4, kDesignH * 3 / 4, kDesignW * 2, kDesignH * 2);
    setSize (kDesignW, kDesignH);
}

RevolaAudioProcessorEditor::~RevolaAudioProcessorEditor() = default;

void RevolaAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (pal::bgBottom);
}

void RevolaAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) kDesignW;
    view->setTransform (juce::AffineTransform::scale (s));
    view->setBounds (0, 0, kDesignW, kDesignH);
}
