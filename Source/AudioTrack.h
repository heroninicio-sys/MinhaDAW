#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "AudioClip.h"
#include "MidiClip.h"
#include "NativeDSPPlugins.h"

struct EnvelopePoint
{
    double timeSec = 0.0;
    float value = 1.0f; // 0.0 a 1.5 para volume, -1.0 a 1.0 para pan
};

class AudioTrack
{
public:
    AudioTrack(const juce::String& trackName, juce::AudioFormatManager& formatMgr, juce::TimeSliceThread& diskThread)
        : name(trackName), formatManager(formatMgr), backgroundThread(diskThread)
    {
    }

    ~AudioTrack()
    {
        stopRecording(0.0);
        vstPlugins.clear(true);
        nativeEffects.clear(true);
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate)
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        currentBlockSize = samplesPerBlockExpected > 0 ? samplesPerBlockExpected : 128;

        for (auto* vst : vstPlugins)
            vst->prepareToPlay(currentSampleRate, currentBlockSize);

        for (auto* fx : nativeEffects)
            fx->prepare(currentSampleRate, currentBlockSize);

        trackBuffer.setSize(2, currentBlockSize);
        tempClipBuffer.setSize(2, currentBlockSize);
    }

    void releaseResources()
    {
        for (auto* vst : vstPlugins)
            vst->releaseResources();
    }

    void addNativeEffect(std::unique_ptr<NativeEffect> fx)
    {
        fx->prepare(currentSampleRate, currentBlockSize);
        nativeEffects.add(std::move(fx));
    }

    void addVstPlugin(std::unique_ptr<juce::AudioPluginInstance> vst)
    {
        vst->prepareToPlay(currentSampleRate, currentBlockSize);
        vstPlugins.add(std::move(vst));
    }

    bool hasActiveEffects() const
    {
        return !vstPlugins.isEmpty() || !nativeEffects.isEmpty();
    }

    // --- ENVELOPES DE AUTOMAÇÃO DA TRACK ---
    void addOrUpdateVolumeEnvelopePoint(double timeSec, float val)
    {
        const juce::ScopedLock sl(envLock);
        val = juce::jlimit(0.0f, 1.5f, val);
        for (auto& pt : volumeEnvelopePoints)
        {
            if (std::abs(pt.timeSec - timeSec) < 0.02)
            {
                pt.value = val;
                return;
            }
        }
        volumeEnvelopePoints.push_back({ juce::jmax(0.0, timeSec), val });
        std::sort(volumeEnvelopePoints.begin(), volumeEnvelopePoints.end(),
                  [](const EnvelopePoint& a, const EnvelopePoint& b) { return a.timeSec < b.timeSec; });
    }

    float evaluateVolumeEnvelopeAt(double timeSec) const
    {
        const juce::ScopedLock sl(envLock);
        if (!showVolumeEnvelope || volumeEnvelopePoints.empty())
            return gain;

        if (timeSec <= volumeEnvelopePoints.front().timeSec)
            return volumeEnvelopePoints.front().value;
        if (timeSec >= volumeEnvelopePoints.back().timeSec)
            return volumeEnvelopePoints.back().value;

        for (size_t i = 0; i + 1 < volumeEnvelopePoints.size(); ++i)
        {
            const auto& p0 = volumeEnvelopePoints[i];
            const auto& p1 = volumeEnvelopePoints[i + 1];
            if (timeSec >= p0.timeSec && timeSec <= p1.timeSec)
            {
                double span = juce::jmax(0.0001, p1.timeSec - p0.timeSec);
                float t = (float)((timeSec - p0.timeSec) / span);
                return p0.value + t * (p1.value - p0.value);
            }
        }
        return gain;
    }

    std::vector<EnvelopePoint> getVolumeEnvelopeCopy() const
    {
        const juce::ScopedLock sl(envLock);
        return volumeEnvelopePoints;
    }

    void injectLiveMidiNote(int noteNumber, bool isNoteOn)
    {
        const juce::ScopedLock sl(liveMidiLock);
        if (isNoteOn)
            liveMidiBuffer.addEvent(juce::MidiMessage::noteOn(1, noteNumber, 0.85f), 0);
        else
            liveMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, noteNumber), 0);
    }

    void sendAllNotesOff()
    {
        const juce::ScopedLock sl(liveMidiLock);
        liveMidiBuffer.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    }

    void processTrack(const juce::AudioSourceChannelInfo& masterOutput,
                      const float* const* hardwareInput,
                      int numInputChannels,
                      double globalPlayheadTime,
                      double bpm,
                      bool isGlobalPlaying,
                      bool anyTrackSoloed)
    {
        const int numSamples = masterOutput.numSamples;
        if (trackBuffer.getNumSamples() < numSamples)
        {
            trackBuffer.setSize(2, numSamples, false, false, true);
            tempClipBuffer.setSize(2, numSamples, false, false, true);
        }

        trackBuffer.clear();
        trackMidiBuffer.clear();

        if (isArmed && isRecording.load() && threadedWriter != nullptr && hardwareInput != nullptr && numInputChannels > 0)
        {
            threadedWriter->write(hardwareInput, numSamples);

            float inPeak = 0.0f;
            for (int ch = 0; ch < numInputChannels; ++ch)
            {
                if (hardwareInput[ch] != nullptr)
                {
                    auto range = juce::FloatVectorOperations::findMinAndMax(hardwareInput[ch], numSamples);
                    inPeak = juce::jmax(inPeak, std::abs(range.getStart()), std::abs(range.getEnd()));
                }
            }

            liveRecordSamplesWritten += numSamples;
            const juce::ScopedLock rl(liveWaveLock);
            liveRecordPeaks.push_back(inPeak);
        }

        if (isMonitoring && hardwareInput != nullptr && numInputChannels > 0)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                int inCh = ch % numInputChannels;
                if (hardwareInput[inCh] != nullptr)
                    juce::FloatVectorOperations::add(trackBuffer.getWritePointer(ch), hardwareInput[inCh], numSamples);
            }
        }

        if (isGlobalPlaying)
        {
            for (auto* clip : audioClips)
                clip->renderBlock(trackBuffer, tempClipBuffer, globalPlayheadTime, currentSampleRate);

            for (auto* mClip : midiClips)
                mClip->renderMidiBlock(trackMidiBuffer, globalPlayheadTime, numSamples, currentSampleRate, bpm);
        }

        {
            const juce::ScopedLock sl(liveMidiLock);
            if (!liveMidiBuffer.isEmpty())
            {
                trackMidiBuffer.addEvents(liveMidiBuffer, 0, numSamples, 0);
                liveMidiBuffer.clear();
            }
        }

        for (auto* vst : vstPlugins)
            vst->processBlock(trackBuffer, trackMidiBuffer);

        for (auto* fx : nativeEffects)
            fx->process(trackBuffer);

        float effectiveGain = showVolumeEnvelope ? evaluateVolumeEnvelopeAt(globalPlayheadTime) : gain;

        float blockPeak = 0.0f;
        for (int ch = 0; ch < trackBuffer.getNumChannels(); ++ch)
            blockPeak = juce::jmax(blockPeak, trackBuffer.getMagnitude(ch, 0, numSamples));

        if (isRecording.load() && hardwareInput != nullptr && numInputChannels > 0)
        {
            auto r = juce::FloatVectorOperations::findMinAndMax(hardwareInput[0], numSamples);
            blockPeak = juce::jmax(blockPeak, std::abs(r.getStart()), std::abs(r.getEnd()));
        }

        float scaledPeak = blockPeak * effectiveGain;
        float prevPeak = currentPeakLevel.load();
        currentPeakLevel.store(scaledPeak > prevPeak ? scaledPeak : prevPeak * 0.88f);

        bool shouldBeAudible = !isMuted && (!anyTrackSoloed || isSoloed);
        if (shouldBeAudible && masterOutput.buffer->getNumChannels() > 0)
        {
            float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            float leftGain  = effectiveGain * std::cos(angle);
            float rightGain = effectiveGain * std::sin(angle);

            masterOutput.buffer->addFrom(0, masterOutput.startSample, trackBuffer, 0, 0, numSamples, leftGain);
            if (masterOutput.buffer->getNumChannels() > 1)
            {
                int srcRight = trackBuffer.getNumChannels() > 1 ? 1 : 0;
                masterOutput.buffer->addFrom(1, masterOutput.startSample, trackBuffer, srcRight, 0, numSamples, rightGain);
            }
        }
    }

    void addClipFromFile(const juce::File& file, double startTimeOnTimeline = 0.0)
    {
        auto clip = std::make_unique<AudioClip>(file, formatManager, startTimeOnTimeline);
        if (clip->duration > 0.0)
            audioClips.add(std::move(clip));
    }

    MidiClip* createOrGetMidiClipAt(double startBeat, double lengthBeats = 8.0)
    {
        for (auto* mc : midiClips)
            if (startBeat >= mc->timelineStartBeats && startBeat < mc->getEndBeats())
                return mc;

        auto newClip = std::make_unique<MidiClip>(startBeat, lengthBeats);
        newClip->notes.emplace_back(60, 0.0, 1.0, 0.85f);
        return midiClips.add(std::move(newClip));
    }

    bool splitClipsAt(double splitTimeSeconds)
    {
        bool didSplit = false;
        for (int i = audioClips.size() - 1; i >= 0; --i)
        {
            auto* clip = audioClips[i];
            const double minClipLen = 0.05;

            if (splitTimeSeconds > clip->timelineStart + minClipLen &&
                splitTimeSeconds < clip->getTimelineEnd() - minClipLen)
            {
                double leftDuration  = splitTimeSeconds - clip->timelineStart;
                double rightDuration = clip->getTimelineEnd() - splitTimeSeconds;

                auto rightClip = clip->cloneClip(formatManager);
                rightClip->timelineStart     = splitTimeSeconds;
                rightClip->sourceStartOffset = clip->sourceStartOffset + leftDuration;
                rightClip->duration          = rightDuration;
                rightClip->fadeInDuration    = 0.01;

                clip->duration        = leftDuration;
                clip->fadeOutDuration = 0.01;

                audioClips.add(std::move(rightClip));
                didSplit = true;
            }
        }
        return didSplit;
    }

    void removeClip(AudioClip* clipToRemove) { audioClips.removeObject(clipToRemove, true); }
    void removeMidiClip(MidiClip* clipToRemove) { midiClips.removeObject(clipToRemove, true); }

    bool startRecording(const juce::File& targetFile, double recordStartTimelinePos, double roundTripLatencySec = 0.0)
    {
        if (!isArmed) return false;
        stopRecording(recordStartTimelinePos);

        if (!backgroundThread.isThreadRunning())
            backgroundThread.startThread();

        if (currentSampleRate <= 0.0)
            currentSampleRate = 48000.0;

        targetFile.getParentDirectory().createDirectory();
        recordedFile = targetFile;
        recordingStartPos = juce::jmax(0.0, recordStartTimelinePos - roundTripLatencySec);
        pendingLatencyTrimSec = (recordStartTimelinePos < roundTripLatencySec) ? (roundTripLatencySec - recordStartTimelinePos) : 0.0;
        liveRecordSamplesWritten = 0;

        {
            const juce::ScopedLock rl(liveWaveLock);
            liveRecordPeaks.clear();
            liveRecordPeaks.reserve(16384);
        }

        if (auto fileStream = std::unique_ptr<juce::FileOutputStream>(recordedFile.createOutputStream()))
        {
            juce::WavAudioFormat wavFormat;
            if (auto* writer = wavFormat.createWriterFor(fileStream.get(), currentSampleRate, 2, 24, {}, 0))
            {
                fileStream.release();
                threadedWriter.reset(new juce::AudioFormatWriter::ThreadedWriter(writer, backgroundThread, 65536));
                isRecording.store(true);
                return true;
            }
        }
        return false;
    }

    void stopRecording(double)
    {
        if (isRecording.load())
        {
            isRecording.store(false);
            threadedWriter.reset();

            if (recordedFile.existsAsFile() && recordedFile.getSize() > 44)
            {
                auto clip = std::make_unique<AudioClip>(recordedFile, formatManager, recordingStartPos);
                if (clip->duration > pendingLatencyTrimSec + 0.02)
                {
                    clip->sourceStartOffset = pendingLatencyTrimSec;
                    clip->duration -= pendingLatencyTrimSec;
                    audioClips.add(std::move(clip));
                }
            }

            const juce::ScopedLock rl(liveWaveLock);
            liveRecordPeaks.clear();
        }
    }

    bool isCurrentlyRecording() const { return isRecording.load(); }
    double getLiveRecordingStartSec() const { return recordingStartPos; }
    double getLiveRecordingDurationSec() const
    {
        return (currentSampleRate > 0.0) ? ((double)liveRecordSamplesWritten / currentSampleRate) : 0.0;
    }

    std::vector<float> getLiveRecordingPeaksSnapshot() const
    {
        const juce::ScopedLock rl(liveWaveLock);
        return liveRecordPeaks;
    }

    double getMaxTimelineEnd(double bpm) const
    {
        double maxEnd = 0.0;
        for (auto* c : audioClips)
            maxEnd = juce::jmax(maxEnd, c->getTimelineEnd());

        double secPerBeat = 60.0 / juce::jmax(20.0, bpm);
        for (auto* mc : midiClips)
            maxEnd = juce::jmax(maxEnd, mc->getEndBeats() * secPerBeat);

        return maxEnd;
    }

    juce::String getName() const { return name; }
    void setName(const juce::String& n) { name = n; }
    juce::OwnedArray<AudioClip>& getClips() { return audioClips; }
    juce::OwnedArray<MidiClip>& getMidiClips() { return midiClips; }
    juce::OwnedArray<NativeEffect>& getNativeEffects() { return nativeEffects; }
    juce::OwnedArray<juce::AudioPluginInstance>& getVstPlugins() { return vstPlugins; }

    bool isArmed = false;
    bool isMonitoring = false;
    bool isMuted = false;
    bool isSoloed = false;
    bool pinnedToTop = false;
    bool controlsLocked = false;
    bool heightLocked = false;
    bool showVolumeEnvelope = false;
    bool showPanEnvelope = false;
    int folderDepth = 0; // 0 = normal, 1 = pasta/agrupada, -1 = fim de pasta
    int spacerBeforePx = 0;
    int spacerAfterPx = 0;

    float gain = 0.85f;
    float pan = 0.0f;
    std::atomic<float> currentPeakLevel { 0.0f };
    juce::StringArray vstFilePaths;

private:
    juce::String name;
    juce::AudioFormatManager& formatManager;
    juce::TimeSliceThread& backgroundThread;

    juce::OwnedArray<AudioClip> audioClips;
    juce::OwnedArray<MidiClip> midiClips;
    juce::OwnedArray<NativeEffect> nativeEffects;
    juce::OwnedArray<juce::AudioPluginInstance> vstPlugins;

    mutable juce::CriticalSection envLock;
    std::vector<EnvelopePoint> volumeEnvelopePoints;

    juce::AudioBuffer<float> trackBuffer;
    juce::AudioBuffer<float> tempClipBuffer;
    juce::MidiBuffer trackMidiBuffer;

    juce::CriticalSection liveMidiLock;
    juce::MidiBuffer liveMidiBuffer;

    mutable juce::CriticalSection liveWaveLock;
    std::vector<float> liveRecordPeaks;
    juce::int64 liveRecordSamplesWritten = 0;

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::File recordedFile;
    double recordingStartPos = 0.0;
    double pendingLatencyTrimSec = 0.0;
    std::atomic<bool> isRecording { false };

    double currentSampleRate = 48000.0;
    int currentBlockSize = 128;
};
