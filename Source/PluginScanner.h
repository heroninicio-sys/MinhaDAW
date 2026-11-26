#pragma once
#include <JuceHeader.h>

class PluginScanOverlay : public juce::Component
{
public:
    PluginScanOverlay()
    {
        setAlwaysOnTop(true);

        addAndMakeVisible(skipBtn);
        skipBtn.setButtonText("Pular Scan Agora (Usar Plugins Nativos)");
        skipBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a48));
        skipBtn.onClick = [this] { if (onSkipClicked) onSkipClicked(); };
    }

    std::function<void()> onSkipClicked;

    void updateProgress(const juce::String& currentFile, int foundCount)
    {
        currentPluginName = currentFile;
        totalFound = foundCount;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.76f));

        auto box = getLocalBounds().withSizeKeepingCentre(520, 200);
        g.setColour(juce::Colour(0xff142337));
        g.fillRoundedRectangle(box.toFloat(), 8.0f);
        g.setColour(juce::Colour(0xff16d8e8));
        g.drawRoundedRectangle(box.toFloat(), 8.0f, 1.5f);

        auto content = box.reduced(24, 18);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        g.drawText("Estamos localizando e organizando os seus plugins",
                   content.removeFromTop(28), juce::Justification::centred);

        content.removeFromTop(6);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText("Os plugins validados ficam salvos para abrir instantaneamente nas proximas vezes.",
                   content.removeFromTop(20), juce::Justification::centred);

        content.removeFromTop(12);
        auto barRect = content.removeFromTop(8).reduced(20, 0);
        g.setColour(juce::Colour(0xff07111d));
        g.fillRoundedRectangle(barRect.toFloat(), 4.0f);

        float pulse = (float)(0.5 + 0.5 * std::sin(juce::Time::getMillisecondCounterHiRes() * 0.005));
        int indicatorW = barRect.getWidth() / 3;
        int indicatorX = barRect.getX() + (int)(pulse * (float)(barRect.getWidth() - indicatorW));
        g.setColour(juce::Colour(0xff16d8e8));
        g.fillRoundedRectangle((float)indicatorX, (float)barRect.getY(), (float)indicatorW, (float)barRect.getHeight(), 4.0f);

        content.removeFromTop(10);
        g.setColour(juce::Colour(0xff8ecae6));
        g.setFont(juce::FontOptions(12.0f));
        juce::String statusLine = currentPluginName.isNotEmpty()
            ? ("Verificando: " + currentPluginName + "  (" + juce::String(totalFound) + " encontrados)")
            : ("Plugins prontos: " + juce::String(totalFound));
        g.drawText(statusLine, content.removeFromTop(22), juce::Justification::centred, true);
    }

    void resized() override
    {
        auto box = getLocalBounds().withSizeKeepingCentre(520, 200);
        skipBtn.setBounds(box.removeFromBottom(42).reduced(110, 6));
    }

private:
    juce::String currentPluginName;
    int totalFound = 0;
    juce::TextButton skipBtn;
};

class PluginScannerEngine : private juce::Timer
{
public:
    PluginScannerEngine()
    {
        formatManager.addDefaultFormats();

        auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("MinhaDAWStudio");
        appDataDir.createDirectory();

        cacheFile      = appDataDir.getChildFile("ScannedPluginsCache.xml");
        scannedLog     = appDataDir.getChildFile("ScannedFilesHistory.txt");
        deadMansPedal  = appDataDir.getChildFile("DeadMansPedal.txt");

        loadCacheFromDisk();
    }

    ~PluginScannerEngine() override
    {
        stopTimer();
    }

    static int runWorkerProcessIfRequested(const juce::String&)
    {
        return -1; // Mantido por compatibilidade com Main.cpp sem criar processos extras
    }

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::Array<juce::PluginDescription> getScannedTypesSafe() const { return knownPlugins.getTypes(); }
    int getNumScannedPlugins() const { return knownPlugins.getNumTypes(); }
    bool isScanning() const { return scanningActive; }
    juce::String getCurrentStatus() const { return currentStatusText; }

