#include "PluginProcessor.h"
#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────── Worker ──

class MusicPackagerAudioProcessor::Worker : public juce::Thread
{
public:
    enum class Mode { Scan, Package };

    Worker(MusicPackagerAudioProcessor& p, Mode m)
        : Thread("MusicPackager"), proc(p), mode(m) {}

    void run() override
    {
        if (mode == Mode::Scan) proc.doScan();
        else                    proc.doPackage();
    }

private:
    MusicPackagerAudioProcessor& proc;
    Mode mode;
};

// ──────────────────────────────────────────────────────────── Static helpers ──

bool MusicPackagerAudioProcessor::hasAudioExtension(const juce::String& path)
{
    static const char* exts[] = {
        ".wav", ".mp3", ".ogg", ".flac", ".aiff", ".aif",
        ".wv",  ".w64", ".m4a", ".opus", nullptr
    };
    auto lower = path.toLowerCase();
    for (int i = 0; exts[i]; ++i)
        if (lower.endsWith(exts[i])) return true;
    return false;
}

// Extracts audio file paths from an FL Studio .flp binary.
// Handles UTF-16 LE Windows paths (C:\...) and UTF-8 macOS paths (/Users/...).
juce::StringArray MusicPackagerAudioProcessor::scanFlpBinary(const juce::File& file)
{
    juce::StringArray result;
    juce::MemoryBlock raw;
    if (!file.loadFileAsData(raw) || raw.getSize() < 4) return result;

    const auto* b  = static_cast<const uint8_t*>(raw.getData());
    const int64 sz = int64(raw.getSize());

    // Validate FL Studio magic header
    if (b[0] != 'F' || b[1] != 'L' || b[2] != 'h' || b[3] != 'd') return result;

    auto addIfAudio = [&](const juce::String& s)
    {
        if (s.length() > 5 && hasAudioExtension(s))
            result.addIfNotAlreadyThere(s.trim());
    };

    for (int64 i = 0; i < sz - 6; ++i)
    {
        // UTF-16 LE Windows drive path: [A-Z] \0 ':' \0 '\' \0
        if (b[i] >= 'A' && b[i] <= 'Z' &&
            b[i+1] == 0x00 && b[i+2] == ':' && b[i+3] == 0x00 &&
            (b[i+4] == '\\' || b[i+4] == '/') && b[i+5] == 0x00)
        {
            juce::String s;
            for (int64 j = i; j + 1 < sz; j += 2)
            {
                uint16_t wc = uint16_t(b[j]) | (uint16_t(b[j+1]) << 8);
                if (wc == 0 || wc < 0x09 || wc == 0x0A || wc == 0x0D) break;
                s += juce::juce_wchar(wc);
            }
            addIfAudio(s);
        }
        // UTF-8 macOS / Linux paths
        else if (b[i] == '/' && i + 1 < sz && b[i+1] > 0x20)
        {
            const char* prefixes[] = { "/Users/", "/Volumes/", "/home/", nullptr };
            bool match = false;
            for (int k = 0; prefixes[k]; ++k)
            {
                size_t plen = strlen(prefixes[k]);
                if (i + int64(plen) <= sz && memcmp(b + i, prefixes[k], plen) == 0)
                { match = true; break; }
            }
            if (match)
            {
                juce::String s;
                for (int64 j = i; j < sz && b[j] >= 0x20; ++j)
                    s += char(b[j]);
                addIfAudio(s);
            }
        }
    }
    return result;
}

// ──────────────────────────────────────────────────────────────── Processor ──

MusicPackagerAudioProcessor::MusicPackagerAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{}

MusicPackagerAudioProcessor::~MusicPackagerAudioProcessor() { cancelWork(); }

bool MusicPackagerAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// Audio passthrough — this plugin is analysis/utility only
void MusicPackagerAudioProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) {}

// ─────────────────────────────────────────────────────────── Worker control ──

void MusicPackagerAudioProcessor::startScan()
{
    if (worker && worker->isThreadRunning()) return;
    workerState.store(WorkerState::Working);
    workerProgress.store(0.0f);
    { juce::ScopedLock sl(dataLock); statusMessage = "Scanning FLP..."; foundFiles.clear(); }
    worker = std::make_unique<Worker>(*this, Worker::Mode::Scan);
    worker->startThread();
}

void MusicPackagerAudioProcessor::startPackage()
{
    if (worker && worker->isThreadRunning()) return;
    workerState.store(WorkerState::Working);
    workerProgress.store(0.0f);
    { juce::ScopedLock sl(dataLock); statusMessage = "Starting..."; }
    worker = std::make_unique<Worker>(*this, Worker::Mode::Package);
    worker->startThread();
}

