#pragma once
#include <JuceHeader.h>

class MusicPackagerAudioProcessor : public juce::AudioProcessor
{
public:
    MusicPackagerAudioProcessor();
    ~MusicPackagerAudioProcessor() override;

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Paths and options — written from UI thread, read in worker thread
    juce::String flpPath;
    juce::String outputPath;
    bool useZip = false;

    // Worker control
    void startScan();
    void startPackage();
    void cancelWork();

    enum class WorkerState { Idle, Working, Done, Failed };
    std::atomic<WorkerState> workerState { WorkerState::Idle };
    std::atomic<float>       workerProgress { 0.0f };

    // Results — protected by dataLock
    juce::CriticalSection dataLock;
    juce::String      statusMessage;
    juce::StringArray foundFiles;

private:
    class Worker;
    std::unique_ptr<Worker> worker;

    void doScan();
    void doPackage();

    static juce::StringArray scanFlpBinary(const juce::File& file);
    static bool hasAudioExtension(const juce::String& path);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusicPackagerAudioProcessor)
};
