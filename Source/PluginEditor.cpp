#include "PluginEditor.h"

VocalPianoEditor::VocalPianoEditor (VocalPianoProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    addKnob (threshold, "threshold", "Threshold", " dB");
    addKnob (stability, "stability", "Stability", " ms");
    addKnob (octave,    "octave",    "Octave",    "");
    addKnob (piano,     "piano",     "Piano",     " dB");
    addKnob (dry,       "dry",       "Dry Vocal", "");

    addAndMakeVisible (exportBtn);
    addAndMakeVisible (clearBtn);

    exportBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save vocal score as MIDI",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("VocalPiano.mid"), "*.mid");
        juce::Component::SafePointer<VocalPianoEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto f = fc.getResult();
                if (f != juce::File {}) safe->proc.exportMidi (f.withFileExtension ("mid"));
            });
    };
    clearBtn.onClick = [this] { proc.clearNotes(); };

    setSize (580, 380);
    startTimerHz (30);
}

void VocalPianoEditor::addKnob (Knob& k, const juce::String& id, const juce::String& name, const juce::String& suffix)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.slider.setTextValueSuffix (suffix);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

void VocalPianoEditor::resized()
{
    auto area = getLocalBounds().reduced (15);
    area.removeFromTop (45);
    auto row = area.removeFromTop (130);
    const int cellW = row.getWidth() / 5;
    for (auto* k : { &threshold, &stability, &octave, &piano, &dry })
    {
        auto cell = row.removeFromLeft (cellW);
        k->label.setBounds (cell.removeFromTop (18));
        k->slider.setBounds (cell);
    }
    area.removeFromTop (8);
    auto buttons = area.removeFromTop (28);
    exportBtn.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    clearBtn.setBounds (buttons.removeFromLeft (80));
    area.removeFromTop (8);
    rollArea = area;
}

void VocalPianoEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.f, juce::Font::bold));
    g.drawText ("VocalPiano", 15, 10, 200, 30, juce::Justification::centredLeft);

    const int cur = proc.currentNote.load();
    g.setFont (juce::FontOptions (20.f));
    g.setColour (cur >= 0 ? juce::Colour (0xff6ee7b7) : juce::Colours::grey);
    g.drawText (cur >= 0 ? juce::MidiMessage::getMidiNoteName (cur, true, true, 3) : juce::String ("--"),
                getWidth() - 115, 10, 100, 30, juce::Justification::centredRight);

    // mini piano roll: last 8 beats
    g.setColour (juce::Colour (0xff1f222a));
    g.fillRoundedRectangle (rollArea.toFloat(), 6.f);

    const double nowBeat = proc.currentBeat.load();
    const double span = 8.0;
    auto recent = proc.copyRecentNotes (300);

    int lo = 48, hi = 84;
    for (auto& n : recent)
        if (n.startBeat > nowBeat - span - 1) { lo = std::min (lo, n.note - 2); hi = std::max (hi, n.note + 2); }
    const float rowH = (float) rollArea.getHeight() / (float) (hi - lo + 1);

    g.saveState();
    g.reduceClipRegion (rollArea);
    g.setColour (juce::Colour (0xff6ee7b7));
    for (auto& n : recent)
    {
        const double end = n.endBeat < 0.0 ? nowBeat : n.endBeat;
        if (end < nowBeat - span) continue;
        const float x1 = rollArea.getX() + (float) ((n.startBeat - (nowBeat - span)) / span) * rollArea.getWidth();
        const float x2 = rollArea.getX() + (float) ((end         - (nowBeat - span)) / span) * rollArea.getWidth();
        const float y  = rollArea.getBottom() - (float) (n.note - lo + 1) * rowH;
        g.fillRoundedRectangle (x1, y, std::max (3.f, x2 - x1), std::max (3.f, rowH - 1.f), 2.f);
    }
    g.restoreState();
}
