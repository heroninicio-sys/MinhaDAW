#pragma once
#include <JuceHeader.h>
#include "AudioTrack.h"
#include "PluginScanner.h"

class StudioKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioKnobLookAndFeel(juce::Colour neonCol) : ringColour(neonCol) {}

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(4.0f);
        float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        juce::Path bgArc;
        bgArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff101015));
        g.strokePath(bgArc, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (sliderPos > 0.0f)
        {
            juce::Path valArc;
            valArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, toAngle, true);
            g.setColour(ringColour.withAlpha(0.35f));
            g.strokePath(valArc, juce::PathStrokeType(6.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(ringColour);
            g.strokePath(valArc, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        float knobR = radius - 5.0f;
        juce::ColourGradient grad(juce::Colour(0xff3a3d46), cx - knobR, cy - knobR,
                                  juce::Colour(0xff141519), cx + knobR, cy + knobR, false);
        g.setGradientFill(grad);
        g.fillEllipse(cx - knobR, cy - knobR, knobR * 2.0f, knobR * 2.0f);

        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawEllipse(cx - knobR, cy - knobR, knobR * 2.0f, knobR * 2.0f, 1.2f);

        juce::Path pointer;
        pointer.addRoundedRectangle(-1.5f, -knobR + 2.0f, 3.0f, knobR * 0.52f, 1.5f);
        g.setColour(juce::Colours::white);
        g.fillPath(pointer, juce::AffineTransform::rotation(toAngle).translated(cx, cy));
    }

private:
    juce::Colour ringColour;
};

class StudioRackModuleCard : public juce::Component
{
public:
    StudioRackModuleCard(const juce::String& titleText,
                         const juce::String& badgeText,
                         juce::Colour accentNeon,
                         bool isBypassed,
                         std::function<void()> onOpenFullEditor,
                         std::function<void(bool)> onBypassChanged,
                         std::function<void()> onDeleteModule)
        : title(titleText),
          badge(badgeText),
          neonColour(accentNeon),
          knobLnf(accentNeon),
          openEditorCb(std::move(onOpenFullEditor)),
          bypassCb(std::move(onBypassChanged)),
          deleteCb(std::move(onDeleteModule))
    {
        addAndMakeVisible(powerBtn);
        powerBtn.setButtonText("ON");
        powerBtn.setClickingTogglesState(true);
        powerBtn.setToggleState(!isBypassed, juce::dontSendNotification);
        powerBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a1a22));
        powerBtn.setColour(juce::TextButton::buttonOnColourId, neonColour);
        powerBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        powerBtn.onClick = [this]
        {
            bool active = powerBtn.getToggleState();
            bypassBtn.setToggleState(!active, juce::dontSendNotification);
            if (bypassCb) bypassCb(!active);
            repaint();
        };

        addAndMakeVisible(bypassBtn);
        bypassBtn.setButtonText("Bypass");
        bypassBtn.setClickingTogglesState(true);
        bypassBtn.setToggleState(isBypassed, juce::dontSendNotification);
        bypassBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25262e));
        bypassBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe07a5f));
        bypassBtn.onClick = [this]
        {
            bool byp = bypassBtn.getToggleState();
            powerBtn.setToggleState(!byp, juce::dontSendNotification);
            if (bypassCb) bypassCb(byp);
            repaint();
        };

        addAndMakeVisible(delBtn);
        delBtn.setButtonText("X");
        delBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff9b2226));
        delBtn.onClick = [this] { if (deleteCb) deleteCb(); };

        addAndMakeVisible(uiBtn);
        uiBtn.setButtonText("Abrir Tela");
        uiBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2b2d38));
        uiBtn.onClick = [this] { if (openEditorCb) openEditorCb(); };
    }

    ~StudioRackModuleCard() override
    {
        for (auto* k : knobs)
            k->setLookAndFeel(nullptr);
    }

    void addMacroKnob(const juce::String& label, float minVal, float maxVal, float initialVal,
                      const juce::String& unitSuffix, std::function<void(float)> onChange)
    {
        auto* k = knobs.add(new juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        k->setLookAndFeel(&knobLnf);
        k->setRange(minVal, maxVal, 0.01);
        k->setValue(initialVal, juce::dontSendNotification);
        addAndMakeVisible(k);

        auto* nameLbl = knobNameLabels.add(new juce::Label({}, label));
        nameLbl->setFont(juce::FontOptions(11.0f, juce::Font::bold));
        nameLbl->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        nameLbl->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(nameLbl);

        auto* valLbl = knobValueLabels.add(new juce::Label({}, juce::String(initialVal, 1) + unitSuffix));
        valLbl->setFont(juce::FontOptions(10.5f));
        valLbl->setColour(juce::Label::textColourId, juce::Colours::white);
        valLbl->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(valLbl);

        k->onValueChange = [k, valLbl, unitSuffix, onChange]
        {
            float v = (float)k->getValue();
            valLbl->setText(juce::String(v, 1) + unitSuffix, juce::dontSendNotification);
            if (onChange) onChange(v);
        };
        resized();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(2.0f);

        juce::ColourGradient chassis(juce::Colour(0xff172638), 0.0f, b.getY(),
                                     juce::Colour(0xff0c1623), 0.0f, b.getBottom(), false);
        g.setGradientFill(chassis);
        g.fillRoundedRectangle(b, 8.0f);

        bool isActive = powerBtn.getToggleState();
        // Brilho externo neon colorido
        g.setColour(isActive ? neonColour.withAlpha(0.25f) : juce::Colours::transparentBlack);
        g.drawRoundedRectangle(b.expanded(1.0f), 9.0f, 3.0f);
        g.setColour(isActive ? neonColour.withAlpha(0.85f) : juce::Colour(0xff2a4057));
        g.drawRoundedRectangle(b, 8.0f, 1.5f);

        g.setColour(juce::Colour(0xff4a4d5c));
        float screwOff = 8.0f;
        g.fillEllipse(b.getX() + screwOff, b.getY() + screwOff, 5.0f, 5.0f);
        g.fillEllipse(b.getRight() - screwOff - 5.0f, b.getY() + screwOff, 5.0f, 5.0f);
        g.fillEllipse(b.getX() + screwOff, b.getBottom() - screwOff - 5.0f, 5.0f, 5.0f);
        g.fillEllipse(b.getRight() - screwOff - 5.0f, b.getBottom() - screwOff - 5.0f, 5.0f, 5.0f);

        auto leftBrand = b.withWidth(115.0f).reduced(14.0f, 16.0f);
        g.setColour(isActive ? neonColour : juce::Colours::grey);
        g.fillRoundedRectangle(leftBrand.getX(), leftBrand.getY(), 4.5f, leftBrand.getHeight(), 2.0f);

        g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        g.drawText(badge, leftBrand.withTrimmedLeft(10.0f).removeFromTop(38), juce::Justification::centred);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawFittedText(title, leftBrand.withTrimmedLeft(10.0f).withTrimmedTop(40).toNearestInt(),
                         juce::Justification::centred, 2);

        g.setColour(juce::Colour(0xff2a4057));
        g.drawVerticalLine(122, b.getY() + 10.0f, b.getBottom() - 10.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(title, 134, (int)b.getY() + 8, getWidth() - 320, 22, juce::Justification::centredLeft, true);

        auto displayRect = juce::Rectangle<float>((float)getWidth() - 155.0f, b.getY() + 38.0f, 138.0f, b.getHeight() - 48.0f);
        g.setColour(juce::Colour(0xff07111d));
        g.fillRoundedRectangle(displayRect, 4.0f);
        g.setColour(neonColour.withAlpha(0.55f));
        g.drawRoundedRectangle(displayRect, 4.0f, 1.2f);

        int bars = 16;
        float barStep = (displayRect.getWidth() - 12.0f) / (float)bars;
        for (int i = 0; i < bars; ++i)
        {
            float norm = std::exp(-0.16f * (float)i) * (0.6f + 0.4f * std::sin((float)i * 0.9f));
            float bh = norm * (displayRect.getHeight() - 12.0f);
            float bx = displayRect.getX() + 6.0f + (float)i * barStep;
            g.setColour(isActive ? neonColour.withAlpha(0.90f) : juce::Colours::dimgrey);
            g.fillRect(bx, displayRect.getBottom() - 6.0f - bh, juce::jmax(2.0f, barStep - 2.5f), bh);
        }
    }

    void resized() override
    {
        auto topRight = getLocalBounds().reduced(12, 10).removeFromTop(24);
        delBtn.setBounds(topRight.removeFromRight(26).reduced(1));
        bypassBtn.setBounds(topRight.removeFromRight(56).reduced(1));
        powerBtn.setBounds(topRight.removeFromRight(36).reduced(1));
        uiBtn.setBounds(topRight.removeFromRight(72).reduced(1));

        int startX = 132;
        int availW = getWidth() - 300;
        int count = knobs.size();
        if (count > 0)
        {
            int slotW = availW / count;
            for (int i = 0; i < count; ++i)
            {
                int kx = startX + i * slotW;
                knobs[i]->setBounds(kx + (slotW - 52) / 2, 36, 52, 52);
                knobNameLabels[i]->setBounds(kx, 88, slotW, 15);
                knobValueLabels[i]->setBounds(kx, 103, slotW, 15);
            }
        }
    }

private:
    juce::String title, badge;
    juce::Colour neonColour;
    StudioKnobLookAndFeel knobLnf;
    std::function<void()> openEditorCb;
    std::function<void(bool)> bypassCb;
    std::function<void()> deleteCb;

    juce::TextButton powerBtn, bypassBtn, delBtn, uiBtn;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label> knobNameLabels;
    juce::OwnedArray<juce::Label> knobValueLabels;
};

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
        addNativeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0077b6));
        addNativeBtn.onClick = [this] { showNativeMenu(); };

        addAndMakeVisible(addVstBtn);
        addVstBtn.setButtonText("+ Plugin VST3");
        addVstBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5e548e));
        addVstBtn.onClick = [this] { showScannedPluginsMenu(); };

        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&modulesContainer, false);
        viewport.setScrollBarsShown(true, false);

        rebuildList();
        setSize(680, 500);
    }

    ~TrackRackComponent() override
    {
        activeWindows.clear(true);
    }

    void rebuildList()
    {
        const juce::ScopedLock sl(audioLock);
        moduleCards.clear(true);

        const juce::Colour vstPalette[] = {
            juce::Colour(0xff16c7c9), juce::Colour(0xffa78bfa), juce::Colour(0xfff59e42), juce::Colour(0xff22c58b)
        };

        for (int i = 0; i < track.getVstPlugins().size(); ++i)
        {
            auto* vst = track.getVstPlugins()[i];
            auto* card = moduleCards.add(new StudioRackModuleCard(
                "[VST3] " + vst->getName(),
                vst->getName().substring(0, 2).toUpperCase(),
                vstPalette[i % 4],
                track.isVstPluginBypassed(i),
                [this, vst]
                {
                    if (vst->hasEditor())
                    {
                        auto* win = activeWindows.add(new PluginEditorWindow(vst->getName(), juce::Colours::black));
                        win->setContentOwned(vst->createEditor(), true);
                        win->setVisible(true);
                    }
                },
                [safeThis = juce::Component::SafePointer<TrackRackComponent>(this), vst](bool shouldBypass)
                {
                    if (safeThis == nullptr) return;
                    const juce::ScopedLock sl(safeThis->audioLock);
                    const int currentIndex = safeThis->track.getVstPlugins().indexOf(vst);
                    if (currentIndex < 0) return;
                    safeThis->track.setVstPluginBypassed(currentIndex, shouldBypass);
                },
                [safeThis = juce::Component::SafePointer<TrackRackComponent>(this), vst]
                {
                    juce::MessageManager::callAsync([safeThis, vst]
                    {
                        if (safeThis == nullptr) return;
                        safeThis->activeWindows.clear(true);
                        {
                            const juce::ScopedLock sl(safeThis->audioLock);
                            const int currentIndex = safeThis->track.getVstPlugins().indexOf(vst);
                            if (currentIndex >= 0)
                            {
                                safeThis->track.removeVstPlugin(currentIndex);
                            }
                        }
                        safeThis->rebuildList();
                    });
                }
            ));

            auto& params = vst->getParameters();
            int macroCount = juce::jmin(4, params.size());
            for (int p = 0; p < macroCount; ++p)
            {
                auto* param = params[p];
                card->addMacroKnob(param->getName(12), 0.0f, 100.0f, param->getValue() * 100.0f, "%",
                                   [param](float val) { param->setValueNotifyingHost(val / 100.0f); });
            }

            modulesContainer.addAndMakeVisible(card);
        }

        for (int i = 0; i < track.getNativeEffects().size(); ++i)
        {
            auto* fx = track.getNativeEffects()[i];
            juce::Colour neon = juce::Colour(0xff9d4edd);
            juce::String badge = "FX";

            if (fx->getTypeId() == "NativeEQ")            { neon = juce::Colour(0xff16d8e8); badge = "EQ"; }
            else if (fx->getTypeId() == "NativeComp")     { neon = juce::Colour(0xff22c58b); badge = "CP"; }
            else if (fx->getTypeId() == "NativeDist")     { neon = juce::Colour(0xfff06b8b); badge = "DS"; }
            else if (fx->getTypeId() == "NativeAutoTune") { neon = juce::Colour(0xff398bff); badge = "AT"; }
            else if (fx->getTypeId() == "NativeReverb")   { neon = juce::Colour(0xff9d4edd); badge = "RV"; }
            else if (fx->getTypeId() == "NativeDelay")    { neon = juce::Colour(0xfff59e42); badge = "DL"; }

            auto* card = moduleCards.add(new StudioRackModuleCard(
                fx->getName(),
                badge,
                neon,
                fx->bypassed.load(),
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
                [fx](bool isBypassed) { fx->bypassed.store(isBypassed); },
                [safeThis = juce::Component::SafePointer<TrackRackComponent>(this), i]
                {
                    juce::MessageManager::callAsync([safeThis, i]
                    {
                        if (safeThis == nullptr) return;
                        safeThis->activeWindows.clear(true);
                        {
                            const juce::ScopedLock sl(safeThis->audioLock);
                            if (i < safeThis->track.getNativeEffects().size())
                                safeThis->track.getNativeEffects().remove(i, true);
                        }
                        safeThis->rebuildList();
                    });
                }
            ));

            if (auto* rev = dynamic_cast<ReverbEffect*>(fx))
            {
                card->addMacroKnob("Mix",       0.0f, 100.0f, rev->wetLevel * 100.0f, "%",  [rev](float v) { rev->wetLevel = v / 100.0f; });
                card->addMacroKnob("Room Size", 0.0f, 100.0f, rev->roomSize * 100.0f, "%",  [rev](float v) { rev->roomSize = v / 100.0f; });
                card->addMacroKnob("Damping",   0.0f, 100.0f, rev->damping * 100.0f,  "%",  [rev](float v) { rev->damping  = v / 100.0f; });
            }
            else if (auto* dly = dynamic_cast<DelayEffect*>(fx))
            {
                card->addMacroKnob("Mix",      0.0f,  100.0f,  dly->mix * 100.0f,      "%",  [dly](float v) { dly->mix = v / 100.0f; });
                card->addMacroKnob("Time",     10.0f, 2000.0f, dly->delayTimeMs,       "ms", [dly](float v) { dly->delayTimeMs = v; });
                card->addMacroKnob("Feedback", 0.0f,  90.0f,   dly->feedback * 100.0f, "%",  [dly](float v) { dly->feedback = v / 100.0f; });
            }
            else if (auto* cmp = dynamic_cast<CompressorEffect*>(fx))
            {
                card->addMacroKnob("Thresh", -60.0f, 0.0f,   cmp->thresholdDb, "dB", [cmp](float v) { cmp->thresholdDb = v; });
                card->addMacroKnob("Ratio",  1.0f,   20.0f,  cmp->ratio,       ":1", [cmp](float v) { cmp->ratio = v; });
                card->addMacroKnob("Attack", 0.5f,   150.0f, cmp->attackMs,    "ms", [cmp](float v) { cmp->attackMs = v; });
                card->addMacroKnob("Makeup", 0.0f,   24.0f,  cmp->makeupDb,    "dB", [cmp](float v) { cmp->makeupDb = v; });
            }
            else if (auto* dst = dynamic_cast<DistortionEffect*>(fx))
            {
                card->addMacroKnob("Drive",  1.0f,  40.0f, dst->drive,      "x",  [dst](float v) { dst->drive = v; });
                card->addMacroKnob("Mix",    0.0f,  100.0f,dst->mix * 100.0f,"%", [dst](float v) { dst->mix = v / 100.0f; });
                card->addMacroKnob("Output", -24.0f,6.0f,  dst->outputDb,   "dB", [dst](float v) { dst->outputDb = v; });
            }
            else if (auto* at = dynamic_cast<AutoTuneEffect*>(fx))
            {
                card->addMacroKnob("Speed", 5.0f, 100.0f, at->retuneSpeed * 100.0f, "%", [at](float v) { at->retuneSpeed = v / 100.0f; });
                card->addMacroKnob("Mix",   0.0f, 100.0f, at->mix * 100.0f,         "%", [at](float v) { at->mix = v / 100.0f; });
            }

            modulesContainer.addAndMakeVisible(card);
        }

        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101116));
        if (moduleCards.isEmpty())
        {
            g.setColour(juce::Colours::grey);
            g.setFont(14.0f);
            g.drawText("Rack Vazio - Clique em '+ Plugin Nativo' ou '+ Plugin VST3' acima",
                       getLocalBounds().reduced(20), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        auto topBar = area.removeFromTop(38);
        addNativeBtn.setBounds(topBar.removeFromLeft(175).reduced(2));
        addVstBtn.setBounds(topBar.removeFromLeft(175).reduced(2));

        area.removeFromTop(8);
        viewport.setBounds(area);

        int cardH = 128;
        int totalH = juce::jmax(viewport.getHeight(), moduleCards.size() * cardH);
        modulesContainer.setSize(viewport.getWidth() - 12, totalH);
        for (int i = 0; i < moduleCards.size(); ++i)
            moduleCards[i]->setBounds(0, i * cardH, modulesContainer.getWidth(), cardH - 4);
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

    void showScannedPluginsMenu()
    {
        juce::PopupMenu menu;
        auto types = scanner.getScannedTypesSafe();

        if (types.isEmpty())
            menu.addItem(9999, scanner.isScanning() ? "Escaneando pastas... aguarde" : "Nenhum VST3 encontrado no cache", false);
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

        juce::Component::SafePointer<TrackRackComponent> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, types](int res)
        {
            if (safeThis == nullptr || res == 0) return;
            if (res == 8001)
            {
                if (safeThis->onBrowseFile)
                    safeThis->onBrowseFile(safeThis->track, [safeThis] { if (safeThis != nullptr) safeThis->rebuildList(); });
                return;
            }
            if (res == 8002)
            {
                safeThis->scanner.startBackgroundAutoScan(true);
                return;
            }

            const int idx = res - 1;
            if (idx >= 0 && idx < types.size() && safeThis->onLoadFromDesc)
                safeThis->onLoadFromDesc(safeThis->track, types.getReference(idx),
                    [safeThis] { if (safeThis != nullptr) safeThis->rebuildList(); });
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

        juce::Component::SafePointer<TrackRackComponent> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis](int res)
        {
            if (safeThis == nullptr || res == 0) return;

            std::unique_ptr<NativeEffect> fx;
            if (res >= 108 && res <= 220) fx = std::make_unique<MultiBandEQEffect>(res - 100);
            else if (res == 1) fx = std::make_unique<CompressorEffect>();
            else if (res == 2) fx = std::make_unique<DistortionEffect>();
            else if (res == 3) fx = std::make_unique<AutoTuneEffect>();
            else if (res == 4) fx = std::make_unique<ReverbEffect>();
            else if (res == 5) fx = std::make_unique<DelayEffect>();

            if (fx != nullptr)
            {
                {
                    const juce::ScopedLock sl(safeThis->audioLock);
                    safeThis->track.addNativeEffect(std::move(fx));
                }
                safeThis->rebuildList();
            }
        });
    }

    AudioTrack& track;
    PluginScannerEngine& scanner;
    juce::CriticalSection& audioLock;
    std::function<void(AudioTrack&, const juce::PluginDescription&, std::function<void()>)> onLoadFromDesc;
    std::function<void(AudioTrack&, std::function<void()>)> onBrowseFile;

    juce::TextButton addNativeBtn, addVstBtn;
    juce::Viewport viewport;
    juce::Component modulesContainer;
    juce::OwnedArray<StudioRackModuleCard> moduleCards;
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
                         juce::Colour(0xff101116),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new TrackRackComponent(track, scanner, engineLock, std::move(loadDescCallback), std::move(browseFileCallback)), true);
        centreWithSize(680, 500);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }
};
