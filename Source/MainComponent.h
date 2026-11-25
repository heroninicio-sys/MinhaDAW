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

        // Tela de Boas-Vindas (inicia o scan somente após o usuário fechar esta tela)
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
                pluginScanner.start
