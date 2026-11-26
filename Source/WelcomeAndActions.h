#pragma once
#include <JuceHeader.h>
#include "AudioClip.h"

class WelcomeOverlayComponent : public juce::Component
{
public:
    WelcomeOverlayComponent(std::function<void()> onNewProject,
                            std::function<void()> onOpenProject,
                            std::function<void()> onOpenAudioPrefs)
        : newProjCb(std::move(onNewProject)),
          openProjCb(std::move(onOpenProject)),
          prefsCb(std::move(onOpenAudioPrefs))
    {
        setAlwaysOnTop(true);

        addAndMakeVisible(newBtn);
        newBtn.setButtonText("+ Novo Projeto Limpo (Ctrl+N)");
        newBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0077b6));
        newBtn.onClick = [this] { dismissAndRun(newProjCb); };

        addAndMakeVisible(openBtn);
        openBtn.setButtonText("Abrir Projeto Salvo (Ctrl+O)");
        openBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2b9348));
        openBtn.onClick = [this] { dismissAndRun(openProjCb); };

        addAndMakeVisible(prefsBtn);
        prefsBtn.setButtonText("Configurar Placa de Som / ASIO (Ctrl+P)");
        prefsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a48));
        prefsBtn.onClick = [this] { dismissAndRun(prefsCb); };

        addAndMakeVisible(dontShowToggle);
        dontShowToggle.setButtonText("Nao mostrar esta tela novamente na proxima inicializacao");
        dontShowToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
        dontShowToggle.onClick = [this]
        {
            savePreference(dontShowToggle.getToggleState());
        };
    }

    static bool shouldShowOnStartup()
    {
        auto file = getPrefFile();
        if (!file.existsAsFile())
            return true;
        return file.loadFileAsString().trim() != "hide=1";
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.78f));

        auto card = getLocalBounds().withSizeKeepingCentre(540, 310);
        g.setColour(juce::Colour(0xff142337));
        g.fillRoundedRectangle(card.toFloat(), 10.0f);
        g.setColour(juce::Colour(0xff16d8e8));
        g.drawRoundedRectangle(card.toFloat(), 10.0f, 1.5f);

        auto inner = card.reduced(28, 22);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        g.drawText("Bem-vindo ao MinhaDAW Studio", inner.removeFromTop(32), juce::Justification::centred);

        g.setColour(juce::Colour(0xff90e0ef));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText("Estacao de Trabalho de Audio Digital 64-bit | Pressione '?' a qualquer momento para ver os atalhos",
                   inner.removeFromTop(24), juce::Justification::centred);
    }

    void resized() override
    {
        auto card = getLocalBounds().withSizeKeepingCentre(540, 310).reduced(36, 24);
        card.removeFromTop(68);

        newBtn.setBounds(card.removeFromTop(38).reduced(0, 2));
        card.removeFromTop(6);
        openBtn.setBounds(card.removeFromTop(38).reduced(0, 2));
        card.removeFromTop(6);
        prefsBtn.setBounds(card.removeFromTop(38).reduced(0, 2));
        card.removeFromTop(14);
        dontShowToggle.setBounds(card.removeFromTop(26));
    }

private:
    void dismissAndRun(const std::function<void()>& fn)
    {
        setVisible(false);
        if (fn) fn();
    }

    static juce::File getPrefFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("MinhaDAWStudio")
            .getChildFile("WelcomeScreenPref.txt");
    }

    static void savePreference(bool hideNextTime)
    {
        auto f = getPrefFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText(hideNextTime ? "hide=1" : "hide=0");
    }

    juce::TextButton newBtn, openBtn, prefsBtn;
    juce::ToggleButton dontShowToggle;
    std::function<void()> newProjCb, openProjCb, prefsCb;
};

class ActionListWindow : public juce::DocumentWindow
{
public:
    struct ActionItem
    {
        juce::String shortcut;
        juce::String description;
        std::function<void()> execute;
    };

    ActionListWindow(std::vector<ActionItem> actions)
        : DocumentWindow("Action List - Lista de Acoes e Atalhos [?]",
                         juce::Colour(0xff101d2c),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new Content(std::move(actions), [this] { setVisible(false); }), true);
        centreWithSize(560, 520);
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    class Content : public juce::Component
    {
    public:
        Content(std::vector<ActionItem> items, std::function<void()> closeWin)
            : actionItems(std::move(items)), onClose(std::move(closeWin))
        {
            addAndMakeVisible(viewport);
            viewport.setViewedComponent(&listComp, false);

            for (auto& item : actionItems)
            {
                auto* btn = buttons.add(new juce::TextButton(item.shortcut + "   ->   " + item.description));
                btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff172638));
                auto fn = item.execute;
                btn->onClick = [this, fn]
                {
                    if (onClose) onClose();
                    if (fn) fn();
                };
                listComp.addAndMakeVisible(btn);
            }
            setSize(560, 520);
        }

        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff08111d)); }

        void resized() override
        {
            viewport.setBounds(getLocalBounds().reduced(8));
            int h = juce::jmax(viewport.getHeight(), buttons.size() * 32);
            listComp.setSize(viewport.getWidth() - 14, h);
            for (int i = 0; i < buttons.size(); ++i)
                buttons[i]->setBounds(0, i * 32, listComp.getWidth(), 30);
        }

    private:
        std::vector<ActionItem> actionItems;
        std::function<void()> onClose;
        juce::Viewport viewport;
        juce::Component listComp;
        juce::OwnedArray<juce::TextButton> buttons;
    };
};
