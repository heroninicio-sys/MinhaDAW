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
#include "WelcomeAndActions.h"

class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer
{
public:
    MainComponent()
    {
        audioFormatManager.registerBasicFormats();

        masterTrack = std::make_unique<AudioTrack>("MASTER", audioFormatManager, backgroundThread);

        addAndMakeVisible(timecodeLabel);
        timecodeLabel.setText("1.1.00 / 0:00.000", juce::dontSendNotification);
        timecodeLabel.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        timecodeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        timecodeLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(statusStateLabel);
        statusStateLabel.setText("[Stopped]", juce::dontSendNotification);
        statusStateLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        statusStateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        setupButton(rewindBtn, "|<",   "Inicio [Home]",        [this] { seekTo(0.0); });
        setupButton(playBtn,   "PLAY", "Play / Stop [Espaco]", [this] { startPlayback(); }, juce::Colour(0xff2b9348));
        setupButton(stopBtn,   "STOP", "Stop [Espaco]",        [this] { stopTransport(); });
        setupButton(recBtn,    "REC",  "Gravar [R]",           [this] { startRecording(); }, juce::Colour(0xffc1121f));

        setupButton(clickBtn,  "CLIQUE", "Metronomo",          [this] { metronome.enabled = clickBtn.getToggleState(); });
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

        setupButton(masterFxBtn, "MASTER FX", "Efeitos no Canal Master", [this] { loadPluginForTrack(*masterTrack); }, juce::Colour(0xff5e548e));
        setupButton(actionsBtn,  "Acoes [?]", "Action List [?]",         [this] { openActionListWindow(); });
        setupButton(mixerToggleBtn, "Mixer [Ctrl+M]", "Mostrar/Ocultar Mixer", [this]
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

        setupButton(addTrackBtn,      "+ Faixa",  "Nova Faixa [Ctrl+T]",          [this] { addNewTrack(); }, juce::Colour(0xff3a506b));
        setupButton(saveProjBtn,      "Salvar",   "Salvar Projeto [Ctrl+S]",      [this] { saveProjectDialog(); });
        setupButton(openProjBtn,      "Abrir",    "Abrir Projeto [Ctrl+O]",       [this] { openProjectDialog(); });
        setupButton(bounceBtn,        "Render",   "Renderizar [Ctrl+Alt+R]",      [this] { exportProjectDialog(); }, juce::Colour(0xff2d6a4f));
        setupButton(audioSettingsBtn, "Prefs",    "Preferencias [Ctrl+P]",        [this] { showAudioSettings(); });

        addAndMakeVisible(tracksViewport);
        tracksViewport.setViewedComponent(&tracksContainer, false);
        tracksViewport.setScrollBarsShown(true, true);

        addAndMakeVisible(bottomMixer);

        welcomeOverlay = std::make_unique<WelcomeOverlayComponent>(
            [this] { createNewCleanProject(); },
            [this] { openProjectDialog(); },
            [this] { showAudioSettings(); }
        );
        addChildComponent(*welcomeOverlay);
        welcomeOverlay->setVisible(WelcomeOverlayComponent::shouldShowOnStartup());

        addChildComponent(scanOverlay);
        pluginScanner.attachOverlay(&scanOverlay);

        setWantsKeyboardFocus(true);
        setSize(1366, 740);
        startTimerHz(30);

        juce::MessageManager::callAsync([this]
        {
            setAudioChannels(2, 2);
            rebuildMixerDock();
            pluginScanner.startBackgroundAutoScan(false);
        });
    }

    ~MainComponent() override
    {
        stopTimer();
        actionListWindow.reset();
        trackRackWindow.reset();
        pianoRollWindow.reset();
        stopTransport();
        bottomMixer.clearStrips();
        shutdownAudio();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        bool ctrl  = key.getModifiers().isCtrlDown();
        bool alt   = key.getModifiers().isAltDown();
        bool shift = key.getModifiers().isShiftDown();

        if (key.getTextCharacter() == '?')
        {
            openActionListWindow();
            return true;
        }

        if (ctrl && !alt && !shift && ch == 'n') { createNewCleanProject(); return true; }
        if (ctrl && !alt && !shift && ch == 'o') { openProjectDialog(); return true; }
        if (ctrl && !alt && !shift && ch == 's') { saveProjectDialog(); return true; }
        if (ctrl && alt && ch == 'r') { exportProjectDialog(); return true; }
        if (ctrl && !alt && ch == 'p') { showAudioSettings(); return true; }
        if (ctrl && !alt && ch == 'm') { mixerToggleBtn.triggerClick(); return true; }

        if (key == juce::KeyPress::spaceKey)
        {
            if (isPlaying) stopTransport();
            else startPlayback();
            return true;
        }

        if (!ctrl && !alt && !shift && ch == 'r') { startRecording(); return true; }
        if (!ctrl && alt && ch == 's') { snapBtn.triggerClick(); return true; }

        if (!ctrl && alt && ch == 'g')
        {
            showGridLines = !showGridLines;
            updateGridParameters();
            return true;
        }

        if (!ctrl && alt && ch == 'x')
        {
            autoCrossfadeEnabled = !autoCrossfadeEnabled;
            return true;
        }

        if (!ctrl && !alt && !shift && ch == 'l')
        {
            itemsLocked = !itemsLocked;
            updateGridParameters();
            return true;
        }

        if (ctrl && !alt && ch == 't') { addNewTrack(); return true; }
        if (ctrl && !shift && ch == 'z') { undoManager.undo(); return true; }
        if (ctrl && shift && ch == 'z')  { undoManager.redo(); return true; }

        if (ctrl && !alt && ch == 'a')
        {
            allItemsSelected = !allItemsSelected;
            for (auto* r : trackRows)
                r->setSelectionState(r == selectedRow, selectedAudioClip, selectedMidiClip, allItemsSelected);
            return true;
        }

        if ((!ctrl && !alt && shift && ch == 's') || (!ctrl && !alt && !shift && ch == 's'))
        {
            splitSelectedOrAtPlayhead();
            return true;
        }

        if (key == juce::KeyPress::F2Key)
        {
            openSelectedItemProperties();
            return true;
        }

        if (!ctrl && !alt && !shift && ch == 't')
        {
            if (selectedRow != nullptr && selectedAudioClip != nullptr)
            {
                {
                    const juce::ScopedLock sl(trackLock);
                    selectedAudioClip->cycleNextTake(audioFormatManager);
                }
                selectedRow->rebuildThumbnails();
            }
            return true;
        }

        if (ctrl && shift && ch == 'k') { analyzeSelectedClipPeak(); return true; }
        if (ctrl && shift && ch == 'n') { normalizeSelectedClip(); return true; }

        if (key == juce::KeyPress::homeKey) { seekTo(0.0); return true; }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelectedClip(); return true; }

        return false;
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        bool ctrl = e.mods.isCtrlDown();
        bool alt  = e.mods.isAltDown();

        if (ctrl && alt)
        {
            auto pos = tracksViewport.getViewPosition();
            tracksViewport.setViewPosition(pos.x, juce::jmax(0, pos.y - (int)(wheel.deltaY * 120.0f)));
            return;
        }
        if (!ctrl && alt)
        {
            auto pos = tracksViewport.getViewPosition();
            tracksViewport.setViewPosition(juce::jmax(0, pos.x - (int)(wheel.deltaY * 150.0f)), pos.y);
            return;
        }
        if (ctrl && !alt)
        {
            changeVZoom(wheel.deltaY > 0 ? 12 : -12);
            return;
        }
        if (!ctrl && !alt)
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
        if (masterTrack != nullptr)
            masterTrack->prepareToPlay(samplesPerBlockExpected, sampleRate);

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

        if (masterTrack != nullptr)
        {
            juce::MidiBuffer emptyMidi;
            for (auto* vst : masterTrack->getVstPlugins())
                vst->processBlock(*bufferToFill.buffer, emptyMidi);
            for (auto* fx : masterTrack->getNativeEffects())
                fx->process(*bufferToFill.buffer);
        }

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
        if (masterTrack != nullptr)
            masterTrack->releaseResources();
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

            juce::String flagsText = "Grade:" + juce::String(showGridLines ? "ON" : "OFF") +
                                     " | XFade:" + juce::String(autoCrossfadeEnabled ? "ON" : "OFF") +
                                     " | Lock:" + juce::String(itemsLocked ? "ON" : "OFF");
            g.setColour(juce::Colours::lightgrey);
            g.setFont(10.5f);
            g.drawText(flagsText, regionArea.getX() + 8, regionArea.getY(), 252, regionArea.getHeight(), juce::Justification::centredLeft, true);

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

        if (tracks.isEmpty())
        {
            g.setColour(juce::Colours::grey);
            g.setFont(15.0f);
            g.drawText("Projeto Limpo - Clique em '+ Faixa' ou pressione Ctrl+T para adicionar uma faixa",
                       tracksViewport.getBounds(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        if (welcomeOverlay != nullptr)
        {
            welcomeOverlay->setBounds(area);
            welcomeOverlay->toFront(false);
        }
        scanOverlay.setBounds(area);
        scanOverlay.toFront(false);

        auto topBar = area.removeFromTop(48);

        timecodeLabel.setBounds(16, 8, 175, 32);
        statusStateLabel.setBounds(192, 12, 68, 24);

        auto controls = topBar.withTrimmedLeft(272).reduced(4, 9);

        rewindBtn.setBounds(controls.removeFromLeft(34).reduced(1));
        playBtn.setBounds(controls.removeFromLeft(50).reduced(1));
        stopBtn.setBounds(controls.removeFromLeft(50).reduced(1));
        recBtn.setBounds(controls.removeFromLeft(46).reduced(1));

        controls.removeFromLeft(6);
        clickBtn.setBounds(controls.removeFromLeft(54).reduced(1));
        snapBtn.setBounds(controls.removeFromLeft(48).reduced(1));
        gridDivBox.setBounds(controls.removeFromLeft(54).reduced(1));
        masterFxBtn.setBounds(controls.removeFromLeft(78).reduced(1));
        actionsBtn.setBounds(controls.removeFromLeft(68).reduced(1));
        mixerToggleBtn.setBounds(controls.removeFromLeft(88).reduced(1));

        audioSettingsBtn.setBounds(controls.removeFromRight(48).reduced(1));
        bounceBtn.setBounds(controls.removeFromRight(58).reduced(1));
        openProjBtn.setBounds(controls.removeFromRight(48).reduced(1));
        saveProjBtn.setBounds(controls.removeFromRight(50).reduced(1));
        addTrackBtn.setBounds(controls.removeFromRight(66).reduced(1));
        controls.removeFromRight(4);
        bpmSlider.setBounds(controls.removeFromRight(80).reduced(1));
        bpmLabel.setBounds(controls.removeFromRight(30));

        regionArea = area.removeFromTop(22).reduced(8, 0);
        rulerArea  = area.removeFromTop(22).reduced(8, 0);

        if (showBottomMixer)
            bottomMixer.setBounds(area.removeFromBottom(185).reduced(8, 2));

        tracksViewport.setBounds(area.reduced(8, 2));
        layoutTracks();
    }

private:
    juce::Label timecodeLabel, statusStateLabel, bpmLabel;
    juce::TextButton rewindBtn, playBtn, stopBtn, recBtn, clickBtn, snapBtn, masterFxBtn, actionsBtn, mixerToggleBtn;
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
    std::unique_ptr<AudioTrack> masterTrack;
    BottomMixerDock bottomMixer { masterGainAtomic, masterPeakAtomic };

    std::unique_ptr<WelcomeOverlayComponent> welcomeOverlay;
    std::unique_ptr<ActionListWindow> actionListWindow;
    PluginScanOverlay scanOverlay;
    MetronomeEngine metronome;
    PluginScannerEngine pluginScanner;
    juce::AudioFormatManager audioFormatManager;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    juce::TimeSliceThread backgroundThread { "MultiTrack Disk Thread" };
    juce::UndoManager undoManager;

    juce::CriticalSection trackLock;
    juce::OwnedArray<AudioTrack> tracks;
    juce::OwnedArray<TrackRowComponent> trackRows;
    std::unique_ptr<PianoRollWindow> pianoRollWindow;
    std::unique_ptr<TrackRackWindow> trackRackWindow;

    TrackRowComponent* selectedRow = nullptr;
    AudioClip* selectedAudioClip = nullptr;
    MidiClip* selectedMidiClip = nullptr;
    bool allItemsSelected = false;

    bool isPlaying = false;
    bool isRecording = false;
    bool showGridLines = true;
    bool autoCrossfadeEnabled = true;
    bool itemsLocked = false;
    double globalTimelineSeconds = 0.0;
    std::atomic<double> currentBpmAtomic { 120.0 };
    double pixelsPerSecond = 55.0;
    int trackRowHeight = 64;
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

    void createNewCleanProject()
    {
        stopTransport();
        trackRackWindow.reset();
        pianoRollWindow.reset();
        selectedRow = nullptr;
        selectedAudioClip = nullptr;
        selectedMidiClip = nullptr;
        allItemsSelected = false;

        {
            const juce::ScopedLock sl(trackLock);
            tracks.clear(true);
        }
        trackRows.clear(true);
        regions.clear();
        globalTimelineSeconds = 0.0;
        layoutTracks();
        rebuildMixerDock();
        repaint();
    }

    void deleteTrackAtIndex(int idx)
    {
        stopTransport();
        trackRackWindow.reset();
        selectedRow = nullptr;
        selectedAudioClip = nullptr;
        selectedMidiClip = nullptr;

        {
            const juce::ScopedLock sl(trackLock);
            if (idx >= 0 && idx < tracks.size())
                tracks.remove(idx, true);
        }
        rebuildTrackRowsFromModel();
        rebuildMixerDock();
        repaint();
    }

    void rebuildTrackRowsFromModel()
    {
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
                [this](TrackRowComponent* r, AudioClip* ac, MidiClip* mc) { handleSelection(r, ac, mc); },
                [this](int delIdx) { deleteTrackAtIndex(delIdx); }
            ));
            row->onTrackStateChanged = [this] { bottomMixer.refreshMetersAndState(); };
            row->setPixelsPerSecond(pixelsPerSecond);
            row->setGridSettings(bpmSlider.getValue(), snapBtn.getToggleState(), gridDivBox.getSelectedId(), showGridLines, itemsLocked);
            row->rebuildThumbnails();
            row->syncButtonsFromTrack();
            tracksContainer.addAndMakeVisible(row);
        }
        layoutTracks();
    }

