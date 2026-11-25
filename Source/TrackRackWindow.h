#pragma once
#include <JuceHeader.h>
#include "AudioTrack.h"
#include "PluginScanner.h"

class TrackRackComponent : public juce::Component
{
public:
    TrackRackComponent(AudioTrack& trackRef,
                       PluginScannerEngine& scannerRef,
                       juce::CriticalSection& engineLock,
                       std::function<void(AudioTrack&, const juce::PluginDescription&, std::function<void()>)> loadDescCallback,
                       std::function<void(AudioTrack&, std::function<void()>)> browseFileCallback)
        : track(trackRef),
          scanner(scannerRef),
          audioLock(engineLock),
          onLoadFromDesc(std::move(loadDescCallback)),
          onBrowseFile(std::move(browseFileCallback))
    {
        addAndMakeVisible(addNativeBtn);
        addNativeBtn.setButtonText("+ Plugin Nativo");
        addNativeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a6f97));
        addNativeBtn.onClick = [this] { showNativeMenu(); };

        addAndMakeVisible(addVstBtn);
        addVstBtn.setButtonText("+ Plugin VST3");
        addVstBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5c4b7d));
        addVstBtn.onClick = [this] { showScannedPluginsMenu(); };

        rebuildList();
        setSize(460, 430);
    }

    ~TrackRackComponent() override
    {
        activeWindows.clear(true);
    }

    void rebuildList()
    {
        rowItems.clear();

        for (int i = 0; i < track.getVstPlugins().size(); ++i)
        {
            auto* vst = track.getVstPlugins()[i];
            auto* row = rowItems.add(new SlotRow(
                "[" + vst->getPluginDescription().pluginFormatName + "] " + vst->getName(),
                false,
                [this, vst]
                {
                    if (vst->hasEditor())
                    {
                        auto* win = activeWindows.add(new PluginEditorWindow(vst->getName(), juce::Colours::black));
                        win->setContentOwned(vst->createEditor(), true);
                        win->setVisible(true);
                    }
                },
                [](bool) {},
                [this, i]
                {
                    activeWindows.clear(true);
                    {
                        const juce::ScopedLock sl(audioLock);
                        track.getVstPlugins().remove(i, true);
                        if (i < track.vstFilePaths.size())
                            track.vstFilePaths.remove(i);
                    }
                    rebuildList();
                }
            ));
            addAndMakeVisible(row);
        }

        for (int i = 0; i < track.getNativeEffects().size(); ++i)
        {
            auto* fx = track.getNativeEffects()[i];
            auto* row = rowItems.add(new SlotRow(
                fx->getName(),
                fx->bypassed,
                [this, fx]
                {
                    auto editor = fx->createEditor();
                    if (editor != nullptr)
                    {
                        auto* win = activeWindows.add(new PluginEditorWindow(fx->getName(), juce::Colour(0xff1e1e24)));
                        win->setContentOwned(editor.release(), true);
                        win->centreWithSize(win->getWidth(), win->getHeight());
                        win->setVisible(true);
                    }
                },
                [fx](bool isBypassed) { fx->bypassed = isBypassed; },
                [this, i]
                {
                    activeWindows.clear(true);
                    {
                        const juce::ScopedLock sl(audioLock);
                        track.getNativeEffects().remove(i, true);
                    }
                    rebuildList();
                }
            ));
            addAndMakeVisible(row);
        }

        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff18181f));
        if (rowItems.isEmpty())
        {
            g.setColour(juce::Colours::grey);
            g.drawText("Rack Vazio - Adicione Plugins Nativos ou VST3",
                       getLocalBounds().reduced(20), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        auto topBar = area.removeFromTop(34);
        addNativeBtn.setBounds(topBar.removeFromLeft(155).reduced(2));
        addVstBtn.setBounds(topBar.removeFromLeft(175).reduced(2));

        area.removeFromTop(10);
        for (auto* row : rowItems)
            row->setBounds(area.removeFromTop(38).reduced(0, 2));
    }

private:
    class PluginEditorWindow : public juce::DocumentWindow
    {
    public:
        PluginEditorWindow(const juce::String& name, juce::Colour bg)
            : DocumentWindow(name, bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
        }
        void closeButtonPressed() override { setVisible(false); }
    };

    class SlotRow : public juce::Component
    {
    public:
        SlotRow(const juce::String& title, bool isBypassed,
                std::function<void()> onOpen,
                std::function<void(bool)> onBypass,
                std::function<void()> onRemove)
        {
            addAndMakeVisible(openBtn);
            openBtn.setButtonText(title);
            openBtn.onClick = std::move(onOpen);

            addAndMakeVisible(bypassBtn);
            bypassBtn.setButtonText("Bypass");
            bypassBtn.setClickingTogglesState(true);
            bypassBtn.setToggleState(isBypassed, juce::dontSendNotification);
            bypassBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
            bypassBtn.onClick = [this, onBypass] { onBypass(bypassBtn.getToggleState()); };

            addAndMakeVisible(delBtn);
            delBtn.setButtonText("X");
            delBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
            delBtn.onClick = std::move(onRemove);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            delBtn.setBounds(b.removeFromRight(32).reduced(1));
            bypassBtn.setBounds(b.removeFromRight(62).reduced(1));
            openBtn.setBounds(b.reduced(1));
        }

    private:
        juce::TextButton openBtn, bypassBtn, delBtn;
    };

    void showScannedPluginsMenu()
    {
        juce::PopupMenu menu;
        auto types = scanner.getScannedTypesSafe();

        if (types.isEmpty())
        {
            menu.addItem(9999, scanner.isScanning() ? "Escaneando pastas... aguarde" : "Nenhum VST3 encontrado no cache", false);
        }
        else
        {
            for (int i = 0; i < types.size(); ++i)
            {
                const auto& desc = types.getReference(i);
                menu.addItem(i + 1, desc.name + " (" + desc.manufacturerName + ")");
            }
        }

        menu.addSeparator();
        menu.addItem(8001, "Procurar arquivo .vst3 manualmente...");
        menu.addItem(8002, "Re-escanear pastas VST3 agora");

        menu.showMenuAsync(juce::PopupMenu::Options(), [this, types](int res)
        {
            if (res == 0) return;

            if (res == 8001)
            {
                if (onBrowseFile)
                    onBrowseFile(track, [this] { rebuildList(); });
                return;
            }
            if (res == 8002)
            {
                scanner.startBackgroundAutoScan(true);
                return;
            }

            int idx = res - 1;
            if (idx >= 0 && idx < types.size() && onLoadFromDesc)
            {
                onLoadFromDesc(track, types.getReference(idx), [this] { rebuildList(); });
            }
        });
    }

    void showNativeMenu()
    {
        juce::PopupMenu menu;
        juce::PopupMenu eqSubMenu;
        for (int b = 8; b <= 120; b += 8)
            eqSubMenu.addItem(100 + b, "EQ " + juce::String(b) + " Canais/Bandas");

        menu.addSubMenu("Equalizador Multi-Banda (8 a 120 ch)", eqSubMenu);
        menu.addItem(1, "Compressor Dinamico");
        menu.addItem(2, "Distorcao / Overdrive");
        menu.addItem(3, "Auto-Tune Vocal (Pitch Correction)");
        menu.addItem(4, "Reverb Stereo");
        menu.addItem(5, "Delay Stereo");

        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int res)
        {
            if (res == 0) return;

            std::unique_ptr<NativeEffect> fx;
            if (res >= 108 && res <= 220) fx = std::make_unique<MultiBandEQEffect>(res - 100);
            else if (res == 1) fx = std::make_unique<CompressorEffect>();
            else if (res == 2) fx = std::make_unique<DistortionEffect>();
            else if (res == 3) fx = std::make_unique<AutoTuneEffect>();
            else if (res == 4) fx = std::make_unique<ReverbEffect>();
            else if (res == 5) fx = std::make_unique<DelayEffect>();

            if (fx != nullptr)
            {
                const juce::ScopedLock sl(audioLock);
                track.addNativeEffect(std::move(fx));
                rebuildList();
            }
        });
    }

    AudioTrack& track;
    PluginScannerEngine& scanner;
    juce::CriticalSection& audioLock;
    std::function<void(AudioTrack&, const juce::PluginDescription&, std::function<void()>)> onLoadFromDesc;
    std::function<void(AudioTrack&, std::function<void()>)> onBrowseFile;

    juce::TextButton addNativeBtn, addVstBtn;
    juce::OwnedArray<SlotRow> rowItems;
    juce::OwnedArray<PluginEditorWindow> activeWindows;
};

class TrackRackWindow : public juce::DocumentWindow
{
public:
    TrackRackWindow(AudioTrack& track,
                    PluginScannerEngine& scanner,
                    juce::CriticalSection& engineLock,
                    std::function<void(AudioTrack&, const juce::PluginDescription&, std::function<void()>)> loadDescCallback,
                    std::function<void(AudioTrack&, std::function<void()>)> browseFileCallback)
        : DocumentWindow("Rack de Plugins & Efeitos - " + track.getName(),
                         juce::Colour(0xff1e1e24),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new TrackRackComponent(track, scanner, engineLock, std::move(loadDescCallback), std::move(browseFileCallback)), true);
        centreWithSize(460, 430);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }
};
