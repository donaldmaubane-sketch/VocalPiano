#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "threshold", 1 }, "Threshold",
                 NormalisableRange<float> (-70.f, -15.f, 0.1f), -42.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "stability", 1 }, "Stability",
                 NormalisableRange<float> (10.f, 200.f, 1.f), 45.f));
    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { "octave", 1 }, "Octave", -3, 3, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "piano", 1 }, "Piano",
                 NormalisableRange<float> (-24.f, 6.f, 0.1f), 0.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "dry", 1 }, "Dry Vocal",
                 NormalisableRange<float> (0.f, 1.f, 0.01f), 0.f));
    return { p.begin(), p.end() };
}

VocalPianoProcessor::VocalPianoProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    notes.reserve (50000);
    frame.resize (kWindow);
}

bool VocalPianoProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void VocalPianoProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    engine.prepare (sr);
    yin.prepare (sr, kWindow);
    ring.fill (0.f);
    writePos = hopCounter = 0;
    activeNote = candidate = -1;
    candidateCount = 0;
    currentNote = -1;
}

void VocalPianoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    const float thresholdDb = *apvts.getRawParameterValue ("threshold");
    const float stabilityMs = *apvts.getRawParameterValue ("stability");
    const int   octave      = (int) *apvts.getRawParameterValue ("octave");
    const float pianoGain   = juce::Decibels::decibelsToGain ((float) *apvts.getRawParameterValue ("piano"));
    const float dryGain     = *apvts.getRawParameterValue ("dry");
    const int   needFrames  = juce::jmax (1, (int) std::round (stabilityMs * 0.001 * sampleRate / kHop));

    // host transport (used to place recorded notes on the project's beat grid)
    double bpm = 120.0, hostPpq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) { hostPpq = *q; playing = pos->getIsPlaying(); }
        }
    lastBpm = bpm;
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    const double blockStartBeat = playing ? hostPpq : freeBeats;
    if (! playing) freeBeats += numSamples * beatsPerSample;
    currentBeat = blockStartBeat + numSamples * beatsPerSample;
    // detection lag, subtracted so exported notes line up with what you sang
    const double latencyBeats = (kWindow / 2 + (needFrames - 1) * kHop) * beatsPerSample;

    auto recordOn = [&] (int note, int vel, double beat)
    {
        juce::SpinLock::ScopedTryLockType l (noteLock);
        if (l.isLocked() && notes.size() < notes.capacity())
        {
            notes.push_back ({ note, std::max (0.0, beat), -1.0, vel });
            activeRec = (int) notes.size() - 1;
        }
        else activeRec = -1;
    };
    auto recordOff = [&] (double beat)
    {
        juce::SpinLock::ScopedTryLockType l (noteLock);
        const int idx = activeRec;
        if (l.isLocked() && idx >= 0 && idx < (int) notes.size())
            notes[(size_t) idx].endBeat = std::max (0.0, beat);
        activeRec = -1;
    };

    auto analyse = [&] (int sampleIdx)
    {
        for (int k = 0; k < kWindow; ++k)
            frame[(size_t) k] = ring[(size_t) ((writePos + k) & (kWindow - 1))];

        double sq = 0.0;
        for (int k = kWindow - 1024; k < kWindow; ++k) sq += (double) frame[(size_t) k] * frame[(size_t) k];
        const float db = juce::Decibels::gainToDecibels ((float) std::sqrt (sq / 1024.0), -120.f);

        int detected = -1;
        float midiFloat = 0.f;
        if (db > thresholdDb)
        {
            const auto r = yin.detect (frame.data());
            if (r.freq > 0.f && r.prob > 0.85f)
            {
                midiFloat = 69.f + 12.f * std::log2 (r.freq / 440.f) + (float) (octave * 12);
                detected  = (int) std::round (midiFloat);
                // vibrato hysteresis: stay on the current note unless clearly moved away
                if (activeNote >= 0 && std::fabs (midiFloat - (float) activeNote) < 0.7f)
                    detected = activeNote;
                if (detected < 21 || detected > 108) detected = -1;
            }
        }

        if (detected == candidate) ++candidateCount; else { candidate = detected; candidateCount = 1; }

        if (candidateCount >= needFrames && candidate != activeNote)
        {
            const double beat = blockStartBeat + sampleIdx * beatsPerSample - latencyBeats;
            if (activeNote >= 0)
            {
                engine.noteOff (activeNote);
                midi.addEvent (juce::MidiMessage::noteOff (1, activeNote), sampleIdx);
                recordOff (beat);
            }
            if (candidate >= 0)
            {
                const int vel = juce::jlimit (30, 127,
                    (int) juce::jmap (db, thresholdDb, thresholdDb + 35.f, 45.f, 120.f));
                engine.noteOn (candidate, vel);
                midi.addEvent (juce::MidiMessage::noteOn (1, candidate, (juce::uint8) vel), sampleIdx);
                recordOn (candidate, vel, beat);
            }
            activeNote = candidate;
            currentNote = activeNote;
        }
    };

    float* data[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = data[0][i];
        if (data[1] != nullptr) mono = 0.5f * (mono + data[1][i]);

        ring[(size_t) writePos] = mono;
        writePos = (writePos + 1) & (kWindow - 1);

        if (++hopCounter >= kHop) { hopCounter = 0; analyse (i); }

        const float piano = engine.render() * pianoGain;
        for (int c = 0; c < numCh; ++c)
            data[c][i] = data[c][i] * dryGain + piano;
    }
}

void VocalPianoProcessor::clearNotes()
{
    juce::SpinLock::ScopedLockType l (noteLock);
    notes.clear();
    activeRec = -1;
}

std::vector<RecordedNote> VocalPianoProcessor::copyRecentNotes (size_t maxCount)
{
    juce::SpinLock::ScopedLockType l (noteLock);
    const size_t n = std::min (maxCount, notes.size());
    return { notes.end() - (std::ptrdiff_t) n, notes.end() };
}

bool VocalPianoProcessor::exportMidi (const juce::File& file)
{
    std::vector<RecordedNote> copy;
    { juce::SpinLock::ScopedLockType l (noteLock); copy = notes; }
    if (copy.empty()) return false;

    constexpr int ppq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / lastBpm.load())), 0.0);

    for (auto& n : copy)
    {
        const double s = n.startBeat * ppq;
        const double e = (n.endBeat < 0.0 ? n.startBeat + 0.25 : std::max (n.endBeat, n.startBeat + 0.0625)) * ppq;
        seq.addEvent (juce::MidiMessage::noteOn  (1, n.note, (juce::uint8) n.velocity), s);
        seq.addEvent (juce::MidiMessage::noteOff (1, n.note), e);
    }
    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (seq);

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
    return os != nullptr && mf.writeTo (*os);
}

void VocalPianoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void VocalPianoProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VocalPianoProcessor::createEditor() { return new VocalPianoEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VocalPianoProcessor(); }
