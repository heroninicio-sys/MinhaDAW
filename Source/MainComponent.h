#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>
#include "StudioLookAndFeel.h"
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

class MainComponent : public juce::Component,
                      private juce::AudioIODeviceCallback,
                      private juce::Timer
{
public:
    MainComponent()
    {
        setLookAndFeel(&proLookAndFeel);
        audioFormatManager.registerBasicFormats();

        masterTrack = std::make_unique<AudioTrack>("MASTER", audioFormatManager, backgroundThread);

        addAndMakeVisible(brandLabel);
        brandLabel.setText("MINHADAW  /  STUDIO", juce::dontSendNotification);
        brandLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        brandLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8da6bf));

        addAndMakeVisible(timecodeLabel);
        timecodeLabel.setText("1.1.00 / 0:00.000", juce::dontSendNotification);
        timecodeLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        timecodeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00f5d4));
        timecodeLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(statusStateLabel);
        statusStateLabel.setText("[Stopped]", juce::dontSendNotification);
        statusStateLabel.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        statusStateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        setupButton(rewindBtn, "|<",   "Inicio [Home]",        [this] { seekTo(0.0); });
        setupButton(playBtn,   "PLAY", "Play / Stop [Espaco]", [this]
        {
            if (playBtn.getToggleState()) startPlayback();
            else stopTransport();
        }, juce::Colour(0xff0b765c));
        playBtn.setClickingTogglesState(true);
        setupButton(stopBtn,   "STOP", "Stop [Espaco]",        [this] { stopTransport(); });
        setupButton(recBtn,    "REC",  "Gravar [R]",           [this]
        {
            if (recBtn.getToggleState()) startRecording();
            else stopTransport();
        }, juce::Colour(0xff9e2638));
        recBtn.setClickingTogglesState(true);

        setupButton(clickBtn,  "CLICK", "Metronomo",           [this] { metronome.enabled = clickBtn.getToggleState(); });
        clickBtn.setClickingTogglesState(true);
        clickBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe07a5f));

        setupButton(snapBtn,   "SNAP",  "Grade Magnetica [Alt+S]", [this] { updateGridParameters(); });
        snapBtn.setClickingTogglesState(true);
        snapBtn.setToggleState(true, juce::dontSendNotification);
        snapBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff07566a));

        addAndMakeVisible(gridDivBox);
        gridDivBox.addItem("1/4", 1);
        gridDivBox.addItem("1/8", 2);
        gridDivBox.addItem("1/16", 4);
        gridDivBox.setSelectedId(1, juce::dontSendNotification);
        gridDivBox.onChange = [this] { updateGridParameters(); };

        setupButton(masterFxBtn, "MASTER FX", "Efeitos no Canal Master", [this] { loadPluginForTrack(*masterTrack); }, juce::Colour(0xff57427f));
        setupButton(actionsBtn,  "Acoes [?]", "Action List [?]",         [this] { openActionListWindow(); });
        setupButton(mixerToggleBtn, "Mixer", "Mostrar/Ocultar Mixer [Ctrl+M]", [this]
        {
            showBottomMixer = !showBottomMixer;
            bottomMixer.setVisible(showBottomMixer);
            resized();
        });
        mixerToggleBtn.setClickingTogglesState(true);
        mixerToggleBtn.setToggleState(true, juce::dontSendNotification);
        mixerToggleBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff087c61));

        setupButton(out2ToggleBtn, "OUT 3/4", "Ativa/Desativa 2a Via de Monitoramento Simultaneo", [this]
        {
            out2EnabledAtomic.store(out2ToggleBtn.getToggleState());
        });
        out2ToggleBtn.setClickingTogglesState(true);
        out2ToggleBtn.setToggleState(true, juce::dontSendNotification);
        out2ToggleBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff096b8a));

        addAndMakeVisible(out2VolSlider);
        out2VolSlider.setRange(0.0, 1.5, 0.01);
        out2VolSlider.setValue(1.0, juce::dontSendNotification);
        out2VolSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        out2VolSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        out2VolSlider.onValueChange = [this]
        {
            float v = (float)out2VolSlider.getValue();
            out2GainAtomic.store(v);
            out2PctLabel.setText("V2:" + juce::String((int)std::round(v * 100.0f)) + "%", juce::dontSendNotification);
        };

        addAndMakeVisible(out2PctLabel);
        out2PctLabel.setText("V2:100%", juce::dontSendNotification);
        out2PctLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        out2PctLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00f5d4));

        addAndMakeVisible(bpmLabel);
        bpmLabel.setText("BPM", juce::dontSendNotification);
        bpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        addAndMakeVisible(bpmSlider);
        bpmSlider.setRange(40.0, 240.0, 1.0);
        bpmSlider.setValue(120.0);
        bpmSlider.setSliderStyle(juce::Slider::IncDecButtons);
        bpmSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 24);
        bpmSlider.onValueChange = [this] { updateGridParameters(); };

        setupButton(addTrackBtn,      "+ Faixa", "Nova Faixa [Ctrl+T]",     [this] { addNewTrack(); }, juce::Colour(0xff1c526d));
        setupButton(saveProjBtn,      "Salvar",  "Salvar Projeto [Ctrl+S]", [this] { saveProjectDialog(); });
        setupButton(openProjBtn,      "Abrir",   "Abrir Projeto [Ctrl+O]",  [this] { openProjectDialog(); });
        setupButton(bounceBtn,        "Render",  "Renderizar [Ctrl+Alt+R]", [this] { exportProjectDialog(); }, juce::Colour(0xff146b50));
        setupButton(audioSettingsBtn, "Prefs",   "Preferencias [Ctrl+P]",   [this] { showAudioSettings(); });

        addAndMakeVisible(tracksViewport);
        tracksViewport.setViewedComponent(&tracksContainer, false);
        tracksViewport.setScrollBarsShown(true, true);

        addAndMakeVisible(bottomMixer);

        welcomeOverlay = std::make_unique<WelcomeOverlayComponent>(
            [this]
            {
                createNewCleanProject();
                pluginScanner.startBackgroundAutoScan(false);
            },
            [this]
            {
                openProjectDialog();
                pluginScanner.startBackgroundAutoScan(false);
            },
            [this]
            {
                showAudioSettings();
                pluginScanner.startBackgroundAutoScan(false);
            }
        );
        addChildComponent(*welcomeOverlay);
        welcomeOverlay->setVisible(WelcomeOverlayComponent::shouldShowOnStartup());

        addChildComponent(scanOverlay);
        pluginScanner.attachOverlay(&scanOverlay);

        setWantsKeyboardFocus(true);
        setSize(1440, 860);
        startTimerHz(30);

        juce::Component::SafePointer<MainComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis]
        {
            if (safeThis == nullptr) return;
            juce::String audioError = safeThis->deviceManager.initialise(2, 4, nullptr, true);
            if (audioError.isNotEmpty())
            {
                safeThis->deviceManager.closeAudioDevice();
                audioError = safeThis->deviceManager.initialise(2, 2, nullptr, true);
            }
            if (audioError.isNotEmpty())
            {
                safeThis->deviceManager.closeAudioDevice();
                audioError = safeThis->deviceManager.initialise(1, 4, nullptr, true);
            }
            if (audioError.isNotEmpty())
            {
                safeThis->deviceManager.closeAudioDevice();
                audioError = safeThis->deviceManager.initialise(1, 2, nullptr, true);
            }
            if (audioError.isNotEmpty())
            {
                safeThis->deviceManager.closeAudioDevice();
                audioError = safeThis->deviceManager.initialise(0, 2, nullptr, true);
            }

            // Register even when initial device discovery fails: selecting a device
            // later in Preferences will then restart this callback automatically.
            safeThis->deviceManager.addAudioCallback(safeThis);
            if (audioError.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "Audio indisponivel", audioError);

            safeThis->rebuildMixerDock();
            if (safeThis->welcomeOverlay != nullptr && !safeThis->welcomeOverlay->isVisible())
                safeThis->pluginScanner.startBackgroundAutoScan(false);
        });
    }

    ~MainComponent() override
    {
        stopTimer();
        exportDialogWindow.reset();
        audioSettingsWindow.reset();
        deviceManager.removeAudioCallback(this);
        deviceManager.closeAudioDevice();
        actionListWindow.reset();
        trackRackWindow.reset();
        pianoRollWindow.reset();
        stopTransport();
        releaseResources();
        bottomMixer.clearStrips();
        setLookAndFeel(nullptr);
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

        if (!ctrl && !alt && !shift && ch == 'r') { if (isRecording.load()) stopTransport(); else startRecording(); return true; }
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

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        if (device == nullptr) return;
        prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());

        const auto activeOutputs = device->getActiveOutputChannels();
        const bool hasSecondaryBus = activeOutputs[2] && activeOutputs[3];
        const bool hasPrimaryBus = activeOutputs[0] && activeOutputs[1];
        const bool canToggleSecondaryBus = hasSecondaryBus && hasPrimaryBus;
        const int inputCount = device->getActiveInputChannels().countNumberOfSetBits();
        juce::Component::SafePointer<MainComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis, hasSecondaryBus, canToggleSecondaryBus, inputCount]
        {
            if (safeThis == nullptr) return;
            safeThis->recBtn.setEnabled(inputCount > 0);
            safeThis->recBtn.setTooltip(inputCount > 0 ? "Gravar audio [R]" : "Nenhuma entrada de audio ativa; configure uma entrada nas preferencias");
            safeThis->out2ToggleBtn.setEnabled(canToggleSecondaryBus);
            safeThis->out2ToggleBtn.setTooltip(canToggleSecondaryBus
                ? "Ativa/desativa a segunda via de monitoramento (saidas 3/4)"
                : (hasSecondaryBus ? "Saidas 3/4 sao a unica saida ativa; roteamento mantido ligado"
                                   : "Seu dispositivo oferece apenas duas saidas ativas"));
            if (!hasSecondaryBus)
            {
                safeThis->out2ToggleBtn.setToggleState(false, juce::dontSendNotification);
                safeThis->out2EnabledAtomic.store(false);
            }
            else if (!hasPrimaryBus)
            {
                safeThis->out2ToggleBtn.setToggleState(true, juce::dontSendNotification);
                safeThis->out2EnabledAtomic.store(true);
            }
        });
    }

    void audioDeviceStopped() override
    {
        releaseResources();
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        if (numOutputChannels <= 0 || outputChannelData == nullptr || numSamples <= 0)
            return;

        if (inputBuffer.getNumSamples() != numSamples)
            inputBuffer.setSize(2, numSamples, false, false, true);
        if (outputBuffer.getNumSamples() != numSamples)
            outputBuffer.setSize(2, numSamples, false, false, true);
        inputBuffer.clear();
        outputBuffer.clear();

        const int capturedChannels = juce::jmin(2, numInputChannels);
        for (int ch = 0; ch < capturedChannels; ++ch)
            if (inputChannelData != nullptr && inputChannelData[ch] != nullptr)
                inputBuffer.copyFrom(ch, 0, inputChannelData[ch], numSamples);

        juce::AudioSourceChannelInfo bufferToFill(&outputBuffer, 0, numSamples);
        renderAudioBlock(bufferToFill, capturedChannels);

        // The device may expose sparse channel masks (e.g. only outputs 3/4).
        // Clear every non-null output first, then copy the internal stereo mix.
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);

        if (numOutputChannels > 0 && outputChannelData[0] != nullptr)
            juce::FloatVectorOperations::copy(outputChannelData[0], outputBuffer.getReadPointer(0), numSamples);
        if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
            juce::FloatVectorOperations::copy(outputChannelData[1], outputBuffer.getReadPointer(1), numSamples);

        if (numOutputChannels > 3 && out2EnabledAtomic.load())
        {
            const float masterGain = masterGainAtomic.load();
            const float secondaryRatio = masterGain > 0.0001f ? out2GainAtomic.load() / masterGain : 0.0f;
            if (outputChannelData[2] != nullptr)
            {
                juce::FloatVectorOperations::copy(outputChannelData[2], outputBuffer.getReadPointer(0), numSamples);
                juce::FloatVectorOperations::multiply(outputChannelData[2], secondaryRatio, numSamples);
            }
            if (outputChannelData[3] != nullptr)
            {
                juce::FloatVectorOperations::copy(outputChannelData[3], outputBuffer.getReadPointer(1), numSamples);
                juce::FloatVectorOperations::multiply(outputChannelData[3], secondaryRatio, numSamples);
            }
        }
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate)
    {
        const double activeSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int activeBlockSize = samplesPerBlockExpected > 0 ? samplesPerBlockExpected : 128;
        currentSampleRate.store(activeSampleRate);
        currentBlockSize.store(activeBlockSize);
        metronome.prepare(activeSampleRate);
        backgroundThread.startThread();
        inputBuffer.setSize(2, activeBlockSize, false, true, true);
        outputBuffer.setSize(2, activeBlockSize, false, true, true);

        const juce::ScopedLock sl(trackLock);
        if (masterTrack != nullptr)
            masterTrack->prepareToPlay(activeBlockSize, activeSampleRate);

        for (auto* track : tracks)
            track->prepareToPlay(activeBlockSize, activeSampleRate);
        audioPrepared.store(true);
    }

    void releaseResources()
    {
        if (!audioPrepared.exchange(false))
            return;
        const juce::ScopedLock sl(trackLock);
        if (masterTrack != nullptr)
            masterTrack->releaseResources();
        for (auto* track : tracks)
            track->releaseResources();
    }

    void renderAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill, int numInputChannels)
    {
        bufferToFill.clearActiveBufferRegion();
        const int totalCh = bufferToFill.buffer->getNumChannels();

        const juce::ScopedLock sl(trackLock);

        bool anySolo = false;
        for (auto* track : tracks)
            if (track->isSoloed) { anySolo = true; break; }

        const double currentBpm = currentBpmAtomic.load();
        const double playheadSeconds = globalTimelineSeconds.load();
        const bool playingNow = isPlaying.load();

        for (auto* track : tracks)
        {
            track->processTrack(bufferToFill,
                                inputBuffer.getArrayOfReadPointers(),
                                numInputChannels,
                                playheadSeconds,
                                currentBpm,
                                playingNow,
                                anySolo);
        }

        metronome.process(*bufferToFill.buffer, playheadSeconds, currentBpm, playingNow);

        if (masterTrack != nullptr && totalCh >= 2)
        {
            juce::AudioBuffer<float> masterStereoView(bufferToFill.buffer->getArrayOfWritePointers(), 2,
                                                       bufferToFill.startSample, bufferToFill.numSamples);
            juce::MidiBuffer emptyMidi;
            for (int i = 0; i < masterTrack->getVstPlugins().size(); ++i)
            {
                auto* vst = masterTrack->getVstPlugins()[i];
                const bool isBypassed = masterTrack->isVstPluginBypassed(i);
                if (!isBypassed || vst->getBypassParameter() != nullptr)
                    vst->processBlock(masterStereoView, emptyMidi);
            }
            for (auto* fx : masterTrack->getNativeEffects())
                fx->process(masterStereoView);
        }

        const float mGain1 = masterGainAtomic.load();
        if (totalCh >= 1) bufferToFill.buffer->applyGain(0, bufferToFill.startSample, bufferToFill.numSamples, mGain1);
        if (totalCh >= 2) bufferToFill.buffer->applyGain(1, bufferToFill.startSample, bufferToFill.numSamples, mGain1);

        float masterPeak = 0.0f;
        for (int ch = 0; ch < juce::jmin(2, totalCh); ++ch)
            masterPeak = juce::jmax(masterPeak, bufferToFill.buffer->getMagnitude(ch,
                bufferToFill.startSample, bufferToFill.numSamples));

        const float previousPeak = masterPeakAtomic.load();
        masterPeakAtomic.store(masterPeak > previousPeak ? masterPeak : previousPeak * 0.88f);

        const double activeSampleRate = currentSampleRate.load();
        if (playingNow && activeSampleRate > 0.0)
            globalTimelineSeconds.store(playheadSeconds + ((double)bufferToFill.numSamples / activeSampleRate));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff080f19));

        juce::ColourGradient topGrad(juce::Colour(0xff172638), 0.0f, 0.0f,
                                     juce::Colour(0xff0c1623), 0.0f, 76.0f, false);
        g.setGradientFill(topGrad);
        g.fillRect(0, 0, getWidth(), 76);
        g.setColour(juce::Colour(0xff263d53));
        g.drawHorizontalLine(75, 0.0f, (float)getWidth());

        g.setColour(juce::Colour(0xff07111d));
        g.fillRoundedRectangle(8.0f, 23.0f, 256.0f, 46.0f, 8.0f);
        g.setColour(juce::Colour(0xff2a465e));
        g.drawRoundedRectangle(8.0f, 23.0f, 256.0f, 46.0f, 8.0f, 1.0f);
        g.setColour(juce::Colour(0xff16d8e8));
        g.fillRoundedRectangle(8.0f, 28.0f, 3.0f, 36.0f, 1.5f);

        int timelineStartX = regionArea.getX() + 264;
        if (!regionArea.isEmpty())
        {
            g.setColour(juce::Colour(0xff0d1927));
            g.fillRect(regionArea);
            g.setColour(juce::Colour(0xff294158));
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
            g.setColour(juce::Colour(0xff111e2d));
            g.fillRect(rulerArea);
            g.setColour(juce::Colour(0xff2c4962));
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

            float playheadX = (float)timelineStartX + (float)(globalTimelineSeconds.load() * pixelsPerSecond);
            if (playheadX >= (float)timelineStartX && playheadX <= (float)rulerArea.getRight())
            {
                g.setColour(juce::Colour(0xff16d8e8));
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
            g.drawText("Projeto Limpo - Clique em '+ Faixa' (Ctrl+T), arme o botao 'R' e pressione REC",
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

        auto topBar = area.removeFromTop(76);

        brandLabel.setBounds(14, 2, 240, 18);
        timecodeLabel.setBounds(18, 29, 184, 31);
        statusStateLabel.setBounds(202, 37, 58, 18);

        auto controls = topBar.withTrimmedLeft(272).reduced(4, 16);

        rewindBtn.setBounds(controls.removeFromLeft(30).reduced(1));
        playBtn.setBounds(controls.removeFromLeft(46).reduced(1));
        stopBtn.setBounds(controls.removeFromLeft(46).reduced(1));
        recBtn.setBounds(controls.removeFromLeft(42).reduced(1));

        controls.removeFromLeft(4);
        clickBtn.setBounds(controls.removeFromLeft(50).reduced(1));
        snapBtn.setBounds(controls.removeFromLeft(46).reduced(1));
        gridDivBox.setBounds(controls.removeFromLeft(52).reduced(1));
        masterFxBtn.setBounds(controls.removeFromLeft(74).reduced(1));
        actionsBtn.setBounds(controls.removeFromLeft(64).reduced(1));
        mixerToggleBtn.setBounds(controls.removeFromLeft(52).reduced(1));

        controls.removeFromLeft(4);
        out2ToggleBtn.setBounds(controls.removeFromLeft(64).reduced(1));
        out2VolSlider.setBounds(controls.removeFromLeft(54).reduced(1));
        out2PctLabel.setBounds(controls.removeFromLeft(48));

        audioSettingsBtn.setBounds(controls.removeFromRight(46).reduced(1));
        bounceBtn.setBounds(controls.removeFromRight(56).reduced(1));
        openProjBtn.setBounds(controls.removeFromRight(46).reduced(1));
        saveProjBtn.setBounds(controls.removeFromRight(48).reduced(1));
        addTrackBtn.setBounds(controls.removeFromRight(62).reduced(1));
        controls.removeFromRight(4);
        bpmSlider.setBounds(controls.removeFromRight(76).reduced(1));
        bpmLabel.setBounds(controls.removeFromRight(28));

        regionArea = area.removeFromTop(22).reduced(8, 0);
        rulerArea  = area.removeFromTop(28).reduced(8, 0);

        if (showBottomMixer)
            bottomMixer.setBounds(area.removeFromBottom(208).reduced(8, 2));

        tracksViewport.setBounds(area.reduced(8, 2));
        layoutTracks();
    }

private:
    class AudioSettingsWindow : public juce::DocumentWindow
    {
    public:
        AudioSettingsWindow(juce::AudioDeviceManager& manager, juce::LookAndFeel* lookAndFeel)
            : DocumentWindow("Preferencias de Audio - MinhaDAW Studio",
                             juce::Colour(0xff0b1420), DocumentWindow::allButtons)
        {
            setLookAndFeel(lookAndFeel);
            setUsingNativeTitleBar(true);
            auto* selector = new juce::AudioDeviceSelectorComponent(manager, 0, 256, 0, 256,
                                                                     true, true, true, false);
            selector->setSize(520, 460);
            setContentOwned(selector, true);
            setResizable(true, true);
            centreWithSize(540, 500);
            setVisible(true);
        }
        void closeButtonPressed() override { setVisible(false); }
    };

    ProStudioLookAndFeel proLookAndFeel;

    juce::Label brandLabel, timecodeLabel, statusStateLabel, bpmLabel, out2PctLabel;
    juce::TextButton rewindBtn, playBtn, stopBtn, recBtn, clickBtn, snapBtn, masterFxBtn, actionsBtn, mixerToggleBtn, out2ToggleBtn;
    juce::TextButton addTrackBtn, saveProjBtn, openProjBtn, bounceBtn, audioSettingsBtn;
    juce::Slider bpmSlider, out2VolSlider;
    juce::ComboBox gridDivBox;

    juce::Rectangle<int> regionArea, rulerArea;
    std::vector<TimelineRegion> regions;

    juce::Viewport tracksViewport;
    juce::Component tracksContainer;
    bool showBottomMixer = true;

    std::atomic<float> masterGainAtomic { 1.0f };
    std::atomic<float> masterPeakAtomic { 0.0f };
    std::atomic<float> out2GainAtomic { 1.0f };
    std::atomic<bool> out2EnabledAtomic { true };
    std::unique_ptr<AudioTrack> masterTrack;
    BottomMixerDock bottomMixer { masterGainAtomic, masterPeakAtomic };

    std::unique_ptr<WelcomeOverlayComponent> welcomeOverlay;
    std::unique_ptr<ActionListWindow> actionListWindow;
    PluginScanOverlay scanOverlay;
    MetronomeEngine metronome;
    PluginScannerEngine pluginScanner;
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager audioFormatManager;
    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> outputBuffer;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    juce::TimeSliceThread backgroundThread { "MultiTrack Disk Thread" };
    juce::UndoManager undoManager;

    juce::CriticalSection trackLock;
    juce::OwnedArray<AudioTrack> tracks;
    juce::OwnedArray<TrackRowComponent> trackRows;
    std::unique_ptr<PianoRollWindow> pianoRollWindow;
    std::unique_ptr<TrackRackWindow> trackRackWindow;
    std::unique_ptr<juce::DocumentWindow> exportDialogWindow;
    std::unique_ptr<AudioSettingsWindow> audioSettingsWindow;

    TrackRowComponent* selectedRow = nullptr;
    AudioClip* selectedAudioClip = nullptr;
    MidiClip* selectedMidiClip = nullptr;
    bool allItemsSelected = false;

    std::atomic<bool> isPlaying { false };
    std::atomic<bool> isRecording { false };
    bool showGridLines = true;
    bool autoCrossfadeEnabled = true;
    bool itemsLocked = false;
    std::atomic<double> globalTimelineSeconds { 0.0 };
    std::atomic<double> currentBpmAtomic { 120.0 };
    double pixelsPerSecond = 55.0;
    int trackRowHeight = 84;
    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<int> currentBlockSize { 128 };
    std::atomic<bool> audioPrepared { false };

    void setupButton(juce::TextButton& btn, const juce::String& text, const juce::String& tooltip,
                     std::function<void()> onClick, juce::Colour bg = juce::Colour(0xff294158))
    {
        addAndMakeVisible(btn);
        btn.setButtonText(text);
        btn.setTooltip(tooltip);
        btn.setColour(juce::TextButton::buttonColourId, bg);
        btn.onClick = std::move(onClick);
    }

    double calculateRoundTripLatencySeconds()
    {
        if (auto* dev = deviceManager.getCurrentAudioDevice())
        {
            double sr = dev->getCurrentSampleRate();
            if (sr > 0.0)
            {
                int totalLatencySamples = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples();
                return (double)totalLatencySamples / sr;
            }
        }
        return 0.0;
    }

    void createNewCleanProject()
    {
        stopTransport();
        exportDialogWindow.reset();
        trackRackWindow.reset();
        pianoRollWindow.reset();
        selectedRow = nullptr;
        selectedAudioClip = nullptr;
        selectedMidiClip = nullptr;
        allItemsSelected = false;

        trackRows.clear(true);
        bottomMixer.clearStrips();
        {
            const juce::ScopedLock sl(trackLock);
            tracks.clear(true);
            if (masterTrack != nullptr)
            {
                masterTrack->getNativeEffects().clear(true);
                masterTrack->clearVstPlugins();
            }
        }
        regions.clear();
        globalTimelineSeconds.store(0.0);
        masterGainAtomic.store(1.0f);
        bpmSlider.setValue(120.0, juce::sendNotification);
        layoutTracks();
        rebuildMixerDock();
        syncAllTrackViews();
        repaint();
    }

    void deleteTrackAtIndex(int idx)
    {
        stopTransport();
        trackRackWindow.reset();
        pianoRollWindow.reset();
        selectedRow = nullptr;
        selectedAudioClip = nullptr;
        selectedMidiClip = nullptr;

        trackRows.clear(true);
        bottomMixer.clearStrips();
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
            { "R",                "Gravar / Parar gravacao",             [this] { if (isRecording.load()) stopTransport(); else startRecording(); } },
            { "Alt + S",          "Ativar / desativar Snap (grade)",    [this] { snapBtn.triggerClick(); } },
            { "Alt + G",          "Ativar / desativar linhas de grade", [this] { showGridLines = !showGridLines; updateGridParameters(); } },
            { "Alt + X",          "Ativar / desativar crossfade auto",  [this] { autoCrossfadeEnabled = !autoCrossfadeEnabled; repaint(); } },
            { "L",                "Ativar / desativar bloqueio (Lock)", [this] { itemsLocked = !itemsLocked; updateGridParameters(); } },
            { "Ctrl + A",         "Selecionar todos os itens",          [this] { allItemsSelected = true; repaint(); } },
            { "Shift + S",        "Dividir itens na agulha (Split)",    [this] { splitSelectedOrAtPlayhead(); } },
            { "F2",               "Propriedades do item selecionado",   [this] { openSelectedItemProperties(); } },
            { "T",                "Ciclar entre takes do item",         [this]
                {
                    if (selectedRow && selectedAudioClip)
                    {
                        const juce::ScopedLock sl(trackLock);
                        selectedAudioClip->cycleNextTake(audioFormatManager);
                        selectedRow->rebuildThumbnails();
                    }
                } },
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
                              (masterTrack && masterTrack->hasActiveEffects()) ? juce::Colour(0xff087c61) : juce::Colour(0xff57427f));
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
        newTrack->prepareToPlay(currentBlockSize.load(), currentSampleRate.load());

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
        int totalHeight = juce::jmax(0, juce::jmax(tracksViewport.getHeight(), trackRows.size() * trackRowHeight));
        int totalWidth  = juce::jmax(0, juce::jmax(tracksViewport.getWidth() - 12, (int)(300.0 * pixelsPerSecond) + 300));
        tracksContainer.setSize(totalWidth, totalHeight);

        for (int i = 0; i < trackRows.size(); ++i)
            trackRows[i]->setBounds(0, i * trackRowHeight, tracksContainer.getWidth(), trackRowHeight);
    }

    void startPlayback()
    {
        const juce::ScopedLock sl(trackLock);
        metronome.reset();
        isRecording.store(false);
        recBtn.setToggleState(false, juce::dontSendNotification);
        isPlaying.store(true);
        playBtn.setToggleState(true, juce::dontSendNotification);
        statusStateLabel.setText("[Playing]", juce::dontSendNotification);
        statusStateLabel.setColour(juce::Label::textColourId, juce::Colour(0xff18c58c));
    }

    void startRecording()
    {
        auto* activeDevice = deviceManager.getCurrentAudioDevice();
        const int availableInputs = activeDevice != nullptr
            ? activeDevice->getActiveInputChannels().countNumberOfSetBits() : 0;
        if (availableInputs <= 0)
        {
            recBtn.setToggleState(false, juce::dontSendNotification);
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Entrada de audio ausente", "Selecione uma entrada de audio nas preferencias antes de gravar.");
            return;
        }

        if (tracks.isEmpty())
            addNewTrack();

        bool anyArmed = false;
        for (auto* t : tracks)
            if (t->isArmed) { anyArmed = true; break; }

        if (!anyArmed)
        {
            if (selectedRow != nullptr)
                selectedRow->getTrack().isArmed = true;
            else
                tracks[0]->isArmed = true;

            for (auto* r : trackRows)
                r->syncButtonsFromTrack();
        }

        double rtlSec = calculateRoundTripLatencySeconds();
        const juce::ScopedLock sl(trackLock);
        auto recDir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("MinhaDAW_Gravacoes");
        recDir.createDirectory();

        bool startedAnyTrack = false;
        for (auto* track : tracks)
        {
            if (track->isArmed)
            {
                auto file = recDir.getNonexistentChildFile(track->getName() + "_Take", ".wav");
                startedAnyTrack = track->startRecording(file, globalTimelineSeconds.load(), rtlSec) || startedAnyTrack;
            }
        }

        if (!startedAnyTrack)
        {
            isRecording.store(false);
            isPlaying.store(false);
            recBtn.setToggleState(false, juce::dontSendNotification);
            playBtn.setToggleState(false, juce::dontSendNotification);
            statusStateLabel.setText("[Record Error]", juce::dontSendNotification);
            statusStateLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffb86b));
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Falha na gravacao", "Nao foi possivel iniciar a gravacao. Verifique a entrada de audio e a pasta de destino.");
            return;
        }

        metronome.reset();
        isRecording.store(true);
        isPlaying.store(true);
        recBtn.setToggleState(true, juce::dontSendNotification);
        playBtn.setToggleState(true, juce::dontSendNotification);
        statusStateLabel.setText("[Recording]", juce::dontSendNotification);
        statusStateLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe63946));
    }

    void stopTransport()
    {
        const juce::ScopedLock sl(trackLock);
        isPlaying.store(false);
        isRecording.store(false);
        playBtn.setToggleState(false, juce::dontSendNotification);
        recBtn.setToggleState(false, juce::dontSendNotification);
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
        repaint();
    }

    void seekTo(double seconds)
    {
        const juce::ScopedLock sl(trackLock);
        globalTimelineSeconds = juce::jmax(0.0, seconds);
        metronome.reset();
        for (auto* track : tracks)
            track->sendAllNotesOff();

        for (auto* row : trackRows)
            row->setPlayheadPosition(globalTimelineSeconds);
        repaint(rulerArea);
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
                    rightClip->prepareToPlay(currentBlockSize.load(), currentSampleRate.load());
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
        const double playheadSeconds = globalTimelineSeconds.load();
        double totalBeatsFloat = playheadSeconds / beatSec;
        int totalBeats = (int)totalBeatsFloat;
        int barNum = (totalBeats / 4) + 1;
        int beatInBar = (totalBeats % 4) + 1;
        int hundredths = (int)((totalBeatsFloat - totalBeats) * 100.0);

        int mins = (int)(playheadSeconds / 60.0);
        double secs = std::fmod(playheadSeconds, 60.0);

        timecodeLabel.setText(juce::String::formatted("%d.%d.%02d / %d:%06.3f", barNum, beatInBar, hundredths, mins, secs),
                              juce::dontSendNotification);

        if (isPlaying || isRecording)
        {
            for (auto* row : trackRows)
                row->setPlayheadPosition(playheadSeconds);

            if (showBottomMixer)
                bottomMixer.refreshMetersAndState();

            repaint(rulerArea);
        }

        if (scanOverlay.isVisible())
            scanOverlay.repaint();
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

        juce::Component::SafePointer<MainComponent> safeThis(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis, chooser](const juce::FileChooser& fc)
            {
                if (safeThis == nullptr) return;
                auto file = fc.getResult();
                if (file != juce::File())
                {
                    safeThis->stopTransport();
                    juce::File target = file.hasFileExtension(".dawproj") ? file : file.withFileExtension(".dawproj");
                    bool ok = ProjectSerializer::saveProject(target, safeThis->bpmSlider.getValue(), safeThis->regions,
                                                               safeThis->tracks, safeThis->trackLock,
                                                               safeThis->masterTrack.get(), safeThis->masterGainAtomic.load());
                    juce::AlertWindow::showMessageBoxAsync(
                        ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                        ok ? "Sessao Salva" : "Falha ao salvar",
                        ok ? "Sessao, faixas, plugins e master salvos em:\n" + target.getFullPathName()
                           : "Nao foi possivel salvar o projeto. Verifique o caminho e as permissoes de escrita.");
                }
            });
    }

    void openProjectDialog()
    {
        stopTransport();
        exportDialogWindow.reset();
        trackRackWindow.reset();
        pianoRollWindow.reset();
        selectedRow = nullptr;
        selectedAudioClip = nullptr;
        selectedMidiClip = nullptr;

        auto chooser = std::make_shared<juce::FileChooser>("Abrir Sessao (.dawproj)",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory), "*.dawproj");

        juce::Component::SafePointer<MainComponent> safeThis(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser](const juce::FileChooser& fc)
            {
                if (safeThis == nullptr) return;
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    double loadedBpm = 120.0;
                    float loadedMasterGain = 1.0f;
                    if (ProjectSerializer::loadProject(file, loadedBpm, safeThis->regions, safeThis->tracks, safeThis->trackLock,
                                                       safeThis->audioFormatManager, safeThis->pluginScanner.getFormatManager(),
                                                       safeThis->backgroundThread, safeThis->currentSampleRate.load(), safeThis->currentBlockSize.load(),
                                                       safeThis->masterTrack.get(), &loadedMasterGain))
                    {
                        safeThis->masterGainAtomic.store(loadedMasterGain);
                        safeThis->bpmSlider.setValue(loadedBpm, juce::sendNotification);
                        safeThis->rebuildTrackRowsFromModel();
                        safeThis->rebuildMixerDock();
                        safeThis->syncAllTrackViews();
                        safeThis->repaint();
                    }
                    else
                    {
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                            "Projeto invalido", "O arquivo nao possui uma estrutura de projeto MinhaDAW valida.");
                    }
                }
            });
    }

    void exportProjectDialog()
    {
        stopTransport();
        if (exportDialogWindow != nullptr && exportDialogWindow->isVisible())
        {
            exportDialogWindow->toFront(true);
            return;
        }
        // Recreate a dismissed render window so it snapshots the latest master gain.
        exportDialogWindow.reset();
        exportDialogWindow = AudioExporter::createExportDialogWindow(
            tracks, trackLock, currentSampleRate.load(), currentBlockSize.load(),
            bpmSlider.getValue(), masterTrack.get(), &masterGainAtomic, &proLookAndFeel);
    }

    void importAudioForTrack(AudioTrack& track, TrackRowComponent& row)
    {
        auto chooser = std::make_shared<juce::FileChooser>("Importar Audio (WAV/MP3/FLAC)",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.mp3;*.aiff;*.flac");

        auto* trackPtr = &track;
        auto* rowPtr = &row;
        juce::Component::SafePointer<MainComponent> safeThis(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser, trackPtr, rowPtr](const juce::FileChooser& fc)
            {
                if (safeThis == nullptr || !safeThis->tracks.contains(trackPtr) ||
                    !safeThis->trackRows.contains(rowPtr))
                    return;
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    {
                        const juce::ScopedLock sl(safeThis->trackLock);
                        trackPtr->addClipFromFile(file, safeThis->globalTimelineSeconds.load());
                    }
                    rowPtr->rebuildThumbnails();
                }
            });
    }

    void loadPluginFromDescription(AudioTrack& targetTrack, const juce::PluginDescription& desc, std::function<void()> onLoaded)
    {
        juce::String error;
        auto instance = pluginScanner.getFormatManager().createPluginInstance(desc, currentSampleRate.load(), currentBlockSize.load(), error);
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

        auto* targetTrackPtr = &targetTrack;
        juce::Component::SafePointer<MainComponent> safeThis(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::canSelectDirectories,
            [safeThis, chooser, targetTrackPtr, onLoaded](const juce::FileChooser& fc)
            {
                if (safeThis == nullptr || !safeThis->tracks.contains(targetTrackPtr))
                    return;
                auto file = fc.getResult();
                if (file.exists())
                {
                    juce::OwnedArray<juce::PluginDescription> descriptions;
                    auto& fmtMgr = safeThis->pluginScanner.getFormatManager();
                    for (int i = 0; i < fmtMgr.getNumFormats(); ++i)
                    {
                        fmtMgr.getFormat(i)->findAllTypesForFile(descriptions, file.getFullPathName());
                        if (descriptions.size() > 0) break;
                    }
                    if (descriptions.size() > 0 && safeThis->tracks.contains(targetTrackPtr))
                        safeThis->loadPluginFromDescription(*targetTrackPtr, *descriptions[0], onLoaded);
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
        // Device reconfiguration changes sample rate/block size; never leave a take half-open.
        stopTransport();
        if (audioSettingsWindow != nullptr)
        {
            audioSettingsWindow->setVisible(true);
            audioSettingsWindow->toFront(true);
            return;
        }
        audioSettingsWindow = std::make_unique<AudioSettingsWindow>(deviceManager, &proLookAndFeel);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
