#pragma once
#include <JuceHeader.h>
#include "Metronome.h"
#include "PluginScanner.h"
#include "AudioTrack.h"
#include "TimelineComponent.h"
#include "MixerComponent.h"
#include "PianoRollWindow.h"
#include "TrackRackWindow.h"
#include "AudioExporter.h"
#include "ProjectSerializer.h"

class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer
{
public:
    MainComponent()
    {
        audioFormatManager.registerBasicFormats();

        addAndMakeVisible(timecodeLabel);
        timecodeLabel.setText("1.1.00 / 0:00.000", juce::dontSendNotification);
        timecodeLabel.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        timecodeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        timecodeLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(statusStateLabel);
        statusStateLabel.setText("[Stopped]", juce::dontSendNotification);
        statusStateLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        statusStateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        setupButton(rewindBtn, "|<",   "Inicio [W / Home]",    [this] { seekTo(0.0); });
        setupButton(playBtn,   "PLAY", "Play [Espaco]",        [this] { startPlayback(); }, juce::Colour(0xff2b9348));
        setupButton(stopBtn,   "STOP", "Stop [Espaco]",        [this] { stopTransport(); });
        setupButton(recBtn,    "REC",  "Gravar [Ctrl+R]",      [this] { startRecording(); }, juce::Colour(0xffc1121f));

        setupButton(clickBtn,  "CLIQUE", "Metronomo [C]",      [this] { metronome.enabled = clickBtn.getToggleState(); });
        clickBtn.setClickingTogglesState(true);
        clickBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe07a5f));

        setupButton(snapBtn,   "SNAP",   "Grade Magnetica [Alt+S]", [this] { updateGridParameters(); });
        snapBtn.setClickingTogglesState(true);
        snapBtn.setToggleState(true, juce::dontSendNotification);
        snapBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0077b6));

        addAndMakeVisible(gridDivBox);
        gridDivBox.addItem("1/4", 1);
        gridDivBox.addItem("1/8", 2);
        gridDivBox.addItem("1/16", 4);
        gridDivBox.setSelectedId(1, juce::dontSendNotification);
        gridDivBox.onChange = [this] { updateGridParameters(); };

        setupButton(splitBtn,  "Split [S]", "Cortar Clipe na Agulha [S]", [this] { splitSelectedOrAtPlayhead(); }, juce::Colour(0xff4a4e69));
        setupButton(mixerToggleBtn, "Mixer [Ctrl+M]", "Mostrar/Ocultar Mixer Inferior", [this]
        {
            showBottomMixer = !showBottomMixer;
            bottomMixer.setVisible(showBottomMixer);
            resized();
        });
        mixerToggleBtn.setClickingTogglesState(true);
        mixerToggleBtn.setToggleState(true, juce::dontSendNotification);
        mixerToggleBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2b9348));

        addAndMakeVisible(bpmLabel);
        bpmLabel.setText("BPM", juce::dontSendNotification);
        bpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        addAndMakeVisible(bpmSlider);
        bpmSlider.setRange(40.0, 240.0, 1.0);
        bpmSlider.setValue(120.0);
        bpmSlider.setSliderStyle(juce::Slider::IncDecButtons);
        bpmSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 24);
        bpmSlider.onValueChange = [this] { updateGridParameters(); };

        setupButton(addTrackBtn,      "+ Pista",  "Nova Faixa [Ctrl+T]",  [this] { addNewTrack(); }, juce::Colour(0xff3a506b));
        setupButton(saveProjBtn,      "Salvar",   "Salvar Sessao [Ctrl+S]", [this] { saveProjectDialog(); });
        setupButton(openProjBtn,      "Abrir",    "Abrir Sessao [Ctrl+O]",  [this] { openProjectDialog(); });
        setupButton(bounceBtn,        "Exportar", "Bounce MP3/WAV/FLAC",  [this] { exportProjectDialog(); }, juce::Colour(0xff2d6a4f));
        setupButton(audioSettingsBtn, "ASIO",     "Audio / ASIO",         [this] { showAudioSettings(); });

        addAndMakeVisible(tracksViewport);
        tracksViewport.setViewedComponent(&tracksContainer, false);
        tracksViewport.setScrollBarsShown(true, false);

        addAndMakeVisible(bottomMixer);

        regions.push_back({ "Intro",    0.0,  8.0,  juce::Colour(0xff2b9348) });
        regions.push_back({ "Verse 1",  8.0,  24.0, juce::Colour(0xff3a86ff) });
        regions.push_back({ "Chorus 1", 24.0, 40.0, juce::Colour(0xff8338ec) });

        setWantsKeyboardFocus(true);

        for (int i = 0; i < 6; ++i)
            addNewTrack();

        setAudioChannels(2, 2);
        setSize(1366, 740);
        startTimerHz(30);

        pluginScanner.startBackgroundAutoScan(false);
    }

    ~MainComponent() override
    {
        stopTimer();
        trackRackWindow.reset();
        pianoRollWindow.reset();
        stopTransport();
        bottomMixer.clearStrips();
        shutdownAudio();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        bool ctrl = key.getModifiers().isCtrlDown();
        bool alt  = key.getModifiers().isAltDown();

        if (key == juce::KeyPress::spaceKey)
        {
            if (isPlaying) stopTransport();
            else startPlayback();
            return true;
        }
        if (ctrl && ch == 'r') { startRecording(); return true; }
        if (ctrl && ch == 's') { saveProjectDialog(); return true; }
        if (ctrl && ch == 'o') { openProjectDialog(); return true; }
        if (ctrl && ch == 't') { addNewTrack(); return true; }
        if (ctrl && ch == 'd') { duplicateSelectedClip(); return true; }
        if (ctrl && ch == 'm') { mixerToggleBtn.triggerClick(); return true; }
        if (alt && ch == 's')  { snapBtn.triggerClick(); return true; }

        if (!ctrl && !alt && ch == 's') { splitSelectedOrAtPlayhead(); return true; }
        if (!ctrl && !alt && ch == 'c') { clickBtn.triggerClick(); return true; }
        if (!ctrl && !alt && ch == 'm' && selectedRow != nullptr)
        {
            selectedRow->getTrack().isMuted = !selectedRow->getTrack().isMuted;
            selectedRow->syncButtonsFromTrack();
            bottomMixer.refreshMetersAndState();
            return true;
        }
        if ((!ctrl && !alt && ch == 'w') || key == juce::KeyPress::homeKey) { seekTo(0.0); return true; }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelectedClip(); return true; }

        if (ch == '+' || ch == '=') { changeHZoom(1.25); return true; }
        if (ch == '-')               { changeHZoom(0.8);  return true; }
        if (key == juce::KeyPress::pageUpKey)   { changeVZoom(12);  return true; }
        if (key == juce::KeyPress::pageDownKey) { changeVZoom(-12); return true; }

        return false;
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isCtrlDown())
        {
            changeHZoom(wheel.deltaY > 0 ? 1.18 : 0.85);
            return;
        }
        Component::mouseWheelMove(e, wheel);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        if (regionArea.contains(e.getPosition()) && e.x >= 272 && e.mods.isPopupMenu())
        {
            double clickSec = juce::jmax(0.0, (double)(e.x - 272) / pixelsPerSecond);
            regions.push_back({ "Secao " + juce::String((int)regions.size() + 1), clickSec, clickSec + 8.0, juce::Colour(0xffe07a5f) });
            repaint(regionArea);
            return;
        }

        if ((rulerArea.contains(e.getPosition()) || regionArea.contains(e.getPosition())) && e.x >= 272)
        {
            double clickedSec = juce::jmax(0.0, (double)(e.x - 272) / pixelsPerSecond);
            if (snapBtn.getToggleState())
            {
                double stepSec = (60.0 / bpmSlider.getValue()) / (double)gridDivBox.getSelectedId();
                clickedSec = std::round(clickedSec / stepSec) * stepSec;
            }
            seekTo(clickedSec);
        }
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        currentSampleRate = sampleRate;
        currentBlockSize = samplesPerBlockExpected;
        metronome.prepare(sampleRate);
        backgroundThread.startThread();

        const juce::ScopedLock sl(trackLock);
        for (auto* track : tracks)
            track->prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        int numInputs = bufferToFill.buffer->getNumChannels();
        juce::AudioBuffer<float> inputCopy(numInputs, bufferToFill.numSamples);
        for (int ch = 0; ch < numInputs; ++ch)
            inputCopy.copyFrom(ch, 0, *bufferToFill.buffer, ch, bufferToFill.startSample, bufferToFill.numSamples);

        bufferToFill.clearActiveBufferRegion();

        const juce::ScopedLock sl(trackLock);

        bool anySolo = false;
        for (auto* track : tracks)
            if (track->isSoloed) { anySolo = true; break; }

        double currentBpm = currentBpmAtomic.load();

        for (auto* track : tracks)
        {
            track->processTrack(bufferToFill,
                                inputCopy.getArrayOfReadPointers(),
                                numInputs,
                                globalTimelineSeconds,
                                currentBpm,
                                isPlaying,
                                anySolo);
        }

        metronome.process(*bufferToFill.buffer, globalTimelineSeconds, currentBpm, isPlaying);

        float mGain = masterGainAtomic.load();
        bufferToFill.buffer->applyGain(bufferToFill.startSample, bufferToFill.numSamples, mGain);

        float mPeak = 0.0f;
        for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
            mPeak = juce::jmax(mPeak, bufferToFill.buffer->getMagnitude(ch, bufferToFill.startSample, bufferToFill.numSamples));

        float prevMaster = masterPeakAtomic.load();
        masterPeakAtomic.store(mPeak > prevMaster ? mPeak : prevMaster * 0.88f);

        if (isPlaying && currentSampleRate > 0.0)
            globalTimelineSeconds += ((double)bufferToFill.numSamples / currentSampleRate);
    }

    void releaseResources() override
    {
        const juce::ScopedLock sl(trackLock);
        for (auto* track : tracks)
            track->releaseResources();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1b1b20));

        g.setColour(juce::Colour(0xff25252c));
        g.fillRect(0, 0, getWidth(), 48);

        g.setColour(juce::Colour(0xff141418));
        g.fillRoundedRectangle(8.0f, 6.0f, 256.0f, 36.0f, 4.0f);
        g.setColour(juce::Colour(0xff383844));
        g.drawRoundedRectangle(8.0f, 6.0f, 256.0f, 36.0f, 4.0f, 1.0f);

        int timelineStartX = regionArea.getX() + 264;
        if (!regionArea.isEmpty())
        {
            g.setColour(juce::Colour(0xff1a1a20));
            g.fillRect(regionArea);
            g.setColour(juce::Colour(0xff2e2e38));
            g.drawRect(regionArea, 1);

            g.setColour(pluginScanner.isScanning() ? juce::Colour(0xff00f5d4) : juce::Colours::grey);
            g.setFont(10.5f);
            g.drawText(pluginScanner.getCurrentStatus(), regionArea.getX() + 8, regionArea.getY(), 252, regionArea.getHeight(), juce::Justification::centredLeft, true);

            for (const auto& reg : regions)
            {
                int rx = timelineStartX + (int)std::round(reg.startSec * pixelsPerSecond);
                int rw = juce::jmax(20, (int)std::round((reg.endSec - reg.startSec) * pixelsPerSecond));
                auto rRect = juce::Rectangle<int>(rx, regionArea.getY() + 2, rw, regionArea.getHeight() - 4);
                if (!rRect.intersects(regionArea.withTrimmedLeft(264))) continue;

                g.setColour(reg.colour.withAlpha(0.75f));
                g.fillRoundedRectangle(rRect.toFloat(), 3.0f);
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
                g.drawText(reg.name, rRect.reduced(6, 0), juce::Justification::centredLeft, true);
            }
        }

        if (!rulerArea.isEmpty())
        {
            g.setColour(juce::Colour(0xff202027));
            g.fillRect(rulerArea);
            g.setColour(juce::Colour(0xff343440));
            g.drawRect(rulerArea, 1);

            double bpm = bpmSlider.getValue();
            double barDurationSec = (60.0 / bpm) * 4.0;
            int totalBars = (int)((double)(rulerArea.getWidth() - 264) / (barDurationSec * pixelsPerSecond)) + 2;

            for (int bar = 0; bar < totalBars; ++bar)
            {
                int barX = timelineStartX + (int)std::round((double)bar * barDurationSec * pixelsPerSecond);
                if (barX > rulerArea.getRight()) break;

                g.setColour(juce::Colours::lightgrey);
                g.drawVerticalLine(barX, (float)rulerArea.getY() + 3.0f, (float)rulerArea.getBottom());
                g.setFont(10.5f);
                g.drawText(juce::String(bar + 1) + ".1", barX + 4, rulerArea.getY(), 40, rulerArea.getHeight(), juce::Justification::centredLeft);
            }

            float playheadX = (float)timelineStartX + (float)(globalTimelineSeconds * pixelsPerSecond);
            if (playheadX >= (float)timelineStartX && playheadX <= (float)rulerArea.getRight())
            {
                g.setColour(juce::Colour(0xffe63946));
                juce::Path head;
                head.addTriangle(playheadX - 5.0f, (float)rulerArea.getY(),
                                 playheadX + 5.0f, (float)rulerArea.getY(),
                                 playheadX, (float)rulerArea.getBottom());
                g.fillPath(head);
            }
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto topBar = area.removeFromTop(48);

        timecodeLabel.setBounds(16, 8, 175, 32);
        statusStateLabel.setBounds(192, 12, 68, 24);

        auto controls = topBar.withTrimmedLeft(272).reduced(4, 9);

        rewindBtn.setBounds(controls.removeFromLeft(34).reduced(1));
        playBtn.setBounds(controls.removeFromLeft(52).reduced(1));
        stopBtn.setBounds(controls.removeFromLeft(52).reduced(1));
        recBtn.setBounds(controls.removeFromLeft(48).reduced(1));

        controls.removeFromLeft(8);
        clickBtn.setBounds(controls.removeFromLeft(58).reduced(1));
        snapBtn.setBounds(controls.removeFromLeft(52).reduced(1));
        gridDivBox.setBounds(controls.removeFromLeft(58).reduced(1));
        splitBtn.setBounds(controls.removeFromLeft(68).reduced(1));
        mixerToggleBtn.setBounds(controls.removeFromLeft(92).reduced(1));

        audioSettingsBtn.setBounds(controls.removeFromRight(50).reduced(1));
        bounceBtn.setBounds(controls.removeFromRight(72).reduced(1));
        openProjBtn.setBounds(controls.removeFromRight(54).reduced(1));
        saveProjBtn.setBounds(controls.removeFromRight(54).reduced(1));
        addTrackBtn.setBounds(controls.removeFromRight(68).reduced(1));
        controls.removeFromRight(6);
        bpmSlider.setBounds(controls.removeFromRight(84).reduced(1));
        bpmLabel.setBounds(controls.removeFromRight(32));

        regionArea = area.removeFromTop(22).reduced(8, 0);
        rulerArea  = area.removeFromTop(22).reduced(8, 0);

        if (showBottomMixer)
            bottomMixer.setBounds(area.removeFromBottom(185).reduced(8, 2));

        tracksViewport.setBounds(area.reduced(8, 2));
        layoutTracks();
    }

