#pragma once
#include <JuceHeader.h>
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

    bool bypassed = false;
};

// ============================================================================
// 1. EQUALIZADOR MULTI-BANDA (Múltiplos de 8: 8, 16, 24 ... até 120 canais)
// ============================================================================
class MultiBandEQEffect : public NativeEffect
{
public:
    MultiBandEQEffect(int initialBands = 8)
    {
        setBandCount(initialBands);
    }

    juce::String getName() const override   { return "EQ Multi-Banda (" + juce::String(numBands) + " ch)"; }
    juce::String getTypeId() const override { return "NativeEQ"; }

    void setBandCount(int bands)
    {
        const juce::ScopedLock sl(lock);
        numBands = juce::jlimit(8, 120, (bands / 8) * 8);
        bandGainsDb.assign((size_t)numBands, 0.0f);
        rebuildFilters();
    }

    int getBandCount() const { return numBands; }

    void setBandGain(int bandIndex, float gainDb)
    {
        const juce::ScopedLock sl(lock);
        if (bandIndex >= 0 && bandIndex < numBands)
        {
            bandGainsDb[(size_t)bandIndex] = juce::jlimit(-18.0f, 18.0f, gainDb);
            updateFilterCoefficients(bandIndex);
        }
    }

    float getBandGain(int bandIndex) const
    {
        if (bandIndex >= 0 && bandIndex < numBands)
            return bandGainsDb[(size_t)bandIndex];
        return 0.0f;
    }

    void prepare(double sr, int) override
    {
        const juce::ScopedLock sl(lock);
        sampleRate = sr > 0.0 ? sr : 44100.0;
        rebuildFilters();
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed) return;
        const juce::ScopedLock sl(lock);

        int numCh = juce::jmin(2, buffer.getNumChannels());
        int numSamples = buffer.getNumSamples();

        for (int b = 0; b < numBands; ++b)
        {
            if (std::abs(bandGainsDb[(size_t)b]) < 0.05f)
                continue;

            for (int ch = 0; ch < numCh; ++ch)
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
        auto* obj = new juce::DynamicObject();
        obj->setProperty("numBands", numBands);
        juce::Array<juce::var> gains;
        for (float g : bandGainsDb) gains.add(g);
        obj->setProperty("gains", gains);
        return juce::var(obj);
    }

    void loadState(const juce::var& state) override
    {
        if (auto* obj = state.getDynamicObject())
        {
            int bCount = (int)obj->getProperty("numBands");
            setBandCount(bCount > 0 ? bCount : 8);
            if (auto* arr = obj->getProperty("gains").getArray())
            {
                for (int i = 0; i < juce::jmin(numBands, arr->size()); ++i)
                    setBandGain(i, (float)(*arr)[i]);
            }
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;

private:
    void rebuildFilters()
    {
        filters[0].resize((size_t)numBands);
        filters[1].resize((size_t)numBands);
        for (int b = 0; b < numBands; ++b)
            updateFilterCoefficients(b);
    }

    void updateFilterCoefficients(int b)
    {
        double norm = (numBands > 1) ? (double)b / (double)(numBands - 1) : 0.5;
        double freq = 20.0 * std::pow(1000.0, norm);
        freq = juce::jlimit(20.0, sampleRate * 0.45, freq);

        double q = juce::jlimit(1.2, 14.0, (double)numBands / 8.0);
        float linearGain = juce::Decibels::decibelsToGain(bandGainsDb[(size_t)b]);

        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, (float)freq, (float)q, linearGain);
        filters[0][(size_t)b].coefficients = coeffs;
        filters[1][(size_t)b].coefficients = coeffs;
    }

    juce::CriticalSection lock;
    double sampleRate = 44100.0;
    int numBands = 8;
    std::vector<float> bandGainsDb;
    std::vector<juce::dsp::IIR::Filter<float>> filters[2];
};

// ============================================================================
// 2. COMPRESSOR DINÂMICO
// ============================================================================
class CompressorEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Compressor Dinamico"; }
    juce::String getTypeId() const override { return "NativeComp"; }

    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float makeupDb = 3.0f;

