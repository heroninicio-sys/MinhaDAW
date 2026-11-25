#pragma once
#include <JuceHeader.h>

class ProStudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ProStudioLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff14151a));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1c1e26));
        setColour(juce::PopupMenu::textColourId, juce::Colours::white);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff0077b6));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff232631));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a3f50));
        setColour(juce::ComboBox::textColourId, juce::Colours::white);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto baseCol = button.getToggleState()
            ? button.findColour(juce::TextButton::buttonOnColourId)
            : backgroundColour;

        if (shouldDrawButtonAsDown)
            baseCol = baseCol.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            baseCol = baseCol.brighter(0.14f);

        juce::ColourGradient grad(baseCol.brighter(0.10f), 0.0f, bounds.getY(),
                                  baseCol.darker(0.18f), 0.0f, bounds.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, 4.0f);

        g.setColour(button.getToggleState() ? baseCol.brighter(0.45f) : juce::Colour(0xff3e4354));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.setColour(button.getToggleState()
            ? button.findColour(juce::TextButton::textColourOnId)
            : button.findColour(juce::TextButton::textColourOffId));

        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().reduced(3, 1),
                         juce::Justification::centred, 1);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float, float,
                          const juce::Slider::SliderStyle style, juce::Slider&) override
    {
        if (style == juce::Slider::LinearHorizontal)
        {
            float trackY = (float)y + (float)height * 0.5f;
            g.setColour(juce::Colour(0xff0f1014));
            g.fillRoundedRectangle((float)x, trackY - 3.0f, (float)width, 6.0f, 3.0f);

            g.setColour(juce::Colour(0xff00b4d8));
            g.fillRoundedRectangle((float)x, trackY - 3.0f, sliderPos - (float)x, 6.0f, 3.0f);

            g.setColour(juce::Colours::white);
            g.fillRoundedRectangle(sliderPos - 5.0f, trackY - 7.0f, 10.0f, 14.0f, 2.5f);
            g.setColour(juce::Colours::black);
            g.drawLine(sliderPos, trackY - 5.0f, sliderPos, trackY + 5.0f, 1.2f);
        }
        else if (style == juce::Slider::LinearVertical)
        {
            float trackX = (float)x + (float)width * 0.5f;
            g.setColour(juce::Colour(0xff0d0e12));
            g.fillRoundedRectangle(trackX - 3.0f, (float)y, 6.0f, (float)height, 3.0f);

            g.setColour(juce::Colour(0xff00b4d8).withAlpha(0.75f));
            g.fillRoundedRectangle(trackX - 3.0f, sliderPos, 6.0f, ((float)y + (float)height) - sliderPos, 3.0f);

            auto cap = juce::Rectangle<float>(trackX - 11.0f, sliderPos - 7.0f, 22.0f, 14.0f);
            juce::ColourGradient capGrad(juce::Colour(0xffe2e8f0), cap.getX(), cap.getY(),
                                         juce::Colour(0xff94a3b8), cap.getX(), cap.getBottom(), false);
            g.setGradientFill(capGrad);
            g.fillRoundedRectangle(cap, 2.5f);
            g.setColour(juce::Colours::black);
            g.drawRoundedRectangle(cap, 2.5f, 1.0f);
            g.drawLine(cap.getX() + 2.0f, sliderPos, cap.getRight() - 2.0f, sliderPos, 1.5f);
        }
    }
};
