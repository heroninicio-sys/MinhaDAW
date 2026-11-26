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
private:
    class ProjectRenderSource : public juce::AudioSource
    {
    public:
        ProjectRenderSource(juce::OwnedArray<AudioTrack>& trackList,
                            juce::CriticalSection& engineLock,
                            AudioTrack* master,
                            double engineRate,
                            double tempo,
                            float masterLevel,
                            juce::int64 totalSamples)
            : tracks(trackList), lock(engineLock), masterTrack(master), sampleRate(engineRate),
              bpm(tempo), masterGain(masterLevel), totalEngineSamples(totalSamples) {}

        void prepareToPlay(int, double) override {}
        void releaseResources() override {}

        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
        {
            info.clearActiveBufferRegion();
            if (info.buffer == nullptr || info.numSamples <= 0 || info.buffer->getNumChannels() < 2)
                return;

            const juce::ScopedLock sl(lock);
            const juce::int64 remaining = juce::jmax((juce::int64)0, totalEngineSamples - renderedSamples);
            const int samplesToRender = (int)juce::jmin((juce::int64)info.numSamples, remaining);
            if (samplesToRender <= 0)
                return;

            bool anySolo = false;
            for (auto* track : tracks)
                if (track->isSoloed) { anySolo = true; break; }

            const double blockStartSeconds = (double)renderedSamples / sampleRate;
            juce::AudioSourceChannelInfo renderInfo(info.buffer, info.startSample, samplesToRender);
            for (auto* track : tracks)
                track->processTrack(renderInfo, nullptr, 0, blockStartSeconds, bpm, true, anySolo);

            if (masterTrack != nullptr)
            {
                juce::AudioBuffer<float> masterView(info.buffer->getArrayOfWritePointers(), 2,
                                                     info.startSample, samplesToRender);
                juce::MidiBuffer emptyMidi;
                for (int i = 0; i < masterTrack->getVstPlugins().size(); ++i)
                {
                    auto* vst = masterTrack->getVstPlugins()[i];
                    const bool isBypassed = masterTrack->isVstPluginBypassed(i);
                    if (!isBypassed || vst->getBypassParameter() != nullptr)
                        vst->processBlock(masterView, emptyMidi);
                }
                for (auto* fx : masterTrack->getNativeEffects())
                    fx->process(masterView);
            }

            info.buffer->applyGain(0, info.startSample, samplesToRender, masterGain);
            info.buffer->applyGain(1, info.startSample, samplesToRender, masterGain);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* samples = info.buffer->getWritePointer(ch, info.startSample);
                for (int i = 0; i < samplesToRender; ++i)
                    samples[i] = juce::jlimit(-0.99f, 0.99f, samples[i]);
            }

            renderedSamples += samplesToRender;
        }

    private:
        juce::OwnedArray<AudioTrack>& tracks;
        juce::CriticalSection& lock;
        AudioTrack* masterTrack = nullptr;
        double sampleRate = 48000.0;
        double bpm = 120.0;
        float masterGain = 1.0f;
        juce::int64 totalEngineSamples = 0;
        juce::int64 renderedSamples = 0;
    };

