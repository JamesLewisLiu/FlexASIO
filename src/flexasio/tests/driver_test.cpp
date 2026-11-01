#include "../FlexASIO/flexasio.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <thread>

using namespace flexasio;
namespace {
    FlexASIO* driver = nullptr;
    std::array<ASIOBufferInfo, 8> buffers{};
    std::atomic<int> switches{0}, resets{0};
    bool useTimeInfo = false, useOutputReady = false;
    std::string outputDevice;
    bool lowLatency = false;
    bool exclusive = false, profile = false;
    bool allowConversion = false;
    long bufferFrames = 512;
    int runSeconds = 0;
    void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
    void Switch(long index, ASIOBool) {
        for (const auto& buffer : buffers)
            if (buffer.buffers[index]) std::memset(buffer.buffers[index], 0, static_cast<size_t>(bufferFrames) * sizeof(int32_t));
        ++switches;
        if (useOutputReady) driver->OutputReady();
    }
    ASIOTime* SwitchTime(ASIOTime* time, long index, ASIOBool direct) { Switch(index, direct); return time; }
    void RateChanged(ASIOSampleRate) {}
    long Message(long selector, long value, void*, double*) {
        if (selector == kAsioSelectorSupported) return value == kAsioResetRequest || value == kAsioSupportsTimeInfo;
        if (selector == kAsioSupportsTimeInfo) return useTimeInfo ? 1 : 0;
        if (selector == kAsioResetRequest) { ++resets; return 1; }
        return 0;
    }
    void WriteConfig(const std::filesystem::path& directory, bool enabled, double gain) {
        std::ofstream file(directory / "FlexASIO.toml");
        file << "backend = \"Windows WASAPI\"\nbufferSizeSamples = " << bufferFrames << "\n[input]\ndevice = \"\"\n[output]\nchannels = 2\nsampleType = \"Int32\"\n";
        if (!outputDevice.empty()) file << "device = " << std::quoted(outputDevice) << '\n';
        if (lowLatency) file << "suggestedLatencySeconds = 0.0\n";
        file << "wasapiExclusiveMode = " << (exclusive ? "true" : "false") << '\n';
        if (allowConversion) file << "wasapiExplicitSampleFormat = false\n";
        file << "[output.mixer]\nenabled = "
             << (enabled ? "true" : "false") << "\ngain = " << gain << '\n';
        Check(bool(file), "write test configuration");
    }
    void Run(const std::filesystem::path& directory, bool enabled, bool timeInfo, bool outputReady) {
        WriteConfig(directory, enabled, 1);
        FlexASIO instance(nullptr, directory);
        driver = &instance; useTimeInfo = timeInfo; useOutputReady = outputReady;
        switches = 0; resets = 0; buffers = {};
        long in = -1, out = -1;
        instance.GetChannels(&in, &out);
        Check(in == 0 && out == (enabled ? 8 : 2), "logical channel enumeration");
        long minimum, maximum, preferred, granularity;
        instance.GetBufferSize(&minimum, &maximum, &preferred, &granularity);
        Check(minimum == bufferFrames && maximum == bufferFrames && preferred == bufferFrames && granularity == 0, "fixed buffer size");
        Check(instance.CanSampleRate(48000), "48 kHz format support");
        instance.SetSampleRate(48000);
        ASIOSampleRate rate; instance.GetSampleRate(&rate);
        Check(rate == 48000, "48 kHz sample rate");
        // Deliberately reverse the channel order to exercise channelNum lookup.
        for (long i = 0; i < out; ++i) { buffers[static_cast<size_t>(i)].channelNum = out - i - 1; buffers[static_cast<size_t>(i)].isInput = ASIOFalse; }
        ASIOCallbacks callbacks{Switch, RateChanged, Message, SwitchTime};
        if (outputReady) instance.OutputReady();
        instance.CreateBuffers(buffers.data(), out, bufferFrames, &callbacks);
        for (long i = 0; i < out; ++i) {
            ASIOChannelInfo info{}; info.channel = i; info.isInput = ASIOFalse;
            instance.GetChannelInfo(&info);
            Check(info.type == ASIOSTInt32LSB && info.isActive == ASIOTrue, "Int32 active channel");
        }
        long inputLatency, outputLatency; instance.GetLatencies(&inputLatency, &outputLatency);
        Check(outputLatency >= 0, "reported output latency");
        for (int iteration = 0; iteration < 2; ++iteration) {
            const auto before = switches.load();
            instance.Start();
            for (int wait = 0; wait < 100 && switches.load() < before + 4; ++wait) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (runSeconds > 0) {
                const auto running = switches.load();
                std::this_thread::sleep_for(std::chrono::seconds(runSeconds));
                Check(switches.load() > running, "callbacks continue during sustained playback");
            }
            instance.Stop();
            Check(switches.load() >= before + 4, "audio callbacks and restart");
        }
        WriteConfig(directory, enabled, 0.75);
        for (int wait = 0; wait < 150 && resets.load() == 0; ++wait) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Check(resets.load() > 0, "configuration change issues reset");
        instance.DisposeBuffers();
        std::cout << "PASS: buffer=" << bufferFrames << " exclusive=" << exclusive << " enabled=" << enabled << " timeInfo=" << timeInfo << " outputReady=" << outputReady << " callbacks=" << switches << " latency=" << outputLatency << " frames (" << outputLatency / 48.0 << " ms)\n";
        driver = nullptr;
    }

    void TestInvalid(const std::filesystem::path& directory) {
        for (const auto* output : {"channels = 8", "device = \"\""}) {
            { std::ofstream file(directory / "FlexASIO.toml");
              file << "[output]\n" << output << "\n[output.mixer]\nenabled=true\n"; }
            bool rejected = false;
            try { FlexASIO invalid(nullptr, directory); }
            catch (const std::exception& error) {
                rejected = std::string(error.what()).find("Invalid configuration") != std::string::npos;
            }
            Check(rejected, "invalid mixer hardware configuration rejected");
        }
        std::cout << "PASS: invalid hardware channel count and disabled output rejected\n";
    }
}
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: FlexASIODriverTest <scratch-directory> [WASAPI-device-name] [--low-latency] [--exclusive] [--convert] [--frames N] [--seconds N] [--profile] (plays silence)\n"; return 2; }
    try {
        int arg = 2;
        if (arg < argc && std::string(argv[arg]).rfind("--", 0) != 0) outputDevice = argv[arg++];
        while (arg < argc) {
            const std::string option = argv[arg++];
            if (option == "--low-latency") lowLatency = true;
            else if (option == "--exclusive") exclusive = true;
            else if (option == "--convert") allowConversion = true;
            else if (option == "--profile") profile = true;
            else if (option == "--frames" || option == "--seconds") {
                Check(arg < argc, "missing option value");
                const int value = std::stoi(argv[arg++]);
                if (option == "--frames") { Check(value >= 32 && value <= 4096, "frames must be 32..4096"); bufferFrames = value; }
                else { Check(value >= 0 && value <= 60, "seconds must be 0..60"); runSeconds = value; }
            }
            else throw std::runtime_error("unknown option: " + option);
        }
        const auto directory = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(directory);
        if (profile) { Run(directory, true, false, true); return 0; }
        for (bool timeInfo : {false, true})
            for (bool outputReady : {false, true}) Run(directory, true, timeInfo, outputReady);
        Run(directory, false, false, false);
        TestInvalid(directory);
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
