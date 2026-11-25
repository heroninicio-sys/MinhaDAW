#pragma once
#include <JuceHeader.h>
#include <cmath>

struct AudioClip
{
    juce::String id;
    juce::File audioFile;
    juce::Array<juce::File> takes;
    int activeTakeIndex = 0;

    double timelineStart = 0.0;
    double sourceStartOffset = 0.0;
    double duration = 0.0;
    double totalFileLength = 0.0;

    double fadeInDuration = 0.0;
    double fadeOutDuration = 0.0;
    float clipGain = 1.0f;

    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    double fileSampleRate = 44100.0;

    AudioClip(const juce::File& file, juce::AudioFormatManager& formatManager, double startPosOnTimeline = 0.0)
        : id(juce::Uuid().toString()), audioFile(file), timelineStart(startPosOnTimeline)
    {
        takes.add(file);
        loadFromFileInternal(file, formatManager, true);
    }

    void loadFromFileInternal(const juce::File& file, juce::AudioFormatManager& formatManager, bool resetDuration)
    {
        if (auto* reader = formatManager.createReaderFor(file))
        {
            audioFile = file;
            fileSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
            totalFileLength = (double)reader->lengthInSamples / fileSampleRate;
            if (resetDuration)
                duration = totalFileLength;
            else
                duration = juce::jmin(duration, totalFileLength);
            readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
        }
    }

    void cycleNextTake(juce::AudioFormatManager& formatManager)
    {
        if (takes.size() <= 1) return;
        activeTakeIndex = (activeTakeIndex + 1) % takes.size();
        loadFromFileInternal(takes[activeTakeIndex], formatManager, false);
    }

    float analyzePeakLevel(juce::AudioFormatManager& formatManager) const
    {
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(audioFile));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return 0.0f;

        float maxMag = 0.0f;
        const int chunk = 16384;
        juce::AudioBuffer<float> buf((int)reader->numChannels, chunk);
        juce::int64 pos = 0;

        while (pos < reader->lengthInSamples)
        {
            int toRead = (int)juce::jmin((juce::int64)chunk, reader->lengthInSamples - pos);
            reader->read(&buf, 0, toRead, pos, true, true);
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                maxMag = juce::jmax(maxMag, buf.getMagnitude(ch, 0, toRead));
            pos += toRead;
        }
        return maxMag * clipGain;
    }

    void normalizeClip(juce::AudioFormatManager& formatManager)
    {
        float currentPeak = analyzePeakLevel(formatManager);
        if (currentPeak > 0.0001f)
            clipGain = juce::jlimit(0.1f, 8.0f, (0.98f / currentPeak) * clipGain);
    }

    std::unique_ptr<AudioClip> cloneClip(juce::AudioFormatManager& formatManager) const
    {
        auto copy = std::make_unique<AudioClip>(audioFile, formatManager, timelineStart);
        copy->takes = takes;
        copy->activeTakeIndex = activeTakeIndex;
        copy->sourceStartOffset = sourceStartOffset;
        copy->duration = duration;
        copy->fadeInDuration = fadeInDuration;
        copy->fadeOutDuration = fadeOutDuration;
        copy->clipGain = clipGain;
        return copy;
    }

    double getTimelineEnd() const { return timelineStart + duration; }

    void renderBlock(juce::AudioBuffer<float>& trackBuffer,
                     juce::AudioBuffer<float>& tempClipBuffer,
                     double blockStartTime,
                     double sampleRate)
    {
        if (readerSource == nullptr || duration <= 0.0 || sampleRate <= 0.0)
            return;

        const int numSamples = trackBuffer.getNumSamples();
        const double blockDuration = (double)numSamples / sampleRate;
        const double blockEndTime = blockStartTime + blockDuration;

        if (blockEndTime <= timelineStart || blockStartTime >= getTimelineEnd())
            return;

        double overlapStart = juce::jmax(blockStartTime, timelineStart);
        double overlapEnd   = juce::jmin(blockEndTime, getTimelineEnd());

        int startSampleInBlock = (int)std::round((overlapStart - blockStartTime) * sampleRate);
        int samplesToRead      = (int)std::round((overlapEnd - overlapStart) * sampleRate);

        if (samplesToRead <= 0)
            return;

        if (startSampleInBlock + samplesToRead > numSamples)
            samplesToRead = numSamples - startSampleInBlock;

        if (samplesToRead <= 0)
            return;

        double timeInsideClip = overlapStart - timelineStart;
        double fileReadTime   = sourceStartOffset + timeInsideClip;
        juce::int64 readPosSamples = (juce::int64)(fileReadTime * fileSampleRate);

        readerSource->setNextReadPosition(readPosSamples);

        tempClipBuffer.clear();
        juce::AudioSourceChannelInfo info(&tempClipBuffer, 0, samplesToRead);
        readerSource->getNextAudioBlock(info);

        for (int i = 0; i < samplesToRead; ++i)
        {
            double currentClipTime = timeInsideClip + ((double)i / sampleRate);
            float fadeGain = clipGain;

            if (fadeInDuration > 0.001 && currentClipTime < fadeInDuration)
                fadeGain *= (float)(currentClipTime / fadeInDuration);

            double timeFromEnd = duration - currentClipTime;
            if (fadeOutDuration > 0.001 && timeFromEnd < fadeOutDuration)
                fadeGain *= (float)juce::jmax(0.0, timeFromEnd / fadeOutDuration);

            for (int ch = 0; ch < trackBuffer.getNumChannels(); ++ch)
            {
                int srcCh = ch % tempClipBuffer.getNumChannels();
                float sampleVal = tempClipBuffer.getSample(srcCh, i) * fadeGain;
                trackBuffer.addSample(ch, startSampleInBlock + i, sampleVal);
            }
        }
    }
};
