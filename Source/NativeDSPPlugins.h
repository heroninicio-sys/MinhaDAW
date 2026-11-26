#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <vector>

class NativeEffect
{
public:
    virtual ~NativeEffect() = default;
    virtual juce::String getName() const = 0;
    virtual juce::String getTypeId() const = 0;
    virtual void prepare(double sampleRate, int samplesPerBlock) = 0;
    virtual void process(juce::AudioBuffer<float>& buffer) = 0;
    virtual std::unique_ptr<juce::Component> createEditor() = 0;
    virtual juce::var saveState() const = 0;
    virtual void loadState(const juce::var& state) = 0;

    std::atomic<bool> bypassed { false };
};

class MultiChannelEQEffect : public NativeEffect
{
public:
    explicit MultiChannelEQEffect(int initialChannels = 16)
    {
        setChannelCount(initialChannels);
    }

    juce::String getName() const override   { return "Equalizador Multicanal (" + juce::String(numChannels) + " ch)"; }
    juce::String getTypeId() const override { return "NativeEQ"; }

    void setChannelCount(int channels)
    {
        const juce::ScopedLock sl(lock);
        numChannels = juce::jlimit(1, 120, channels);
        channelGainsDb.assign((size_t)numChannels, 0.0f);
        rebuildFilters();
    }

    int getChannelCount() const
    {
        const juce::ScopedLock sl(lock);
        return numChannels;
    }

    void setChannelGain(int chIndex, float gainDb)
    {
        const juce::ScopedLock sl(lock);
        if (chIndex >= 0 && chIndex < numChannels)
        {
            channelGainsDb[(size_t)chIndex] = juce::jlimit(-18.0f, 18.0f, gainDb);
            updateFilterCoefficients(chIndex);
        }
    }

    float getChannelGain(int chIndex) const
    {
        const juce::ScopedLock sl(lock);
        if (chIndex >= 0 && chIndex < numChannels)
            return channelGainsDb[(size_t)chIndex];
        return 0.0f;
    }

    void prepare(double sr, int) override
    {
        const juce::ScopedLock sl(lock);
        sampleRate = sr > 0.0 ? sr : 48000.0;
        rebuildFilters();
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed.load()) return;
        const juce::ScopedLock sl(lock);

        int bufCh = juce::jmin(2, buffer.getNumChannels());
        int numSamples = buffer.getNumSamples();

        for (int b = 0; b < numChannels; ++b)
        {
            if (std::abs(channelGainsDb[(size_t)b]) < 0.05f)
                continue;

            for (int ch = 0; ch < bufCh; ++ch)
            {
                float* samples = buffer.getWritePointer(ch);
                auto& filter = filters[(size_t)ch][(size_t)b];
                for (int i = 0; i < numSamples; ++i)
                    samples[i] = filter.processSample(samples[i]);
            }
        }
    }

    juce::var saveState() const override
    {
        const juce::ScopedLock sl(lock);
        auto* obj = new juce::DynamicObject();
        obj->setProperty("numChannels", numChannels);
        juce::Array<juce::var> gains;
        for (float g : channelGainsDb) gains.add(g);
        obj->setProperty("gains", gains);
        return juce::var(obj);
    }

    void loadState(const juce::var& state) override
    {
        if (auto* obj = state.getDynamicObject())
        {
            int cCount = obj->hasProperty("numChannels") ? (int)obj->getProperty("numChannels")
                                                         : (int)obj->getProperty("numBands");
            setChannelCount(cCount > 0 ? cCount : 16);
            if (auto* arr = obj->getProperty("gains").getArray())
            {
                for (int i = 0; i < juce::jmin(numChannels, arr->size()); ++i)
                    setChannelGain(i, (float)(*arr)[i]);
            }
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;

private:
    void rebuildFilters()
    {
        filters[0].resize((size_t)numChannels);
        filters[1].resize((size_t)numChannels);
        for (int b = 0; b < numChannels; ++b)
            updateFilterCoefficients(b);
    }

    void updateFilterCoefficients(int b)
    {
        double norm = (numChannels > 1) ? (double)b / (double)(numChannels - 1) : 0.5;
        double freq = 20.0 * std::pow(1000.0, norm);
        freq = juce::jlimit(20.0, sampleRate * 0.45, freq);

        double q = juce::jlimit(1.2, 14.0, (double)numChannels / 8.0);
        float linearGain = juce::Decibels::decibelsToGain(channelGainsDb[(size_t)b]);

        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, (float)freq, (float)q, linearGain);
        filters[0][(size_t)b].coefficients = coeffs;
        filters[1][(size_t)b].coefficients = coeffs;
    }

    mutable juce::CriticalSection lock;
    double sampleRate = 48000.0;
    int numChannels = 16;
    std::vector<float> channelGainsDb;
    std::vector<juce::dsp::IIR::Filter<float>> filters[2];
};

class CompressorEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Compressor Dinamico"; }
    juce::String getTypeId() const override { return "NativeComp"; }

    std::atomic<float> thresholdDb { -18.0f };
    std::atomic<float> ratio { 4.0f };
    std::atomic<float> attackMs { 10.0f };
    std::atomic<float> releaseMs { 100.0f };
    std::atomic<float> makeupDb { 3.0f };

    void prepare(double sr, int) override
    {
        sampleRate = sr > 0.0 ? sr : 48000.0;
        envelope = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed.load()) return;

        const float thresh = thresholdDb.load();
        const float rVal   = juce::jmax(1.0f, ratio.load());
        const float attMs  = juce::jmax(0.1f, attackMs.load());
        const float relMs  = juce::jmax(1.0f, releaseMs.load());
        const float mkDb   = makeupDb.load();

        float attackCoeff  = std::exp(-1.0f / (float)(sampleRate * (attMs * 0.001f)));
        float releaseCoeff = std::exp(-1.0f / (float)(sampleRate * (relMs * 0.001f)));
        float makeupLin    = juce::Decibels::decibelsToGain(mkDb);

        int numCh = buffer.getNumChannels();
        int numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax(peak, std::abs(buffer.getSample(ch, i)));

            if (peak > envelope)
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * peak;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * peak;

            float envDb = juce::Decibels::gainToDecibels(envelope + 1e-6f);
            float gainReductionDb = 0.0f;

            if (envDb > thresh)
            {
                float overDb = envDb - thresh;
                gainReductionDb = overDb * (1.0f - (1.0f / rVal));
            }

            float totalGain = juce::Decibels::decibelsToGain(-gainReductionDb) * makeupLin;

            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * totalGain);
        }
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("threshold", thresholdDb.load());
        o->setProperty("ratio", ratio.load());
        o->setProperty("attack", attackMs.load());
        o->setProperty("release", releaseMs.load());
        o->setProperty("makeup", makeupDb.load());
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            thresholdDb.store((float)o->getProperty("threshold"));
            ratio.store((float)o->getProperty("ratio"));
            attackMs.store((float)o->getProperty("attack"));
            releaseMs.store((float)o->getProperty("release"));
            makeupDb.store((float)o->getProperty("makeup"));
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;

private:
    double sampleRate = 48000.0;
    float envelope = 0.0f;
};

class DistortionEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Distorcao / Overdrive"; }
    juce::String getTypeId() const override { return "NativeDist"; }

    std::atomic<float> drive { 8.0f };
    std::atomic<float> mix { 0.8f };
    std::atomic<float> outputDb { -3.0f };

    void prepare(double, int) override {}

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed.load()) return;

        const float drv     = drive.load();
        const float mx      = juce::jlimit(0.0f, 1.0f, mix.load());
        const float outGain = juce::Decibels::decibelsToGain(outputDb.load());

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float dry = data[i];
                float wet = std::tanh(dry * drv);
                data[i] = ((dry * (1.0f - mx)) + (wet * mx)) * outGain;
            }
        }
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("drive", drive.load());
        o->setProperty("mix", mix.load());
        o->setProperty("output", outputDb.load());
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            drive.store((float)o->getProperty("drive"));
            mix.store((float)o->getProperty("mix"));
            outputDb.store((float)o->getProperty("output"));
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;
};

class AutoTuneEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Auto-Tune Vocal"; }
    juce::String getTypeId() const override { return "NativeAutoTune"; }

    std::atomic<float> retuneSpeed { 0.85f };
    std::atomic<float> mix { 1.0f };

    void prepare(double sr, int) override
    {
        sampleRate = sr > 0.0 ? sr : 48000.0;
        analysisBuffer.assign(2048, 0.0f);
        delayLine.assign(4096, 0.0f);
        writeIdx = 0;
        phase = 0.0f;
        currentPitchRatio = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed.load() || buffer.getNumChannels() == 0) return;

        const float spd = retuneSpeed.load();
        const float mx  = juce::jlimit(0.0f, 1.0f, mix.load());
        int numSamples = buffer.getNumSamples();
        float* left = buffer.getWritePointer(0);

        for (int i = 0; i < numSamples; ++i)
        {
            analysisBuffer[(size_t)analysisWritePos] = left[i];
            analysisWritePos = (analysisWritePos + 1) % (int)analysisBuffer.size();
        }

        float detectedFreq = detectPitchAutocorrelation();
        float targetRatio = 1.0f;

        if (detectedFreq > 75.0f && detectedFreq < 950.0f)
        {
            float midiNote = 69.0f + 12.0f * std::log2(detectedFreq / 440.0f);
            float nearestSemitone = std::round(midiNote);
            float targetFreq = 440.0f * std::pow(2.0f, (nearestSemitone - 69.0f) / 12.0f);
            targetRatio = juce::jlimit(0.75f, 1.35f, targetFreq / detectedFreq);
        }

        float alpha = juce::jlimit(0.02f, 0.95f, spd * 0.5f);
        currentPitchRatio += alpha * (targetRatio - currentPitchRatio);

        const float grainSize = 1024.0f;
        float phasorInc = (1.0f - currentPitchRatio) / grainSize;

        for (int i = 0; i < numSamples; ++i)
        {
            float inSample = left[i];
            delayLine[(size_t)writeIdx] = inSample;

            phase += phasorInc;
            if (phase >= 1.0f) phase -= 1.0f;
            if (phase < 0.0f)  phase += 1.0f;

            float phase2 = std::fmod(phase + 0.5f, 1.0f);

            float tap1 = readDelayInterpolated(phase * grainSize);
            float tap2 = readDelayInterpolated(phase2 * grainSize);

            float weight1 = 1.0f - std::abs(2.0f * phase - 1.0f);
            float weight2 = 1.0f - weight1;

            float tuned = (tap1 * weight1) + (tap2 * weight2);
            float out = (inSample * (1.0f - mx)) + (tuned * mx);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, out);

            writeIdx = (writeIdx + 1) % (int)delayLine.size();
        }
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("retuneSpeed", retuneSpeed.load());
        o->setProperty("mix", mix.load());
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            retuneSpeed.store((float)o->getProperty("retuneSpeed"));
            mix.store((float)o->getProperty("mix"));
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;

private:
    float detectPitchAutocorrelation() const
    {
        int n = (int)analysisBuffer.size();
        int minLag = (int)(sampleRate / 900.0);
        int maxLag = juce::jmin(n / 2, (int)(sampleRate / 80.0));

        float bestCorr = 0.0f;
        int bestLag = 0;

        for (int lag = minLag; lag < maxLag; ++lag)
        {
            float corr = 0.0f;
            float norm = 0.0f;
            for (int i = 0; i < n / 2; i += 2)
            {
                float s1 = analysisBuffer[(size_t)i];
                float s2 = analysisBuffer[(size_t)(i + lag)];
                corr += s1 * s2;
                norm += s1 * s1;
            }
            if (norm > 1e-5f && (corr / norm) > bestCorr)
            {
                bestCorr = corr / norm;
                bestLag = lag;
            }
        }

        if (bestCorr > 0.45f && bestLag > 0)
            return (float)(sampleRate / (double)bestLag);
        return 0.0f;
    }

    float readDelayInterpolated(float delaySamples) const
    {
        int size = (int)delayLine.size();
        float readPos = (float)writeIdx - delaySamples;
        while (readPos < 0.0f) readPos += (float)size;

        int idx0 = ((int)readPos) % size;
        int idx1 = (idx0 + 1) % size;
        float frac = readPos - std::floor(readPos);

        return delayLine[(size_t)idx0] * (1.0f - frac) + delayLine[(size_t)idx1] * frac;
    }

    double sampleRate = 48000.0;
    std::vector<float> analysisBuffer;
    std::vector<float> delayLine;
    int analysisWritePos = 0;
    int writeIdx = 0;
    float phase = 0.0f;
    float currentPitchRatio = 1.0f;
};

class ReverbEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Reverb Stereo"; }
    juce::String getTypeId() const override { return "NativeReverb"; }

    std::atomic<float> roomSize { 0.55f };
    std::atomic<float> damping { 0.5f };
    std::atomic<float> wetLevel { 0.30f };

    void prepare(double sr, int) override
    {
        reverb.setSampleRate(sr > 0.0 ? sr : 48000.0);
        updateParams();
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed.load()) return;
        updateParams();

        if (buffer.getNumChannels() >= 2)
            reverb.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        else if (buffer.getNumChannels() == 1)
            reverb.processMono(buffer.getWritePointer(0), buffer.getNumSamples());
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("roomSize", roomSize.load());
        o->setProperty("damping", damping.load());
        o->setProperty("wetLevel", wetLevel.load());
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            roomSize.store((float)o->getProperty("roomSize"));
            damping.store((float)o->getProperty("damping"));
            wetLevel.store((float)o->getProperty("wetLevel"));
            updateParams();
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;

private:
    void updateParams()
    {
        const float wet = wetLevel.load();
        params.roomSize = roomSize.load();
        params.damping  = damping.load();
        params.wetLevel = wet;
        params.dryLevel = 1.0f - (wet * 0.5f);
        params.width    = 1.0f;
        reverb.setParameters(params);
    }

    juce::Reverb reverb;
    juce::Reverb::Parameters params;
};

class DelayEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Delay Stereo"; }
    juce::String getTypeId() const override { return "NativeDelay"; }

    std::atomic<float> delayTimeMs { 350.0f };
    std::atomic<float> feedback { 0.40f };
    std::atomic<float> mix { 0.35f };

    void prepare(double sr, int) override
    {
        sampleRate = sr > 0.0 ? sr : 48000.0;
        int maxSamples = (int)(sampleRate * 2.5);
        delayBuffer.setSize(2, maxSamples);
        delayBuffer.clear();
        writePos = 0;
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed.load() || delayBuffer.getNumSamples() == 0) return;

        const float dTimeMs = delayTimeMs.load();
        const float fb      = feedback.load();
        const float mx      = juce::jlimit(0.0f, 1.0f, mix.load());

        int numCh = juce::jmin(2, buffer.getNumChannels());
        int numSamples = buffer.getNumSamples();
        int bufLen = delayBuffer.getNumSamples();
        int delaySamples = juce::jlimit(1, bufLen - 1, (int)((dTimeMs * 0.001f) * (float)sampleRate));

        for (int i = 0; i < numSamples; ++i)
        {
            int readPos = (writePos - delaySamples + bufLen) % bufLen;

            for (int ch = 0; ch < numCh; ++ch)
            {
                float in = buffer.getSample(ch, i);
                float delayed = delayBuffer.getSample(ch, readPos);

                delayBuffer.setSample(ch, writePos, in + (delayed * fb));
                buffer.setSample(ch, i, (in * (1.0f - mx)) + (delayed * mx));
            }

            writePos = (writePos + 1) % bufLen;
        }
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("delayTimeMs", delayTimeMs.load());
        o->setProperty("feedback", feedback.load());
        o->setProperty("mix", mix.load());
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            delayTimeMs.store((float)o->getProperty("delayTimeMs"));
            feedback.store((float)o->getProperty("feedback"));
            mix.store((float)o->getProperty("mix"));
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;

private:
    double sampleRate = 48000.0;
    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
};

