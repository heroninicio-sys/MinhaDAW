#pragma once
#include <JuceHeader.h>
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

class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer
{
public:
    MainComponent()
    {
        setLookAndFeel(&proLookAndFeel);
        audioFormatManager.registerBasicFormats();

        masterTrack = std::make_unique<AudioTrack>("MASTER", audioFormatManager, backgroundThread);

        addAndMakeVisible(timecodeLabel);
        timecodeLabel.setText("1.1.00 / 0:00.000", juce::dontSendNotification);
        timecodeLabel.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        timecodeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00f5d4));
        timecodeLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(statusStateLabel);
        statusStateLabel.setText("[Stopped]", juce::dontSendNotification);
        statusStateLabel.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        statusStateLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        setupButton(rewindBtn, "|<",   "Inicio [Home]",        [this] { seekTo(0.0); });
        setupButton(playBtn,   "PLAY", "Play / Stop [Espaco]", [this] { startPlayback(); }, juce::Colour(0xff2b9348));
        setupButton(stopBtn,   "STOP", "Stop [Espaco]",        [this] { stopTransport(); });
        setupButton(recBtn,    "REC",  "Gravar [R]",           [this] { startRecording(); }, juce::Colour(0xffc1121f));

        setupButton(clickBtn,  "CLICK", "Metronomo",           [this] { metronome.enabled = clickBtn.getToggleState(); });
        clickBtn.setClickingTogglesState(true);
        clickBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe07a5f));

        setupButton(snapBtn,   "SNAP",  "Grade Magnetica [Alt+S]", [this] { updateGridParameters(); });
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
        setupButton(mixerToggleBtn, "Mixer", "Mostrar/Ocultar Mixer [Ctrl+M]", [this]
        {
            showBottomMixer = !showBottomMixer;
            bottomMixer.setVisible(showBottomMixer);
            resized();
        });
        mixerToggleBtn.setClickingTogglesState(true);
        mixerToggleBtn.setToggleState(true, juce::dontSendNotification);
        mixerToggleBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2b9348));

        setupButton(out2ToggleBtn, "OUT 3/4", "Ativa/Desativa 2a Via de Monitoramento Simultaneo", [this]
        {
            out2EnabledAtomic.store(out2ToggleBtn.getToggleState());
        });
        out2ToggleBtn.setClickingTogglesState(true);
        out2ToggleBtn.setToggleState(true, juce::dontSendNotification);
        out2ToggleBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0096c7));

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

        setupButton(addTrackBtn,      "+ Faixa", "Nova Faixa [Ctrl+T]",     [this] { addNewTrack(); }, juce::Colour(0xff3a506b));
        setupButton(saveProjBtn,      "Salvar",  "Salvar Projeto [Ctrl+S]", [this] { saveProjectDialog(); });
        setupButton(openProjBtn,      "Abrir",   "Abrir Projeto [Ctrl+O]",  [this] { openProjectDialog(); });
        setupButton(bounceBtn,        "Render",  "Renderizar [Ctrl+Alt+R]", [this] { exportProjectDialog(); }, juce::Colour(0xff2d6a4f));
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
        setSize(1366, 740);
        startTimerHz(30);

        juce::MessageManager::callAsync([this]
        {
            setAudioChannels(2, 4);
            rebuildMixerDock();

            if (!welcomeOverlay->isVisible())
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
        setLookAndFeel(nullptr);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        bool ctrl  = key.getModifiers().isCtrlDown();
        bool alt   = key.getModifiers().isAltDown();
        bool shift = key.getModifiers().isShiftDown();
        int kc     = key.getKeyCode();

        if (key.getTextCharacter() == '?' || key == juce::KeyPress::F4Key)
        {
            openActionListWindow();
            return true;
        }

        if (ctrl && !alt && !shift && ch == 'n') { createNewCleanProject(); return true; }
        if (ctrl && !alt && !shift && ch == 'o') { openProjectDialog(); return true; }
        if (ctrl && !alt && !shift && ch == 's') { saveProjectDialog(); return true; }
        if (ctrl && alt && !shift && ch == 'r') { exportProjectDialog(); return true; }
        if (!ctrl && alt && !shift && ch == 'r') { exportProjectDialog(); return true; }
        if (ctrl && alt && !shift && ch == 'b') { exportProjectDialog(); return true; }
        if (ctrl && !alt && !shift && ch == 'p') { showAudioSettings(); return true; }
        if (ctrl && !alt && !shift && ch == 'm') { mixerToggleBtn.triggerClick(); return true; }
        if (ctrl && shift && !alt && ch == 'm') { clickBtn.triggerClick(); return true; }

        if (key == juce::KeyPress::spaceKey)
        {
            if (isPlaying) stopTransport();
            else startPlayback();
            return true;
        }
        if (!ctrl && !alt && !shift && ch == 'r') { startRecording(); return true; }
        if (!ctrl && !alt && !shift && ch == 'w') { seekTo(0.0); return true; }
        if (ctrl && key == juce::KeyPress::homeKey) { seekTo(0.0); return true; }

        if (!ctrl && alt && !shift && ch == 's') { snapBtn.triggerClick(); return true; }
        if (!ctrl && alt && !shift && ch == 'g') { showGridLines = !showGridLines; updateGridParameters(); return true; }
        if (!ctrl && alt && !shift && ch == 'x') { autoCrossfadeEnabled = !autoCrossfadeEnabled; repaint(regionArea); return true; }
        if (!ctrl && alt && shift && ch == 'x') { autoCrossfadeEnabled = !autoCrossfadeEnabled; repaint(regionArea); return true; }
        if (!ctrl && !alt && !shift && ch == 'l') { itemsLocked = !itemsLocked; updateGridParameters(); return true; }
        if (!ctrl && !alt && !shift && ch == 'g') { snapBtn.triggerClick(); return true; }
        if (!ctrl && alt && !shift && ch == 'n') { snapBtn.triggerClick(); return true; }

        if (ctrl && shift && !alt)
        {
            if (key.getTextCharacter() == '1') { setGridDivisionAndSnap(1); return true; }
            if (key.getTextCharacter() == '2') { setGridDivisionAndSnap(2); return true; }
            if (key.getTextCharacter() == '3') { setGridDivisionAndSnap(8); return true; }
            if (key.getTextCharacter() == '4') { setGridDivisionAndSnap(1); return true; }
            if (key.getTextCharacter() == '5') { setGridDivisionAndSnap(3); return true; }
            if (key.getTextCharacter() == '6') { setGridDivisionAndSnap(4); return true; }
            if (key.getTextCharacter() == '7') { setGridDivisionAndSnap(6); return true; }
            if (key.getTextCharacter() == '8') { setGridDivisionAndSnap(2); return true; }
            if (key.getTextCharacter() == '9') { setGridDivisionAndSnap(3); return true; }
            if (key.getTextCharacter() == '0') { setGridDivisionAndSnap(12); return true; }
        }

        if (!ctrl && !alt && shift && ch == 'w')
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Automacao [Shift+W]", "Valores atuais gravados nas envelopes em writing para a selecao de tempo.");
            return true;
        }
        if (ctrl && !alt && !shift && ch == 'w')
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Automacao [Ctrl+W]", "Valores atuais gravados nas envelopes em writing do cursor ate o fim do projeto.");
            return true;
        }
        if (ctrl && alt && !shift && ch == 'p') { toggleEnvelopeFlag("Pan Envelope"); return true; }
        if (ctrl && alt && !shift && ch == 'v') { toggleEnvelopeFlag("Volume Envelope"); return true; }
        if (ctrl && alt && shift && ch == 'v')  { toggleEnvelopeFlag("Pre-FX Volume Envelope"); return true; }
        if (ctrl && alt && shift && ch == 'p')  { toggleEnvelopeFlag("Pre-FX Pan / Take Pitch Envelope"); return true; }
        if (ctrl && alt && !shift && key == juce::KeyPress::F5Key) { toggleEnvelopeFlag("Mute Envelope"); return true; }
        if (!ctrl && !alt && shift && ch == 'l') { toggleEnvelopeFlag("Visualizar Envelopes da Faixa Ativa"); return true; }
        if (!ctrl && alt && !shift && ch == 'l') { nudgeEnvelopePointSelection(1, false); return true; }
        if (!ctrl && alt && shift && ch == 'l')  { nudgeEnvelopePointSelection(-1, false); return true; }
        if (!ctrl && alt && shift && ch == 'e')  { insertOrEditEnvelopePointAtCursor(true); return true; }
        if (!ctrl && !alt && shift && ch == 'e') { insertOrEditEnvelopePointAtCursor(false); return true; }
        if (!ctrl && alt && !shift && ch == 'j') { nudgeEnvelopePointSelection(-1, false); return true; }
        if (!ctrl && alt && !shift && ch == 'k') { nudgeEnvelopePointSelection(1, false); return true; }
        if (!ctrl && alt && shift && ch == 'j')  { nudgeEnvelopePointSelection(-1, true); return true; }
        if (!ctrl && alt && shift && ch == 'k')  { nudgeEnvelopePointSelection(1, true); return true; }
        if (!ctrl && alt && (kc == juce::KeyPress::numberPad2 || key.getTextCharacter() == '2')) { nudgeSelectedClipGain(false); return true; }
        if (!ctrl && alt && (kc == juce::KeyPress::numberPad8 || key.getTextCharacter() == '8')) { nudgeSelectedClipGain(true); return true; }
        if (!ctrl && !alt && shift && ch == 'o') { toggleEnvelopeFlag("Mostrar Envelopes Ativos"); return true; }
        if (ctrl && shift && !alt && ch == 'i')  { toggleEnvelopeFlag("Inserir Item de Automacao"); return true; }
        if (ctrl && alt && !shift && key.getTextCharacter() == '4') { toggleEnvelopeFlag("4 Pontos de Envelope na Selecao de Tempo"); return true; }
        if (!ctrl && !alt && shift && ch == 'h') { toggleEnvelopeFlag("Ocultar Envelopes das Faixas"); return true; }
        if (ctrl && shift && !alt && ch == 'h')  { toggleEnvelopeFlag("Ocultar Envelopes de Todas as Faixas"); return true; }

        if (ctrl && !alt && !shift && ch == 'c') { copySelectedClip(false); return true; }
        if (!ctrl && !alt && shift && ch == 'c') { copySelectedClip(true); return true; }
        if (ctrl && !alt && !shift && ch == 'x') { cutSelectedClip(false); return true; }
        if (!ctrl && !alt && shift && ch == 'x') { cutSelectedClip(true); return true; }
        if (ctrl && !alt && !shift && ch == 'v') { pasteClipboardClip(); return true; }
        if (ctrl && !alt && !shift && ch == 'a')
        {
            allItemsSelected = !allItemsSelected;
            for (auto* r : trackRows)
                r->setSelectionState(r == selectedRow, selectedAudioClip, selectedMidiClip, allItemsSelected);
            return true;
        }
        if (ctrl && alt && !shift && ch == 'a')
        {
            allItemsSelected = true;
            if (selectedRow != nullptr)
                selectedRow->setSelectionState(true, selectedAudioClip, selectedMidiClip, true);
            return true;
        }

        if (ctrl && !alt && !shift && ch == 't') { addNewTrack(); return true; }
        if (ctrl && !shift && !alt && ch == 'z') { undoManager.undo(); return true; }
        if (ctrl && shift && !alt && ch == 'z')  { undoManager.redo(); return true; }
        if (ctrl && alt && !shift && ch == 'z')  { openActionListWindow(); return true; }

        if (!ctrl && !alt && !shift && ch == 't') { cycleTakeOnSelected(true); return true; }
        if (!ctrl && !alt && shift && ch == 't') { cycleTakeOnSelected(false); return true; }
        if (ctrl && !alt && !shift && ch == 'd') { duplicateSelectedClip(); return true; }
        if (ctrl && shift && !alt && ch == 'd')  { duplicateSelectedClip(); return true; }

        if ((!ctrl && !alt && shift && ch == 's') || (!ctrl && !alt && !shift && ch == 's'))
        {
            splitSelectedOrAtPlayhead();
            return true;
        }
        if (!ctrl && !alt && !shift && ch == 'a')
        {
            splitSelectedOrAtPlayhead();
            return true;
        }

        if (key == juce::KeyPress::F2Key) { openSelectedItemProperties(); return true; }
        if (ctrl && shift && !alt && ch == 'k') { analyzeSelectedClipPeak(); return true; }
        if (ctrl && shift && !alt && ch == 'n') { normalizeSelectedClip(); return true; }
        if (!ctrl && !alt && shift && ch == 'n') { normalizeSelectedClip(); return true; }

        if (!ctrl && !alt && !shift && ch == 'f' && selectedRow != nullptr) { loadPluginForTrack(selectedRow->getTrack()); return true; }
        if (!ctrl && !alt && shift && ch == 'f') { loadPluginForTrack(*masterTrack); return true; }
        if (ctrl && !alt && !shift && ch == 'f' && selectedRow != nullptr) { loadPluginForTrack(selectedRow->getTrack()); return true; }

        if (!ctrl && !alt && !shift && ch == 'm')
        {
            regions.push_back({ "Marker " + juce::String((int)regions.size() + 1), globalTimelineSeconds, globalTimelineSeconds + 2.0, juce::Colour(0xffe07a5f) });
            repaint(regionArea);
            return true;
        }
        if (!ctrl && !alt && shift && ch == 'r')
        {
            regions.push_back({ "Region " + juce::String((int)regions.size() + 1), globalTimelineSeconds, globalTimelineSeconds + 8.0, juce::Colour(0xff3a86ff) });
            repaint(regionArea);
            return true;
        }

        if (!ctrl && !alt && key == juce::KeyPress::pageUpKey)   { seekTo(globalTimelineSeconds - (60.0 / bpmSlider.getValue()) * 4.0); return true; }
        if (!ctrl && !alt && key == juce::KeyPress::pageDownKey) { seekTo(globalTimelineSeconds + (60.0 / bpmSlider.getValue()) * 4.0); return true; }
        if (ctrl && !alt && key == juce::KeyPress::pageUpKey)    { seekTo(globalTimelineSeconds - (60.0 / bpmSlider.getValue())); return true; }
        if (ctrl && !alt && key == juce::KeyPress::pageDownKey)  { seekTo(globalTimelineSeconds + (60.0 / bpmSlider.getValue())); return true; }
        if (!ctrl && alt && key == juce::KeyPress::homeKey)      { seekTo(0.0); return true; }
        if (!ctrl && alt && key == juce::KeyPress::endKey)       { seekTo(globalTimelineSeconds + (60.0 / bpmSlider.getValue()) * 4.0); return true; }
        if (!ctrl && !alt && !shift && key == juce::KeyPress::leftKey)  { seekTo(globalTimelineSeconds - 0.05); return true; }
        if (!ctrl && !alt && !shift && key == juce::KeyPress::rightKey) { seekTo(globalTimelineSeconds + 0.05); return true; }
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
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        currentBlockSize = samplesPerBlockExpected > 0 ? samplesPerBlockExpected : 128;
        metronome.prepare(currentSampleRate);
        backgroundThread.startThread();

        const juce::ScopedLock sl(trackLock);
        if (masterTrack != nullptr)
            masterTrack->prepareToPlay(currentBlockSize, currentSampleRate);

        for (auto* track : tracks)
            track->prepareToPlay(currentBlockSize, currentSampleRate);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        int totalCh = bufferToFill.buffer->getNumChannels();
        int numInputs = juce::jmin(2, totalCh);
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

        if (masterTrack != nullptr && totalCh >= 2)
        {
            juce::AudioBuffer<float> masterStereoView(bufferToFill.buffer->getArrayOfWritePointers(), 2, bufferToFill.startSample, bufferToFill.numSamples);
            juce::MidiBuffer emptyMidi;
            for (auto* vst : masterTrack->getVstPlugins())
                vst->processBlock(masterStereoView, emptyMidi);
            for (auto* fx : masterTrack->getNativeEffects())
                fx->process(masterStereoView);
        }

        float mGain1 = masterGainAtomic.load();
        if (totalCh >= 1) bufferToFill.buffer->applyGain(0, bufferToFill.startSample, bufferToFill.numSamples, mGain1);
        if (totalCh >= 2) bufferToFill.buffer->applyGain(1, bufferToFill.startSample, bufferToFill.numSamples, mGain1);

        if (totalCh >= 4)
        {
            if (out2EnabledAtomic.load())
            {
                float gainRatio2 = (mGain1 > 0.0001f) ? (out2GainAtomic.load() / mGain1) : 0.0f;
                bufferToFill.buffer->copyFrom(2, bufferToFill.startSample, *bufferToFill.buffer, 0, bufferToFill.startSample, bufferToFill.numSamples);
                bufferToFill.buffer->copyFrom(3, bufferToFill.startSample, *bufferToFill.buffer, 1, bufferToFill.startSample, bufferToFill.numSamples);
                bufferToFill.buffer->applyGain(2, bufferToFill.startSample, bufferToFill.numSamples, gainRatio2);
                bufferToFill.buffer->applyGain(3, bufferToFill.startSample, bufferToFill.numSamples, gainRatio2);
            }
            else
            {
                bufferToFill.buffer->clear(2, bufferToFill.startSample, bufferToFill.numSamples);
                bufferToFill.buffer->clear(3, bufferToFill.startSample, bufferToFill.numSamples);
            }
        }

        float mPeak = 0.0f;
        for (int ch = 0; ch < juce::jmin(2, totalCh); ++ch)
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
        g.fillAll(juce::Colour(0xff121318));

        juce::ColourGradient topGrad(juce::Colour(0xff232631), 0.0f, 0.0f,
                                     juce::Colour(0xff171920), 0.0f, 48.0f, false);
        g.setGradientFill(topGrad);
        g.fillRect(0, 0, getWidth(), 48);

        g.setColour(juce::Colour(0xff0d0
