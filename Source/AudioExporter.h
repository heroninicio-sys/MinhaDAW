#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <utility>
#include <vector>
#include "AudioTrack.h"

struct ExportSettings
{
    enum class Format
    {
        WAV = 1,
        WMA = 2,
        OGG = 3,
        FLAC = 4,
        MP3 = 5
    };

    Format format = Format::MP3;
    double targetSampleRate = 48000.0;
    int bitRateKbps = 320;      // 128, 192, 256, 320
    int bitsPerSample = 24;     // 16 ou 24 para WAV/FLAC
    int numChannels = 2;        // 1 (Mono) ou 2 (Estéreo)

    juce::String getExtension() const
    {
        switch (format)
        {
            case Format::WAV:  return ".wav";
            case Format::WMA:  return ".wma";
            case Format::OGG:  return ".ogg";
            case Format::FLAC: return ".flac";
            case Format::MP3:  return ".mp3";
            default:           return ".wav";
        }
    }

    juce::String getFormatName() const
    {
        switch (format)
        {
            case Format::WAV:  return "WAV";
            case Format::WMA:  return "WMA";
            case Format::OGG:  return "OGG";
            case Format::FLAC: return "FLAC";
            case Format::MP3:  return "MP3";
            default:           return "WAV";
        }
    }
};

class AudioExporter
{
public:
    static juce::String bounceProjectWithSettings(const juce::File& outputFile,
                                                  juce::OwnedArray<AudioTrack>& tracks,
                                                  juce::CriticalSection& trackLock,
                                                  double engineSampleRate,
                                                  int blockSize,
                                                  double bpm,
                                                  const ExportSettings& settings)
    {
        if (engineSampleRate <= 0.0) engineSampleRate = 48000.0;
        if (blockSize <= 0) blockSize = 128;

        const bool needsTranscode = (settings.format == ExportSettings::Format::MP3 ||
                                     settings.format == ExportSettings::Format::OGG ||
                                     settings.format == ExportSettings::Format::WMA);

        juce::File tempWavFile = juce::File::createTempFile(".wav");
        juce::File finalWriteTarget = needsTranscode ? tempWavFile : outputFile;

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
            {
                tempWavFile.deleteFile();
                return "ERRO: Projeto vazio. Adicione audio ou notas MIDI antes de exportar.";
            }

            totalDurationSeconds += 1.5;
            juce::int64 totalEngineSamples = (juce::int64)(totalDurationSeconds * engineSampleRate);

            if (finalWriteTarget.existsAsFile())
                finalWriteTarget.deleteFile();

            auto fileStream = std::unique_ptr<juce::FileOutputStream>(finalWriteTarget.createOutputStream());
            if (fileStream == nullptr)
            {
                tempWavFile.deleteFile();
                return "ERRO: Nao foi possivel criar o arquivo de destino.";
            }

            std::unique_ptr<juce::AudioFormat> audioFormat;
            int writeBits = settings.bitsPerSample;
            if (!needsTranscode && settings.format == ExportSettings::Format::FLAC)
            {
                audioFormat = std::make_unique<juce::FlacAudioFormat>();
                writeBits = juce::jmin(24, settings.bitsPerSample);
            }
            else
            {
                audioFormat = std::make_unique<juce::WavAudioFormat>();
            }

            int outCh = juce::jlimit(1, 2, settings.numChannels);
            std::unique_ptr<juce::AudioFormatWriter> writer(
                audioFormat->createWriterFor(fileStream.get(), settings.targetSampleRate, (unsigned int)outCh, writeBits, {}, 0));

            if (writer == nullptr)
            {
                tempWavFile.deleteFile();
                return "ERRO: Falha ao inicializar o gravador de audio.";
            }

            fileStream.release();

            juce::AudioBuffer<float> mixBuffer(2, blockSize);
            juce::AudioSourceChannelInfo channelInfo(&mixBuffer, 0, blockSize);

            for (auto* track : tracks)
                track->prepareToPlay(blockSize, engineSampleRate);

            double ratio = engineSampleRate / settings.targetSampleRate;
            juce::LagrangeInterpolator resamplers[2];
            resamplers[0].reset();
            resamplers[1].reset();

            juce::int64 samplesRendered = 0;
            while (samplesRendered < totalEngineSamples)
            {
                int samplesThisBlock = (int)juce::jmin((juce::int64)blockSize, totalEngineSamples - samplesRendered);
                channelInfo.numSamples = samplesThisBlock;
                mixBuffer.clear();

                double currentTimeSec = (double)samplesRendered / engineSampleRate;

                for (auto* track : tracks)
                    track->processTrack(channelInfo, nullptr, 0, currentTimeSec, bpm, true, anySolo);

                for (int ch = 0; ch < 2; ++ch)
                {
                    float* data = mixBuffer.getWritePointer(ch);
                    for (int i = 0; i < samplesThisBlock; ++i)
                        data[i] = juce::jlimit(-0.99f, 0.99f, data[i]);
                }

                if (std::abs(ratio - 1.0) < 0.0001)
                {
                    if (outCh == 1)
                    {
                        juce::AudioBuffer<float> monoBuf(1, samplesThisBlock);
                        monoBuf.copyFrom(0, 0, mixBuffer, 0, 0, samplesThisBlock);
                        monoBuf.addFrom(0, 0, mixBuffer, 1, 0, samplesThisBlock);
                        monoBuf.applyGain(0, 0, samplesThisBlock, 0.5f);
                        writer->writeFromAudioSampleBuffer(monoBuf, 0, samplesThisBlock);
                    }
                    else
                    {
                        writer->writeFromAudioSampleBuffer(mixBuffer, 0, samplesThisBlock);
                    }
                }
                else
                {
                    int expectedOutSamples = (int)std::ceil((double)samplesThisBlock / ratio) + 4;
                    juce::AudioBuffer<float> resampledBuf(outCh, expectedOutSamples);
                    resampledBuf.clear();

                    for (int ch = 0; ch < outCh; ++ch)
                    {
                        int srcCh = (outCh == 1) ? 0 : ch;
                        resamplers[ch].process(ratio,
                                               mixBuffer.getReadPointer(srcCh),
                                               resampledBuf.getWritePointer(ch),
                                               expectedOutSamples);
                    }

                    int actualOut = (int)((double)samplesThisBlock / ratio);
                    actualOut = juce::jlimit(1, expectedOutSamples, actualOut);
                    writer->writeFromAudioSampleBuffer(resampledBuf, 0, actualOut);
                }

                samplesRendered += samplesThisBlock;
            }
        }

