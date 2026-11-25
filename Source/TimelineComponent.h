#pragma once
#include <JuceHeader.h>
#include "AudioTrack.h"

class MixerStripComponent : public juce::Component
{
public:
    MixerStripComponent(AudioTrack& trackRef,
                        int trackIdx,
                        juce::Colour accent,
                        std::function<void(AudioTrack&)> onOpenRack,
                        std::function<void()> onStateChanged)
        : track(trackRef),
          index(trackIdx),
          accentColour(accent),
          openRackCallback(std::move(onOpenRack)),
          stateChangedCallback(std::move(onStateChanged))
    {
        addAndMakeVisible(panSlider);
        panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        panSlider.setRange(-1.0, 1.0, 0.01);
        panSlider.setValue(track.pan, juce::dontSendNotification);
        panSlider.setTooltip("Pan (Esquerda / Direita)");
        panSlider.setColour(juce::Slider::rotarySliderFillColourId, accentColour);
        panSlider.onValueChange = [this] { track.pan = (float)panSlider.getValue(); };

        addAndMakeVisible(fxBtn);
        fxBtn.setButtonText("FX");
        fxBtn.onClick = [this] { if (openRackCallback) openRackCallback(track); };

        addAndMakeVisible(muteBtn);
        muteBtn.setButtonText("M");
        muteBtn.setClickingTogglesState(true);
        muteBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe07a5f));
        muteBtn.onClick = [this]
        {
            track.isMuted = muteBtn.getToggleState();
            if (stateChangedCallback) stateChangedCallback();
        };

        addAndMakeVisible(soloBtn);
        soloBtn.setButtonText("S");
        soloBtn.setClickingTogglesState(true);
        soloBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xfff2cc8f));
        soloBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        soloBtn.onClick = [this]
        {
            track.isSoloed = soloBtn.getToggleState();
            if (stateChangedCallback) stateChangedCallback();
        };

        addAndMakeVisible(volPercentLabel);
        volPercentLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        volPercentLabel.setColour(juce::Label::textColourId, juce::Colour(0xff90e0ef));
        volPercentLabel.setJustificationType(juce::Justification::centred);

        addAndMakeVisible(faderSlider);
        faderSlider.setSliderStyle(juce::Slider::LinearVertical);
        faderSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        faderSlider.setRange(0.0, 1.5, 0.01);
        faderSlider.setValue(track.gain, juce::dontSendNotification);
        faderSlider.setColour(juce::Slider::thumbColourId, juce::Colours::lightgrey);
        faderSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff141418));
        faderSlider.onValueChange = [this]
        {
            track.gain = (float)faderSlider.getValue();
            updatePercentText();
            if (stateChangedCallback) stateChangedCallback();
        };

        syncFromTrack();
    }

    void syncFromTrack()
    {
        muteBtn.setToggleState(track.isMuted, juce::dontSendNotification);
        soloBtn.setToggleState(track.isSoloed, juce::dontSendNotification);
        faderSlider.setValue(track.gain, juce::dontSendNotification);
        panSlider.setValue(track.pan, juce::dontSendNotification);
        updatePercentText();

        bool hasFx = track.hasActiveEffects();
        fxBtn.setColour(juce::TextButton::buttonColourId, hasFx ? juce::Colour(0xff2b9348) : juce::Colour(0xff34343e));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        g.setColour(juce::Colour(0xff25252b));
        g.fillRect(b);
        g.setColour(juce::Colour(0xff16161a));
        g.drawRect(b, 1);

        float peak = juce::jlimit(0.0f, 1.0f, track.currentPeakLevel.load());
        auto meterArea = juce::Rectangle<int>(b.getRight() - 16, 74, 8, b.getHeight() - 106);
        g.setColour(juce::Colour(0xff121216));
        g.fillRect(meterArea);

        if (peak > 0.005f)
        {
            int fillH = (int)(peak * (float)meterArea.getHeight());
            auto filled = meterArea.removeFromBottom(fillH);
            g.setColour(peak > 0.90f ? juce::Colours::red : (peak > 0.72f ? juce::Colours::yellow : juce::Colour(0xff38b000)));
            g.fillRect(filled);
        }

        auto footer = b.removeFromBottom(26);
        g.setColour(accentColour.withBrightness(0.42f));
        g.fillRect(footer);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(track.getName(), footer.reduced(4, 0), juce::Justification::centredLeft, true);

        auto numBox = footer.removeFromRight(22);
        g.setColour(accentColour);
        g.fillRect(numBox);
        g.setColour(juce::Colours::white);
        g.drawText(juce::String(index + 1), numBox, juce::Justification::centred);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(4);
        auto topRow = b.removeFromTop(24);
        panSlider.setBounds(topRow.removeFromLeft(26));
        fxBtn.setBounds(topRow.reduced(2));

        auto btnRow = b.removeFromTop(20);
        muteBtn.setBounds(btnRow.removeFromLeft(btnRow.getWidth() / 2).reduced(1));
        soloBtn.setBounds(btnRow.reduced(1));

        volPercentLabel.setBounds(b.removeFromTop(16));

        b.removeFromBottom(24);
        faderSlider.setBounds(b.withTrimmedRight(16).reduced(4, 2));
    }

