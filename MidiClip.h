#pragma once
#include <JuceHeader.h>
#include <vector>

struct MidiNoteEvent
{
    juce::String id;
    int noteNumber = 60;
    double startBeat = 0.0;
    double lengthBeats = 1.0;
    float velocity = 0.85f;

    MidiNoteEvent(int note = 60, double start = 0.0, double len = 1.0, float vel = 0.85f)
        : id(juce::Uuid().toString()), noteNumber(note), startBeat(start), lengthBeats(len), velocity(vel) {}
};

struct MidiClip
{
    juce::String id;
    juce::String name = "Padrao MIDI";
    double timelineStartBeats = 0.0;
    double durationBeats = 4.0;
    std::vector<MidiNoteEvent> notes;

    MidiClip(double startBeat = 0.0, double lengthInBeats = 4.0)
        : id(juce::Uuid().toString()), timelineStartBeats(startBeat), durationBeats(lengthInBeats) {}

    double getEndBeats() const { return timelineStartBeats + durationBeats; }

    void renderMidiBlock(juce::MidiBuffer& targetMidiBuffer,
                         double blockStartSec,
                         int numSamples,
                         double sampleRate,
                         double bpm,
                         int midiChannel = 1) const
    {
        if (bpm <= 0.0 || sampleRate <= 0.0 || numSamples <= 0 || durationBeats <= 0.0)
            return;
        if (midiChannel < 1 || midiChannel > 16)
            midiChannel = 1;

        double beatsPerSec = bpm / 60.0;
        double blockStartBeat = blockStartSec * beatsPerSec;
        double blockDurationBeats = ((double)numSamples / sampleRate) * beatsPerSec;
        double blockEndBeat = blockStartBeat + blockDurationBeats;

        if (blockEndBeat <= timelineStartBeats || blockStartBeat >= getEndBeats())
            return;

        for (const auto& note : notes)
        {
            if (note.lengthBeats <= 0.0 || note.noteNumber < 0 || note.noteNumber > 127)
                continue;
            const double noteStart = juce::jmax(0.0, note.startBeat);
            double absNoteStart = timelineStartBeats + noteStart;
            double absNoteEnd   = absNoteStart + note.lengthBeats;

            if (absNoteStart >= blockStartBeat && absNoteStart < blockEndBeat)
            {
                int sampleOffset = (int)std::round(((absNoteStart - blockStartBeat) / beatsPerSec) * sampleRate);
                sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
                targetMidiBuffer.addEvent(juce::MidiMessage::noteOn(midiChannel, note.noteNumber,
                    juce::jlimit(0.0f, 1.0f, note.velocity)), sampleOffset);
            }

            if (absNoteEnd >= blockStartBeat && absNoteEnd < blockEndBeat)
            {
                int sampleOffset = (int)std::round(((absNoteEnd - blockStartBeat) / beatsPerSec) * sampleRate);
                sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
                targetMidiBuffer.addEvent(juce::MidiMessage::noteOff(midiChannel, note.noteNumber), sampleOffset);
            }
        }
    }
};
