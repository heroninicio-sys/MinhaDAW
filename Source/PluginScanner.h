#pragma once
#include <JuceHeader.h>

class PluginScanOverlay : public juce::Component
{
public:
    PluginScanOverlay()
    {
        setAlwaysOnTop(true);
    }

    void updateProgress(const juce::String& currentFile, int foundCount)
    {
        currentPluginName = currentFile;
        totalFound = foundCount;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        // Fundo escuro semitransparente cobrindo a DAW
        g.fillAll(juce::Colours::black.withAlpha(0.72f));

        auto box = getLocalBounds().withSizeKeepingCentre(520, 175);
        g.setColour(juce::Colour(0xff23232b));
        g.fillRoundedRectangle(box.toFloat(), 8.0f);
        g.setColour(juce::Colour(0xff3a86ff));
        g.drawRoundedRectangle(box.toFloat(), 8.0f, 1.5f);

        auto content = box.reduced(24, 20);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        g.drawText("Estamos localizando e organizando os seus plugins",
                   content.removeFromTop(28), juce::Justification::centred);

        content.removeFromTop(10);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(12.5f));
        g.drawText("Os plugins validados ficam salvos para abrir instantaneamente nas proximas vezes.",
                   content.removeFromTop(22), juce::Justification::centred);

        content.removeFromTop(12);
        // Barra de progresso animada
        auto barRect = content.removeFromTop(10).reduced(20, 0);
        g.setColour(juce::Colour(0xff141418));
        g.fillRoundedRectangle(barRect.toFloat(), 5.0f);

        float pulse = (float)(0.5 + 0.5 * std::sin(juce::Time::getMillisecondCounterHiRes() * 0.006));
        int indicatorW = barRect.getWidth() / 3;
        int indicatorX = barRect.getX() + (int)(pulse * (float)(barRect.getWidth() - indicatorW));
        g.setColour(juce::Colour(0xff00f5d4));
        g.fillRoundedRectangle((float)indicatorX, (float)barRect.getY(), (float)indicatorW, (float)barRect.getHeight(), 5.0f);

        content.removeFromTop(12);
        g.setColour(juce::Colour(0xff8ecae6));
        g.setFont(juce::FontOptions(12.0f));
        juce::String statusLine = currentPluginName.isNotEmpty()
            ? ("Verificando: " + currentPluginName + "  (" + juce::String(totalFound) + " encontrados)")
            : ("Plugins prontos: " + juce::String(totalFound));
        g.drawText(statusLine, content, juce::Justification::centred, true);
    }

