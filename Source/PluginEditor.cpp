#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::Colour bg     { 0xff1e1e2e };
static juce::Colour surface{ 0xff313244 };
static juce::Colour border { 0xff45475a };
static juce::Colour accent { 0xff89b4fa };
static juce::Colour fg     { 0xffcdd6f4 };
static juce::Colour fgDim  { 0xff6c7086 };

// ────────────────────────────────────────────────── constructor / destructor ──

MusicPackagerAudioProcessorEditor::MusicPackagerAudioProcessorEditor(
    MusicPackagerAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // ── Path display labels ──────────────────────────────────────────────────
    auto setupPathLabel = [](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setColour(juce::Label::backgroundColourId, surface);
        l.setColour(juce::Label::textColourId, fg);
        l.setFont(juce::FontOptions(11.0f));
        l.setMinimumHorizontalScale(0.4f);
    };

    setupPathLabel(flpPathLabel, p.flpPath.isEmpty() ? "No file selected" : p.flpPath);
    setupPathLabel(outPathLabel, p.outputPath.isEmpty() ? "No folder selected" : p.outputPath);

    // ── Buttons ──────────────────────────────────────────────────────────────
    flpBrowseBtn.onClick = [this] { browseFlp(); };
    outBrowseBtn.onClick = [this] { browseOutput(); };
    scanBtn.onClick      = [this] { doScan(); };
    packageBtn.onClick   = [this] { doPackage(); };

    // ── Format toggles (radio group) ─────────────────────────────────────────
    folderToggle.setRadioGroupId(1);
    zipToggle.setRadioGroupId(1);
    folderToggle.setToggleState(!p.useZip, juce::dontSendNotification);
    zipToggle.setToggleState(p.useZip,     juce::dontSendNotification);
    folderToggle.setColour(juce::ToggleButton::textColourId, fg);
    zipToggle.setColour(juce::ToggleButton::textColourId, fg);
    folderToggle.onClick = [this] { processor.useZip = false; };
    zipToggle.onClick    = [this] { processor.useZip = true;  };

    // ── File list ─────────────────────────────────────────────────────────────
    fileListBox.setMultiLine(true);
    fileListBox.setReadOnly(true);
    fileListBox.setScrollbarsShown(true);
    fileListBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff11111f));
    fileListBox.setColour(juce::TextEditor::textColourId, accent);
    fileListBox.setFont(juce::FontOptions(10.5f));
    fileListBox.setText("No files scanned yet.", juce::dontSendNotification);

    // ── Status label ──────────────────────────────────────────────────────────
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(juce::FontOptions(10.5f));
    statusLabel.setColour(juce::Label::textColourId, fgDim);

    // ── Add all children ─────────────────────────────────────────────────────
    addAndMakeVisible(flpPathLabel);
    addAndMakeVisible(flpBrowseBtn);
    addAndMakeVisible(outPathLabel);
    addAndMakeVisible(outBrowseBtn);
    addAndMakeVisible(folderToggle);
    addAndMakeVisible(zipToggle);
    addAndMakeVisible(scanBtn);
    addAndMakeVisible(packageBtn);
    addAndMakeVisible(fileListBox);
    addAndMakeVisible(progressBar);
    addAndMakeVisible(statusLabel);

    updateButtonStates();
    startTimerHz(10);
    setSize(500, 420);
}

MusicPackagerAudioProcessorEditor::~MusicPackagerAudioProcessorEditor()
{
    stopTimer();
}

// ──────────────────────────────────────────────────────────────────── Timer ──

void MusicPackagerAudioProcessorEditor::timerCallback()
{
    using WS = MusicPackagerAudioProcessor::WorkerState;
    auto state = processor.workerState.load();

    progressValue = double(processor.workerProgress.load());

    juce::String msg;
    {
        juce::ScopedLock sl(processor.dataLock);
        msg = processor.statusMessage;
    }
    statusLabel.setText(msg, juce::dontSendNotification);

    if (state == WS::Done || state == WS::Failed)
    {
        refreshFileList();
        updateButtonStates();
        // Latch state to Idle so we only refresh once
        processor.workerState.store(WS::Idle);
    }

    progressBar.repaint();
}

// ────────────────────────────────────────────────────────────── UI helpers ──

void MusicPackagerAudioProcessorEditor::refreshFileList()
{
    juce::String text;
    {
        juce::ScopedLock sl(processor.dataLock);
        if (processor.foundFiles.isEmpty())
        {
            text = "No audio files found.";
        }
        else
        {
            for (auto& f : processor.foundFiles)
                text += f + "\n";
        }
    }
    fileListBox.setText(text, juce::dontSendNotification);
}