void MusicPackagerAudioProcessor::cancelWork()
{
    if (worker) { worker->signalThreadShouldExit(); worker->waitForThreadToExit(3000); }
}

// ─────────────────────────────────────────────────────────────── Background ──

void MusicPackagerAudioProcessor::doScan()
{
    juce::File flp(flpPath);
    if (!flp.existsAsFile())
    {
        juce::ScopedLock sl(dataLock);
        statusMessage = "Error: FLP file not found.";
        workerState.store(WorkerState::Failed);
        return;
    }

    auto files = scanFlpBinary(flp);
    {
        juce::ScopedLock sl(dataLock);
        foundFiles    = files;
        statusMessage = "Found " + juce::String(files.size()) + " audio file reference(s).";
    }
    workerProgress.store(1.0f);
    workerState.store(WorkerState::Done);
}

void MusicPackagerAudioProcessor::doPackage()
{
    juce::File flp(flpPath);
    juce::File outRoot(outputPath);

    if (!flp.existsAsFile())
    {
        juce::ScopedLock sl(dataLock);
        statusMessage = "Error: FLP file not found.";
        workerState.store(WorkerState::Failed);
        return;
    }

    juce::String pkgName = flp.getFileNameWithoutExtension() + "_packaged";

    // For ZIP mode we stage into a temp folder first; otherwise write directly
    juce::File stageDir = useZip
        ? juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(pkgName)
        : outRoot.getChildFile(pkgName);

    if (!stageDir.createDirectory())
    {
        juce::ScopedLock sl(dataLock);
        statusMessage = "Error: Cannot create output directory.";
        workerState.store(WorkerState::Failed);
        return;
    }

    // Copy FLP into the package root
    flp.copyFileTo(stageDir.getChildFile(flp.getFileName()));

    // Copy audio into Samples/
    juce::File samplesDir = stageDir.getChildFile("Samples");
    samplesDir.createDirectory();

    juce::StringArray localFiles;
    { juce::ScopedLock sl(dataLock); localFiles = foundFiles; }

    const int total = localFiles.size();
    int copied = 0;

    for (int i = 0; i < total; ++i)
    {
        if (juce::Thread::currentThreadShouldExit()) break;

        workerProgress.store(float(i) / float(std::max(1, total)));

        juce::File src(localFiles[i]);
        {
            juce::ScopedLock sl(dataLock);
            statusMessage = "Copying " + juce::String(i + 1) + "/" + juce::String(total)
                          + ": " + src.getFileName();
        }

        if (src.existsAsFile())
        {
            // Disambiguate name clashes by appending a counter suffix
            juce::File dest = samplesDir.getChildFile(src.getFileName());
            if (dest.existsAsFile())
                dest = samplesDir.getChildFile(src.getFileNameWithoutExtension()
                           + "_" + juce::String(i) + src.getFileExtension());
            src.copyFileTo(dest);
            ++copied;
        }
    }

    if (!juce::Thread::currentThreadShouldExit() && useZip)
    {
        { juce::ScopedLock sl(dataLock); statusMessage = "Creating ZIP archive..."; }

        juce::ZipFile::Builder builder;
        auto allFiles = stageDir.findChildFiles(juce::File::findFiles, true);
        for (auto& f : allFiles)
            builder.addFile(f, 6, f.getRelativePathFrom(stageDir));

        juce::File zipOut = outRoot.getChildFile(pkgName + ".zip");
        if (auto stream = zipOut.createOutputStream())
        {
            double zipProg = 0.0;
            builder.writeToStream(*stream, &zipProg);
        }
        stageDir.deleteRecursively();
    }

    workerProgress.store(1.0f);
    {
        juce::ScopedLock sl(dataLock);
        int missed = total - copied;
        statusMessage = "Done! Copied " + juce::String(copied) + "/" + juce::String(total) + " file(s)";
        if (missed > 0) statusMessage += " (" + juce::String(missed) + " not found on this machine)";
        if (useZip)     statusMessage += " → " + pkgName + ".zip";
    }
    workerState.store(WorkerState::Done);
}

// ─────────────────────────────────────────────────────────── State save/load ──

void MusicPackagerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("MusicPackager");
    xml.setAttribute("flpPath",    flpPath);
    xml.setAttribute("outputPath", outputPath);
    xml.setAttribute("useZip",     useZip);
    copyXmlToBinary(xml, destData);
}

void MusicPackagerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        flpPath    = xml->getStringAttribute("flpPath");
        outputPath = xml->getStringAttribute("outputPath");
        useZip     = xml->getBoolAttribute("useZip");
    }
}

juce::AudioProcessorEditor* MusicPackagerAudioProcessor::createEditor()
{
    return new MusicPackagerAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MusicPackagerAudioProcessor();
}