        if (needsTranscode)
        {
            bool ok = transcodeWithFfmpeg(tempWavFile, outputFile, settings);
            tempWavFile.deleteFile();

            if (!ok)
            {
                return "AVISO (" + settings.getFormatName() + "): Coloque 'ffmpeg.exe' na pasta do executavel para gerar " +
                       settings.getFormatName() + " direto em " + juce::String(settings.bitRateKbps) +
                       " kbps (taxa " + juce::String((int)settings.targetSampleRate) + " Hz).";
            }
        }

        return "OK: Exportado em " + settings.getFormatName() + " (" +
               juce::String((int)settings.targetSampleRate) + " Hz, " +
               juce::String(settings.bitRateKbps) + " kbps) para:\n" + outputFile.getFullPathName();
    }

    static void showExportDialog(juce::OwnedArray<AudioTrack>& tracks,
                                 juce::CriticalSection& trackLock,
                                 double engineSampleRate,
                                 int blockSize,
                                 double bpm)
    {
        auto* panel = new ExportDialogPanel(tracks, trackLock, engineSampleRate, blockSize, bpm);
        panel->setSize(440, 320);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned(panel);
        opts.dialogTitle = "Configurar Exportacao / Renderizacao";
        opts.dialogBackgroundColour = juce::Colour(0xff181a22);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.launchAsync();
    }