void MusicPackagerAudioProcessorEditor::updateButtonStates()
{
    using WS = MusicPackagerAudioProcessor::WorkerState;
    bool busy = processor.workerState.load() == WS::Working;
    bool hasFlp = processor.flpPath.isNotEmpty();
    bool hasOut = processor.outputPath.isNotEmpty();

    int numFound = 0;
    { juce::ScopedLock sl(processor.dataLock); numFound = processor.foundFiles.size(); }

    scanBtn.setEnabled(!busy && hasFlp);
    packageBtn.setEnabled(!busy && hasFlp && hasOut && numFound > 0);
    flpBrowseBtn.setEnabled(!busy);
    outBrowseBtn.setEnabled(!busy);
}

// ────────────────────────────────────────────────────────── File browsing ──

void MusicPackagerAudioProcessorEditor::browseFlp()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select FL Studio Project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.flp");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile())
            {
                processor.flpPath = f.getFullPathName();
                flpPathLabel.setText(f.getFullPathName(), juce::dontSendNotification);
                // Reset found files since the FLP changed
                { juce::ScopedLock sl(processor.dataLock); processor.foundFiles.clear(); }
                fileListBox.setText("Press \"Scan FLP\" to find audio files.",
                                    juce::dontSendNotification);
                updateButtonStates();
            }
        });
}

void MusicPackagerAudioProcessorEditor::browseOutput()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select Output Folder",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory));

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.isDirectory())
            {
                processor.outputPath = f.getFullPathName();
                outPathLabel.setText(f.getFullPathName(), juce::dontSendNotification);
                updateButtonStates();
            }
        });
}

// ──────────────────────────────────────────────────────── Action buttons ──

void MusicPackagerAudioProcessorEditor::doScan()
{
    processor.startScan();
    updateButtonStates();
    statusLabel.setText("Scanning...", juce::dontSendNotification);
}

void MusicPackagerAudioProcessorEditor::doPackage()
{
    processor.startPackage();
    updateButtonStates();
    statusLabel.setText("Packaging...", juce::dontSendNotification);
}

// ──────────────────────────────────────────────────────────────── paint ──

void MusicPackagerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);

    g.setColour(border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 6.0f, 1.0f);

    // Title
    g.setColour(fg);
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText("Music Packager", 0, 0, getWidth(), 30, juce::Justification::centred);

    // Section sub-labels
    g.setFont(juce::FontOptions(10.5f));
    g.setColour(fgDim);
    g.drawText("FL Studio Project (.flp):", 10, 32, 300, 14, juce::Justification::left);
    g.drawText("Output Folder:",            10, 80, 300, 14, juce::Justification::left);

    // "Found files" header — show count if available
    int n = 0;
    { juce::ScopedLock sl(processor.dataLock); n = processor.foundFiles.size(); }
    juce::String header = n > 0
        ? "Found " + juce::String(n) + " audio file(s):"
        : "Audio files (scan to populate):";
    g.drawText(header, 10, 152, getWidth() - 20, 14, juce::Justification::left);
}

// ─────────────────────────────────────────────────────────────── layout ──

void MusicPackagerAudioProcessorEditor::resized()
{
    const int W    = getWidth();
    const int btnW = 80;
    const int pad  = 10;

    // FLP row: y=46
    flpPathLabel.setBounds(pad, 46, W - pad * 2 - btnW - 4, 28);
    flpBrowseBtn.setBounds(W - pad - btnW, 46, btnW, 28);

    // Output row: y=94
    outPathLabel.setBounds(pad, 94, W - pad * 2 - btnW - 4, 28);
    outBrowseBtn.setBounds(W - pad - btnW, 94, btnW, 28);

    // Options + action row: y=130
    folderToggle.setBounds(pad,       130, 70, 22);
    zipToggle.setBounds   (pad + 74,  130, 60, 22);
    packageBtn.setBounds  (W - pad - 90,     130, 90, 22);
    scanBtn.setBounds     (W - pad - 90 - 84, 130, 82, 22);

    // File list: y=166, h=170
    fileListBox.setBounds(pad, 166, W - pad * 2, 170);

    // Progress bar: y=344, h=20
    progressBar.setBounds(pad, 344, W - pad * 2, 20);

    // Status label: y=368
    statusLabel.setBounds(pad, 368, W - pad * 2, 18);
}
