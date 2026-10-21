//    Audionaut - Audio editing application for multitrack recordings.
//    Copyright (C) 2025 Klaus Voltmer
//
//    Audionaut uses a GPL/commercial licence - see LICENCE.md for details.

// The AudioThumbnail is shared per file, and every clip cut from the file
// draws its own range of it. Each caller owns the per-pixel columns it draws
// from (AudioThumbnail::DrawCache), so a repaint of many clips rebuilds
// nothing - the thumbnail-wide LRU that preceded it thrashed above 32 clips.
#include <catch2/catch_test_macros.hpp>

#include "Engine/Resource/AudioResourceContainer.h"
#include "Interface/Widgets/audium_AudioThumbnail.h"
#include "TestEngine.h"

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int numChannels = 2;
    constexpr int numSamples = (int) sampleRate;   // one second
    constexpr int samplesPerThumbSample = 64;

    juce::AudioBuffer<float> noise()
    {
        juce::AudioBuffer<float> buffer (numChannels, numSamples);
        juce::Random random (1234);
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
        return buffer;
    }

    std::shared_ptr<audium::AudioThumbnail> makeThumbnail (const TestEngine& engine)
    {
        auto resources = engine->getAudioResourceContainer();
        return std::make_shared<audium::AudioThumbnail> (samplesPerThumbSample,
                                                         *resources->getAudioFormatManager(),
                                                         *resources->getAudioThumbnailCache());
    }

    /// A thumbnail filled synchronously from the buffer (the recording path):
    /// draws come from the stored low-res points.
    std::shared_ptr<audium::AudioThumbnail> storedPointsThumbnail (const TestEngine& engine,
                                                                   const juce::AudioBuffer<float>& buffer)
    {
        auto thumbnail = makeThumbnail (engine);
        thumbnail->reset (numChannels, sampleRate, numSamples);
        thumbnail->addBlock (0, buffer, 0, numSamples);
        return thumbnail;
    }

    /// A thumbnail reading the buffer through a reader (what a file gets):
    /// zoomed in far enough, draws read the source per pixel.
    std::shared_ptr<audium::AudioThumbnail> readerThumbnail (const TestEngine& engine,
                                                             const juce::AudioBuffer<float>& buffer,
                                                             juce::int64 hash)
    {
        auto thumbnail = makeThumbnail (engine);
        thumbnail->setSource (&buffer, sampleRate, hash);
        for (int i = 0; i < 400 && ! thumbnail->isFullyLoaded(); ++i)
            juce::Thread::sleep (25);
        REQUIRE (thumbnail->isFullyLoaded());
        return thumbnail;
    }

    struct Canvas
    {
        juce::Image image { juce::Image::ARGB, 120, 40, true };
        juce::Graphics g { image };
        const juce::Rectangle<int> area { 0, 0, 100, 40 };

        Canvas() { g.setColour (juce::Colours::white); }

        bool hasInk() const
        {
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                    if (image.getPixelAt (x, y).getAlpha() > 0)
                        return true;
            return false;
        }
    };

    constexpr int numClips = 64;   // twice the old LRU capacity

    /// One paint pass of numClips clip views (both channel rows each) that
    /// share the thumbnail, every clip with its own range.
    void paintPass (audium::AudioThumbnail& thumbnail, Canvas& canvas,
                    std::vector<audium::AudioThumbnail::DrawCache>& caches,
                    double rangeSeconds, double scrollSeconds = 0.0)
    {
        for (int clip = 0; clip < numClips; ++clip)
        {
            const auto start = scrollSeconds + clip * 0.002;
            for (int channel = 0; channel < numChannels; ++channel)
                thumbnail.drawChannel (canvas.g, canvas.area, start, start + rangeSeconds,
                                       channel, 1.0f, nullptr, caches[(size_t) clip]);
        }
    }
}

TEST_CASE ("thumbnail draw cache: many clips of one file don't evict each other", "[thumbnail][cache]")
{
    TestEngine engine;
    const auto buffer = noise();
    Canvas canvas;
    std::vector<audium::AudioThumbnail::DrawCache> caches (numClips);

    SECTION ("stored points (zoomed out)")
    {
        auto thumbnail = storedPointsThumbnail (engine, buffer);
        // 100 px over 0.2 s = 96 source samples per pixel, above the 64 stored
        const double range = 0.2;

        paintPass (*thumbnail, canvas, caches, range);
        paintPass (*thumbnail, canvas, caches, range);
        paintPass (*thumbnail, canvas, caches, range);

        for (auto& cache : caches)
            CHECK (cache.getNumRefills() == 1);
        CHECK (canvas.hasInk());

        SECTION ("a scroll moves every clip's range: one refill each, then stable")
        {
            paintPass (*thumbnail, canvas, caches, range, 0.05);
            paintPass (*thumbnail, canvas, caches, range, 0.05);
            for (auto& cache : caches)
                CHECK (cache.getNumRefills() == 2);
        }

        SECTION ("new level data invalidates every cache once")
        {
            thumbnail->addBlock (0, buffer, 0, numSamples);
            paintPass (*thumbnail, canvas, caches, range);
            paintPass (*thumbnail, canvas, caches, range);
            for (auto& cache : caches)
                CHECK (cache.getNumRefills() == 2);
        }

        SECTION ("a cache moved to another thumbnail refills")
        {
            auto other = storedPointsThumbnail (engine, buffer);
            paintPass (*other, canvas, caches, range);
            paintPass (*other, canvas, caches, range);
            for (auto& cache : caches)
                CHECK (cache.getNumRefills() == 2);
        }
    }

    SECTION ("source reads (zoomed in)")
    {
        auto thumbnail = readerThumbnail (engine, buffer, 0x7a1b);
        // 100 px over 0.1 s = 48 source samples per pixel, below the 64 stored:
        // a refill reads the source once per pixel
        const double range = 0.1;

        paintPass (*thumbnail, canvas, caches, range);
        paintPass (*thumbnail, canvas, caches, range);
        paintPass (*thumbnail, canvas, caches, range);

        for (auto& cache : caches)
            CHECK (cache.getNumRefills() == 1);
        CHECK (canvas.hasInk());
    }

    SECTION ("the overloads without a cache still draw")
    {
        auto thumbnail = storedPointsThumbnail (engine, buffer);
        thumbnail->drawChannel (canvas.g, canvas.area, 0.0, 0.2, 0, 1.0f);
        thumbnail->drawChannel (canvas.g, canvas.area, 0.3, 0.5, 1, 1.0f, nullptr);
        thumbnail->drawChannels (canvas.g, canvas.area, 0.0, 1.0, 1.0f);
        CHECK (canvas.hasInk());
    }
}
