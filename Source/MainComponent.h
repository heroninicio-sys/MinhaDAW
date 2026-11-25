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

        g.setColour(juce::Colour(0xff0d0e12));
        g.fillRoundedRectangle(8.0f, 6.0f, 256.0f, 36.0f, 5.0f);
        g.setColour(juce::Colour(0xff323646));
        g.drawRoundedRectangle(8.0f, 6.0f, 256.0f, 36.0f, 5.0f, 1.0f);

        int timelineStartX = regionArea.getX() + 264;
        if (!regionArea.isEmpty())
        {
            g.setColour(juce::Colour(0xff181a22));
            g.fillRect(regionArea);
            g.setColour(juce::Colour(0xff2c303e));
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
            g.setColour(juce::Colour(0xff1d2029));
            g.fillRect(rulerArea);
            g.setColour(juce::Colour(0xff323646));
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

        auto topBar = area.removeFromTop(48);

        timecodeLabel.setBounds(14, 8, 174, 32);
        statusStateLabel.setBounds(188, 12, 72, 24);

        auto controls = topBar.withTrimmedLeft(272).reduced(4, 9);

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
        rulerArea  = area.removeFromTop(22).reduced(8, 0);

        if (showBottomMixer)
            bottomMixer.setBounds(area.removeFromBottom(185).reduced(8, 2));

        tracksViewport.setBounds(area.reduced(8, 2));
        layoutTracks();
    }

private:
    ProStudioLookAndFeel proLookAndFeel;

    juce::Label timecodeLabel, statusStateLabel, bpmLabel, out2PctLabel;
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

    std::unique_ptr<AudioClip> clipboardClip;
    int selectedEnvelopePointIdx = 0;

    bool isPlaying = false;
    bool isRecording = false;
    bool showGridLines = true;
    bool autoCrossfadeEnabled = true;
    bool itemsLocked = false;
    double globalTimelineSeconds = 0.0;
    std::atomic<double> currentBpmAtomic { 120.0 };
    double pixelsPerSecond = 55.0;
    int trackRowHeight = 64;
    double currentSampleRate = 48000.0;
    int currentBlockSize = 128;

    void setupButton(juce::TextButton& btn, const juce::String& text, const juce::String& tooltip,
                     std::function<void()> onClick, juce::Colour bg = juce::Colour(0xff2c303e))
    {
        addAndMakeVisible(btn);
        btn.setButtonText(text);
        btn.setTooltip(tooltip);
        btn.setColour(juce::TextButton::buttonColourId, bg);
        btn.onClick = std::move(onClick);
    }

    void setGridDivisionAndSnap(int division)
    {
        gridDivBox.setSelectedId(1, juce::dontSendNotification);
        if (division == 2) gridDivBox.setSelectedId(2, juce::dontSendNotification);
        else if (division == 4 || division == 8 || division == 12) gridDivBox.setSelectedId(4, juce::dontSendNotification);
        snapBtn.setToggleState(true, juce::dontSendNotification);
        updateGridParameters();
    }

    void toggleEnvelopeFlag(const juce::String& label)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            "Envelopes / Automacao", label + " alternado no cursor " + juce::String(globalTimelineSeconds, 2) + "s.");
    }

    void nudgeEnvelopePointSelection(int delta, bool keepSelection)
    {
        selectedEnvelopePointIdx = juce::jmax(0, selectedEnvelopePointIdx + delta);
        juce::ignoreUnused(keepSelection);
    }

    void insertOrEditEnvelopePointAtCursor(bool editMode)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            editMode ? "Editar Ponto de Envelope" : "Inserir Ponto de Envelope",
            "Ponto #" + juce::String(selectedEnvelopePointIdx + 1) + " em " + juce::String(globalTimelineSeconds, 2) + "s.");
    }

    void nudgeSelectedClipGain(bool up)
    {
        if (selectedAudioClip == nullptr || selectedRow == nullptr) return;
        {
            const juce::ScopedLock sl(trackLock);
            float factor = up ? 1.122f : 0.891f;
            selectedAudioClip->clipGain = juce::jlimit(0.05f, 8.0f, selectedAudioClip->clipGain * factor);
        }
        selectedRow->repaint();
    }

    void copySelectedClip(bool smartCopy)
    {
        juce::ignoreUnused(smartCopy);
        if (selectedAudioClip == nullptr) return;
        const juce::ScopedLock sl(trackLock);
        clipboardClip = selectedAudioClip->cloneClip(audioFormatManager);
    }

    void cutSelectedClip(bool smartCut)
    {
        if (selectedRow == nullptr || selectedAudioClip == nullptr || itemsLocked) return;
        copySelectedClip(smartCut);
        deleteSelectedClip();
    }

    void pasteClipboardClip()
    {
        if (clipboardClip == nullptr || itemsLocked) return;
        if (tracks.isEmpty()) addNewTrack();
        TrackRowComponent* targetRow = (selectedRow != nullptr) ? selectedRow : trackRows.getLast();
        if (targetRow == nullptr) return;

        {
            const juce::ScopedLock sl(trackLock);
            auto copy = clipboardClip->cloneClip(audioFormatManager);
            copy->timelineStart = globalTimelineSeconds;
            targetRow->getTrack().getClips().add(std::move(copy));
        }
        targetRow->rebuildThumbnails();
    }

    void duplicateSelectedClip()
    {
        if (selectedRow == nullptr || selectedAudioClip == nullptr || itemsLocked) return;
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

    void cycleTakeOnSelected(bool nextTake)
    {
        if (selectedRow == nullptr || selectedAudioClip == nullptr) return;
        {
            const juce::ScopedLock sl(trackLock);
            if (nextTake)
            {
                selectedAudioClip->cycleNextTake(audioFormatManager);
            }
            else if (selectedAudioClip->takes.size() > 1)
            {
                int n = selectedAudioClip->takes.size();
                selectedAudioClip->activeTakeIndex = (selectedAudioClip->activeTakeIndex - 1 + n) % n;
                selectedAudioClip->loadFromFileInternal(selectedAudioClip->takes[selectedAudioClip->activeTakeIndex], audioFormatManager, false);
            }
        }
        selectedRow->rebuildThumbnails();
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
            { "T",                "Ciclar entre takes do item",         [this] { cycleTakeOnSelected(true); } },
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

        for (auto* track : tracks)
        {
            if (track->isArmed)
            {
                auto file = recDir.getNonexistentChildFile(track->getName() + "_Take", ".wav");
                track->startRecording(file, globalTimelineSeconds, rtlSec);
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

        if (isPlaying || isRecording)
        {
            for (auto* row : trackRows)
                row->setPlayheadPosition(globalTimelineSeconds);

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
        AudioExporter::showExportDialog(tracks, trackLock, currentSampleRate, currentBlockSize, bpmSlider.getValue());
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
