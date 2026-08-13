// Renders one example loop per (family, mode) pair from real samples, for
// listening sessions. Usage:
//   OrchaAudition <outDir> <bpm> sample1.wav [sample2.wav [sample3.wav]]

#include <JuceHeader.h>
#include "../Engine/SampleLoader.h"
#include "../Engine/SampleAnalyzer.h"
#include "../Engine/LoopGenerator.h"
#include "../Engine/PatternValidator.h"
#include "../Engine/LoopRenderer.h"

using namespace orcha;

// Phase A3/A4: the blind-listening kit. Renders paired A/B cases (engine 2
// vs frozen engine 1, same seeds) with anonymized X/Y order, a hidden key,
// a fixed 120-case matrix manifest and a ratings template. No engine name,
// seed or score is visible to the listener.
static int makeBlindKit (const juce::File& outDir, double bpm,
                         std::vector<orcha::InputSample::Ptr> samples)
{
    using namespace orcha;
    outDir.createDirectory();
    const auto keyDir = outDir.getChildFile (".key");
    keyDir.createDirectory();
    const double sr = 48000.0;

    LoopRenderer::Context ctx;
    ctx.sampleRate = sr;
    ctx.bpm = bpm;
    ctx.samples = std::move (samples);
    ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);

    juce::WavAudioFormat wav;
    juce::Random order (20260813);   // fixed: the kit is reproducible
    juce::String key = "case,X,Y,family,mode,bars,energy,density,randomness,motifSeed,repeatOf\n";
    juce::String ratings;

    // 120-case matrix manifest (definitions only; renders below cover the
    // 40-case first-round subset + hidden repeats).
    juce::String matrix = "case,family,mode,bars,energy,density,randomness\n";
    int matrixCase = 0;
    for (int f = 0; f < 10; ++f)
        for (int m = 0; m < 4; ++m)
            for (int macroLevel = 0; macroLevel < 3; ++macroLevel)
            {
                GeneratorSettings s;
                s.family = (Family) f;
                s.mode = (Mode) m;
                s.bars = macroLevel == 0 ? 1 : macroLevel == 1 ? 2 : 4;
                s.energy = 0.25f + 0.25f * (float) macroLevel;
                s.density = 0.3f + 0.2f * (float) macroLevel;
                s.randomness = 0.15f + 0.25f * (float) macroLevel;
                matrix << ++matrixCase << ',' << familyName (s.family) << ','
                       << modeName (s.mode) << ',' << s.bars << ','
                       << juce::String (s.energy, 2) << ','
                       << juce::String (s.density, 2) << ','
                       << juce::String (s.randomness, 2) << "\n";
            }
    outDir.getChildFile ("test-matrix.csv").replaceWithText (matrix);

    auto renderCase = [&] (int caseNum, const GeneratorSettings& s,
                           juce::uint64 seed, int repeatOf)
    {
        const auto p2 = PatternValidator::validate (LoopGenerator::generateV2 (
            seed, LoopGenerator::deriveSeed (seed, 4242), s));
        const auto p1 = PatternValidator::validate (LoopGenerator::generate (seed, s));
        auto b2 = LoopRenderer::render (p2, ctx);
        auto b1 = LoopRenderer::render (p1, ctx);
        // Consistent loudness: both normalized to the same peak. Musical
        // dynamics inside each loop stay untouched.
        for (auto* b : { &b2, &b1 })
        {
            const float peak = b->getMagnitude (0, b->getNumSamples());
            if (peak > 0.001f)
                b->applyGain (juce::Decibels::decibelsToGain (-1.0f) / peak);
        }
        const bool engine2First = order.nextBool();
        auto writeOne = [&] (const juce::AudioBuffer<float>& buf, const juce::String& tag)
        {
            const auto f = outDir.getChildFile (
                "case_" + juce::String (caseNum).paddedLeft ('0', 3) + "_" + tag + ".wav");
            f.deleteFile();
            std::unique_ptr<juce::FileOutputStream> stream (f.createOutputStream());
            if (stream == nullptr) return;
            std::unique_ptr<juce::AudioFormatWriter> w (
                wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));
            if (w == nullptr) return;
            stream.release();
            w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        };
        writeOne (engine2First ? b2 : b1, "X");
        writeOne (engine2First ? b1 : b2, "Y");
        key << caseNum << ',' << (engine2First ? "engine2" : "engine1") << ','
            << (engine2First ? "engine1" : "engine2") << ','
            << familyName (s.family) << ',' << modeName (s.mode) << ','
            << s.bars << ',' << juce::String (s.energy, 2) << ','
            << juce::String (s.density, 2) << ',' << juce::String (s.randomness, 2)
            << ',' << juce::String::toHexString ((juce::int64) seed) << ','
            << repeatOf << "\n";
        ratings << "{\"case\":" << caseNum
                << ",\"choice\":\"\",\"confidence\":0,\"tags\":[],\"note\":\"\"}\n";
    };

    int caseNum = 0;
    std::vector<std::pair<GeneratorSettings, juce::uint64>> rendered;
    for (int f = 0; f < 10; ++f)
        for (int m = 0; m < 4; ++m)
        {
            GeneratorSettings s;
            s.family = (Family) f;
            s.mode = (Mode) m;
            s.bars = 1 + (caseNum % 3 == 1 ? 1 : caseNum % 3 == 2 ? 3 : 0);
            const auto seed = LoopGenerator::deriveSeed (0xB11D0, caseNum);
            renderCase (++caseNum, s, seed, 0);
            rendered.push_back ({ s, seed });
        }
    // Hidden repeats: 6 of the 40, re-rendered under new case numbers, for
    // the listener-consistency check.
    for (int r = 0; r < 6; ++r)
    {
        const int src = order.nextInt ((int) rendered.size());
        renderCase (++caseNum, rendered[(size_t) src].first,
                    rendered[(size_t) src].second, src + 1);
    }

    keyDir.getChildFile ("key.csv").replaceWithText (key);
    outDir.getChildFile ("ratings_template.jsonl").replaceWithText (ratings);
    std::cout << "blind kit: " << caseNum << " A/B cases in "
              << outDir.getFullPathName() << "\n";
    return 0;
}