private:
    void updatePercentText()
    {
        int pct = (int)std::round(track.gain * 100.0f);
        volPercentLabel.setText(juce::String(pct) + "%", juce::dontSendNotification);
    }

    AudioTrack& track;
    int index = 0;
    juce::Colour accentColour;
    std::function<void(AudioTrack&)> openRackCallback;
    std::function<void()> stateChangedCallback;

    juce::Label volPercentLabel;
    juce::Slider panSlider, faderSlider;
    juce::TextButton fxBtn, muteBtn, soloBtn;
};

class MasterStripComponent : public juce::Component
{
public:
    MasterStripComponent(std::atomic<float>& masterGainRef, std::atomic<float>& masterPeakRef)
        : masterGain(masterGainRef), masterPeak(masterPeakRef)
    {
        addAndMakeVisible(masterVolLabel);
        masterVolLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        masterVolLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00f5d4));
        masterVolLabel.setJustificationType(juce::Justification::centred);
        masterVolLabel.setText("100%", juce::dontSendNotification);

        addAndMakeVisible(fader);
        fader.setSliderStyle(juce::Slider::LinearVertical);
        fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange(0.0, 1.5, 0.01);
        fader.setValue(1.0, juce::dontSendNotification);
        fader.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        fader.onValueChange = [this]
        {
            float val = (float)fader.getValue();
            masterGain.store(val);
            masterVolLabel.setText(juce::String((int)std::round(val * 100.0f)) + "%", juce::dontSendNotification);
        };
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        g.setColour(juce::Colour(0xff1e1e24));
        g.fillRect(b);
        g.setColour(juce::Colour(0xff3a3a46));
        g.drawRect(b, 1);

        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("MASTER", b.removeFromTop(20), juce::Justification::centred);

        float peak = juce::jlimit(0.0f, 1.0f, masterPeak.load());
        auto meterArea = juce::Rectangle<int>(b.getRight() - 22, 42, 12, b.getHeight() - 72);
        g.setColour(juce::Colour(0xff101014));
        g.fillRect(meterArea);

        if (peak > 0.005f)
        {
            int fillH = (int)(peak * (float)meterArea.getHeight());
            auto filled = meterArea.removeFromBottom(fillH);
            g.setColour(peak > 0.92f ? juce::Colours::red : (peak > 0.75f ? juce::Colours::yellow : juce::Colour(0xff00e676)));
            g.fillRect(filled);
        }

        auto footer = b.removeFromBottom(26);
        g.setColour(juce::Colour(0xff34343e));
        g.fillRect(footer);
        g.setColour(juce::Colours::white);
        g.drawText("MASTER OUT", footer, juce::Justification::centred);
    }

    void resized() override
    {
        auto b = getLocalBounds().withTrimmedTop(20).withTrimmedBottom(28);
        masterVolLabel.setBounds(b.removeFromTop(18));
        fader.setBounds(b.withTrimmedRight(24).reduced(8, 2));
    }

private:
    std::atomic<float>& masterGain;
    std::atomic<float>& masterPeak;
    juce::Label masterVolLabel;
    juce::Slider fader;
};

class BottomMixerDock : public juce::Component
{
public:
    BottomMixerDock(std::atomic<float>& masterGainRef, std::atomic<float>& masterPeakRef)
        : masterStrip(masterGainRef, masterPeakRef)
    {
        addAndMakeVisible(masterStrip);
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&stripsContainer, false);
        viewport.setScrollBarsShown(false, true);
    }

    void clearStrips()
    {
        strips.clear(true);
    }

    void rebuildStrips(juce::OwnedArray<AudioTrack>& tracks,
                       std::function<void(AudioTrack&)> onOpenRack,
                       std::function<void()> onSyncAll)
    {
        strips.clear(true);
        const juce::Colour palette[] = {
            juce::Colour(0xff2ec4b6), juce::Colour(0xff3a86ff), juce::Colour(0xff38b000),
            juce::Colour(0xff8338ec), juce::Colour(0xffe63946), juce::Colour(0xfffb5607),
            juce::Colour(0xffffbe0b), juce::Colour(0xff00b4d8)
        };

        for (int i = 0; i < tracks.size(); ++i)
        {
            auto* s = strips.add(new MixerStripComponent(
                *tracks[i], i, palette[i % 8], onOpenRack, onSyncAll));
            stripsContainer.addAndMakeVisible(s);
        }
        resized();
    }

    void refreshMetersAndState()
    {
        masterStrip.repaint();
        for (auto* s : strips)
            s->syncFromTrack();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1b1b20));
        g.setColour(juce::Colour(0xff383844));
        g.drawHorizontalLine(0, 0.0f, (float)getWidth());
    }

    void resized() override
    {
        auto b = getLocalBounds().withTrimmedTop(2);
        masterStrip.setBounds(b.removeFromLeft(96));
        viewport.setBounds(b);

        int stripW = 74;
        int totalW = juce::jmax(viewport.getWidth(), strips.size() * stripW);
        stripsContainer.setSize(totalW, viewport.getHeight());
        for (int i = 0; i < strips.size(); ++i)
            strips[i]->setBounds(i * stripW, 0, stripW, viewport.getHeight());
    }

private:
    MasterStripComponent masterStrip;
    juce::Viewport viewport;
    juce::Component stripsContainer;
    juce::OwnedArray<MixerStripComponent> strips;
};
