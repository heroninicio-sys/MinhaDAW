#pragma once
#include <JuceHeader.h>

struct AudioClip
{
    juce::String id;
    juce::File audioFile;
    
    double timelineStart = 0.0;
    double sourceStartOffset = 0.0;
    double duration = 0.0;
    double totalFileLength = 0.0;

    double fadeInDuration = 0.0;
    double fadeOutDuration = 0.0;

    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    double fileSampleRate = 44100.0;

    AudioClip(const juce::File& file, juce::AudioFormatManager& formatManager, double startPosOnTimeline = 0.0)
        : id(juce::Uuid().toString()), audioFile(file), timelineStart(startPosOnTimeline)
    {
        if (auto* reader = formatManager.createReaderFor(file))
        {
            fileSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
            totalFileLength = (double)reader->lengthInSamples / fileSampleRate;
            duration = totalFileLength;
            readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
        }
    }

    std::unique_ptr<AudioClip> cloneClip(juce::AudioFormatManager& formatManager) const
    {
        auto copy = std::make_unique<AudioClip>(audioFile, formatManager, timelineStart);
        copy->sourceStartOffset = sourceStartOffset;
        copy->duration = duration;
        copy->fadeInDuration = fadeInDuration;
        copy->fadeOutDuration = fadeOutDuration;
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
            float fadeGain = 1.0f;

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
