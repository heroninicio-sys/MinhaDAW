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
                      int trackIndex,
                      juce::AudioFormatManager& formatMgr,
                      juce::AudioThumbnailCache& cache,
                      juce::CriticalSection& engineLock,
                      std::function<void(AudioTrack&)> onLoadPluginCallback,
                      std::function<void(AudioTrack&, TrackRowComponent&)> onImportAudioCallback,
                      std::function<void(AudioTrack&, MidiClip&)> onOpenPianoRollCallback,
                      std::function<void(double)> onSeekCallback,
                      std::function<void(TrackRowComponent*, AudioClip*, MidiClip*)> onSelectCallback)
        : track(trackRef),
          index(trackIndex),
          formatManager(formatMgr),
          thumbnailCache(cache),
          audioLock(engineLock),
          loadPluginCallback(std::move(onLoadPluginCallback)),
          importAudioCallback(std::move(onImportAudioCallback)),
          openPianoRollCallback(std::move(onOpenPianoRollCallback)),
          seekCallback(std::move(onSeekCallback)),
          selectCallback(std::move(onSelectCallback))
    {
        const juce::Colour palette[] = {
            juce::Colour(0xff2ec4b6), juce::Colour(0xff3a86ff), juce::Colour(0xff38b000),
            juce::Colour(0xff8338ec), juce::Colour(0xffe63946), juce::Colour(0xfffb5607),
            juce::Colour(0xffffbe0b), juce::Colour(0xff00b4d8)
        };
        trackAccentColour = palette[index % 8];

        addAndMakeVisible(armBtn);
        armBtn.setButtonText("R");
        armBtn.setTooltip("Armar Gravacao");
        armBtn.setClickingTogglesState(true);
        armBtn.setToggleState(track.isArmed, juce::dontSendNotification);
        armBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d34));
        armBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe63946));
        armBtn.onClick = [this] { track.isArmed = armBtn.getToggleState(); notifySelected(nullptr, nullptr); };

        addAndMakeVisible(monBtn);
        monBtn.setButtonText("MON");
        monBtn.setTooltip("Som de Retorno (Monitorar Entrada ao Vivo)");
        monBtn.setClickingTogglesState(true);
        monBtn.setToggleState(track.isMonitoring, juce::dontSendNotification);
        monBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d34));
        monBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff00b4d8));
        monBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        monBtn.onClick = [this] { track.isMonitoring = monBtn.getToggleState(); notifySelected(nullptr, nullptr); };

        addAndMakeVisible(nameLabel);
        nameLabel.setText(track.getName(), juce::dontSendNotification);
        nameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        nameLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1e1e24));
        nameLabel.setFont(juce::FontOptions(12.5f, juce::Font::bold));

        addAndMakeVisible(muteBtn);
        muteBtn.setButtonText("M");
        muteBtn.setTooltip("Mute [M]");
        muteBtn.setClickingTogglesState(true);
        muteBtn.setToggleState(track.isMuted, juce::dontSendNotification);
        muteBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d34));
        muteBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe07a5f));
        muteBtn.onClick = [this]
        {
            track.isMuted = muteBtn.getToggleState();
            notifySelected(nullptr, nullptr);
            if (onTrackStateChanged) onTrackStateChanged();
        };

        addAndMakeVisible(soloBtn);
        soloBtn.setButtonText("S");
        soloBtn.setTooltip("Solo");
        soloBtn.setClickingTogglesState(true);
        soloBtn.setToggleState(track.isSoloed, juce::dontSendNotification);
        soloBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d34));
        soloBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xfff2cc8f));
        soloBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        soloBtn.onClick = [this]
        {
            track.isSoloed = soloBtn.getToggleState();
            notifySelected(nullptr, nullptr);
            if (onTrackStateChanged) onTrackStateChanged();
        };

        addAndMakeVisible(volumeSlider);
        volumeSlider.setRange(0.0, 1.5, 0.01);
        volumeSlider.setValue(track.gain, juce::dontSendNotification);
        volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        volumeSlider.setColour(juce::Slider::thumbColourId, juce::Colours::lightgrey);
        volumeSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff18181e));
        volumeSlider.onValueChange = [this]
        {
            track.gain = (float)volumeSlider.getValue();
            if (onTrackStateChanged) onTrackStateChanged();
        };

        addAndMakeVisible(importBtn);
        importBtn.setButtonText("WAV");
        importBtn.setTooltip("Importar Audio (WAV/MP3/FLAC)");
        importBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d36));
        importBtn.onClick = [this] { if (importAudioCallback) importAudioCallback(track, *this); };

        addAndMakeVisible(midiBtn);
        midiBtn.setButtonText("MIDI");
        midiBtn.setTooltip("Abrir Piano Roll MIDI");
        midiBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff234e52));
        midiBtn.onClick = [this]
        {
            MidiClip* mClip = nullptr;
            {
                const juce::ScopedLock sl(audioLock);
                mClip = track.createOrGetMidiClipAt(applySnap(currentPlayheadSec) * (bpm / 60.0), 8.0);
            }
            notifySelected(nullptr, mClip);
            repaint();
            if (mClip != nullptr && openPianoRollCallback)
                openPianoRollCallback(track, *mClip);
        };

        addAndMakeVisible(fxBtn);
        fxBtn.setButtonText("FX");
        fxBtn.setTooltip("Rack de Plugins (EQ 8..120ch, Auto-Tune, Comp, VST3)");
        fxBtn.onClick = [this] { if (loadPluginCallback) loadPluginCallback(track); };
    }

    std::function<void()> onTrackStateChanged;

    AudioTrack& getTrack() { return track; }

    void syncButtonsFromTrack()
    {
        armBtn.setToggleState(track.isArmed, juce::dontSendNotification);
        monBtn.setToggleState(track.isMonitoring, juce::dontSendNotification);
        muteBtn.setToggleState(track.isMuted, juce::dontSendNotification);
        soloBtn.setToggleState(track.isSoloed, juce::dontSendNotification);
        volumeSlider.setValue(track.gain, juce::dontSendNotification);
        fxBtn.setColour(juce::TextButton::buttonColourId,
                        track.hasActiveEffects() ? juce::Colour(0xff2b9348) : juce::Colour(0xff2d2d36));
        repaint();
    }

    void setSelectionState(bool trackSel, AudioClip* selAudio, MidiClip* selMidi)
    {
        isTrackSelected = trackSel;
        selectedAudioClip = selAudio;
        selectedMidiClip = selMidi;
        repaint();
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

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth)
        {
            setMouseCursor(juce::MouseCursor::NormalCursor);
            return;
        }

        AudioClip* hitClip = nullptr;
        DragMode mode = detectAudioHitZone(e.getPosition(), hitClip);

        switch (mode)
        {
            case DragMode::TrimLeft:
            case DragMode::TrimRight:
            case DragMode::FadeIn:
            case DragMode::FadeOut:
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                break;
            case DragMode::MoveAudioClip:
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                break;
            default:
                setMouseCursor(juce::MouseCursor::IBeamCursor);
                break;
        }
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
        if (e.x < headerWidth)
        {
            notifySelected(nullptr, nullptr);
            return;
        }

        for (int i = track.getMidiClips().size() - 1; i >= 0; --i)
        {
            auto* mc = track.getMidiClips()[i];
            auto rect = getMidiClipBounds(*mc);
            if (rect.contains(e.getPosition()))
            {
                notifySelected(nullptr, mc);

                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu menu;
                    menu.addItem(1, "Abrir Piano Roll");
                    menu.addItem(2, "Excluir Clipe MIDI [Del]");
                    menu.showMenuAsync(juce::PopupMenu::Options(), [this, mc](int res)
                    {
                        if (res == 1 && openPianoRollCallback) openPianoRollCallback(track, *mc);
                        else if (res == 2) { const juce::ScopedLock sl(audioLock); track.removeMidiClip(mc); notifySelected(nullptr, nullptr); repaint(); }
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

        if (hitClip != nullptr)
            notifySelected(hitClip, nullptr);
        else
            notifySelected(nullptr, nullptr);

        if (e.mods.isPopupMenu() && hitClip != nullptr)
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Cortar Clipe na Agulha [S]");
            menu.addItem(2, "Duplicar Clipe [Ctrl+D]");
            menu.addItem(3, "Remover Fade In / Fade Out");
            menu.addSeparator();
            menu.addItem(4, "Excluir Clipe [Del]");

            menu.showMenuAsync(juce::PopupMenu::Options(), [this, hitClip](int result)
            {
                if (result == 1)
                {
                    { const juce::ScopedLock sl(audioLock); track.splitClipsAt(currentPlayheadSec); }
                    rebuildThumbnails();
                }
                else if (result == 2)
                {
                    {
                        const juce::ScopedLock sl(audioLock);
                        auto copy = hitClip->cloneClip(formatManager);
                        copy->timelineStart = hitClip->getTimelineEnd();
                        track.getClips().add(std::move(copy));
                    }
                    rebuildThumbnails();
                }
                else if (result == 3)
                {
                    const juce::ScopedLock sl(audioLock);
                    hitClip->fadeInDuration = 0.0;
                    hitClip->fadeOutDuration = 0.0;
                    repaint();
                }
                else if (result == 4)
                {
                    { const juce::ScopedLock sl(audioLock); track.removeClip(hitClip); }
                    notifySelected(nullptr, nullptr);
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
        g.setColour(isTrackSelected ? juce::Colour(0xff34343e) : juce::Colour(0xff28282f));
        g.fillRect(controlArea);

        auto numCol = controlArea.removeFromLeft(24);
        g.setColour(trackAccentColour);
        g.fillRect(numCol);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(juce::String(index + 1), numCol, juce::Justification::centred);

        auto meterCol = controlArea.removeFromRight(10).reduced(2, 4);
        g.setColour(juce::Colour(0xff141418));
        g.fillRect(meterCol);
        float peak = juce::jlimit(0.0f, 1.0f, track.currentPeakLevel.load());
        if (peak > 0.005f)
        {
            int h = (int)(peak * (float)meterCol.getHeight());
            g.setColour(peak > 0.90f ? juce::Colours::red : juce::Colour(0xff38b000));
            g.fillRect(meterCol.removeFromBottom(h));
        }

        g.setColour(juce::Colour(0xff18181d));
        g.drawRect(0, 0, headerWidth, getHeight(), 1);

        g.setColour(index % 2 == 0 ? juce::Colour(0xff232328) : juce::Colour(0xff26262c));
        g.fillRect(bounds);

        double beatSec = 60.0 / juce::jmax(20.0, bpm);
        double stepSec = beatSec / (double)gridDivision;
        int totalSteps = (int)((double)(getWidth() - headerWidth) / (stepSec * pixelsPerSecond)) + 2;

        for (int step = 0; step < totalSteps; ++step)
        {
            int gridX = headerWidth + (int)std::round(((double)step * stepSec) * pixelsPerSecond);
            if (gridX > getWidth()) break;

            int stepsPerBar = 4 * gridDivision;
            if (step % stepsPerBar == 0)       g.setColour(juce::Colour(0xff42424d));
            else if (step % gridDivision == 0) g.setColour(juce::Colour(0xff303039));
            else                               g.setColour(juce::Colour(0xff282830));

            g.drawVerticalLine(gridX, (float)bounds.getY(), (float)bounds.getBottom());
        }

        g.setColour(juce::Colour(0xff19191e));
        g.drawHorizontalLine(bounds.getBottom() - 1, (float)bounds.getX(), (float)bounds.getRight());

        for (auto* clip : track.getClips())
        {
            auto clipRect = getAudioClipBounds(*clip);
            if (!clipRect.intersects(bounds)) continue;

            bool isSelected = (clip == selectedAudioClip);

            g.setColour(isSelected ? trackAccentColour.brighter(0.15f) : trackAccentColour.withBrightness(0.48f));
            g.fillRoundedRectangle(clipRect.toFloat(), 3.0f);

            auto titleStrip = clipRect.withHeight(16);
            g.setColour(juce::Colours::black.withAlpha(0.32f));
            g.fillRect(titleStrip);

            if (auto it = thumbnails.find(clip->id); it != thumbnails.end())
            {
                g.setColour(isSelected ? juce::Colours::white : juce::Colour(0xfff0f4f8));
                it->second->drawChannels(g, clipRect.withTrimmedTop(16).reduced(1, 2),
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
                g.setColour(juce::Colours::black.withAlpha(0.55f));
                g.fillPath(fadePath);
                g.setColour(juce::Colours::white);
                g.drawLine((float)clipRect.getX(), (float)clipRect.getBottom(),
                           (float)clipRect.getX() + fadeInPx, (float)clipRect.getY(), 1.4f);
            }

            if (clip->fadeOutDuration > 0.005)
            {
                float fadeOutPx = (float)(clip->fadeOutDuration * pixelsPerSecond);
                juce::Path fadePath;
                fadePath.startNewSubPath((float)clipRect.getRight(), (float)clipRect.getY());
                fadePath.lineTo((float)clipRect.getRight() - fadeOutPx, (float)clipRect.getY());
                fadePath.lineTo((float)clipRect.getRight(), (float)clipRect.getBottom());
                fadePath.closeSubPath();
                g.setColour(juce::Colours::black.withAlpha(0.55f));
                g.fillPath(fadePath);
                g.setColour(juce::Colours::white);
                g.drawLine((float)clipRect.getRight() - fadeOutPx, (float)clipRect.getY(),
                           (float)clipRect.getRight(), (float)clipRect.getBottom(), 1.4f);
            }

            float handleInX  = (float)clipRect.getX() + (float)(clip->fadeInDuration * pixelsPerSecond);
            float handleOutX = (float)clipRect.getRight() - (float)(clip->fadeOutDuration * pixelsPerSecond);
            g.setColour(juce::Colours::white);
            g.fillRect(handleInX - 3.0f, (float)clipRect.getY(), 6.0f, 6.0f);
            g.fillRect(handleOutX - 3.0f, (float)clipRect.getY(), 6.0f, 6.0f);

            g.setColour(isSelected ? juce::Colours::white : juce::Colours::black.withAlpha(0.7f));
            g.drawRoundedRectangle(clipRect.toFloat(), 3.0f, isSelected ? 1.8f : 1.0f);

            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            g.drawText(clip->audioFile.getFileNameWithoutExtension(),
                       titleStrip.reduced(6, 0), juce::Justification::centredLeft, true);
        }

        for (auto* mc : track.getMidiClips())
        {
            auto mcRect = getMidiClipBounds(*mc);
            if (!mcRect.intersects(bounds)) continue;

            bool isSelected = (mc == selectedMidiClip);

            g.setColour(isSelected ? juce::Colour(0xff2b9348) : juce::Colour(0xff1e6b36));
            g.fillRoundedRectangle(mcRect.toFloat(), 3.0f);
            g.setColour(isSelected ? juce::Colours::white : juce::Colour(0xff55a630));
            g.drawRoundedRectangle(mcRect.toFloat(), 3.0f, isSelected ? 1.8f : 1.0f);

            for (const auto& note : mc->notes)
            {
                float nx = (float)mcRect.getX() + (float)((note.startBeat / mc->durationBeats) * (double)mcRect.getWidth());
                float nw = juce::jmax(3.0f, (float)((note.lengthBeats / mc->durationBeats) * (double)mcRect.getWidth()));
                float normPitch = juce::jlimit(0.0f, 1.0f, (float)(84 - note.noteNumber) / 48.0f);
                float ny = (float)mcRect.getY() + 15.0f + normPitch * ((float)mcRect.getHeight() - 18.0f);

                g.setColour(juce::Colours::yellow);
                g.fillRect(nx, ny, nw, 2.5f);
            }

            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
            g.drawText("MIDI (" + juce::String((int)mc->notes.size()) + ")",
                       mcRect.reduced(5, 1).removeFromTop(14), juce::Justification::centredLeft, true);
        }

        float playheadX = (float)headerWidth + (float)(currentPlayheadSec * pixelsPerSecond);
        if (playheadX >= (float)headerWidth && playheadX <= (float)getWidth())
        {
            g.setColour(juce::Colour(0xffe63946));
            g.drawLine(playheadX, (float)bounds.getY(), playheadX, (float)bounds.getBottom(), 1.5f);
        }
    }

    void resized() override
    {
        auto controlArea = getLocalBounds().removeFromLeft(headerWidth).withTrimmedLeft(26).withTrimmedRight(12).reduced(4, 4);
        
        auto topRow = controlArea.removeFromTop(22);
        armBtn.setBounds(topRow.removeFromLeft(22).reduced(1));
        monBtn.setBounds(topRow.removeFromLeft(34).reduced(1));
        fxBtn.setBounds(topRow.removeFromRight(28).reduced(1));
        midiBtn.setBounds(topRow.removeFromRight(36).reduced(1));
        importBtn.setBounds(topRow.removeFromRight(32).reduced(1));
        nameLabel.setBounds(topRow.reduced(2, 0));

        controlArea.removeFromTop(6);
        auto bottomRow = controlArea.removeFromTop(22);
        muteBtn.setBounds(bottomRow.removeFromLeft(24).reduced(1));
        soloBtn.setBounds(bottomRow.removeFromLeft(24).reduced(1));
        volumeSlider.setBounds(bottomRow.reduced(4, 0));
    }

private:
    void notifySelected(AudioClip* ac, MidiClip* mc)
    {
        if (selectCallback)
            selectCallback(this, ac, mc);
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }

    juce::Rectangle<int> getAudioClipBounds(const AudioClip& clip) const
    {
        int x = headerWidth + (int)std::round(clip.timelineStart * pixelsPerSecond);
        int w = juce::jmax(8, (int)std::round(clip.duration * pixelsPerSecond));
        return { x, 3, w, getHeight() - 6 };
    }

    juce::Rectangle<int> getMidiClipBounds(const MidiClip& mc) const
    {
        double secPerBeat = 60.0 / juce::jmax(20.0, bpm);
        int x = headerWidth + (int)std::round((mc.timelineStartBeats * secPerBeat) * pixelsPerSecond);
        int w = juce::jmax(16, (int)std::round((mc.durationBeats * secPerBeat) * pixelsPerSecond));
        return { x, 3, w, getHeight() - 6 };
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
    int index = 0;
    juce::Colour trackAccentColour;
    juce::AudioFormatManager& formatManager;
    juce::AudioThumbnailCache& thumbnailCache;
    juce::CriticalSection& audioLock;

    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;

    static constexpr int headerWidth = 264;
    double pixelsPerSecond = 55.0;
    double currentPlayheadSec = 0.0;
    double bpm = 120.0;
    bool snapEnabled = true;
    int gridDivision = 1;

    bool isTrackSelected = false;
    AudioClip* selectedAudioClip = nullptr;
    MidiClip* selectedMidiClip = nullptr;

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
    juce::TextButton armBtn, monBtn, muteBtn, soloBtn, importBtn, midiBtn, fxBtn;
    juce::Slider volumeSlider;

    std::function<void(AudioTrack&)> loadPluginCallback;
    std::function<void(AudioTrack&, TrackRowComponent&)> importAudioCallback;
    std::function<void(AudioTrack&, MidiClip&)> openPianoRollCallback;
    std::function<void(double)> seekCallback;
    std::function<void(TrackRowComponent*, AudioClip*, MidiClip*)> selectCallback;
};
