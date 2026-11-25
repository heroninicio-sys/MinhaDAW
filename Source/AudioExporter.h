#pragma once
#include <JuceHeader.h>
#include "AudioTrack.h"

class AudioExporter
{
public:
    enum class ExportFormat
    {
        WAV,
        FLAC,
        MP3
    };

    static juce::String bounceProjectToFile(const juce::File& outputFile,
                                            juce::OwnedArray<AudioTrack>& tracks,
                                            juce::CriticalSection& trackLock,
                                            double sampleRate,
                                            int blockSize,
                                            double bpm,
                                            ExportFormat formatType)
    {
        juce::File renderTarget = (formatType == ExportFormat::MP3)
            ? juce::File::createTempFile(".wav")
            : outputFile;

        {
            const juce::ScopedLock sl(trackLock);

            double totalDurationSeconds = 0.0;
            bool anySolo = false;

            for (auto* track : tracks)
            {
                totalDurationSeconds = juce::jmax(totalDurationSeconds, track->getMaxTimelineEnd(bpm));
                if (track->isSoloed)
                    anySolo = true;
            }

            if (totalDurationSeconds <= 0.05)
                return "ERRO: Projeto vazio. Adicione audio ou notas MIDI antes de exportar.";

            totalDurationSeconds += 2.0;
            juce::int64 totalSamplesToRender = (juce::int64)(totalDurationSeconds * sampleRate);

            if (renderTarget.existsAsFile())
                renderTarget.deleteFile();

            auto fileStream = std::unique_ptr<juce::FileOutputStream>(renderTarget.createOutputStream());
            if (fileStream == nullptr)
                return "ERRO: Nao foi possivel criar o arquivo de destino.";

            std::unique_ptr<juce::AudioFormat> audioFormat;
            if (formatType == ExportFormat::FLAC)
                audioFormat = std::make_unique<juce::FlacAudioFormat>();
            else
                audioFormat = std::make_unique<juce::WavAudioFormat>();

            std::unique_ptr<juce::AudioFormatWriter> writer(
                audioFormat->createWriterFor(fileStream.get(), sampleRate, 2, 24, {}, 0));

            if (writer == nullptr)
                return "ERRO: Falha ao inicializar o gravador de audio.";

            fileStream.release();

            juce::AudioBuffer<float> mixBuffer(2, blockSize);
            juce::AudioSourceChannelInfo channelInfo(&mixBuffer, 0, blockSize);

            for (auto* track : tracks)
                track->prepareToPlay(blockSize, sampleRate);

            juce::int64 samplesRendered = 0;
            while (samplesRendered < totalSamplesToRender)
            {
                int samplesThisBlock = (int)juce::jmin((juce::int64)blockSize, totalSamplesToRender - samplesRendered);
                channelInfo.numSamples = samplesThisBlock;
                mixBuffer.clear();

                double currentTimeSec = (double)samplesRendered / sampleRate;

                for (auto* track : tracks)
                {
                    track->processTrack(channelInfo,
                                        nullptr,
                                        0,
                                        currentTimeSec,
                                        bpm,
                                        true,
                                        anySolo);
                }

                for (int ch = 0; ch < mixBuffer.getNumChannels(); ++ch)
                {
                    float* data = mixBuffer.getWritePointer(ch);
                    for (int i = 0; i < samplesThisBlock; ++i)
                        data[i] = juce::jlimit(-0.99f, 0.99f, data[i]);
                }

                writer->writeFromAudioSampleBuffer(mixBuffer, 0, samplesThisBlock);
                samplesRendered += samplesThisBlock;
            }
        }

        if (formatType == ExportFormat::MP3)
        {
            bool encoded = convertWavToMp3(renderTarget, outputFile);
            renderTarget.deleteFile();

            if (!encoded)
            {
                return "ERRO MP3: Coloque 'ffmpeg.exe' ou 'lame.exe' na mesma pasta da DAW (ou no PATH do Windows) para codificar MP3.";
            }
        }

        return "OK: Mixagem exportada com sucesso para:\n" + outputFile.getFullPathName();
    }

private:
    static bool convertWavToMp3(const juce::File& wavSource, const juce::File& mp3Dest)
    {
        if (mp3Dest.existsAsFile())
            mp3Dest.deleteFile();

        auto appDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        
        juce::File localFfmpeg = appDir.getChildFile("ffmpeg.exe");
        juce::String ffmpegCmd = localFfmpeg.existsAsFile() ? localFfmpeg.getFullPathName() : "ffmpeg";

        juce::StringArray ffmpegArgs {
            ffmpegCmd, "-y", "-i", wavSource.getFullPathName(),
            "-codec:a", "libmp3lame", "-b:a", "320k",
            mp3Dest.getFullPathName()
        };

        juce::ChildProcess proc;
        if (proc.start(ffmpegArgs) && proc.waitForProcessToFinish(30000))
        {
            if (mp3Dest.existsAsFile() && mp3Dest.getSize() > 0)
                return true;
        }

        juce::File localLame = appDir.getChildFile("lame.exe");
        juce::String lameCmd = localLame.existsAsFile() ? localLame.getFullPathName() : "lame";

        juce::StringArray lameArgs {
            lameCmd, "-b", "320", "-q", "2",
            wavSource.getFullPathName(),
            mp3Dest.getFullPathName()
        };

        if (proc.start(lameArgs) && proc.waitForProcessToFinish(30000))
        {
            if (mp3Dest.existsAsFile() && mp3Dest.getSize() > 0)
                return true;
        }

        return false;
    }
};