private:
    static bool transcodeWithFfmpeg(const juce::File& wavSource,
                                    const juce::File& targetFile,
                                    const ExportSettings& settings)
    {
        if (targetFile.existsAsFile())
            targetFile.deleteFile();

        auto appDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        juce::File localFfmpeg = appDir.getChildFile("ffmpeg.exe");
        juce::String ffmpegBin = localFfmpeg.existsAsFile() ? localFfmpeg.getFullPathName() : "ffmpeg";

        juce::String bitrateArg = juce::String(settings.bitRateKbps) + "k";
        juce::String codecArg = "libmp3lame";
        if (settings.format == ExportSettings::Format::OGG) codecArg = "libvorbis";
        else if (settings.format == ExportSettings::Format::WMA) codecArg = "wmav2";

        juce::StringArray args {
            ffmpegBin, "-y", "-i", wavSource.getFullPathName(),
            "-ar", juce::String((int)settings.targetSampleRate),
            "-ac", juce::String(settings.numChannels),
            "-codec:a", codecArg, "-b:a", bitrateArg,
            targetFile.getFullPathName()
        };

        juce::ChildProcess proc;
        if (proc.start(args) && proc.waitForProcessToFinish(45000))
        {
            if (targetFile.existsAsFile() && targetFile.getSize() > 0)
                return true;
        }
        return false;
    }

    class ExportDialogPanel : public juce::Component
    {
    public:
        ExportDialogPanel(juce::OwnedArray<AudioTrack>& tRef,
                          juce::CriticalSection& lRef,
                          double sr, int bs, double curBpm)
            : tracks(tRef), lock(lRef), sampleRate(sr), blockSize(bs), bpm(curBpm)
        {
            addLabelAndBox(formatLbl, "Formato de Saida:", formatBox,
                           { { "WAV (PCM Sem Perdas)", 1 },
                             { "FLAC (Compressao Sem Perdas)", 4 },
                             { "MP3 (MPEG Audio)", 5 },
                             { "OGG (Ogg Vorbis)", 3 },
                             { "WMA (Windows Media Audio)", 2 } }, 5);

            addLabelAndBox(rateLbl, "Taxa de Amostragem:", rateBox,
                           { { "44100 Hz", 44100 },
                             { "48000 Hz", 48000 },
                             { "88200 Hz", 88200 },
                             { "96000 Hz", 96000 } }, 48000);

            addLabelAndBox(bitrateLbl, "Bit Rate / Resolucao:", bitrateBox,
                           { { "128 kbps", 128 },
                             { "192 kbps", 192 },
                             { "256 kbps", 256 },
                             { "320 kbps", 320 } }, 320);

            addLabelAndBox(chLbl, "Canais de Saida:", chBox,
                           { { "Estereo (2 Canais)", 2 },
                             { "Mono (1 Canal)", 1 } }, 2);

            addAndMakeVisible(renderBtn);
            renderBtn.setButtonText("Exportar Agora");
            renderBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2b9348));
            renderBtn.onClick = [this] { runExport(); };
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff181a22));
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(18, 16);
            auto placeRow = [&](juce::Label& lbl, juce::ComboBox& box)
            {
                auto row = area.removeFromTop(38);
                lbl.setBounds(row.removeFromLeft(165));
                box.setBounds(row.reduced(0, 4));
                area.removeFromTop(6);
            };

            placeRow(formatLbl, formatBox);
            placeRow(rateLbl, rateBox);
            placeRow(bitrateLbl, bitrateBox);
            placeRow(chLbl, chBox);

            area.removeFromTop(8);
            renderBtn.setBounds(area.removeFromTop(38).reduced(60, 0));
        }

    private:
        void addLabelAndBox(juce::Label& lbl, const juce::String& text, juce::ComboBox& box,
                            const std::vector<std::pair<juce::String, int>>& items, int selectedId)
        {
            addAndMakeVisible(lbl);
            lbl.setText(text, juce::dontSendNotification);
            lbl.setColour(juce::Label::textColourId, juce::Colours::white);
            lbl.setFont(juce::FontOptions(12.5f, juce::Font::bold));

            addAndMakeVisible(box);
            for (const auto& it : items)
                box.addItem(it.first, it.second);
            box.setSelectedId(selectedId, juce::dontSendNotification);
        }

        void runExport()
        {
            ExportSettings s;
            s.format = (ExportSettings::Format)formatBox.getSelectedId();
            s.targetSampleRate = (double)rateBox.getSelectedId();
            s.bitRateKbps = bitrateBox.getSelectedId();
            s.numChannels = chBox.getSelectedId();

            juce::String defaultName = "Mix_" + juce::String((int)(s.targetSampleRate / 1000.0)) + "k_" +
                                       juce::String(s.bitRateKbps) + "kbps" + s.getExtension();
            juce::String wildcard = "*" + s.getExtension();

            auto chooser = std::make_shared<juce::FileChooser>(
                "Salvar Arquivo (" + s.getFormatName() + ")",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(defaultName),
                wildcard);

            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                [this, chooser, s](const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();
                    if (file != juce::File())
                    {
                        juce::File target = file.hasFileExtension(s.getExtension())
                            ? file
                            : file.withFileExtension(s.getExtension());

                        juce::String status = AudioExporter::bounceProjectWithSettings(
                            target, tracks, lock, sampleRate, blockSize, bpm, s);

                        bool ok = status.startsWith("OK");
                        juce::AlertWindow::showMessageBoxAsync(
                            ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                            ok ? "Exportacao Concluida" : "Resultado da Exportacao",
                            status);

                        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                            dw->exitModalState(0);
                    }
                });
        }

        juce::OwnedArray<AudioTrack>& tracks;
        juce::CriticalSection& lock;
        double sampleRate;
        int blockSize;
        double bpm;

        juce::Label formatLbl, rateLbl, bitrateLbl, chLbl;
        juce::ComboBox formatBox, rateBox, bitrateBox, chBox;
        juce::TextButton renderBtn;
    };
};