int main (int argc, char* argv[])
{
    if (argc < 4)
    {
        std::cout << "usage: OrchaAudition <outDir> <bpm> <sample1> [sample2 [sample3]]\n"
                     "       OrchaAudition blind <outDir> <bpm> <samples...>\n";
        return 1;
    }
    const bool blind = juce::String (argv[1]) == "blind";
    if (blind && argc < 5)
        return 1;

    const int base = blind ? 1 : 0;
    const juce::File outDir { juce::String (argv[1 + base]) };
    outDir.createDirectory();
    const double bpm = juce::jlimit (60.0, 200.0,
                                     juce::String (argv[2 + base]).getDoubleValue());
    const double sr = 48000.0;

    LoopRenderer::Context ctx;
    ctx.sampleRate = sr;
    ctx.bpm = bpm;
    for (int i = 0; i < juce::jmin (3, argc - 3 - base); ++i)
    {
        auto s = SampleLoader::load (juce::File { juce::String (argv[3 + base + i]) });
        if (s == nullptr)
        {
            std::cout << "could not load " << argv[3 + i] << "\n";
            return 1;
        }
        ctx.samples.push_back (std::move (s));
    }
    ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);

    if (blind)
        return makeBlindKit (outDir, bpm, ctx.samples);

    juce::WavAudioFormat wav;
    int written = 0, expected = 0;
    for (auto family : { Family::EDM, Family::MELODIC_TECHNO, Family::PSYTRANCE,
                         Family::URBAN, Family::BREAKS, Family::ARABIC,
                         Family::MEDITERRANEAN, Family::AFRO, Family::CINEMATIC,
                         Family::HYBRID })
        for (auto mode : { Mode::DROP, Mode::BREAK, Mode::BUILD, Mode::GROOVE })
        {
            ++expected;
            GeneratorSettings s;
            s.family = family;
            s.mode = mode;
            s.bars = 2;
            s.energy = 0.7f;
            s.density = 0.55f;
            s.randomness = 0.35f;

            const auto seed = LoopGenerator::deriveSeed (
                20260813, (int) family * 16 + (int) mode);
            // The listening set hears ENGINE 2; a legacy/ twin of every file
            // renders the SAME seeds through the frozen v1 for direct A/B.
            const auto pattern = PatternValidator::validate (
                LoopGenerator::generateV2 (seed,
                    LoopGenerator::deriveSeed (seed, 4242), s));
            auto buffer = LoopRenderer::render (pattern, ctx);
            const auto legacy = PatternValidator::validate (
                LoopGenerator::generate (seed, s));
            auto legacyBuffer = LoopRenderer::render (legacy, ctx);

            // Two loop cycles per file, so the seam is audible.
            juce::AudioBuffer<float> twice (2, buffer.getNumSamples() * 2);
            for (int ch = 0; ch < 2; ++ch)
            {
                twice.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());
                twice.copyFrom (ch, buffer.getNumSamples(), buffer, ch, 0,
                                buffer.getNumSamples());
            }

            const auto file = outDir.getChildFile (
                (juce::String (familyName (family)) + "_" + modeName (mode) + ".wav")
                    .replaceCharacter (' ', '_'));
            file.deleteFile();
            std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
            if (stream == nullptr)
                continue;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));
            if (writer == nullptr)
                continue;
            stream.release();
            writer->writeFromAudioSampleBuffer (twice, 0, twice.getNumSamples());
            ++written;

            // Legacy A/B twin.
            const auto legacyDir = outDir.getChildFile ("legacy");
            legacyDir.createDirectory();
            juce::AudioBuffer<float> ltwice (2, legacyBuffer.getNumSamples() * 2);
            for (int ch = 0; ch < 2; ++ch)
            {
                ltwice.copyFrom (ch, 0, legacyBuffer, ch, 0, legacyBuffer.getNumSamples());
                ltwice.copyFrom (ch, legacyBuffer.getNumSamples(), legacyBuffer, ch, 0,
                                 legacyBuffer.getNumSamples());
            }
            const auto lfile = legacyDir.getChildFile (file.getFileName());
            lfile.deleteFile();
            std::unique_ptr<juce::FileOutputStream> lstream (lfile.createOutputStream());
            if (lstream != nullptr)
            {
                std::unique_ptr<juce::AudioFormatWriter> lwriter (
                    wav.createWriterFor (lstream.get(), sr, 2, 24, {}, 0));
                if (lwriter != nullptr)
                {
                    lstream.release();
                    lwriter->writeFromAudioSampleBuffer (ltwice, 0, ltwice.getNumSamples());
                }
            }
        }

    std::cout << "wrote " << written << " audition loops to "
              << outDir.getFullPathName() << "\n";
    return written == expected ? 0 : 1;
}
