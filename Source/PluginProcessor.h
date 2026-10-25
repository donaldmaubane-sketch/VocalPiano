#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>
#include "Yin.h"
#include "PianoEngine.h"

struct RecordedNote { int note; double startBeat; double endBeat; int velocity; };

class VocalPianoProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kWindow = 2048;   // analysis window (power of 2)
    static constexpr int kHop    = 256;    // analysis every 256 samples

    VocalPianoProcessor();
    ~VocalPianoProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VocalPiano"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // score recording
    void clearNotes();
    bool exportMidi (const juce::File&);
    std::vector<RecordedNote> copyRecentNotes (size_t maxCount);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int>    currentNote { -1 };
    std::atomic<double> currentBeat { 0.0 };
    std::atomic<double> lastBpm { 120.0 };

private:
    Yin yin;
    PianoEngine engine;
    double sampleRate = 44100.0;

    std::array<float, kWindow> ring {};
    std::vector<float> frame;
    int writePos = 0, hopCounter = 0;

    int activeNote = -1, candidate = -1, candidateCount = 0;
    float activeMidiFloat = 0.f;
    double freeBeats = 0.0;

    juce::SpinLock noteLock;
    std::vector<RecordedNote> notes;
    std::atomic<int> activeRec { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalPianoProcessor)
};
