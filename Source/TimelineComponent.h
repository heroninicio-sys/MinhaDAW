#pragma once
#include <JuceHeader.h>
#include "AudioTrack.h"

class TrackRowComponent : public juce::Component,
                          private juce::ChangeListener
{
public:
    enum class DragMode
    {
        None,
        MoveAudioClip,
        MoveMidiClip,
        TrimLeft,
        TrimRight,
        FadeIn,
        FadeOut
    };

    TrackRowComponent(AudioTrack& trackRef,
                      juce::AudioFormatManager& formatMgr,
                      juce::AudioThumbnailCache& cache,
                      juce::CriticalSection& engineLock,
                      std::function<void(AudioTrack&)> onLoadPluginCallback,
                      std::function<void(AudioTrack&, TrackRowComponent&)> onImportAudioCallback,
                      std::function<void(AudioTrack&, MidiClip&)> onOpenPianoRollCallback,
                      std::function<void(double)> onSeekCallback)
        : track(trackRef),
          formatManager(formatMgr),
          thumbnailCache(cache),
          audioLock(engineLock),
          loadPluginCallback(std::move(onLoadPluginCallback)),
          importAudioCallback(std::move(onImportAudioCallback)),
          openPianoRollCallback(std::move(onOpenPianoRollCallback)),
          seekCallback(std::move(onSeekCallback))
    {
        addAndMakeVisible(nameLabel);
        nameLabel.setText(track.getName(), juce::dontSendNotification);
        nameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        nameLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));

        addAndMakeVisible(armBtn);
        armBtn.setButtonText("R");
        armBtn.setClickingTogglesState(true);
        armBtn.setToggleState(track.isArmed, juce::dontSendNotification);
        armBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
        armBtn.onClick = [this] { track.isArmed = armBtn.getToggleState(); };

        addAndMakeVisible(muteBtn);
        muteBtn.setButtonText("M");
        muteBtn.setClickingTogglesState(true);
        muteBtn.setToggleState(track.isMuted, juce::dontSendNotification);
        muteBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
        muteBtn.onClick = [this] { track.isMuted = muteBtn.getToggleState(); };

        addAndMakeVisible(soloBtn);
        soloBtn.setButtonText("S");
        soloBtn.setClickingTogglesState(true);
        soloBtn.setToggleState(track.isSoloed, juce::dontSendNotification);
        soloBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
        soloBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        soloBtn.onClick = [this] { track.isSoloed = soloBtn.getToggleState(); };

        addAndMakeVisible(volumeSlider);
        volumeSlider.setRange(0.0, 1.5, 0.01);
        volumeSlider.setValue(track.gain, juce::dontSendNotification);
        volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        volumeSlider.onValueChange = [this] { track.gain = (float)volumeSlider.getValue(); };

        addAndMakeVisible(importBtn);
        importBtn.setButtonText("WAV");
        importBtn.onClick = [this] { if (importAudioCallback) importAudioCallback(track, *this); };

        addAndMakeVisible(midiBtn);
        midiBtn.setButtonText("MIDI");
        midiBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a5c6b));
        midiBtn.onClick = [this]
        {
            MidiClip* mClip = nullptr;
            {
                const juce::ScopedLock sl(audioLock);
                mClip = track.createOrGetMidiClipAt(applySnap(currentPlayheadSec) * (bpm / 60.0), 8.0);
            }
            repaint();
            if (mClip != nullptr && openPianoRollCallback)
                openPianoRollCallback(track, *mClip);
        };

        addAndMakeVisible(fxBtn);
        fxBtn.setButtonText("RACK");
        fxBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a3f6b));
        fxBtn.onClick = [this] { if (loadPluginCallback) loadPluginCallback(track); };
    }

    void rebuildThumbnails()
    {
        thumbnails.clear();
        for (auto* clip : track.getClips())
        {
            auto thumb = std::make_unique<juce::AudioThumbnail>(512, formatManager, thumbnailCache);
            thumb->addChangeListener(this);
            thumb->setSource(new juce::FileInputSource(clip->audioFile));
            thumbnails[clip->id] = std::move(thumb);
        }
        repaint();
    }

    void setPlayheadPosition(double seconds) { currentPlayheadSec = seconds; repaint(); }
    void setPixelsPerSecond(double pps)      { pixelsPerSecond = pps; repaint(); }
    void setGridSettings(double newBpm, bool snap, int division)
    {
        bpm = newBpm;
        snapEnabled = snap;
        gridDivision = division;
        repaint();
    }

    double applySnap(double rawSeconds) const
    {
        if (!snapEnabled || bpm <= 0.0)
            return juce::jmax(0.0, rawSeconds);

        double beatDurationSec = 60.0 / bpm;
        double gridStepSec = beatDurationSec / (double)gridDivision;
        return juce::jmax(0.0, std::round(rawSeconds / gridStepSec) * gridStepSec);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth) return;

        for (int i = track.getMidiClips().size() - 1; i >= 0; --i)
        {
            auto* mc = track.getMidiClips()[i];
            if (getMidiClipBounds(*mc).contains(e.getPosition()))
            {
                if (openPianoRollCallback)
                    openPianoRollCallback(track, *mc);
                return;
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth) return;

        for (int i = track.getMidiClips().size() - 1; i >= 0; --i)
        {
            auto* mc = track.getMidiClips()[i];
            auto rect = getMidiClipBounds(*mc);
            if (rect.contains(e.getPosition()))
            {
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu menu;
                    menu.addItem(1, "Abrir Piano Roll");
                    menu.addItem(2, "Excluir Clipe MIDI");
                    menu.showMenuAsync(juce::PopupMenu::Options(), [this, mc](int res)
                    {
                        if (res == 1 && openPianoRollCallback) openPianoRollCallback(track, *mc);
                        else if (res == 2) { const juce::ScopedLock sl(audioLock); track.removeMidiClip(mc); repaint(); }
                    });
                    return;
                }
                activeMidiClip = mc;
                currentDragMode = DragMode::MoveMidiClip;
                dragStartX = e.x;
                initialMidiStartBeat = mc->timelineStartBeats;
                return;
            }
        }

        AudioClip* hitClip = nullptr;
        DragMode mode = detectAudioHitZone(e.getPosition(), hitClip);

        if (e.mods.isPopupMenu() && hitClip != nullptr)
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Cortar Clipe na Agulha (Split)");
            menu.addItem(2, "Remover Fade In / Fade Out");
            menu.addSeparator();
            menu.addItem(3, "Excluir Clipe");

            menu.showMenuAsync(juce::PopupMenu::Options(), [this, hitClip](int result)
            {
                if (result == 1)
                {
                    { const juce::ScopedLock sl(audioLock); track.splitClipsAt(currentPlayheadSec); }
                    rebuildThumbnails();
                }
                else if (result == 2)
                {
                    const juce::ScopedLock sl(audioLock);
                    hitClip->fadeInDuration = 0.0;
                    hitClip->fadeOutDuration = 0.0;
                    repaint();
                }
                else if (result == 3)
                {
                    { const juce::ScopedLock sl(audioLock); track.removeClip(hitClip); }
                    rebuildThumbnails();
                }
            });
            return;
        }

        if (hitClip != nullptr && mode != DragMode::None)
        {
            activeAudioClip = hitClip;
            currentDragMode = mode;
            dragStartX = e.x;
            initialTimelineStart = hitClip->timelineStart;
            initialSourceOffset  = hitClip->sourceStartOffset;
            initialDuration      = hitClip->duration;
            initialFadeIn        = hitClip->fadeInDuration;
            initialFadeOut       = hitClip->fadeOutDuration;
        }
        else
        {
            double clickedTime = applySnap((double)(e.x - headerWidth) / pixelsPerSecond);
            if (seekCallback)
                seekCallback(clickedTime);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (currentDragMode == DragMode::None) return;

        double deltaSeconds = (double)(e.x - dragStartX) / pixelsPerSecond;
        const juce::ScopedLock sl(audioLock);

        if (currentDragMode == DragMode::MoveMidiClip && activeMidiClip != nullptr)
        {
            double secPerBeat = 60.0 / bpm;
            double newStartSec = applySnap((initialMidiStartBeat * secPerBeat) + deltaSeconds);
            activeMidiClip->timelineStartBeats = newStartSec / secPerBeat;
            repaint();
            return;
        }

        if (activeAudioClip == nullptr) return;

        if (currentDragMode == DragMode::MoveAudioClip)
        {
            activeAudioClip->timelineStart = applySnap(initialTimelineStart + deltaSeconds);
        }
        else if (currentDragMode == DragMode::FadeIn)
        {
            double maxFade = activeAudioClip->duration - activeAudioClip->fadeOutDuration;
            activeAudioClip->fadeInDuration = juce::jlimit(0.0, juce::jmax(0.0, maxFade), initialFadeIn + deltaSeconds);
        }
        else if (currentDragMode == DragMode::FadeOut)
        {
            double maxFade = activeAudioClip->duration - activeAudioClip->fadeInDuration;
            activeAudioClip->fadeOutDuration = juce::jlimit(0.0, juce::jmax(0.0, maxFade), initialFadeOut - deltaSeconds);
        }
        else if (currentDragMode == DragMode::TrimRight)
        {
            double maxAvailable = activeAudioClip->totalFileLength - activeAudioClip->sourceStartOffset;
            double targetEnd = applySnap(initialTimelineStart + initialDuration + deltaSeconds);
            activeAudioClip->duration = juce::jlimit(0.05, maxAvailable, targetEnd - initialTimelineStart);
            activeAudioClip->fadeOutDuration = juce::jmin(activeAudioClip->fadeOutDuration, activeAudioClip->duration);
        }
        else if (currentDragMode == DragMode::TrimLeft)
        {
            double targetStart = applySnap(initialTimelineStart + deltaSeconds);
            double clampedDelta = juce::jlimit(-initialSourceOffset, initialDuration - 0.05, targetStart - initialTimelineStart);

            activeAudioClip->timelineStart     = juce::jmax(0.0, initialTimelineStart + clampedDelta);
            activeAudioClip->sourceStartOffset = initialSourceOffset + clampedDelta;
            activeAudioClip->duration          = initialDuration - clampedDelta;
            activeAudioClip->fadeInDuration    = juce::jmin(activeAudioClip->fadeInDuration, activeAudioClip->duration);
        }

        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        activeAudioClip = nullptr;
        activeMidiClip = nullptr;
        currentDragMode = DragMode::None;
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        
        auto controlArea = bounds.removeFromLeft(headerWidth);
        g.setColour(juce::Colour(0xff25252e));
        g.fillRect(controlArea);
        g.setColour(juce::Colour(0xff383846));
        g.drawRect(controlArea, 1);

        g.setColour(juce::Colour(0xff18181f));
        g.fillRect(bounds);

        double beatSec = 60.0 / juce::jmax(20.0, bpm);
        double stepSec = beatSec / (double)gridDivision;
        int totalSteps = (int)((double)(getWidth() - headerWidth) / (stepSec * pixelsPerSecond)) + 2;

        for (int step = 0; step < totalSteps; ++step)
        {
            int gridX = headerWidth + (int)std::round(((double)step * stepSec) * pixelsPerSecond);
            if (gridX > getWidth()) break;

            int stepsPerBar = 4 * gridDivision;
            if (step % stepsPerBar == 0)       g.setColour(juce::Colour(0xff3c3c4c));
            else if (step % gridDivision == 0) g.setColour(juce::Colour(0xff2a2a36));
            else                               g.setColour(juce::Colour(0xff202029));

            g.drawVerticalLine(gridX, (float)bounds.getY(), (float)bounds.getBottom());
        }

        g.setColour(juce::Colour(0xff2d2d38));
        g.drawRect(bounds, 1);

        for (auto* clip : track.getClips())
        {
            auto clipRect = getAudioClipBounds(*clip);
            if (!clipRect.intersects(bounds)) continue;

            g.setColour(juce::Colour(0xff26466d));
            g.fillRoundedRectangle(clipRect.toFloat(), 4.0f);

            if (auto it = thumbnails.find(clip->id); it != thumbnails.end())
            {
                g.setColour(juce::Colour(0xff7ec8e3));
                it->second->drawChannels(g, clipRect.reduced(1, 8),
                                         clip->sourceStartOffset,
                                         clip->sourceStartOffset + clip->duration,
                                         1.0f);
            }

            if (clip->fadeInDuration > 0.005)
            {
                float fadeInPx = (float)(clip->fadeInDuration * pixelsPerSecond);
                juce::Path fadePath;
                fadePath.startNewSubPath((float)clipRect.getX(), (float)clipRect.getY());
                fadePath.lineTo((float)clipRect.getX() + fadeInPx, (float)clipRect.getY());
                fadePath.lineTo((float)clipRect.getX(), (float)clipRect.getBottom());
                fadePath.closeSubPath();
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillPath(fadePath);
            }

            if (clip->fadeOutDuration > 0.005)
            {
                float fadeOutPx = (float)(clip->fadeOutDuration * pixelsPerSecond);
                juce::Path fadePath;
                fadePath.startNewSubPath((float)clipRect.getRight(), (float)clipRect.getY());
                fadePath.lineTo((float)clipRect.getRight() - fadeOutPx, (float)clipRect.getY());
                fadePath.lineTo((float)clipRect.getRight(), (float)clipRect.getBottom());
                fadePath.closeSubPath();
                g.setColour(juce::Colours::black.withAlpha(0.45f));
                g.fillPath(fadePath);
            }

            g.setColour(juce::Colour(0xff5a9bd5));
            g.drawRoundedRectangle(clipRect.toFloat(), 4.0f, 1.2f);
            g.setColour(juce::Colours::white);
            g.setFont(11.0f);
            g.drawText(clip->audioFile.getFileNameWithoutExtension(),
                       clipRect.reduced(6, 2).removeFromTop(14), juce::Justification::centredLeft, true);
        }

        for (auto* mc : track.getMidiClips())
        {
            auto mcRect = getMidiClipBounds(*mc);
            if (!mcRect.intersects(bounds)) continue;

            g.setColour(juce::Colour(0xff1e565c));
            g.fillRoundedRectangle(mcRect.toFloat(), 4.0f);
            g.setColour(juce::Colour(0xff38b2ac));
            g.drawRoundedRectangle(mcRect.toFloat(), 4.0f, 1.3f);

            for (const auto& note : mc->notes)
            {
                float nx = (float)mcRect.getX() + (float)((note.startBeat / mc->durationBeats) * (double)mcRect.getWidth());
                float nw = juce::jmax(3.0f, (float)((note.lengthBeats / mc->durationBeats) * (double)mcRect.getWidth()));
                float normPitch = juce::jlimit(0.0f, 1.0f, (float)(84 - note.noteNumber) / 48.0f);
                float ny = (float)mcRect.getY() + 14.0f + normPitch * ((float)mcRect.getHeight() - 18.0f);

                g.setColour(juce::Colours::aquamarine);
                g.fillRect(nx, ny, nw, 2.5f);
            }

            g.setColour(juce::Colours::white);
            g.setFont(11.0f);
            g.drawText("MIDI (" + juce::String((int)mc->notes.size()) + " notas)",
                       mcRect.reduced(6, 2).removeFromTop(14), juce::Justification::centredLeft, true);
        }

        float playheadX = (float)headerWidth + (float)(currentPlayheadSec * pixelsPerSecond);
        if (playheadX >= (float)headerWidth && playheadX <= (float)getWidth())
        {
            g.setColour(juce::Colours::cyan);
            g.drawLine(playheadX, (float)bounds.getY(), playheadX, (float)bounds.getBottom(), 1.5f);
        }
    }

    void resized() override
    {
        auto controlArea = getLocalBounds().removeFromLeft(headerWidth).reduced(6);
        auto topRow = controlArea.removeFromTop(24);
        nameLabel.setBounds(topRow.removeFromLeft(76));
        fxBtn.setBounds(topRow.removeFromRight(58).reduced(1));
        midiBtn.setBounds(topRow.removeFromRight(46).reduced(1));
        importBtn.setBounds(topRow.removeFromRight(44).reduced(1));

        controlArea.removeFromTop(6);
        auto bottomRow = controlArea.removeFromTop(24);
        armBtn.setBounds(bottomRow.removeFromLeft(28).reduced(1));
        muteBtn.setBounds(bottomRow.removeFromLeft(28).reduced(1));
        soloBtn.setBounds(bottomRow.removeFromLeft(28).reduced(1));
        volumeSlider.setBounds(bottomRow.reduced(4, 0));
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }

    juce::Rectangle<int> getAudioClipBounds(const AudioClip& clip) const
    {
        int x = headerWidth + (int)std::round(clip.timelineStart * pixelsPerSecond);
        int w = juce::jmax(8, (int)std::round(clip.duration * pixelsPerSecond));
        return { x, 4, w, getHeight() - 8 };
    }

    juce::Rectangle<int> getMidiClipBounds(const MidiClip& mc) const
    {
        double secPerBeat = 60.0 / juce::jmax(20.0, bpm);
        int x = headerWidth + (int)std::round((mc.timelineStartBeats * secPerBeat) * pixelsPerSecond);
        int w = juce::jmax(16, (int)std::round((mc.durationBeats * secPerBeat) * pixelsPerSecond));
        return { x, 4, w, getHeight() - 8 };
    }

    DragMode detectAudioHitZone(juce::Point<int> pos, AudioClip*& outClip)
    {
        for (int i = track.getClips().size() - 1; i >= 0; --i)
        {
            auto* clip = track.getClips()[i];
            auto rect = getAudioClipBounds(*clip);

            if (rect.expanded(4, 0).contains(pos))
            {
                outClip = clip;
                int handleInX  = rect.getX() + (int)(clip->fadeInDuration * pixelsPerSecond);
                int handleOutX = rect.getRight() - (int)(clip->fadeOutDuration * pixelsPerSecond);

                if (pos.y <= rect.getY() + 14 && std::abs(pos.x - handleInX) <= 8)  return DragMode::FadeIn;
                if (pos.y <= rect.getY() + 14 && std::abs(pos.x - handleOutX) <= 8) return DragMode::FadeOut;
                if (std::abs(pos.x - rect.getX()) <= 6)     return DragMode::TrimLeft;
                if (std::abs(pos.x - rect.getRight()) <= 6) return DragMode::TrimRight;

                return DragMode::MoveAudioClip;
            }
        }
        return DragMode::None;
    }

    AudioTrack& track;
    juce::AudioFormatManager& formatManager;
    juce::AudioThumbnailCache& thumbnailCache;
    juce::CriticalSection& audioLock;

    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;

    static constexpr int headerWidth = 260;
    double pixelsPerSecond = 50.0;
    double currentPlayheadSec = 0.0;
    double bpm = 120.0;
    bool snapEnabled = true;
    int gridDivision = 1;

    AudioClip* activeAudioClip = nullptr;
    MidiClip* activeMidiClip = nullptr;
    DragMode currentDragMode = DragMode::None;
    int dragStartX = 0;
    double initialTimelineStart = 0.0;
    double initialSourceOffset = 0.0;
    double initialDuration = 0.0;
    double initialFadeIn = 0.0;
    double initialFadeOut = 0.0;
    double initialMidiStartBeat = 0.0;

    juce::Label nameLabel;
    juce::TextButton armBtn, muteBtn, soloBtn, importBtn, midiBtn, fxBtn;
    juce::Slider volumeSlider;

    std::function<void(AudioTrack&)> loadPluginCallback;
    std::function<void(AudioTrack&, TrackRowComponent&)> importAudioCallback;
    std::function<void(AudioTrack&, MidiClip&)> openPianoRollCallback;
    std::function<void(double)> seekCallback;
};