    void openActionListWindow()
    {
        std::vector<ActionListWindow::ActionItem> items = {
            { "Ctrl + N",         "Novo projeto limpo",                 [this] { createNewCleanProject(); } },
            { "Ctrl + O",         "Abrir projeto salvo",                [this] { openProjectDialog(); } },
            { "Ctrl + S",         "Salvar projeto",                     [this] { saveProjectDialog(); } },
            { "Ctrl + Alt + R",   "Renderizar mixagem final",           [this] { exportProjectDialog(); } },
            { "Ctrl + P",         "Preferencias de Audio / ASIO",       [this] { showAudioSettings(); } },
            { "Ctrl + M",         "Mostrar / Ocultar Mixer",            [this] { mixerToggleBtn.triggerClick(); } },
            { "Ctrl + T",         "Criar nova faixa (Track)",           [this] { addNewTrack(); } },
            { "Espaco",           "Play / Stop",                        [this] { if (isPlaying) stopTransport(); else startPlayback(); } },
            { "R",                "Gravar",                             [this] { startRecording(); } },
            { "Alt + S",          "Ativar / desativar Snap (grade)",    [this] { snapBtn.triggerClick(); } },
            { "Alt + G",          "Ativar / desativar linhas de grade", [this] { showGridLines = !showGridLines; updateGridParameters(); } },
            { "Alt + X",          "Ativar / desativar crossfade auto",  [this] { autoCrossfadeEnabled = !autoCrossfadeEnabled; repaint(); } },
            { "L",                "Ativar / desativar bloqueio (Lock)", [this] { itemsLocked = !itemsLocked; updateGridParameters(); } },
            { "Ctrl + A",         "Selecionar todos os itens",          [this] { allItemsSelected = true; repaint(); } },
            { "Shift + S",        "Dividir itens na agulha (Split)",    [this] { splitSelectedOrAtPlayhead(); } },
            { "F2",               "Propriedades do item selecionado",   [this] { openSelectedItemProperties(); } },
            { "T",                "Ciclar entre takes do item",         [this] { if (selectedRow && selectedAudioClip) { selectedAudioClip->cycleNextTake(audioFormatManager); selectedRow->rebuildThumbnails(); } } },
            { "Ctrl + Shift + K", "Analisar picos do item",             [this] { analyzeSelectedClipPeak(); } },
            { "Ctrl + Shift + N", "Normalizar volume do item",          [this] { normalizeSelectedClip(); } }
        };
        actionListWindow = std::make_unique<ActionListWindow>(std::move(items));
    }

