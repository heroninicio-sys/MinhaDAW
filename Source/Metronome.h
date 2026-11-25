#pragma once
#include <JuceHeader.h>
#include <cmath>

class MetronomeEngine
{
public:
    bool enabled = false;
    float volume = 0.45f;

    void prepare(double sr)
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        lastBeatIndex = -1;
        clickSamplesRemaining = 0;
        phase = 0.0;
    }

    void reset()
    {
        lastBeatIndex = -1;
        clickSamplesRemaining = 0;
        phase = 0.0;
    }

    void process(juce::AudioBuffer<float>& outputBuffer, double blockStartSec, double bpm, bool isPlaying)
    {
        if (!enabled || !isPlaying || bpm <= 0.0 || sampleRate <= 0.0)
            return;

        const int numSamples = outputBuffer.getNumSamples();
        const double secPerBeat = 60.0 / bpm;

        for (int i = 0; i < numSamples; ++i)
        {
            double currentSec = blockStartSec + ((double)i / sampleRate);
            int currentBeat = (int)std::floor(currentSec / secPerBeat);

            if (currentBeat != lastBeatIndex)
            {
                lastBeatIndex = currentBeat;
                bool isDownbeat = (currentBeat % 4 == 0);
                currentFreq = isDownbeat ? 1320.0 : 880.0;
                clickSamplesRemaining = (int)(sampleRate * 0.035);
                totalClickSamples = clickSamplesRemaining;
                phase = 0.0;
            }

            if (clickSamplesRemaining > 0)
            {
                double env = (double)clickSamplesRemaining / (double)juce::jmax(1, totalClickSamples);
                float sample = (float)(std::sin(phase * 2.0 * juce::MathConstants<double>::pi) * env * volume);

                for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                    outputBuffer.addSample(ch, i, sample);

                phase += currentFreq / sampleRate;
                if (phase >= 1.0) phase -= 1.0;
                --clickSamplesRemaining;
            }
        }
    }

private:
    double sampleRate = 44100.0;
    int lastBeatIndex = -1;
    int clickSamplesRemaining = 0;
    int totalClickSamples = 1;
    double currentFreq = 880.0;
    double phase = 0.0;
};