private:
    juce::String currentPluginName;
    int totalFound = 0;
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

        cacheFile     = appDataDir.getChildFile("ScannedPluginsCache.xml");
        scannedLog    = appDataDir.getChildFile("ScannedFilesHistory.txt");
        blacklistFile = appDataDir.getChildFile("BlacklistedPlugins.txt");

        loadCacheFromDisk();
    }

    ~PluginScannerEngine() override
    {
        stopTimer();
    }

    // Processo trabalhador isolado: testa apenas 1 plugin passado via linha de comando e sai
    static int runWorkerProcessIfRequested(const juce::String& commandLine)
    {
        if (!commandLine.startsWith("--scan-worker "))
            return -1; // Não é worker; abre a DAW normalmente

        juce::String filePath = commandLine.fromFirstOccurrenceOf("--scan-worker ", false, false).unquoted().trim();
        juce::File pluginFile(filePath);

        if (!pluginFile.existsAsFile() && !pluginFile.isDirectory())
            return 1;

        juce::AudioPluginFormatManager mgr;
        mgr.addDefaultFormats();

        juce::OwnedArray<juce::PluginDescription> descs;
        for (int i = 0; i < mgr.getNumFormats(); ++i)
        {
            mgr.getFormat(i)->findAllTypesForFile(descs, pluginFile.getFullPathName());
            if (descs.size() > 0)
                break;
        }

        if (descs.size() > 0)
        {
            juce::XmlElement root("PLUGIN_SCAN_RESULT");
            for (auto* d : descs)
                if (auto xml = d->createXml())
                    root.addChildElement(xml.release());

            auto outFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile("MinhaDAWStudio")
                               .getChildFile("WorkerLastResult.xml");
            root.writeTo(outFile);
            return 0;
        }

        return 2;
    }

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    juce::Array<juce::PluginDescription> getScannedTypesSafe() const
    {
        return knownPlugins.getTypes();
    }

    int getNumScannedPlugins() const { return knownPlugins.getNumTypes(); }
    bool isScanning() const          { return scanningActive; }
    juce::String getCurrentStatus() const { return currentStatusText; }

    void attachOverlay(PluginScanOverlay* overlayPtr)
    {
        overlay = overlayPtr;
        if (overlay != nullptr)
            overlay->setVisible(scanningActive);
    }

    void startBackgroundAutoScan(bool forceFullRescan = false)
    {
        if (scanningActive)
            return;

        if (forceFullRescan)
        {
            knownPlugins.clear();
            scannedLog.deleteFile();
            blacklistFile.deleteFile();
        }

        // Carrega histórico do que já foi escaneado para NÃO repetir o processo
        alreadyScannedPaths.clear();
        if (scannedLog.existsAsFile())
            scannedLog.readLines(alreadyScannedPaths);

        blacklistedPaths.clear();
        if (blacklistFile.existsAsFile())
            blacklistFile.readLines(blacklistedPaths);

        // Coleta todos os arquivos .vst3 nas pastas padrão
        filesQueue.clear();
        juce::Array<juce::File> searchFolders;
        searchFolders.add(juce::File("C:\\Program Files\\Common Files\\VST3"));

        for (auto& folder : searchFolders)
        {
            if (folder.isDirectory())
            {
                auto found = folder.findChildFiles(juce::File::findFilesAndDirectories, true, "*.vst3");
                for (auto& f : found)
                {
                    juce::String path = f.getFullPathName();
                    if (!alreadyScannedPaths.contains(path) && !blacklistedPaths.contains(path))
                        filesQueue.addIfNotAlreadyThere(path);
                }
            }
        }

        // Se todos os plugins já estavam salvos no cache, não precisa escanear nada!
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

        startTimer(30);
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
        juce::String pluginShortName = juce::File(pluginPath).getFileNameWithoutExtension();
        currentStatusText = "Localizando: " + pluginShortName;

        if (overlay != nullptr)
            overlay->updateProgress(pluginShortName, knownPlugins.getNumTypes());

        // Executa o teste em um processo isolado com timeout de 6 segundos
        auto workerResultFile = cacheFile.getSiblingFile("WorkerLastResult.xml");
        workerResultFile.deleteFile();

        juce::ChildProcess worker;
        juce::String exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
        juce::StringArray args { exePath, "--scan-worker", pluginPath };

        bool validPlugin = false;
        if (worker.start(args))
        {
            if (worker.waitForProcessToFinish(6000) && worker.getExitCode() == 0 && workerResultFile.existsAsFile())
            {
                if (auto xml = juce::XmlDocument::parse(workerResultFile))
                {
                    for (auto* child : xml->getChildIterator())
                    {
                        juce::PluginDescription desc;
                        if (desc.loadFromXml(*child))
                        {
                            knownPlugins.addType(desc);
                            validPlugin = true;
                        }
                    }
                }
            }
            else
            {
                worker.kill();
            }
        }

        workerResultFile.deleteFile();

        // Salva imediatamente no histórico para não repetir nas próximas aberturas
        if (validPlugin)
        {
            alreadyScannedPaths.addIfNotAlreadyThere(pluginPath);
            scannedLog.appendText(pluginPath + "\n");
            saveCacheToDisk();
        }
        else
        {
            blacklistedPaths.addIfNotAlreadyThere(pluginPath);
            blacklistFile.appendText(pluginPath + "\n");
        }

        currentQueueIndex++;
    }

    void finishScan()
    {
        stopTimer();
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
    juce::File cacheFile, scannedLog, blacklistFile;

    juce::StringArray alreadyScannedPaths;
    juce::StringArray blacklistedPaths;
    juce::StringArray filesQueue;
    int currentQueueIndex = 0;

    juce::String currentStatusText;
    bool scanningActive = false;
    PluginScanOverlay* overlay = nullptr;
};
