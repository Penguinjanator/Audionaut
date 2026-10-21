//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

#include "Stft.h"

#include <algorithm>
#include <cmath>

namespace audium {

namespace {

int fftOrder(int frameSize)
{
    jassert(frameSize > 0 && (frameSize & (frameSize - 1)) == 0);
    auto order = 0;
    while ((1 << order) < frameSize)
        ++order;
    return order;
}

} // namespace

Stft::Stft() : Stft(Parameters()) {}

Stft::Stft(const Parameters& params)
    : parameters(params),
      fft(fftOrder(params.frameSize)),
      window((size_t) params.frameSize, juce::dsp::WindowingFunction<float>::hann, false)
{
    jassert(params.hopSize > 0);
}

int Stft::numFrames(size_t numSamples) const
{
    if (numSamples == 0)
        return 0;
    return 1 + (int) ((numSamples - 1) / (size_t) parameters.hopSize);
}

int Stft::numBins() const
{
    return parameters.frameSize / 2 + 1;
}

double Stft::binFrequency(int bin, double sampleRate) const
{
    return (double) bin * sampleRate / (double) parameters.frameSize;
}

bool Stft::forEachPowerFrame(const std::vector<float>& signal,
                             const std::function<void(int, const float*)>& frameFn,
                             const std::atomic<bool>* shouldAbort) const
{
    const auto aborted = [shouldAbort]
    {
        return shouldAbort != nullptr && shouldAbort->load(std::memory_order_relaxed);
    };

    const auto frameSize = parameters.frameSize;
    const auto half = frameSize / 2;
    const auto numSamples = (int) signal.size();
    const auto frames = numFrames(signal.size());

    // The FFT works in place on a buffer twice the frame size.
    std::vector<float> buffer((size_t) frameSize * 2);
    std::vector<float> power((size_t) numBins());

    for (int t = 0; t < frames; ++t)
    {
        if ((t & 63) == 0 && aborted())
            return false;

        const auto start = t * parameters.hopSize - half;
        std::fill(buffer.begin(), buffer.end(), 0.0f);

        const auto from = std::max(0, start);
        const auto to = std::min(numSamples, start + frameSize);
        if (to > from)
            std::copy(signal.begin() + from, signal.begin() + to, buffer.begin() + (from - start));

        window.multiplyWithWindowingTable(buffer.data(), (size_t) frameSize);
        fft.performFrequencyOnlyForwardTransform(buffer.data(), true);

        for (int bin = 0; bin <= half; ++bin)
            power[(size_t) bin] = buffer[(size_t) bin] * buffer[(size_t) bin];

        frameFn(t, power.data());
    }

    return true;
}

} // namespace audium