class MultiChannelEQEditor : public juce::Component
{
public:
    explicit MultiChannelEQEditor(MultiChannelEQEffect& eqRef) : eq(eqRef)
    {
        addAndMakeVisible(channelsLabel);
        channelsLabel.setText("Quantidade de Canais:", juce::dontSendNotification);
        channelsLabel.setColour(juce::Label::textColourId, juce::Colours::white);

        addAndMakeVisible(channelCountInput);
        channelCountInput.setText(juce::String(eq.getChannelCount()), juce::dontSendNotification);
        channelCountInput.setEditable(true, true, false);
        channelCountInput.setColour(juce::Label::backgroundColourId, juce::Colour(0xff252836));
        channelCountInput.setColour(juce::Label::textColourId, juce::Colour(0xff00f5d4));
        channelCountInput.setJustificationType(juce::Justification::centred);
        channelCountInput.onTextChange = [this]
        {
            int val = channelCountInput.getText().trim().getIntValue();
            if (val > 0)
            {
                eq.setChannelCount(val);
                channelCountInput.setText(juce::String(eq.getChannelCount()), juce::dontSendNotification);
                repaint();
            }
        };

        addAndMakeVisible(presetChannelsBox);
        presetChannelsBox.addItem("8 Canais", 8);
        presetChannelsBox.addItem("16 Canais", 16);
        presetChannelsBox.addItem("32 Canais", 32);
        presetChannelsBox.addItem("64 Canais", 64);
        presetChannelsBox.addItem("120 Canais", 120);
        presetChannelsBox.setText("Presets", juce::dontSendNotification);
        presetChannelsBox.onChange = [this]
        {
            int sel = presetChannelsBox.getSelectedId();
            if (sel > 0)
            {
                eq.setChannelCount(sel);
                channelCountInput.setText(juce::String(eq.getChannelCount()), juce::dontSendNotification);
                repaint();
            }
        };

        addAndMakeVisible(resetBtn);
        resetBtn.setButtonText("Flat (0 dB)");
        resetBtn.onClick = [this]
        {
            for (int i = 0; i < eq.getChannelCount(); ++i)
                eq.setChannelGain(i, 0.0f);
            repaint();
        };

        setSize(760, 330);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff181820));
        auto graph = getLocalBounds().reduced(16).withTrimmedTop(46);

        g.setColour(juce::Colour(0xff22222d));
        g.fillRoundedRectangle(graph.toFloat(), 6.0f);

        float midY = (float)graph.getCentreY();
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawHorizontalLine((int)midY, (float)graph.getX(), (float)graph.getRight());

        int count = eq.getChannelCount();
        float barW = (float)graph.getWidth() / (float)juce::jmax(1, count);

        for (int i = 0; i < count; ++i)
        {
            float gainDb = eq.getChannelGain(i);
            float norm = gainDb / 18.0f;
            float barH = norm * ((float)graph.getHeight() * 0.45f);
            float x = (float)graph.getX() + ((float)i * barW);

            g.setColour(gainDb >= 0.0f ? juce::Colour(0xff00b4d8) : juce::Colour(0xfff77f00));
            if (barH >= 0.0f)
                g.fillRect(x + 1.0f, midY - barH, juce::jmax(1.5f, barW - 2.0f), barH);
            else
                g.fillRect(x + 1.0f, midY, juce::jmax(1.5f, barW - 2.0f), -barH);
        }

        g.setColour(juce::Colours::lightgrey);
        g.setFont(11.0f);
        g.drawText("20 Hz", graph.getX() + 4, graph.getBottom() - 18, 60, 16, juce::Justification::centredLeft);
        g.drawText("1 kHz", graph.getCentreX() - 30, graph.getBottom() - 18, 60, 16, juce::Justification::centred);
        g.drawText("20 kHz", graph.getRight() - 64, graph.getBottom() - 18, 60, 16, juce::Justification::centredRight);
        g.drawText("Canais ativos: " + juce::String(count) + " | Arraste sobre os canais para ajustar",
                   graph.reduced(6).removeFromTop(18), juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& e) override { editChannelAt(e.getPosition()); }
    void mouseDrag(const juce::MouseEvent& e) override { editChannelAt(e.getPosition()); }

    void resized() override
    {
        auto top = getLocalBounds().reduced(16, 12).removeFromTop(30);
        channelsLabel.setBounds(top.removeFromLeft(155));
        channelCountInput.setBounds(top.removeFromLeft(64));
        top.removeFromLeft(8);
        presetChannelsBox.setBounds(top.removeFromLeft(120));
        resetBtn.setBounds(top.removeFromRight(100));
    }

private:
    void editChannelAt(juce::Point<int> p)
    {
        auto graph = getLocalBounds().reduced(16).withTrimmedTop(46);
        if (!graph.contains(p)) return;

        int count = eq.getChannelCount();
        int chIdx = juce::jlimit(0, count - 1, (int)(((float)(p.x - graph.getX()) / (float)graph.getWidth()) * (float)count));
        float normY = ((float)graph.getCentreY() - (float)p.y) / ((float)graph.getHeight() * 0.45f);
        eq.setChannelGain(chIdx, juce::jlimit(-18.0f, 18.0f, normY * 18.0f));
        repaint();
    }

    MultiChannelEQEffect& eq;
    juce::Label channelsLabel, channelCountInput;
    juce::ComboBox presetChannelsBox;
    juce::TextButton resetBtn;
};

