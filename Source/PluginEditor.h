#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MusicPackagerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit MusicPackagerAudioProcessorEditor(MusicPackagerAudioProcessor&);
    ~MusicPackagerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void browseFlp();
    void browseOutput();
    void doScan();
    void doPackage();
    void refreshFileList();
    void updateButtonStates();

    MusicPackagerAudioProcessor& processor;

    // FLP row
    juce::Label      flpPathLabel;
    juce::TextButton flpBrowseBtn  { "Browse" };

    // Output row
    juce::Label      outPathLabel;
    juce::TextButton outBrowseBtn  { "Browse" };

    // Options + actions
    juce::ToggleButton folderToggle { "Folder" };
    juce::ToggleButton zipToggle    { "ZIP"    };
    juce::TextButton   scanBtn      { "Scan FLP" };
    juce::TextButton   packageBtn   { "Package"  };

    // Results
    juce::TextEditor fileListBox;

    // Progress
    double           progressValue = 0.0;
    juce::ProgressBar progressBar  { progressValue };
    juce::Label      statusLabel;

    // Held alive for async file chooser
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusicPackagerAudioProcessorEditor)
};
