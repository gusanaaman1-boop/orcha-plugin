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

int main (int argc, char* argv[])
{
    if (argc < 4)
    {
        std::cout << "usage: OrchaAudition <outDir> <bpm> <sample1> [sample2 [sample3]]\n";
        return 1;
    }

    const juce::File outDir { juce::String (argv[1]) };
    outDir.createDirectory();
    const double bpm = juce::jlimit (60.0, 200.0, juce::String (argv[2]).getDoubleValue());
    const double sr = 48000.0;

    LoopRenderer::Context ctx;
    ctx.sampleRate = sr;
    ctx.bpm = bpm;
    for (int i = 0; i < juce::jmin (3, argc - 3); ++i)
    {
        auto s = SampleLoader::load (juce::File { juce::String (argv[3 + i]) });
        if (s == nullptr)
        {
            std::cout << "could not load " << argv[3 + i] << "\n";
            return 1;
        }
        ctx.samples.push_back (std::move (s));
    }
    ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);

    juce::WavAudioFormat wav;
    int written = 0, expected = 0;
    for (auto family : { Family::EDM, Family::MELODIC_TECHNO, Family::PSYTRANCE,
                         Family::ARABIC, Family::MEDITERRANEAN, Family::AFRO,
                         Family::CINEMATIC, Family::HYBRID })
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
            const auto pattern = PatternValidator::validate (
                LoopGenerator::generate (seed, s));
            auto buffer = LoopRenderer::render (pattern, ctx);

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
        }

    std::cout << "wrote " << written << " audition loops to "
              << outDir.getFullPathName() << "\n";
    return written == expected ? 0 : 1;
}
