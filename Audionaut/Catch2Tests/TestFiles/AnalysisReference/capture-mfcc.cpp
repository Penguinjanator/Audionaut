#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#define EIGEN_HAS_STD_RESULT_OF 0
#include <essentia/algorithmfactory.h>

#include <JuceHeader.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

// One-off, hidden: writes the MFCC frames Essentia computes for the short test
// recordings, as a fixture for the native MFCC. Two configurations per file:
// "matched" is Essentia configured the way the native chain works (22050 Hz,
// 2048 / 512, plain Hann, Slaney mel with unit-area triangles, natural log,
// orthonormal DCT-II), so the two should agree frame for frame; "production"
// is the chain SBicSegmenter ran (44100 Hz, 2048 / 1024, Blackman-Harris 62,
// Essentia's MFCC defaults), for the record. Run with "[mfcc-capture]"; the
// output directory comes from MFCC_CAPTURE_DIR.

namespace {

using essentia::Real;
using essentia::standard::Algorithm;
using essentia::standard::AlgorithmFactory;

struct Chain {
    std::string name;
    Real sampleRate;
    int frameSize, hopSize;
    std::string window;
    bool normalizedWindow;
    std::string warping, weighting, normalize, logType;
    Real highFrequency;
};

struct Captured {
    std::vector<std::vector<Real>> mfcc, bands;
    size_t numSamples = 0;
    std::vector<Real> samples;
};

Captured mfccFrames(const juce::File& audioFile, const Chain& chain)
{
    AlgorithmFactory& factory = AlgorithmFactory::instance();

    auto audio = std::unique_ptr<Algorithm>(factory.create("MonoLoader",
                                                           "filename", audioFile.getFullPathName().toStdString(),
                                                           "sampleRate", chain.sampleRate));
    auto fc = std::unique_ptr<Algorithm>(factory.create("FrameCutter",
                                                        "frameSize", chain.frameSize,
                                                        "hopSize", chain.hopSize));
    auto w = std::unique_ptr<Algorithm>(factory.create("Windowing",
                                                       "type", chain.window,
                                                       "normalized", chain.normalizedWindow,
                                                       "zeroPhase", false));
    auto spec = std::unique_ptr<Algorithm>(factory.create("Spectrum"));
    auto mfcc = std::unique_ptr<Algorithm>(factory.create("MFCC",
                                                          "inputSize", chain.frameSize / 2 + 1,
                                                          "sampleRate", chain.sampleRate,
                                                          "numberBands", 40,
                                                          "numberCoefficients", 13,
                                                          "lowFrequencyBound", (Real) 0,
                                                          "highFrequencyBound", chain.highFrequency,
                                                          "warpingFormula", chain.warping,
                                                          "weighting", chain.weighting,
                                                          "normalize", chain.normalize,
                                                          "type", "power",
                                                          "logType", chain.logType,
                                                          "silenceThreshold", (Real) 1e-10,
                                                          "dctType", 2,
                                                          "liftering", 0));

    std::vector<Real> audioBuffer, frame, windowed, spectrum, bands, coefficients;
    audio->output("audio").set(audioBuffer);
    fc->input("signal").set(audioBuffer);
    fc->output("frame").set(frame);
    w->input("frame").set(frame);
    w->output("frame").set(windowed);
    spec->input("frame").set(windowed);
    spec->output("spectrum").set(spectrum);
    mfcc->input("spectrum").set(spectrum);
    mfcc->output("bands").set(bands);
    mfcc->output("mfcc").set(coefficients);

    audio->compute();

    Captured captured;
    captured.numSamples = audioBuffer.size();
    captured.samples = audioBuffer;
    while (true)
    {
        fc->compute();
        if (frame.empty())
            break;
        w->compute();
        spec->compute();
        mfcc->compute();
        captured.mfcc.push_back(coefficients);
        captured.bands.push_back(bands);
    }
    return captured;
}

} // namespace

SCENARIO("Capture Essentia's MFCC frames", "[.][mfcc-capture]")
{
    const auto* outDir = std::getenv("MFCC_CAPTURE_DIR");
    REQUIRE(outDir != nullptr);

    essentia::init();

    const Chain matched { "matched", 22050.0f, 2048, 512, "hann", false, "slaneyMel", "linear", "unit_tri", "log", 11025.0f };
    const Chain production { "production", 44100.0f, 2048, 1024, "blackmanharris62", true, "htkMel", "warping", "unit_sum", "dbamp", 11000.0f };

    const auto testFiles = juce::String(CURRENT_SOURCE_DIR) + "/TestFiles/";
    for (const std::string name : { "120-funk-export" })
    {
        const juce::File audioFile(testFiles + name + ".wav");
        REQUIRE(audioFile.existsAsFile());

        nlohmann::json out;
        out["audioFile"] = name + ".wav";
        out["essentia"] = essentia::version;
        for (const auto& chain : { matched, production })
        {
            nlohmann::json config;
            config["sampleRate"] = chain.sampleRate;
            config["frameSize"] = chain.frameSize;
            config["hopSize"] = chain.hopSize;
            config["window"] = chain.window;
            config["normalizedWindow"] = chain.normalizedWindow;
            config["warpingFormula"] = chain.warping;
            config["weighting"] = chain.weighting;
            config["normalize"] = chain.normalize;
            config["logType"] = chain.logType;
            config["highFrequencyBound"] = chain.highFrequency;
            const auto captured = mfccFrames(audioFile, chain);
            config["mfcc"] = captured.mfcc;
            config["numSamples"] = captured.numSamples;
            out[chain.name] = config;

            if (chain.name == "matched")
            {
                // The resampled signal as a 16-bit WAV next to the fixture,
                // so the native chain can read the very samples Essentia
                // analysed, with no resampler on either side; then the
                // matched chain run on that file, which is what the native
                // frames are held against exactly.
                const juce::File wavFile = juce::File(outDir).getChildFile(name + ".22050.wav");
                wavFile.deleteFile();
                {
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::OutputStream> wavStream(wavFile.createOutputStream());
                    auto options = juce::AudioFormatWriter::Options{}.withSampleRate(22050.0).withNumChannels(1).withBitsPerSample(16);
                    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(wavStream, options));
                    REQUIRE(writer != nullptr);
                    juce::AudioBuffer<float> buffer(1, (int) captured.samples.size());
                    buffer.copyFrom(0, 0, captured.samples.data(), (int) captured.samples.size());
                    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
                }
                out["resampledFile"] = wavFile.getFileName().toStdString();
                out["matchedFromResampledFile"] = mfccFrames(wavFile, chain).mfcc;

                // The production window alone: Blackman-Harris 62 on the same
                // samples with everything else matched.
                auto blackman = chain;
                blackman.window = "blackmanharris62";
                out["blackmanHarrisFromResampledFile"] = mfccFrames(wavFile, blackman).mfcc;
            }
        }

        const juce::File target = juce::File(outDir).getChildFile(name + ".mfcc.essentia.json");
        std::ofstream stream(target.getFullPathName().toStdString());
        REQUIRE(stream.good());
        stream << out.dump() << "\n";
        WARN("wrote " << target.getFullPathName() << " (" << out["matched"]["mfcc"].size() << " matched frames, "
             << out["matchedFromResampledFile"].size() << " from the resampled file, "
             << out["production"]["mfcc"].size() << " production frames)");
    }

    essentia::shutdown();
}
