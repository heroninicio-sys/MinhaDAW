#pragma once
#include <JuceHeader.h>
#include "AudioTrack.h"
#include "TimelineComponent.h"
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
        pluginFormatManager.addDefaultFormats();

        setupButton(rewindBtn, "|<",   [this] { seekTo(0.0); });
        setupButton(playBtn,   "PLAY", [this] { startPlayback(); }, juce::Colours::forestgreen);
        setupButton(stopBtn,   "STOP", [this] { stopTransport(); });
        setupButton(recBtn,    "REC",  [this] { startRecording(); }, juce::Colours::darkred);

        addAndMakeVisible(timecodeLabel);
        timecodeLabel.setText("1.1 | 00:00.0", juce::dontSendNotification);
        timecodeLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        timecodeLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
        timecodeLabel.setJustificationType(juce::Justification::centred);

        addAndMakeVisible(bpmSlider);
        bpmSlider.setRange(40.0, 240.0, 1.0);
        bpmSlider.setValue(120.0);
        bpmSlider.setSliderStyle(juce::Slider::IncDecButtons);
        bpmSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 26);
        bpmSlider.onValueChange = [this] { updateGridParameters(); };

        addAndMakeVisible(bpmLabel);
        bpmLabel.setText("BPM", juce::dontSendNotification);
        bpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        setupButton(snapBtn, "SNAP", [this] { updateGridParameters(); }, juce::Colour(0xff2c2c38));
        snapBtn.setClickingTogglesState(true);
        snapBtn.setToggleState(true, juce::dontSendNotification);
        snapBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff007acc));

        addAndMakeVisible(gridDivBox);
        gridDivBox.addItem("1/4", 1);
        gridDivBox.addItem("1/8", 2);
        gridDivBox.addItem("1/16", 4);
        gridDivBox.setSelectedId(1, juce::dontSendNotification);
        gridDivBox.onChange = [this] { updateGridParameters(); };

        setupButton(splitBtn, "Cortar [S]", [this] { splitAllAtPlayhead(); }, juce::Colour(0xff5c4b7d));
        setupButton(hZoomOut, "H-",         [this] { changeHZoom(0.8); });
        setupButton(hZoomIn,  "H+",         [this] { changeHZoom(1.25); });
        setupButton(vZoomOut, "V-",         [this] { changeVZoom(-14); });
        setupButton(vZoomIn,  "V+",         [this] { changeVZoom(14); });

        setupButton(addTrackBtn,      "+ Pista",  [this] { addNewTrack(); }, juce::Colour(0xff3a506b));
        setupButton(saveProjBtn,      "Salvar",   [this] { saveProjectDialog(); });
        setupButton(openProjBtn,      "Abrir",    [this] { openProjectDialog(); });
        setupButton(bounceBtn,        "Exportar", [this] { exportProjectDialog(); }, juce::Colour(0xff2e6f40));
        setupButton(audioSettingsBtn, "ASIO",     [this] { showAudioSettings(); });

        addAndMakeVisible(tracksViewport);
        tracksViewport.setViewedComponent(&tracksContainer, false);
        tracksViewport.setScrollBarsShown(true, false);

        setWantsKeyboardFocus(true);

        for (int i = 0; i < 4; ++i)
            addNewTrack();

        setAudioChannels(2, 2);
        setSize(1340, 680);
        startTimerHz(30);
    }

    ~MainComponent() override
    {
        stopTimer();
        trackRackWindow.reset();
        pianoRollWindow.reset();
        stopTransport();
        shutdownAudio();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey)
        {
            if (isPlaying) stopTransport();
            else startPlayback();
            return true;
        }
        if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
        {
            splitAllAtPlayhead();
            return true;
        }
        return false;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (rulerArea.contains(e.getPosition()) && e.x >= 268)
        {
            double clickedSec = juce::jmax(0.0, (double)(e.x - 268) / pixelsPerSecond);
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
                                globalTimelineSeconds,
                                currentBpm,
                                isPlaying,
                                anySolo);
        }

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
        g.fillAll(juce::Colour(0xff141418));

        if (!rulerArea.isEmpty())
        {
            g.setColour(juce::Colour(0xff1f1f27));
            g.fillRect(rulerArea);
            g.setColour(juce::Colour(0xff383846));
            g.drawRect(rulerArea, 1);

            int timelineStartX = rulerArea.getX() + 260;
            g.setColour(juce::Colours::grey);
            g.setFont(11.0f);
            g.drawText("COMPASSOS (4/4)", rulerArea.getX() + 10, rulerArea.getY(), 240, rulerArea.getHeight(), juce::Justification::centredLeft);

            double bpm = bpmSlider.getValue();
            double barDurationSec = (60.0 / bpm) * 4.0;
            int totalBars = (int)((double)(rulerArea.getWidth() - 260) / (barDurationSec * pixelsPerSecond)) + 2;

            for (int bar = 0; bar < totalBars; ++bar)
            {
                int barX = timelineStartX + (int)std::round((double)bar * barDurationSec * pixelsPerSecond);
                if (barX > rulerArea.getRight()) break;

                g.setColour(juce::Colours::lightgrey);
                g.drawVerticalLine(barX, (float)rulerArea.getY() + 4.0f, (float)rulerArea.getBottom());
                g.drawText(juce::String(bar + 1), barX + 4, rulerArea.getY(), 35, rulerArea.getHeight(), juce::Justification::centredLeft);
            }

            float playheadX = (float)timelineStartX + (float)(globalTimelineSeconds * pixelsPerSecond);
            if (playheadX >= (float)timelineStartX && playheadX <= (float)rulerArea.getRight())
            {
                g.setColour(juce::Colours::cyan);
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
        auto bar = area.removeFromTop(48).reduced(6);

        rewindBtn.setBounds(bar.removeFromLeft(32).reduced(2));
        playBtn.setBounds(bar.removeFromLeft(52).reduced(2));
        stopBtn.setBounds(bar.removeFromLeft(52).reduced(2));
        recBtn.setBounds(bar.removeFromLeft(48).reduced(2));

        bar.removeFromLeft(4);
        timecodeLabel.setBounds(bar.removeFromLeft(110));

        bpmLabel.setBounds(bar.removeFromLeft(32));
        bpmSlider.setBounds(bar.removeFromLeft(82).reduced(2));

        bar.removeFromLeft(4);
        snapBtn.setBounds(bar.removeFromLeft(48).reduced(2));
        gridDivBox.setBounds(bar.removeFromLeft(62).reduced(2));

        bar.removeFromLeft(4);
        splitBtn.setBounds(bar.removeFromLeft(76).reduced(2));
        hZoomOut.setBounds(bar.removeFromLeft(28).reduced(2));
        hZoomIn.setBounds(bar.removeFromLeft(28).reduced(2));
        vZoomOut.setBounds(bar.removeFromLeft(28).reduced(2));
        vZoomIn.setBounds(bar.removeFromLeft(28).reduced(2));

        audioSettingsBtn.setBounds(bar.removeFromRight(50).reduced(2));
        bounceBtn.setBounds(bar.removeFromRight(72).reduced(2));
        openProjBtn.setBounds(bar.removeFromRight(54).reduced(2));
        saveProjBtn.setBounds(bar.removeFromRight(54).reduced(2));
        addTrackBtn.setBounds(bar.removeFromRight(68).reduced(2));

        rulerArea = area.removeFromTop(24).reduced(8, 0);
        tracksViewport.setBounds(area.reduced(8, 4));
        layoutTracks();
    }

private:
    juce::TextButton rewindBtn, playBtn, stopBtn, recBtn, snapBtn, splitBtn;
    juce::TextButton hZoomOut, hZoomIn, vZoomOut, vZoomIn;
    juce::TextButton addTrackBtn, saveProjBtn, openProjBtn, bounceBtn, audioSettingsBtn;
    juce::Label timecodeLabel, bpmLabel;
    juce::Slider bpmSlider;
    juce::ComboBox gridDivBox;
    juce::Rectangle<int> rulerArea;

    juce::Viewport tracksViewport;
    juce::Component tracksContainer;

    juce::AudioFormatManager audioFormatManager;
    juce::AudioPluginFormatManager pluginFormatManager;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    juce::TimeSliceThread backgroundThread { "MultiTrack Disk Thread" };

    juce::CriticalSection trackLock;
    juce::OwnedArray<AudioTrack> tracks;
    juce::OwnedArray<TrackRowComponent> trackRows;
    std::unique_ptr<PianoRollWindow> pianoRollWindow;
    std::unique_ptr<TrackRackWindow> trackRackWindow;

    bool isPlaying = false;
    bool isRecording = false;
    double globalTimelineSeconds = 0.0;
    std::atomic<double> currentBpmAtomic { 120.0 };
    double pixelsPerSecond = 50.0;
    int trackRowHeight = 82;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    void setupButton(juce::TextButton& btn, const juce::String& text,
                     std::function<void()> onClick, juce::Colour bg = juce::Colour(0xff2c2c38))
    {
        addAndMakeVisible(btn);
        btn.setButtonText(text);
        btn.setColour(juce::TextButton::buttonColourId, bg);
        btn.onClick = std::move(onClick);
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
        int trackNum = tracks.size() + 1;
        auto* newTrack = tracks.add(new AudioTrack("Pista " + juce::String(trackNum), audioFormatManager, backgroundThread));
        newTrack->prepareToPlay(currentBlockSize, currentSampleRate);

        auto* row = trackRows.add(new TrackRowComponent(
            *newTrack, audioFormatManager, thumbnailCache, trackLock,
            [this](AudioTrack& t) { loadPluginForTrack(t); },
            [this](AudioTrack& t, TrackRowComponent& r) { importAudioForTrack(t, r); },
            [this](AudioTrack& t, MidiClip& mc) { openPianoRoll(t, mc); },
            [this](double newPos) { seekTo(newPos); }
        ));
        row->setPixelsPerSecond(pixelsPerSecond);
        row->setGridSettings(bpmSlider.getValue(), snapBtn.getToggleState(), gridDivBox.getSelectedId());

        tracksContainer.addAndMakeVisible(row);
        layoutTracks();
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
        isPlaying = true;
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
        isRecording = true;
        isPlaying = true;
    }

    void stopTransport()
    {
        const juce::ScopedLock sl(trackLock);
        isPlaying = false;
        isRecording = false;

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
        for (auto* track : tracks)
            track->sendAllNotesOff();
        updatePlayheadUI();
    }

    void splitAllAtPlayhead()
    {
        bool anySplit = false;
        {
            const juce::ScopedLock sl(trackLock);
            for (auto* track : tracks)
                if (track->splitClipsAt(globalTimelineSeconds))
                    anySplit = true;
        }

        if (anySplit)
            for (auto* row : trackRows)
                row->rebuildThumbnails();
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
        trackRowHeight = juce::jlimit(64, 220, trackRowHeight + deltaPx);
        layoutTracks();
    }

    void updatePlayheadUI()
    {
        double bpm = bpmSlider.getValue();
        double beatSec = 60.0 / bpm;
        int totalBeats = (int)(globalTimelineSeconds / beatSec);
        int barNum = (totalBeats / 4) + 1;
        int beatInBar = (totalBeats % 4) + 1;

        int mins = (int)(globalTimelineSeconds / 60.0);
        double secs = std::fmod(globalTimelineSeconds, 60.0);

        timecodeLabel.setText(juce::String::formatted("%d.%d | %02d:%04.1f", barNum, beatInBar, mins, secs),
                              juce::dontSendNotification);

        for (auto* row : trackRows)
            row->setPlayheadPosition(globalTimelineSeconds);
        repaint(rulerArea);
    }

    void timerCallback() override
    {
        updatePlayheadUI();
    }

    void saveProjectDialog()
    {
        auto chooser = std::make_shared<juce::FileChooser>("Salvar Projeto (.dawproj)",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("MeuProjeto.dawproj"),
            "*.dawproj");

        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file != juce::File())
                {
                    bool ok = ProjectSerializer::saveProject(file, bpmSlider.getValue(), tracks, trackLock);
                    if (ok)
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                            "Projeto Salvo", "Sessao salva em:\n" + file.getFullPathName());
                }
            });
    }

    void openProjectDialog()
    {
        stopTransport();
        trackRackWindow.reset();
        pianoRollWindow.reset();

        auto chooser = std::make_shared<juce::FileChooser>("Abrir Projeto (.dawproj)",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory), "*.dawproj");

        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    double loadedBpm = 120.0;
                    if (ProjectSerializer::loadProject(file, loadedBpm, tracks, trackLock,
                                                       audioFormatManager, pluginFormatManager,
                                                       backgroundThread, currentSampleRate, currentBlockSize))
                    {
                        bpmSlider.setValue(loadedBpm, juce::sendNotification);
                        trackRows.clear(true);
                        for (auto* t : tracks)
                        {
                            auto* row = trackRows.add(new TrackRowComponent(
                                *t, audioFormatManager, thumbnailCache, trackLock,
                                [this](AudioTrack& tr) { loadPluginForTrack(tr); },
                                [this](AudioTrack& tr, TrackRowComponent& r) { importAudioForTrack(tr, r); },
                                [this](AudioTrack& tr, MidiClip& mc) { openPianoRoll(tr, mc); },
                                [this](double newPos) { seekTo(newPos); }
                            ));
                            row->setPixelsPerSecond(pixelsPerSecond);
                            row->setGridSettings(loadedBpm, snapBtn.getToggleState(), gridDivBox.getSelectedId());
                            row->rebuildThumbnails();
                            tracksContainer.addAndMakeVisible(row);
                        }
                        layoutTracks();
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

    void loadPluginForTrack(AudioTrack& track)
    {
        trackRackWindow = std::make_unique<TrackRackWindow>(
            track, trackLock,
            [this](AudioTrack& targetTrack, std::function<void()> onLoaded)
            {
                auto chooser = std::make_shared<juce::FileChooser>("Selecionar VST3 / VSTi",
                    juce::File("C:\\Program Files\\Common Files\\VST3"), "*.vst3");

                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this, chooser, &targetTrack, onLoaded](const juce::FileChooser& fc)
                    {
                        auto file = fc.getResult();
                        if (file.existsAsFile())
                        {
                            juce::OwnedArray<juce::PluginDescription> descriptions;
                            juce::VST3PluginFormat vst3Format;
                            vst3Format.findAllTypesForFile(descriptions, file.getFullPathName());

                            if (descriptions.size() > 0)
                            {
                                juce::String error;
                                auto instance = pluginFormatManager.createPluginInstance(*descriptions[0], currentSampleRate, currentBlockSize, error);
                                if (instance != nullptr)
                                {
                                    {
                                        const juce::ScopedLock sl(trackLock);
                                        targetTrack.vstFilePaths.add(file.getFullPathName());
                                        targetTrack.addVstPlugin(std::move(instance));
                                    }
                                    if (onLoaded) onLoaded();
                                }
                            }
                        }
                    });
            });
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