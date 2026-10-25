#pragma once
#include "PluginProcessor.h"

class VocalPianoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalPianoEditor (VocalPianoProcessor&);
    ~VocalPianoEditor() override = default;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    void addKnob (Knob&, const juce::String& id, const juce::String& name, const juce::String& suffix);
    void timerCallback() override { repaint(); }

    VocalPianoProcessor& proc;
    Knob threshold, stability, octave, piano, dry;
    juce::TextButton exportBtn { "Export MIDI" }, clearBtn { "Clear" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> rollArea;
};
