#pragma once
#include <JuceHeader.h>
#include <cmath>

class ProStudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr juce::uint32 canvas = 0xff080f19;
    static constexpr juce::uint32 panel = 0xff111d2b;
    static constexpr juce::uint32 panelRaised = 0xff172638;
    static constexpr juce::uint32 border = 0xff2a4057;
    static constexpr juce::uint32 cyan = 0xff16d8e8;
    static constexpr juce::uint32 muted = 0xff91a8bf;

    ProStudioLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(canvas));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(panelRaised));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffe8f1fa));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff07566a));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b1623));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(border));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffe8f1fa));
        setColour(juce::Label::textColourId, juce::Colour(0xffdce8f4));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xffe6f2fb));
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(cyan));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff263d52));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdce8f4));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xfff7fcff));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto base = button.getToggleState()
            ? button.findColour(juce::TextButton::buttonOnColourId)
            : backgroundColour;
        if (!base.isOpaque()) base = juce::Colour(panelRaised);
        if (down) base = base.darker(0.20f);
        else if (highlighted) base = base.brighter(0.10f);

        g.setColour(juce::Colour(0x18000000));
        g.fillRoundedRectangle(bounds.translated(0.0f, 1.0f), 6.0f);
        g.setGradientFill(juce::ColourGradient(base.brighter(0.08f), 0.0f, bounds.getY(),
                                               base.darker(0.16f), 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(button.getToggleState() ? base.brighter(0.38f).withAlpha(0.85f)
                                            : juce::Colour(border));
        g.drawRoundedRectangle(bounds, 6.0f, button.getToggleState() ? 1.35f : 0.9f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.setColour(button.getToggleState()
            ? button.findColour(juce::TextButton::textColourOnId)
            : button.findColour(juce::TextButton::textColourOffId));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 2),
                         juce::Justification::centred, 1);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float, float,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearHorizontal)
        {
            const float cy = (float)y + (float)height * 0.5f;
            const float trackH = 5.0f;
            g.setColour(juce::Colour(0xff07111b));
            g.fillRoundedRectangle((float)x, cy - trackH * 0.5f, (float)width, trackH, 2.5f);
            g.setColour(slider.findColour(juce::Slider::trackColourId, true).isTransparent()
                            ? juce::Colour(cyan) : slider.findColour(juce::Slider::trackColourId, true));
            g.fillRoundedRectangle((float)x, cy - trackH * 0.5f,
                                   juce::jlimit(0.0f, (float)width, sliderPos - (float)x), trackH, 2.5f);
            g.setColour(juce::Colour(0xffeaf4fb));
            g.fillRoundedRectangle(sliderPos - 5.0f, cy - 7.0f, 10.0f, 14.0f, 3.0f);
            g.setColour(juce::Colour(0xff526b82));
            g.drawRoundedRectangle(sliderPos - 5.0f, cy - 7.0f, 10.0f, 14.0f, 3.0f, 0.8f);
        }
        else if (style == juce::Slider::LinearVertical)
        {
            const float cx = (float)x + (float)width * 0.5f;
            g.setColour(juce::Colour(0xff07111b));
            g.fillRoundedRectangle(cx - 3.0f, (float)y, 6.0f, (float)height, 3.0f);
            g.setColour(juce::Colour(cyan).withAlpha(0.82f));
            g.fillRoundedRectangle(cx - 3.0f, sliderPos, 6.0f,
                                   juce::jmax(0.0f, (float)y + (float)height - sliderPos), 3.0f);
            auto cap = juce::Rectangle<float>(cx - 10.0f, sliderPos - 6.0f, 20.0f, 12.0f);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff0f6fb), cap.getX(), cap.getY(),
                                                   juce::Colour(0xff8fa7bd), cap.getX(), cap.getBottom(), false));
            g.fillRoundedRectangle(cap, 3.0f);
            g.setColour(juce::Colour(0xff41586d));
            g.drawRoundedRectangle(cap, 3.0f, 0.8f);
        }
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStart, float rotaryEnd,
                          juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(4.0f);
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        auto knob = bounds.withSizeKeepingCentre(diameter, diameter).reduced(2.0f);
        const float radius = diameter * 0.5f - 3.0f;
        const float cx = knob.getCentreX(), cy = knob.getCentreY();
        const float angle = rotaryStart + sliderPos * (rotaryEnd - rotaryStart);
        juce::Path arc;
        arc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStart, rotaryEnd, true);
        g.setColour(juce::Colour(0xff263d52));
        g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path active;
        active.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStart, angle, true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId, true));
        g.strokePath(active, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xff1a2a3a));
        g.fillEllipse(knob.reduced(7.0f));
        g.setColour(juce::Colour(0xffdceaf5));
        g.drawEllipse(knob.reduced(7.0f), 1.0f);
        juce::Path pointer;
        pointer.startNewSubPath(cx, cy);
        pointer.lineTo(cx + std::cos(angle - juce::MathConstants<float>::halfPi) * (radius - 7.0f),
                       cy + std::sin(angle - juce::MathConstants<float>::halfPi) * (radius - 7.0f));
        g.setColour(juce::Colour(0xffeaf7ff));
        g.strokePath(pointer, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};