private:
    juce::Label timecodeLabel, statusStateLabel, bpmLabel;
    juce::TextButton rewindBtn, playBtn, stopBtn, recBtn, clickBtn, snapBtn, splitBtn, mixerToggleBtn;
    juce::TextButton addTrackBtn, saveProjBtn, openProjBtn, bounceBtn, audioSettingsBtn;
    juce::Slider bpmSlider;
    juce::ComboBox gridDivBox;

    juce::Rectangle<int> regionArea, rulerArea;
    std::vector<TimelineRegion> regions;

    juce::Viewport tracksViewport;
    juce::Component tracksContainer;
    bool showBottomMixer = true;

    std::atomic<float> masterGainAtomic { 1.0f };
    std::atomic<float> masterPeakAtomic { 0.0f };
    BottomMixerDock bottomMixer { masterGainAtomic, masterPeakAtomic };

    MetronomeEngine metronome;
    PluginScannerEngine pluginScanner;
    juce::AudioFormatManager audioFormatManager;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    juce::TimeSliceThread backgroundThread { "MultiTrack Disk Thread" };

    juce::CriticalSection trackLock;
    juce::OwnedArray<AudioTrack> tracks;
    juce::OwnedArray<TrackRowComponent> trackRows;
    std::unique_ptr<PianoRollWindow> pianoRollWindow;
    std::unique_ptr<TrackRackWindow> trackRackWindow;

    TrackRowComponent* selectedRow = nullptr;
    AudioClip* selectedAudioClip = nullptr;
    MidiClip* selectedMidiClip = nullptr;

    bool isPlaying = false;
    bool isRecording = false;
    double globalTimelineSeconds = 0.0;
    std::atomic<double> currentBpmAtomic { 120.0 };
    double pixelsPerSecond = 55.0;
    int trackRowHeight = 62;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    void setupButton(juce::TextButton& btn, const juce::String& text, const juce::String& tooltip,
                     std::function<void()> onClick, juce::Colour bg = juce::Colour(0xff32323c))
    {
        addAndMakeVisible(btn);
        btn.setButtonText(text);
        btn.setTooltip(tooltip);
        btn.setColour(juce::TextButton::buttonColourId, bg);
        btn.onClick = std::move(onClick);
    }

    void handleSelection(TrackRowComponent* row, AudioClip* ac, MidiClip* mc)
    {
        selectedRow = row;
        selectedAudioClip = ac;
        selectedMidiClip = mc;

        for (auto* r : trackRows)
            r->setSelectionState(r == row, ac, mc);
    }

    void syncAllTrackViews()
    {
        for (auto* r : trackRows)
            r->syncButtonsFromTrack();
        bottomMixer.refreshMetersAndState();
    }

    void rebuildMixerDock()
    {
        bottomMixer.rebuildStrips(
            tracks,
            [this](AudioTrack& tr) { loadPluginForTrack(tr); },
            [this] { syncAllTrackViews(); }
        );
    }

    void updateGridParameters()
    {
        double bpm = bpmSlider.getValue();
        currentBpmAtomic.store(bpm);
        bool snap = snapBtn.getToggleState();
        int div = gridDivBox.getSelectedId();

        for (auto* row : trackRows)
            row->setGridSettings(bpm, snap, div);
        repaint();
    }

    void addNewTrack()
    {
        const juce::ScopedLock sl(trackLock);
        int trackIndex = tracks.size();
        auto* newTrack = tracks.add(new AudioTrack("Track " + juce::String(trackIndex + 1), audioFormatManager, backgroundThread));
        newTrack->prepareToPlay(currentBlockSize, currentSampleRate);

        auto* row = trackRows.add(new TrackRowComponent(
            *newTrack, trackIndex, audioFormatManager, thumbnailCache, trackLock,
            [this](AudioTrack& t) { loadPluginForTrack(t); },
            [this](AudioTrack& t, TrackRowComponent& r) { importAudioForTrack(t, r); },
            [this](AudioTrack& t, MidiClip& mc) { openPianoRoll(t, mc); },
            [this](double newPos) { seekTo(newPos); },
            [this](TrackRowComponent* r, AudioClip* ac, MidiClip* mc) { handleSelection(r, ac, mc); }
        ));
        row->onTrackStateChanged = [this] { bottomMixer.refreshMetersAndState(); };
        row->setPixelsPerSecond(pixelsPerSecond);
        row->setGridSettings(bpmSlider.getValue(), snapBtn.getToggleState(), gridDivBox.getSelectedId());

        tracksContainer.addAndMakeVisible(row);
        layoutTracks();
        rebuildMixerDock();
    }

    void openPianoRoll(AudioTrack& track, MidiClip& clip)
    {
        pianoRollWindow = std::make_unique<PianoRollWindow>(
            track.getName(), clip, trackLock,
            [&track](int noteNum, bool isNoteOn)
            {
                track.injectLiveMidiNote(noteNum, isNoteOn);
            });
    }

    void layoutTracks()
    {
        int totalHeight = juce::jmax(tracksViewport.getHeight(), trackRows.size() * trackRowHeight);
        tracksContainer.setSize(tracksViewport.getWidth() - 12, totalHeight);

        for (int i = 0; i < trackRows.size(); ++i)
            trackRows[i]->setBounds(0, i * trackRowHeight, tracksContainer.getWidth(), trackRowHeight);
    }

    void startPlayback()
    {
        const juce::ScopedLock sl(trackLock);
        metronome.reset();
        isPlaying = true;
        statusStateLabel.setText("[Playing]", juce::dontSendNotification);
        statusStateLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38b000));
    }

    void startRecording()
    {
        const juce::ScopedLock sl(trackLock);
        auto desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);

        for (auto* track : tracks)
        {
            if (track->isArmed)
            {
                auto file = desktop.getNonexistentChildFile(track->getName() + "_Take", ".wav");
                track->startRecording(file, globalTimelineSeconds);
            }
        }
        metronome.reset();
        isRecording = true;
        isPlaying = true;
        statusStateLabel.setText("[Recording]", juce::dontSendNotification);
        statusStateLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe63946));
    }

    void stopTransport()
    {
        const juce::ScopedLock sl(trackLock);
        isPlaying = false;
        isRecording = false;
        metronome.reset();
        statusStateLabel.setText("[Stopped]", juce::dontSendNotification);
        statusStateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        for (int i = 0; i < tracks.size(); ++i)
        {
            tracks[i]->stopRecording(globalTimelineSeconds);
            tracks[i]->sendAllNotesOff();
            trackRows[i]->rebuildThumbnails();
        }
    }

    void seekTo(double seconds)
    {
        const juce::ScopedLock sl(trackLock);
        globalTimelineSeconds = juce::jmax(0.0, seconds);
        metronome.reset();
        for (auto* track : tracks)
            track->sendAllNotesOff();
        updatePlayheadUI();
    }

    void splitSelectedOrAtPlayhead()
    {
        bool anySplit = false;
        {
            const juce::ScopedLock sl(trackLock);
            if (selectedRow != nullptr && selectedAudioClip != nullptr)
            {
                auto& clips = selectedRow->getTrack().getClips();
                if (clips.contains(selectedAudioClip) &&
                    globalTimelineSeconds > selectedAudioClip->timelineStart + 0.05 &&
                    globalTimelineSeconds < selectedAudioClip->getTimelineEnd() - 0.05)
                {
                    double leftDur  = globalTimelineSeconds - selectedAudioClip->timelineStart;
                    double rightDur = selectedAudioClip->getTimelineEnd() - globalTimelineSeconds;

                    auto rightClip = selectedAudioClip->cloneClip(audioFormatManager);
                    rightClip->timelineStart     = globalTimelineSeconds;
                    rightClip->sourceStartOffset = selectedAudioClip->sourceStartOffset + leftDur;
                    rightClip->duration          = rightDur;
                    rightClip->fadeInDuration    = 0.01;

                    selectedAudioClip->duration        = leftDur;
                    selectedAudioClip->fadeOutDuration = 0.01;

                    clips.add(std::move(rightClip));
                    anySplit = true;
                }
            }
            else
            {
                for (auto* track : tracks)
                    if (track->splitClipsAt(globalTimelineSeconds))
                        anySplit = true;
            }
        }

        if (anySplit)
            for (auto* row : trackRows)
                row->rebuildThumbnails();
    }

    void duplicateSelectedClip()
    {
        if (selectedRow == nullptr || selectedAudioClip == nullptr) return;
        {
            const juce::ScopedLock sl(trackLock);
            auto& clips = selectedRow->getTrack().getClips();
            if (clips.contains(selectedAudioClip))
            {
                auto copy = selectedAudioClip->cloneClip(audioFormatManager);
                copy->timelineStart = selectedAudioClip->getTimelineEnd();
                auto* rawPtr = copy.get();
                clips.add(std::move(copy));
                handleSelection(selectedRow, rawPtr, nullptr);
            }
        }
        selectedRow->rebuildThumbnails();
    }

    void deleteSelectedClip()
    {
        if (selectedRow == nullptr) return;

        if (selectedAudioClip != nullptr)
        {
            {
                const juce::ScopedLock sl(trackLock);
                selectedRow->getTrack().removeClip(selectedAudioClip);
            }
            selectedAudioClip = nullptr;
            selectedRow->rebuildThumbnails();
        }
        else if (selectedMidiClip != nullptr)
        {
            {
                const juce::ScopedLock sl(trackLock);
                selectedRow->getTrack().removeMidiClip(selectedMidiClip);
            }
            selectedMidiClip = nullptr;
            selectedRow->repaint();
        }
    }

    void changeHZoom(double factor)
    {
        pixelsPerSecond = juce::jlimit(10.0, 350.0, pixelsPerSecond * factor);
        for (auto* row : trackRows)
            row->setPixelsPerSecond(pixelsPerSecond);
        repaint();
    }

    void changeVZoom(int deltaPx)
    {
        trackRowHeight = juce::jlimit(56, 200, trackRowHeight + deltaPx);
        layoutTracks();
    }

    void updatePlayheadUI()
    {
        double bpm = bpmSlider.getValue();
        double beatSec = 60.0 / bpm;
        double totalBeatsFloat = globalTimelineSeconds / beatSec;
        int totalBeats = (int)totalBeatsFloat;
        int barNum = (totalBeats / 4) + 1;
        int beatInBar = (totalBeats % 4) + 1;
        int hundredths = (int)((totalBeatsFloat - totalBeats) * 100.0);

        int mins = (int)(globalTimelineSeconds / 60.0);
        double secs = std::fmod(globalTimelineSeconds, 60.0);

        timecodeLabel.setText(juce::String::formatted("%d.%d.%02d / %d:%06.3f", barNum, beatInBar, hundredths, mins, secs),
                              juce::dontSendNotification);

        for (auto* row : trackRows)
            row->setPlayheadPosition(globalTimelineSeconds);

        if (showBottomMixer)
            bottomMixer.refreshMetersAndState();

        repaint(regionArea);
        repaint(rulerArea);
    }

    void timerCallback() override
    {
        updatePlayheadUI();
    }

    void saveProjectDialog()
    {
        auto chooser = std::make_shared<juce::FileChooser>("Salvar Sessao Completa (.dawproj)",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("MinhaSessao.dawproj"),
            "*.dawproj");

        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file != juce::File())
                {
                    bool ok = ProjectSerializer::saveProject(file, bpmSlider.getValue(), regions, tracks, trackLock);
                    if (ok)
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                            "Sessao Salva", "Sessao e secoes salvas com sucesso em:\n" + file.getFullPathName());
                }
            });
    }

    void openProjectDialog()
    {
        stopTransport();
        trackRackWindow.reset();
        pianoRollWindow.reset();
        selectedRow = nullptr;
        selectedAudioClip = nullptr;
        selectedMidiClip = nullptr;

        auto chooser = std::make_shared<juce::FileChooser>("Abrir Sessao (.dawproj)",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory), "*.dawproj");

        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    double loadedBpm = 120.0;
                    if (ProjectSerializer::loadProject(file, loadedBpm, regions, tracks, trackLock,
                                                       audioFormatManager, pluginScanner.getFormatManager(),
                                                       backgroundThread, currentSampleRate, currentBlockSize))
                    {
                        bpmSlider.setValue(loadedBpm, juce::sendNotification);
                        trackRows.clear(true);
                        for (int idx = 0; idx < tracks.size(); ++idx)
                        {
                            auto* t = tracks[idx];
                            auto* row = trackRows.add(new TrackRowComponent(
                                *t, idx, audioFormatManager, thumbnailCache, trackLock,
                                [this](AudioTrack& tr) { loadPluginForTrack(tr); },
                                [this](AudioTrack& tr, TrackRowComponent& r) { importAudioForTrack(tr, r); },
                                [this](AudioTrack& tr, MidiClip& mc) { openPianoRoll(tr, mc); },
                                [this](double newPos) { seekTo(newPos); },
                                [this](TrackRowComponent* r, AudioClip* ac, MidiClip* mc) { handleSelection(r, ac, mc); }
                            ));
                            row->onTrackStateChanged = [this] { bottomMixer.refreshMetersAndState(); };
                            row->setPixelsPerSecond(pixelsPerSecond);
                            row->setGridSettings(loadedBpm, snapBtn.getToggleState(), gridDivBox.getSelectedId());
                            row->rebuildThumbnails();
                            row->syncButtonsFromTrack();
                            tracksContainer.addAndMakeVisible(row);
                        }
                        layoutTracks();
                        rebuildMixerDock();
                        repaint();
                    }
                }
            });
    }

    void exportProjectDialog()
    {
        stopTransport();

        auto chooser = std::make_shared<juce::FileChooser>("Exportar Mixagem Final (MP3, WAV ou FLAC)",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("Master_Mix.mp3"),
            "*.mp3;*.wav;*.flac");

        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file != juce::File())
                {
                    AudioExporter::ExportFormat fmt = AudioExporter::ExportFormat::WAV;
                    if (file.hasFileExtension(".mp3"))       fmt = AudioExporter::ExportFormat::MP3;
                    else if (file.hasFileExtension(".flac")) fmt = AudioExporter::ExportFormat::FLAC;

                    juce::String statusMsg = AudioExporter::bounceProjectToFile(
                        file, tracks, trackLock, currentSampleRate, currentBlockSize, bpmSlider.getValue(), fmt);

                    bool ok = statusMsg.startsWith("OK");
                    juce::AlertWindow::showMessageBoxAsync(
                        ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                        ok ? "Exportacao Concluida" : "Aviso na Exportacao",
                        statusMsg);
                }
            });
    }

    void importAudioForTrack(AudioTrack& track, TrackRowComponent& row)
    {
        auto chooser = std::make_shared<juce::FileChooser>("Importar Audio (WAV/MP3/FLAC)",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.mp3;*.aiff;*.flac");

        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser, &track, &row](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    {
                        const juce::ScopedLock sl(trackLock);
                        track.addClipFromFile(file, globalTimelineSeconds);
                    }
                    row.rebuildThumbnails();
                }
            });
    }

    void loadPluginFromDescription(AudioTrack& targetTrack, const juce::PluginDescription& desc, std::function<void()> onLoaded)
    {
        juce::String error;
        auto instance = pluginScanner.getFormatManager().createPluginInstance(desc, currentSampleRate, currentBlockSize, error);
        if (instance != nullptr)
        {
            {
                const juce::ScopedLock sl(trackLock);
                targetTrack.vstFilePaths.add(desc.fileOrIdentifier);
                targetTrack.addVstPlugin(std::move(instance));
            }
            syncAllTrackViews();
            if (onLoaded) onLoaded();
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Erro ao Carregar Plugin", "Nao foi possivel carregar o plugin:\n" + error);
        }
    }

    void browseAndLoadPluginFile(AudioTrack& targetTrack, std::function<void()> onLoaded)
    {
        auto chooser = std::make_shared<juce::FileChooser>("Selecionar Plugin VST3 (.vst3)",
            juce::File("C:\\Program Files\\Common Files\\VST3"), "*.vst3");

        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser, &targetTrack, onLoaded](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    juce::OwnedArray<juce::PluginDescription> descriptions;
                    auto& fmtMgr = pluginScanner.getFormatManager();
                    for (int i = 0; i < fmtMgr.getNumFormats(); ++i)
                    {
                        fmtMgr.getFormat(i)->findAllTypesForFile(descriptions, file.getFullPathName());
                        if (descriptions.size() > 0) break;
                    }

                    if (descriptions.size() > 0)
                        loadPluginFromDescription(targetTrack, *descriptions[0], onLoaded);
                }
            });
    }

    void loadPluginForTrack(AudioTrack& track)
    {
        trackRackWindow = std::make_unique<TrackRackWindow>(
            track, pluginScanner, trackLock,
            [this](AudioTrack& targetTrack, const juce::PluginDescription& desc, std::function<void()> onLoaded)
            {
                loadPluginFromDescription(targetTrack, desc, std::move(onLoaded));
            },
            [this](AudioTrack& targetTrack, std::function<void()> onLoaded)
            {
                browseAndLoadPluginFile(targetTrack, std::move(onLoaded));
            });
        syncAllTrackViews();
    }

    void showAudioSettings()
    {
        auto* selector = new juce::AudioDeviceSelectorComponent(deviceManager, 0, 256, 0, 256, true, true, true, false);
        selector->setSize(500, 450);

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(selector);
        o.dialogTitle = "Configuracoes de Placa de Som (ASIO / WASAPI)";
        o.dialogBackgroundColour = juce::Colour(0xff1e1e24);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.launchAsync();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