    void openSelectedItemProperties()
    {
        if (selectedAudioClip == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Propriedades do Item [F2]", "Selecione um clipe de audio na timeline primeiro.");
            return;
        }

        juce::String info = "Arquivo: " + selectedAudioClip->audioFile.getFileName() +
                            "\nInicio: " + juce::String(selectedAudioClip->timelineStart, 2) + " s" +
                            "\nDuracao: " + juce::String(selectedAudioClip->duration, 2) + " s" +
                            "\nGanho do Item: " + juce::String(juce::Decibels::gainToDecibels(selectedAudioClip->clipGain), 1) + " dB" +
                            "\nTakes Disponiveis: " + juce::String(selectedAudioClip->takes.size());
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Propriedades do Item [F2]", info);
    }

    void analyzeSelectedClipPeak()
    {
        if (selectedAudioClip == nullptr) return;
        float peak = selectedAudioClip->analyzePeakLevel(audioFormatManager);
        float db = juce::Decibels::gainToDecibels(peak + 1e-6f);
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            "Analise de Pico [Ctrl+Shift+K]",
            "Pico maximo detectado: " + juce::String(db, 2) + " dBFS (" + juce::String(peak * 100.0f, 1) + "%)");
    }

    void normalizeSelectedClip()
    {
        if (selectedAudioClip == nullptr || selectedRow == nullptr) return;
        {
            const juce::ScopedLock sl(trackLock);
            selectedAudioClip->normalizeClip(audioFormatManager);
        }
        selectedRow->repaint();
    }

    void handleSelection(TrackRowComponent* row, AudioClip* ac, MidiClip* mc)
    {
        selectedRow = row;
        selectedAudioClip = ac;
        selectedMidiClip = mc;
        allItemsSelected = false;

        for (auto* r : trackRows)
            r->setSelectionState(r == row, ac, mc, false);
    }

    void syncAllTrackViews()
    {
        for (auto* r : trackRows)
            r->syncButtonsFromTrack();
        bottomMixer.refreshMetersAndState();
        masterFxBtn.setColour(juce::TextButton::buttonColourId,
                              (masterTrack && masterTrack->hasActiveEffects()) ? juce::Colour(0xff2b9348) : juce::Colour(0xff5e548e));
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
            row->setGridSettings(bpm, snap, div, showGridLines, itemsLocked);
        repaint();
    }

    void addNewTrack()
    {
        const juce::ScopedLock sl(trackLock);
        int trackIndex = tracks.size();
        auto* newTrack = tracks.add(new AudioTrack("Track " + juce::String(trackIndex + 1), audioFormatManager, backgroundThread));
        newTrack->prepareToPlay(currentBlockSize, currentSampleRate);

        rebuildTrackRowsFromModel();
        rebuildMixerDock();
        repaint();
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
        int totalWidth  = juce::jmax(tracksViewport.getWidth() - 12, (int)(300.0 * pixelsPerSecond) + 300);
        tracksContainer.setSize(totalWidth, totalHeight);

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
            if (i < trackRows.size())
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
        if (itemsLocked) return;
        bool anySplit = false;
        double xfade = autoCrossfadeEnabled ? 0.015 : 0.0;
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
                    rightClip->fadeInDuration    = xfade;

                    selectedAudioClip->duration        = leftDur;
                    selectedAudioClip->fadeOutDuration = xfade;

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

    void deleteSelectedClip()
    {
        if (selectedRow == nullptr || itemsLocked) return;

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
        layoutTracks();
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

        if (scanOverlay.isVisible())
            scanOverlay.repaint();

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
                        rebuildTrackRowsFromModel();
                        rebuildMixerDock();
                        repaint();
                    }
                }
            });
    }

    void exportProjectDialog()
    {
        stopTransport();

        auto chooser = std::make_shared<juce::FileChooser>("Renderizar Mixagem Final (MP3, WAV ou FLAC)",
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
                        ok ? "Renderizacao Concluida" : "Aviso na Renderizacao",
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
        o.dialogTitle = "Preferencias de Placa de Som (ASIO / WASAPI)";
        o.dialogBackgroundColour = juce::Colour(0xff1e1e24);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.launchAsync();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