    void prepare(double sr, int) override
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        envelope = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed) return;

        float attackCoeff  = std::exp(-1.0f / (float)(sampleRate * (attackMs * 0.001f)));
        float releaseCoeff = std::exp(-1.0f / (float)(sampleRate * (releaseMs * 0.001f)));
        float makeupLin    = juce::Decibels::decibelsToGain(makeupDb);

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

            if (envDb > thresholdDb)
            {
                float overDb = envDb - thresholdDb;
                gainReductionDb = overDb * (1.0f - (1.0f / juce::jmax(1.0f, ratio)));
            }

            float totalGain = juce::Decibels::decibelsToGain(-gainReductionDb) * makeupLin;

            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * totalGain);
        }
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("threshold", thresholdDb);
        o->setProperty("ratio", ratio);
        o->setProperty("attack", attackMs);
        o->setProperty("release", releaseMs);
        o->setProperty("makeup", makeupDb);
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            thresholdDb = (float)o->getProperty("threshold");
            ratio       = (float)o->getProperty("ratio");
            attackMs    = (float)o->getProperty("attack");
            releaseMs   = (float)o->getProperty("release");
            makeupDb    = (float)o->getProperty("makeup");
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;

private:
    double sampleRate = 44100.0;
    float envelope = 0.0f;
};

// ============================================================================
// 3. DISTORÇÃO / OVERDRIVE
// ============================================================================
class DistortionEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Distorcao / Overdrive"; }
    juce::String getTypeId() const override { return "NativeDist"; }

    float drive = 8.0f;
    float mix = 0.8f;
    float outputDb = -3.0f;

    void prepare(double, int) override {}

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed) return;
        float outGain = juce::Decibels::decibelsToGain(outputDb);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float dry = data[i];
                float wet = std::tanh(dry * drive);
                data[i] = ((dry * (1.0f - mix)) + (wet * mix)) * outGain;
            }
        }
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("drive", drive);
        o->setProperty("mix", mix);
        o->setProperty("output", outputDb);
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            drive    = (float)o->getProperty("drive");
            mix      = (float)o->getProperty("mix");
            outputDb = (float)o->getProperty("output");
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;
};

// ============================================================================
// 4. AUTO-TUNE VOCAL
// ============================================================================
class AutoTuneEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Auto-Tune Vocal"; }
    juce::String getTypeId() const override { return "NativeAutoTune"; }

    float retuneSpeed = 0.85f;
    float mix = 1.0f;

    void prepare(double sr, int) override
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        analysisBuffer.assign(2048, 0.0f);
        delayLine.assign(4096, 0.0f);
        writeIdx = 0;
        phase = 0.0f;
        currentPitchRatio = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed || buffer.getNumChannels() == 0) return;

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

        float alpha = juce::jlimit(0.02f, 0.95f, retuneSpeed * 0.5f);
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
            float out = (inSample * (1.0f - mix)) + (tuned * mix);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, out);

            writeIdx = (writeIdx + 1) % (int)delayLine.size();
        }
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("retuneSpeed", retuneSpeed);
        o->setProperty("mix", mix);
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            retuneSpeed = (float)o->getProperty("retuneSpeed");
            mix         = (float)o->getProperty("mix");
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

    double sampleRate = 44100.0;
    std::vector<float> analysisBuffer;
    std::vector<float> delayLine;
    int analysisWritePos = 0;
    int writeIdx = 0;
    float phase = 0.0f;
    float currentPitchRatio = 1.0f;
};

// ============================================================================
// 5. REVERB ESTÉREO
// ============================================================================
class ReverbEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Reverb Stereo"; }
    juce::String getTypeId() const override { return "NativeReverb"; }

    float roomSize = 0.55f;
    float damping = 0.5f;
    float wetLevel = 0.30f;

    void prepare(double sr, int) override
    {
        reverb.setSampleRate(sr > 0.0 ? sr : 44100.0);
        updateParams();
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed) return;
        updateParams();

        if (buffer.getNumChannels() >= 2)
            reverb.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
        else if (buffer.getNumChannels() == 1)
            reverb.processMono(buffer.getWritePointer(0), buffer.getNumSamples());
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("roomSize", roomSize);
        o->setProperty("damping", damping);
        o->setProperty("wetLevel", wetLevel);
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            roomSize = (float)o->getProperty("roomSize");
            damping  = (float)o->getProperty("damping");
            wetLevel = (float)o->getProperty("wetLevel");
            updateParams();
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;

