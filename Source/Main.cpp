#include <JuceHeader.h>
#include "PluginScanner.h"
#include "MainComponent.h"

class MinhaDAWApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "MinhaDAW Studio"; }
    const juce::String getApplicationVersion() override    { return "1.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise(const juce::String& commandLine) override
    {
        int workerExit = PluginScannerEngine::runWorkerProcessIfRequested(commandLine);
        if (workerExit >= 0)
        {
            setApplicationReturnValue(workerExit);
            quit();
            return;
        }

        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override { mainWindow = nullptr; }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff1e1e24), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(1320, 700, 3200, 2200);
            centreWithSize(1480, 920);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(MinhaDAWApplication)