public:
    static juce::String bounceProjectWithSettings(const juce::File& outputFile,
                                                  juce::OwnedArray<AudioTrack>& tracks,
                                                  juce::CriticalSection& trackLock,
                                                  double engineSampleRate,
                                                  int blockSize,
                                                  double bpm,
                                                  const ExportSettings& settings,
                                                  AudioTrack* masterTrack = nullptr,
                                                  float masterGain = 1.0f)
    {
        if (engineSampleRate <= 0.0) engineSampleRate = 48000.0;
        if (blockSize <= 0) blockSize = 128;

        const bool needsTranscode = (settings.format == ExportSettings::Format::MP3 ||
                                     settings.format == ExportSettings::Format::OGG ||
                                     settings.format == ExportSettings::Format::WMA);

        if (!outputFile.getParentDirectory().createDirectory())
            return "ERRO: Nao foi possivel criar a pasta de destino.";

        juce::File tempWavFile = juce::File::createTempFile(".wav");
        juce::File finalWriteTarget = needsTranscode ? tempWavFile : outputFile;

        double totalDurationSeconds = 0.0;
        juce::int64 totalEngineSamples = 0;
        const double targetSampleRate = settings.targetSampleRate > 0.0
            ? settings.targetSampleRate : engineSampleRate;
        const int outCh = juce::jlimit(1, 2, settings.numChannels);

        {
            const juce::ScopedLock sl(trackLock);
            for (auto* track : tracks)
                totalDurationSeconds = juce::jmax(totalDurationSeconds, track->getMaxTimelineEnd(bpm));

            if (totalDurationSeconds <= 0.05)
            {
                tempWavFile.deleteFile();
                return "ERRO: Projeto vazio. Adicione audio ou notas MIDI antes de exportar.";
            }

            totalDurationSeconds += 1.5;
            totalEngineSamples = (juce::int64)std::ceil(totalDurationSeconds * engineSampleRate);
            for (auto* track : tracks)
                track->prepareToPlay(blockSize, engineSampleRate);
            if (masterTrack != nullptr)
                masterTrack->prepareToPlay(blockSize, engineSampleRate);
        }

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

        std::unique_ptr<juce::AudioFormatWriter> writer(
            audioFormat->createWriterFor(fileStream.get(), targetSampleRate,
                                         (unsigned int)outCh, writeBits, {}, 0));
        if (writer == nullptr)
        {
            tempWavFile.deleteFile();
            return "ERRO: Falha ao inicializar o gravador de audio.";
        }
        fileStream.release();

        ProjectRenderSource renderSource(tracks, trackLock, masterTrack, engineSampleRate,
                                         bpm, masterGain, totalEngineSamples);
        juce::ResamplingAudioSource resampler(&renderSource, false, 2);
        const double resamplingRatio = engineSampleRate / targetSampleRate;
        resampler.setResamplingRatio(resamplingRatio);
        resampler.prepareToPlay(blockSize, targetSampleRate);

        const juce::int64 totalOutputSamples = (juce::int64)std::ceil((double)totalEngineSamples / resamplingRatio);
        juce::AudioBuffer<float> stereoOutput(2, blockSize);
        juce::AudioBuffer<float> monoOutput(1, blockSize);
        juce::int64 outputSamplesWritten = 0;

        while (outputSamplesWritten < totalOutputSamples)
        {
            const int samplesThisBlock = (int)juce::jmin((juce::int64)blockSize,
                                                         totalOutputSamples - outputSamplesWritten);
            stereoOutput.clear();
            juce::AudioSourceChannelInfo outputInfo(&stereoOutput, 0, samplesThisBlock);
            resampler.getNextAudioBlock(outputInfo);

            if (outCh == 1)
            {
                monoOutput.clear();
                monoOutput.copyFrom(0, 0, stereoOutput, 0, 0, samplesThisBlock);
                monoOutput.addFrom(0, 0, stereoOutput, 1, 0, samplesThisBlock);
                monoOutput.applyGain(0, 0, samplesThisBlock, 0.5f);
                writer->writeFromAudioSampleBuffer(monoOutput, 0, samplesThisBlock);
            }
            else
            {
                writer->writeFromAudioSampleBuffer(stereoOutput, 0, samplesThisBlock);
            }
            outputSamplesWritten += samplesThisBlock;
        }

        resampler.releaseResources();
        writer.reset();

        if (needsTranscode)
        {
            bool ok = transcodeWithFfmpeg(tempWavFile, outputFile, settings);
            if (!ok)
            {
                juce::File fallbackWav = outputFile.withFileExtension(".wav");
                if (fallbackWav.existsAsFile()) fallbackWav.deleteFile();
                const bool preservedWav = tempWavFile.copyFileTo(fallbackWav);
                if (preservedWav)
                    tempWavFile.deleteFile();
                return "AVISO: FFmpeg nao conseguiu gerar " + settings.getFormatName() +
                       ". A mixagem foi preservada em WAV: " +
                       (preservedWav ? fallbackWav.getFullPathName() : tempWavFile.getFullPathName());
            }
            tempWavFile.deleteFile();
        }

        return "OK: Exportado em " + settings.getFormatName() + " (" +
               juce::String((int)settings.targetSampleRate) + " Hz, " +
               juce::String(settings.bitRateKbps) + " kbps) para:\n" + outputFile.getFullPathName();
    }

    class ExportDialogWindow : public juce::DocumentWindow
    {
    public:
        ExportDialogWindow(juce::Component* content, juce::LookAndFeel* lookAndFeel)
            : DocumentWindow("Renderizar Mix - MinhaDAW Studio", juce::Colour(0xff0b1420), DocumentWindow::allButtons)
        {
            setLookAndFeel(lookAndFeel);
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setContentOwned(content, true);
            centreWithSize(620, 390);
            setVisible(true);
        }

        void closeButtonPressed() override { setVisible(false); }
    };

    static std::unique_ptr<juce::DocumentWindow> createExportDialogWindow(
        juce::OwnedArray<AudioTrack>& tracks,
        juce::CriticalSection& trackLock,
        double engineSampleRate,
        int blockSize,
        double bpm,
        AudioTrack* masterTrack = nullptr,
        std::atomic<float>* masterGainRef = nullptr,
        juce::LookAndFeel* lookAndFeel = nullptr)
    {
        auto* panel = new ExportDialogPanel(tracks, trackLock, engineSampleRate, blockSize,
                                            bpm, masterTrack, masterGainRef);
        panel->setSize(560, 340);
        return std::make_unique<ExportDialogWindow>(panel, lookAndFeel);
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
                          double sr, int bs, double curBpm, AudioTrack* masterTrackRef,
                          std::atomic<float>* masterGainRef)
            : tracks(tRef), lock(lRef), sampleRate(sr), blockSize(bs), bpm(curBpm),
              masterTrack(masterTrackRef), masterGainAtomic(masterGainRef)
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
            g.fillAll(juce::Colour(0xff101d2b));
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

            juce::Component::SafePointer<ExportDialogPanel> safeThis(this);
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                [safeThis, chooser, s](const juce::FileChooser& fc)
                {
                    if (safeThis == nullptr) return;
                    auto file = fc.getResult();
                    if (file != juce::File())
                    {
                        juce::File target = file.hasFileExtension(s.getExtension())
                            ? file
                            : file.withFileExtension(s.getExtension());

                        juce::String status = AudioExporter::bounceProjectWithSettings(
                            target, safeThis->tracks, safeThis->lock, safeThis->sampleRate,
                            safeThis->blockSize, safeThis->bpm, s, safeThis->masterTrack,
                            safeThis->masterGainAtomic != nullptr ? safeThis->masterGainAtomic->load() : 1.0f);

                        bool ok = status.startsWith("OK");
                        juce::AlertWindow::showMessageBoxAsync(
                            ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                            ok ? "Exportacao Concluida" : "Resultado da Exportacao",
                            status);

                        if (auto* dw = safeThis->findParentComponentOfClass<juce::DocumentWindow>())
                            dw->setVisible(false);
                    }
                });
        }

        juce::OwnedArray<AudioTrack>& tracks;
        juce::CriticalSection& lock;
        double sampleRate;
        int blockSize;
        double bpm;
        AudioTrack* masterTrack = nullptr;
        std::atomic<float>* masterGainAtomic = nullptr;

        juce::Label formatLbl, rateLbl, bitrateLbl, chLbl;
        juce::ComboBox formatBox, rateBox, bitrateBox, chBox;
        juce::TextButton renderBtn;
    };
};