private:
    void updateParams()
    {
        params.roomSize = roomSize;
        params.damping  = damping;
        params.wetLevel = wetLevel;
        params.dryLevel = 1.0f - (wetLevel * 0.5f);
        params.width    = 1.0f;
        reverb.setParameters(params);
    }

    juce::Reverb reverb;
    juce::Reverb::Parameters params;
};

// ============================================================================
// 6. DELAY ESTÉREO
// ============================================================================
class DelayEffect : public NativeEffect
{
public:
    juce::String getName() const override   { return "Delay Stereo"; }
    juce::String getTypeId() const override { return "NativeDelay"; }

    float delayTimeMs = 350.0f;
    float feedback = 0.40f;
    float mix = 0.35f;

    void prepare(double sr, int) override
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        int maxSamples = (int)(sampleRate * 2.5);
        delayBuffer.setSize(2, maxSamples);
        delayBuffer.clear();
        writePos = 0;
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (bypassed || delayBuffer.getNumSamples() == 0) return;

        int numCh = juce::jmin(2, buffer.getNumChannels());
        int numSamples = buffer.getNumSamples();
        int bufLen = delayBuffer.getNumSamples();
        int delaySamples = juce::jlimit(1, bufLen - 1, (int)((delayTimeMs * 0.001f) * (float)sampleRate));

        for (int i = 0; i < numSamples; ++i)
        {
            int readPos = (writePos - delaySamples + bufLen) % bufLen;

            for (int ch = 0; ch < numCh; ++ch)
            {
                float in = buffer.getSample(ch, i);
                float delayed = delayBuffer.getSample(ch, readPos);

                delayBuffer.setSample(ch, writePos, in + (delayed * feedback));
                buffer.setSample(ch, i, (in * (1.0f - mix)) + (delayed * mix));
            }

            writePos = (writePos + 1) % bufLen;
        }
    }

    juce::var saveState() const override
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("delayTimeMs", delayTimeMs);
        o->setProperty("feedback", feedback);
        o->setProperty("mix", mix);
        return juce::var(o);
    }

    void loadState(const juce::var& s) override
    {
        if (auto* o = s.getDynamicObject())
        {
            delayTimeMs = (float)o->getProperty("delayTimeMs");
            feedback    = (float)o->getProperty("feedback");
            mix         = (float)o->getProperty("mix");
        }
    }

    std::unique_ptr<juce::Component> createEditor() override;

private:
    double sampleRate = 44100.0;
    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
};

// ============================================================================
// INTERFACES GRÁFICAS LEVES PARA OS PLUGINS NATIVOS
// ============================================================================
class MultiBandEQEditor : public juce::Component
{
public:
    MultiBandEQEditor(MultiBandEQEffect& eqRef) : eq(eqRef)
    {
        addAndMakeVisible(bandsLabel);
        bandsLabel.setText("Canais EQ (Multiplos de 8):", juce::dontSendNotification);
        bandsLabel.setColour(juce::Label::textColourId, juce::Colours::white);

        addAndMakeVisible(bandSelector);
        for (int b = 8; b <= 120; b += 8)
            bandSelector.addItem(juce::String(b) + " Bandas", b);
        bandSelector.setSelectedId(eq.getBandCount(), juce::dontSendNotification);
        bandSelector.onChange = [this]
        {
            eq.setBandCount(bandSelector.getSelectedId());
            repaint();
        };

        addAndMakeVisible(resetBtn);
        resetBtn.setButtonText("Flat (0 dB)");
        resetBtn.onClick = [this]
        {
            for (int i = 0; i < eq.getBandCount(); ++i)
                eq.setBandGain(i, 0.0f);
            repaint();
        };

        setSize(760, 320);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff181820));
        auto graph = getLocalBounds().reduced(16).withTrimmedTop(42);

        g.setColour(juce::Colour(0xff22222d));
        g.fillRoundedRectangle(graph.toFloat(), 6.0f);

        float midY = (float)graph.getCentreY();
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.drawHorizontalLine((int)midY, (float)graph.getX(), (float)graph.getRight());

        int count = eq.getBandCount();
        float barW = (float)graph.getWidth() / (float)count;

        for (int i = 0; i < count; ++i)
        {
            float gainDb = eq.getBandGain(i);
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
        g.drawText("Arraste o mouse sobre as barras para desenhar a curva de EQ", graph.reduced(6).removeFromTop(18), juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& e) override { editBandAt(e.getPosition()); }
    void mouseDrag(const juce::MouseEvent& e) override { editBandAt(e.getPosition()); }

    void resized() override
    {
        auto top = getLocalBounds().reduced(16).removeFromTop(30);
        bandsLabel.setBounds(top.removeFromLeft(180));
        bandSelector.setBounds(top.removeFromLeft(140));
        resetBtn.setBounds(top.removeFromRight(100));
    }

private:
    void editBandAt(juce::Point<int> p)
    {
        auto graph = getLocalBounds().reduced(16).withTrimmedTop(42);
        if (!graph.contains(p)) return;

        int count = eq.getBandCount();
        int bandIdx = juce::jlimit(0, count - 1, (int)(((float)(p.x - graph.getX()) / (float)graph.getWidth()) * (float)count));
        float normY = ((float)graph.getCentreY() - (float)p.y) / ((float)graph.getHeight() * 0.45f);
        eq.setBandGain(bandIdx, juce::jlimit(-18.0f, 18.0f, normY * 18.0f));
        repaint();
    }

    MultiBandEQEffect& eq;
    juce::Label bandsLabel;
    juce::ComboBox bandSelector;
    juce::TextButton resetBtn;
};

