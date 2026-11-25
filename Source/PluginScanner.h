#pragma once
#include <JuceHeader.h>

class PluginScannerEngine : private juce::Thread
{
public:
    PluginScannerEngine()
        : juce::Thread("DAW Background Plugin Scanner")
    {
        formatManager.addDefaultFormats();

        cacheFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("MinhaDAWStudio")
                        .getChildFile("ScannedPluginsCache.xml");

        loadCacheFromDisk();
    }

    ~PluginScannerEngine() override
    {
        stopThread(3000);
    }

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    juce::Array<juce::PluginDescription> getScannedTypesSafe() const
    {
        const juce::ScopedLock sl(listLock);
        return knownPlugins.getTypes();
    }

    int getNumScannedPlugins() const
    {
        const juce::ScopedLock sl(listLock);
        return knownPlugins.getNumTypes();
    }

    bool isScanning() const { return scanningActive.load(); }

    juce::String getCurrentStatus() const
    {
        const juce::ScopedLock sl(statusLock);
        return currentStatusText;
    }

    void startBackgroundAutoScan(bool forceFullRescan = false)
    {
        if (isThreadRunning())
            return;

        forceRescan = forceFullRescan;
        scanningActive.store(true);
        startThread(juce::Thread::Priority::low);
    }

    std::function<void()> onScanFinished;

private:
    void run() override
    {
        setStatus("Escaneando plugins VST3...");

        if (forceRescan)
        {
            const juce::ScopedLock sl(listLock);
            knownPlugins.clear();
        }

        juce::FileSearchPath defaultPaths;
        defaultPaths.addIfNotAlreadyThere(juce::File("C:\\Program Files\\Common Files\\VST3"));
        defaultPaths.addIfNotAlreadyThere(juce::File("C:\\Program Files\\VSTPlugins"));
        defaultPaths.addIfNotAlreadyThere(juce::File("C:\\Program Files\\Steinberg\\VSTPlugins"));
        defaultPaths.addIfNotAlreadyThere(juce::File("C:\\VSTPlugins"));

        juce::File deadMansPedal = cacheFile.getSiblingFile("DeadMansPedal.txt");

        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            if (threadShouldExit())
                break;

            auto* format = formatManager.getFormat(i);

            juce::KnownPluginList tempList;
            {
                const juce::ScopedLock sl(listLock);
                if (auto xml = knownPlugins.createXml())
                    tempList.recreateFromXml(*xml);
            }

            juce::PluginDirectoryScanner scanner(tempList,
                                                 *format,
                                                 defaultPaths,
                                                 true,
                                                 deadMansPedal,
                                                 true);

            juce::String pluginBeingScanned;
            while (!threadShouldExit() && scanner.scanNextFile(true, pluginBeingScanned))
            {
                setStatus("Scan [" + format->getName() + "]: " +
                          juce::File(pluginBeingScanned).getFileNameWithoutExtension());

                const juce::ScopedLock sl(listLock);
                if (auto xml = tempList.createXml())
                    knownPlugins.recreateFromXml(*xml);
            }
        }

        saveCacheToDisk();
        scanningActive.store(false);
        setStatus("Plugins VST3 prontos: " + juce::String(getNumScannedPlugins()));

        if (onScanFinished)
        {
            juce::MessageManager::callAsync([cb = onScanFinished]() { cb(); });
        }
    }

    void setStatus(const juce::String& text)
    {
        const juce::ScopedLock sl(statusLock);
        currentStatusText = text;
    }

    void saveCacheToDisk()
    {
        const juce::ScopedLock sl(listLock);
        cacheFile.getParentDirectory().createDirectory();
        if (auto xml = knownPlugins.createXml())
            xml->writeTo(cacheFile);
    }

    void loadCacheFromDisk()
    {
        const juce::ScopedLock sl(listLock);
        if (cacheFile.existsAsFile())
        {
            if (auto xml = juce::XmlDocument::parse(cacheFile))
                knownPlugins.recreateFromXml(*xml);
        }
    }

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::File cacheFile;

    mutable juce::CriticalSection listLock;
    mutable juce::CriticalSection statusLock;
    juce::String currentStatusText { "Iniciando scan..." };
    std::atomic<bool> scanningActive { false };
    bool forceRescan = false;
};
