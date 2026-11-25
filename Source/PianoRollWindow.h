#pragma once
#include <JuceHeader.h>
#include "MidiClip.h"

class PianoRollEditor : public juce::Component
{
public:
    PianoRollEditor(MidiClip& clipToEdit,
                    juce::CriticalSection& engineLock,
                    std::function<void(int noteNumber, bool isNoteOn)> liveAuditionCallback)
        : clip(clipToEdit),
          audioLock(engineLock),
          auditionCallback(std::move(liveAuditionCallback))
    {
        setSize(860, 480);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff18181f));

        int totalNotes = highestNote - lowestNote + 1;
        float rowHeight = (float)getHeight() / (float)totalNotes;

        for (int i = 0; i < totalNotes; ++i)
        {
            int midiNote = highestNote - i;
            float y = (float)i * rowHeight;
            bool isBlackKey = juce::MidiMessage::isMidiNoteBlack(midiNote);

            g.setColour(isBlackKey ? juce::Colour(0xff1b1b24) : juce::Colour(0xff22222d));
            g.fillRect((float)keyWidth, y, (float)(getWidth() - keyWidth), rowHeight);
            g.setColour(juce::Colour(0xff2c2c38));
            g.drawHorizontalLine((int)y, (float)keyWidth, (float)getWidth());

            g.setColour(isBlackKey ? juce::Colour(0xff2a2a32) : juce::Colour(0xffe6e6eb));
            g.fillRect(0.0f, y, (float)keyWidth, rowHeight - 0.5f);
            g.setColour(juce::Colours::black);
            g.drawRect(0.0f, y, (float)keyWidth, rowHeight);

            if (midiNote % 12 == 0)
            {
                g.setColour(isBlackKey ? juce::Colours::white : juce::Colours::black);
                g.setFont(10.0f);
                g.drawText(juce::MidiMessage::getMidiNoteName(midiNote, true, true, 4),
                           4, (int)y, keyWidth - 8, (int)rowHeight, juce::Justification::centredRight);
            }
        }

        int totalSteps = (int)(clip.durationBeats * gridDivision);
        for (int step = 0; step <= totalSteps; ++step)
        {
            float x = (float)keyWidth + ((float)step / (float)gridDivision) * (float)pixelsPerBeat;
            if (x > (float)getWidth()) break;

            g.setColour(step % gridDivision == 0 ? juce::Colour(0xff454558) : juce::Colour(0xff2b2b36));
            g.drawVerticalLine((int)x, 0.0f, (float)getHeight());
        }

        const juce::ScopedLock sl(audioLock);
        for (const auto& note : clip.notes)
        {
            if (note.noteNumber < lowestNote || note.noteNumber > highestNote) continue;

            int rowIdx = highestNote - note.noteNumber;
            float x = (float)keyWidth + (float)(note.startBeat * pixelsPerBeat);
            float y = (float)rowIdx * rowHeight + 1.0f;
            float w = juce::jmax(6.0f, (float)(note.lengthBeats * pixelsPerBeat) - 1.0f);
            float h = juce::jmax(4.0f, rowHeight - 2.0f);

            g.setColour(juce::Colour(0xff00b4d8));
            g.fillRoundedRectangle(x, y, w, h, 3.0f);
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(x, y, w, h, 3.0f, 1.0f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        int totalNotes = highestNote - lowestNote + 1;
        float rowHeight = (float)getHeight() / (float)totalNotes;
        int clickedNote = highestNote - (int)((float)e.y / rowHeight);
        clickedNote = juce::jlimit(lowestNote, highestNote, clickedNote);

        if (e.x < keyWidth)
        {
            lastAuditionedNote = clickedNote;
            if (auditionCallback) auditionCallback(clickedNote, true);
            return;
        }

        double rawBeat = (double)(e.x - keyWidth) / pixelsPerBeat;
        double stepSize = 1.0 / (double)gridDivision;
        double snappedBeat = std::floor(rawBeat / stepSize) * stepSize;

        const juce::ScopedLock sl(audioLock);

        for (int i = (int)clip.notes.size() - 1; i >= 0; --i)
        {
            auto& n = clip.notes[(size_t)i];
            if (n.noteNumber == clickedNote &&
                rawBeat >= n.startBeat && rawBeat <= n.startBeat + n.lengthBeats)
            {
                if (e.mods.isPopupMenu())
                {
                    clip.notes.erase(clip.notes.begin() + i);
                    repaint();
                    return;
                }
                draggingNoteIndex = i;
                dragStartBeatOffset = rawBeat - n.startBeat;
                if (auditionCallback) auditionCallback(clickedNote, true);
                lastAuditionedNote = clickedNote;
                return;
            }
        }

        if (!e.mods.isPopupMenu())
        {
            clip.notes.emplace_back(clickedNote, juce::jmax(0.0, snappedBeat), stepSize, 0.85f);
            draggingNoteIndex = (int)clip.notes.size() - 1;
            dragStartBeatOffset = 0.0;
            if (auditionCallback) auditionCallback(clickedNote, true);
            lastAuditionedNote = clickedNote;
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingNoteIndex < 0) return;

        int totalNotes = highestNote - lowestNote + 1;
        float rowHeight = (float)getHeight() / (float)totalNotes;
        int newNote = juce::jlimit(lowestNote, highestNote, highestNote - (int)((float)e.y / rowHeight));

        double rawBeat = ((double)(e.x - keyWidth) / pixelsPerBeat) - dragStartBeatOffset;
        double stepSize = 1.0 / (double)gridDivision;
        double snappedBeat = juce::jmax(0.0, std::round(rawBeat / stepSize) * stepSize);

        const juce::ScopedLock sl(audioLock);
        if (draggingNoteIndex < (int)clip.notes.size())
        {
            auto& note = clip.notes[(size_t)draggingNoteIndex];
            if (note.noteNumber != newNote)
            {
                if (auditionCallback && lastAuditionedNote >= 0)
                    auditionCallback(lastAuditionedNote, false);
                lastAuditionedNote = newNote;
                if (auditionCallback)
                    auditionCallback(newNote, true);
            }
            note.noteNumber = newNote;
            note.startBeat = snappedBeat;
        }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (auditionCallback && lastAuditionedNote >= 0)
        {
            auditionCallback(lastAuditionedNote, false);
            lastAuditionedNote = -1;
        }
        draggingNoteIndex = -1;
    }

private:
    MidiClip& clip;
    juce::CriticalSection& audioLock;
    std::function<void(int, bool)> auditionCallback;

    static constexpr int keyWidth = 54;
    int lowestNote = 36;
    int highestNote = 84;
    double pixelsPerBeat = 95.0;
    int gridDivision = 4;

    int draggingNoteIndex = -1;
    double dragStartBeatOffset = 0.0;
    int lastAuditionedNote = -1;
};

class PianoRollWindow : public juce::DocumentWindow
{
public:
    PianoRollWindow(const juce::String& trackName,
                    MidiClip& clip,
                    juce::CriticalSection& engineLock,
                    std::function<void(int, bool)> liveAuditionCallback)
        : DocumentWindow("Piano Roll MIDI - " + trackName,
                         juce::Colour(0xff1e1e24),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new PianoRollEditor(clip, engineLock, std::move(liveAuditionCallback)), true);
        centreWithSize(860, 480);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }
};