class GenericSliderEditor : public juce::Component
{
public:
    struct ParamDef { juce::String name; float min; float max; float* target; };

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
            sld->setValue(*p.target, juce::dontSendNotification);
            float* ptr = p.target;
            sld->onValueChange = [sld, ptr] { *ptr = (float)sld->getValue(); };
            addAndMakeVisible(sld);
        }
        setSize(420, 60 + (int)params.size() * 42);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1e1e26));
        g.setColour(juce::Colours::cyan);
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

inline std::unique_ptr<juce::Component> MultiBandEQEffect::createEditor()
{
    return std::make_unique<MultiBandEQEditor>(*this);
}

inline std::unique_ptr<juce::Component> CompressorEffect::createEditor()
{
    return std::make_unique<GenericSliderEditor>("Compressor Dinamico", {
        { "Threshold (dB)", -60.0f, 0.0f,   &thresholdDb },
        { "Ratio (:1)",       1.0f,  20.0f,  &ratio },
        { "Attack (ms)",      0.5f,  150.0f, &attackMs },
        { "Release (ms)",     10.0f, 1000.0f,&releaseMs },
        { "Makeup (dB)",      0.0f,  24.0f,  &makeupDb }
    });
}

inline std::unique_ptr<juce::Component> DistortionEffect::createEditor()
{
    return std::make_unique<GenericSliderEditor>("Distorcao / Overdrive", {
        { "Drive",       1.0f,  40.0f, &drive },
        { "Mix Dry/Wet", 0.0f,  1.0f,  &mix },
        { "Output (dB)", -24.0f, 6.0f, &outputDb }
    });
}

inline std::unique_ptr<juce::Component> AutoTuneEffect::createEditor()
{
    return std::make_unique<GenericSliderEditor>("Auto-Tune Vocal (Correcao Cromatica)", {
        { "Retune Speed", 0.05f, 1.0f, &retuneSpeed },
        { "Mix Dry/Wet",  0.0f,  1.0f, &mix }
    });
}

inline std::unique_ptr<juce::Component> ReverbEffect::createEditor()
{
    return std::make_unique<GenericSliderEditor>("Reverb Stereo", {
        { "Room Size",   0.0f, 1.0f, &roomSize },
        { "Damping",     0.0f, 1.0f, &damping },
        { "Mix Wet",     0.0f, 1.0f, &wetLevel }
    });
}

inline std::unique_ptr<juce::Component> DelayEffect::createEditor()
{
    return std::make_unique<GenericSliderEditor>("Delay Stereo", {
        { "Tempo (ms)",  10.0f, 2000.0f, &delayTimeMs },
        { "Feedback",    0.0f,  0.90f,   &feedback },
        { "Mix Dry/Wet", 0.0f,  1.0f,    &mix }
    });
}