class GenericSliderEditor : public juce::Component
{
public:
    struct ParamDef { juce::String name; float min; float max; std::atomic<float>* target; };

    GenericSliderEditor(const juce::String& titleText, std::vector<ParamDef> params)
        : title(titleText)
    {
        for (auto& p : params)
        {
            auto* lbl = labels.add(new juce::Label({}, p.name));
            lbl->setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(lbl);

            auto* sld = sliders.add(new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
            sld->setRange(p.min, p.max, 0.01);
            sld->setValue(p.target->load(), juce::dontSendNotification);
            auto* ptr = p.target;
            sld->onValueChange = [sld, ptr] { ptr->store((float)sld->getValue()); };
            addAndMakeVisible(sld);
        }
        setSize(420, 60 + (int)params.size() * 42);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0e1a28));
        g.setColour(juce::Colour(0xff16d8e8));
        g.setFont(15.0f);
        g.drawText(title, getLocalBounds().reduced(12).removeFromTop(26), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12).withTrimmedTop(36);
        for (int i = 0; i < sliders.size(); ++i)
        {
            auto row = area.removeFromTop(38);
            labels[i]->setBounds(row.removeFromLeft(130));
            sliders[i]->setBounds(row);
        }
    }

private:
    juce::String title;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Slider> sliders;
};

inline std::unique_ptr<juce::Component> MultiChannelEQEffect::createEditor()
{
    return std::make_unique<MultiChannelEQEditor>(*this);
}

inline std::unique_ptr<juce::Component> CompressorEffect::createEditor()
{
    std::vector<GenericSliderEditor::ParamDef> p {
        { "Threshold (dB)", -60.0f, 0.0f,    &thresholdDb },
        { "Ratio (:1)",       1.0f,  20.0f,   &ratio },
        { "Attack (ms)",      0.5f,  150.0f,  &attackMs },
        { "Release (ms)",     10.0f, 1000.0f, &releaseMs },
        { "Makeup (dB)",      0.0f,  24.0f,   &makeupDb }
    };
    return std::make_unique<GenericSliderEditor>("Compressor Dinamico", p);
}

inline std::unique_ptr<juce::Component> DistortionEffect::createEditor()
{
    std::vector<GenericSliderEditor::ParamDef> p {
        { "Drive",       1.0f,  40.0f, &drive },
        { "Mix Dry/Wet", 0.0f,  1.0f,  &mix },
        { "Output (dB)", -24.0f, 6.0f, &outputDb }
    };
    return std::make_unique<GenericSliderEditor>("Distorcao / Overdrive", p);
}

inline std::unique_ptr<juce::Component> AutoTuneEffect::createEditor()
{
    std::vector<GenericSliderEditor::ParamDef> p {
        { "Retune Speed", 0.05f, 1.0f, &retuneSpeed },
        { "Mix Dry/Wet",  0.0f,  1.0f, &mix }
    };
    return std::make_unique<GenericSliderEditor>("Auto-Tune Vocal (Correcao Cromatica)", p);
}

inline std::unique_ptr<juce::Component> ReverbEffect::createEditor()
{
    std::vector<GenericSliderEditor::ParamDef> p {
        { "Room Size",   0.0f, 1.0f, &roomSize },
        { "Damping",     0.0f, 1.0f, &damping },
        { "Mix Wet",     0.0f, 1.0f, &wetLevel }
    };
    return std::make_unique<GenericSliderEditor>("Reverb Stereo", p);
}

inline std::unique_ptr<juce::Component> DelayEffect::createEditor()
{
    std::vector<GenericSliderEditor::ParamDef> p {
        { "Tempo (ms)",  10.0f, 2000.0f, &delayTimeMs },
        { "Feedback",    0.0f,  0.90f,   &feedback },
        { "Mix Dry/Wet", 0.0f,  1.0f,    &mix }
    };
    return std::make_unique<GenericSliderEditor>("Delay Stereo", p);
}