    void attachOverlay(PluginScanOverlay* overlayPtr)
    {
        overlay = overlayPtr;
        if (overlay != nullptr)
        {
            overlay->onSkipClicked = [this] { finishScan(); };
            overlay->setVisible(scanningActive);
        }
    }

    void startBackgroundAutoScan(bool forceFullRescan = false)
    {
        if (scanningActive)
            return;

        if (forceFullRescan)
        {
            knownPlugins.clear();
            scannedLog.deleteFile();
            deadMansPedal.deleteFile();
        }

        // Se algum plugin travou na execução anterior, pula ele automaticamente
        if (deadMansPedal.existsAsFile())
        {
            juce::String crashed = deadMansPedal.loadFileAsString().trim();
            if (crashed.isNotEmpty())
                scannedLog.appendText(crashed + "\n");
            deadMansPedal.deleteFile();
        }

        alreadyScannedPaths.clear();
        if (scannedLog.existsAsFile())
            scannedLog.readLines(alreadyScannedPaths);

        filesQueue.clear();
        juce::File vst3Folder("C:\\Program Files\\Common Files\\VST3");
        if (vst3Folder.isDirectory())
        {
            auto found = vst3Folder.findChildFiles(juce::File::findFilesAndDirectories, true, "*.vst3");
            for (auto& f : found)
            {
                juce::String path = f.getFullPathName();
                if (!alreadyScannedPaths.contains(path))
                    filesQueue.addIfNotAlreadyThere(path);
            }
        }

        if (filesQueue.isEmpty())
        {
            currentStatusText = "Plugins VST3 prontos: " + juce::String(knownPlugins.getNumTypes());
            if (overlay != nullptr)
                overlay->setVisible(false);
            return;
        }

        currentQueueIndex = 0;
        scanningActive = true;
        if (overlay != nullptr)
        {
            overlay->setVisible(true);
            overlay->updateProgress("", knownPlugins.getNumTypes());
        }

        // Intervalo de 120ms dá tempo para o Windows processar eventos da janela sem congelar
        startTimer(120);
    }

    std::function<void()> onScanFinished;

private:
    void timerCallback() override
    {
        if (currentQueueIndex >= filesQueue.size())
        {
            finishScan();
            return;
        }

        juce::String pluginPath = filesQueue[currentQueueIndex];
        juce::String shortName = juce::File(pluginPath).getFileNameWithoutExtension();
        currentStatusText = "Localizando: " + shortName;

        if (overlay != nullptr)
            overlay->updateProgress(shortName, knownPlugins.getNumTypes());

        // Registra no DeadMansPedal antes de ler
        deadMansPedal.replaceWithText(pluginPath);

        juce::OwnedArray<juce::PluginDescription> descs;
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            formatManager.getFormat(i)->findAllTypesForFile(descs, pluginPath);
            if (descs.size() > 0)
                break;
        }

        deadMansPedal.deleteFile();

        for (auto* d : descs)
            knownPlugins.addType(*d);

        alreadyScannedPaths.addIfNotAlreadyThere(pluginPath);
        scannedLog.appendText(pluginPath + "\n");
        saveCacheToDisk();

        currentQueueIndex++;
    }

    void finishScan()
    {
        stopTimer();
        deadMansPedal.deleteFile();
        scanningActive = false;
        saveCacheToDisk();
        currentStatusText = "Plugins VST3 prontos: " + juce::String(knownPlugins.getNumTypes());

        if (overlay != nullptr)
            overlay->setVisible(false);

        if (onScanFinished)
            onScanFinished();
    }

    void saveCacheToDisk()
    {
        if (auto xml = knownPlugins.createXml())
            xml->writeTo(cacheFile);
    }

    void loadCacheFromDisk()
    {
        if (cacheFile.existsAsFile())
        {
            if (auto xml = juce::XmlDocument::parse(cacheFile))
                knownPlugins.recreateFromXml(*xml);
        }
    }

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::File cacheFile, scannedLog, deadMansPedal;

    juce::StringArray alreadyScannedPaths;
    juce::StringArray filesQueue;
    int currentQueueIndex = 0;

    juce::String currentStatusText;
    bool scanningActive = false;
    PluginScanOverlay* overlay = nullptr;
};
