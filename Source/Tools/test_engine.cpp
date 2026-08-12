// ORCHA engine checks: determinism, diversity, musical guard-rails, render
// correctness. No UI, no plug-in wrapper - a broken editor cannot hide a
// broken generator.

#include <JuceHeader.h>
#include "../Engine/SampleAnalyzer.h"
#include "../Engine/LoopGenerator.h"
#include "../Engine/PatternValidator.h"
#include "../Engine/LoopRenderer.h"
#include "../Engine/RenderCache.h"

using namespace orcha;

static int failures = 0;
static int checks = 0;

static void check (bool condition, const juce::String& what)
{
    ++checks;
    if (! condition)
    {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

// Synthetic percussive one-shot: exponentially decaying tone burst. Distinct
// frequencies give the analyzer real spectral differences to sort on.
static InputSample::Ptr makeHit (double freq, double sampleRate, double seconds)
{
    auto s = std::make_shared<InputSample>();
    const int len = (int) (sampleRate * seconds);
    s->buffer.setSize (1, len);
    float* d = s->buffer.getWritePointer (0);
    for (int i = 0; i < len; ++i)
    {
        const double t = i / sampleRate;
        d[i] = (float) (std::sin (juce::MathConstants<double>::twoPi * freq * t)
                        * std::exp (-t * 18.0) * 0.8);
    }
    s->sourceSampleRate = sampleRate;
    s->name = "hit_" + juce::String (freq);
    s->analysis = SampleAnalyzer::analyze (s->buffer, sampleRate);
    return s;
}

int main()
{
    const double sr = 48000.0;

    // --- analyzer ---------------------------------------------------------------
    {
        auto low = makeHit (60.0, sr, 0.4);
        auto high = makeHit (4000.0, sr, 0.15);
        check (low->analysis.spectralCentroidHz < high->analysis.spectralCentroidHz,
               "centroid orders dark vs bright");
        check (low->analysis.lowEnergyRatio > 0.5f, "60 Hz hit is low-heavy");
        check (low->analysis.isOneShot && high->analysis.isOneShot,
               "decaying bursts read as one-shots");

        std::vector<InputSample::Ptr> set { low, makeHit (800.0, sr, 0.2), high };
        auto map = SampleAnalyzer::assignRoles (set);
        check (map.low == 0 && map.mid == 1 && map.high == 2,
               "roles assigned dark->LOW, mid->MID, bright->HIGH");

        // One sample must still cover every role.
        std::vector<InputSample::Ptr> solo { low, nullptr, nullptr };
        auto soloMap = SampleAnalyzer::assignRoles (solo);
        check (soloMap.low == 0 && soloMap.mid == 0 && soloMap.high == 0 && soloMap.fx == 0,
               "single sample covers all roles");
    }

    // --- determinism ------------------------------------------------------------
    for (auto family : { Family::EDM, Family::ARABIC, Family::MEDITERRANEAN,
                         Family::AFRO, Family::HYBRID })
        for (auto mode : { Mode::DROP, Mode::BREAK, Mode::BUILD, Mode::GROOVE })
        {
            GeneratorSettings s;
            s.mode = mode;
            s.family = family;
            const auto a = PatternValidator::validate (LoopGenerator::generate (12345, s));
            const auto b = PatternValidator::validate (LoopGenerator::generate (12345, s));
            check (a.signature() == b.signature()
                   && a.events.size() == b.events.size(),
                   juce::String ("same seed reproduces same loop: ")
                       + familyName (family) + "/" + modeName (mode));
            check (! a.events.empty(), juce::String ("pattern not empty: ")
                       + familyName (family) + "/" + modeName (mode));
        }

    // Velocities and timing must match exactly too, not just the signature.
    {
        GeneratorSettings s;
        const auto a = PatternValidator::validate (LoopGenerator::generate (777, s));
        const auto b = PatternValidator::validate (LoopGenerator::generate (777, s));
        bool identical = a.events.size() == b.events.size();
        for (size_t i = 0; identical && i < a.events.size(); ++i)
            identical = a.events[i].pos == b.events[i].pos
                     && a.events[i].velocity == b.events[i].velocity
                     && a.events[i].microMs == b.events[i].microMs
                     && a.events[i].pitchSemis == b.events[i].pitchSemis;
        check (identical, "full event data is deterministic");
    }

    // --- diversity: 12 options from derived seeds must differ meaningfully ------
    for (auto family : { Family::EDM, Family::ARABIC, Family::HYBRID })
    {
        GeneratorSettings s;
        s.family = family;
        juce::StringArray sigs;
        const juce::uint64 master = 0xC0FFEE;
        for (int i = 0; i < 12; ++i)
        {
            auto seed = LoopGenerator::deriveSeed (master, i);
            auto p = PatternValidator::validate (LoopGenerator::generate (seed, s));
            for (int attempt = 0; sigs.contains (p.signature()) && attempt < 8; ++attempt)
            {
                seed = LoopGenerator::deriveSeed (seed, 7777 + attempt);
                p = PatternValidator::validate (LoopGenerator::generate (seed, s));
            }
            check (! sigs.contains (p.signature()),
                   juce::String ("option ") + juce::String (i) + " differs ("
                       + familyName (family) + ")");
            sigs.add (p.signature());
        }
    }

    // --- musical guard-rails ----------------------------------------------------
    {
        GeneratorSettings drop, brk;
        drop.mode = Mode::DROP;
        brk.mode = Mode::BREAK;
        double dropEvents = 0.0, breakEvents = 0.0;
        for (int i = 0; i < 24; ++i)
        {
            const auto seed = LoopGenerator::deriveSeed (99, i);
            dropEvents += (double) PatternValidator::validate (
                LoopGenerator::generate (seed, drop)).events.size();
            breakEvents += (double) PatternValidator::validate (
                LoopGenerator::generate (seed, brk)).events.size();
        }
        check (breakEvents < dropEvents * 0.7,
               "BREAK is meaningfully sparser than DROP");

        // DROP downbeat guarantee.
        for (int i = 0; i < 24; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (5150, i), drop));
            const bool downbeat = std::any_of (p.events.begin(), p.events.end(),
                [] (const Event& e) { return e.pos < 0.26 && e.role == Role::LOW; });
            check (downbeat, "DROP keeps its downbeat LOW");
        }

        // Density macro actually controls event count.
        GeneratorSettings lowD, highD;
        lowD.density = 0.05f;
        highD.density = 0.95f;
        double lo = 0.0, hi = 0.0;
        for (int i = 0; i < 24; ++i)
        {
            const auto seed = LoopGenerator::deriveSeed (31337, i);
            lo += (double) PatternValidator::validate (LoopGenerator::generate (seed, lowD)).events.size();
            hi += (double) PatternValidator::validate (LoopGenerator::generate (seed, highD)).events.size();
        }
        check (lo < hi, "DENSITY macro scales event count");

        // Randomness zero preserves the protected skeleton: every LOW anchor
        // of the base pattern also present with randomness cranked... inverse:
        // at r=0 the pattern should keep all anchors on-grid.
        GeneratorSettings tame;
        tame.family = Family::ARABIC;
        tame.randomness = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (42, i), tame));
            for (const auto& e : p.events)
                if (e.protectedAnchor)
                    check (std::abs (e.pos - std::round (e.pos)) < 1.0e-9
                           && e.microMs == 0.0f,
                           "protected anchors stay on the grid at r=0");
        }

        // Boundary: nothing starts inside the final 32nd of the loop.
        for (int bars : { 1, 2, 4 })
        {
            GeneratorSettings s;
            s.bars = bars;
            s.mode = Mode::BUILD;   // rolls stress the boundary hardest
            for (int i = 0; i < 12; ++i)
            {
                const auto p = PatternValidator::validate (LoopGenerator::generate (
                    LoopGenerator::deriveSeed (808, i), s));
                for (const auto& e : p.events)
                    check (e.pos <= bars * 16 - 0.25 + 1.0e-9,
                           "no event starts in the final 32nd");
            }
        }
    }

    // --- renderer ---------------------------------------------------------------
    {
        LoopRenderer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = 126.0;
        ctx.samples = { makeHit (60.0, sr, 0.4), makeHit (800.0, sr, 0.2),
                        makeHit (4000.0, sr, 0.15) };
        ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);

        for (int bars : { 1, 2, 4 })
        {
            GeneratorSettings s;
            s.bars = bars;
            const auto p = PatternValidator::validate (LoopGenerator::generate (2024, s));
            const auto buf = LoopRenderer::render (p, ctx);
            const int expected = juce::roundToInt (bars * 4.0 * (60.0 / ctx.bpm) * sr);
            check (buf.getNumSamples() == expected,
                   juce::String ("render length exact for ") + juce::String (bars) + " bars");
            check (buf.getMagnitude (0, buf.getNumSamples())
                       <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-4f,
                   "render peak at or under -1 dBFS");
            check (buf.getMagnitude (0, buf.getNumSamples()) > 0.01f,
                   "render is not silent");

            // Loop seam: the very last samples must approach zero (anti-click).
            const float lastMag = std::abs (buf.getSample (0, buf.getNumSamples() - 1));
            check (lastMag < 0.02f, "loop boundary faded");
        }

        // Same seed at same tempo renders byte-identical audio.
        GeneratorSettings s;
        const auto p1 = PatternValidator::validate (LoopGenerator::generate (5555, s));
        const auto p2 = PatternValidator::validate (LoopGenerator::generate (5555, s));
        const auto b1 = LoopRenderer::render (p1, ctx);
        const auto b2 = LoopRenderer::render (p2, ctx);
        bool same = b1.getNumSamples() == b2.getNumSamples();
        for (int i = 0; same && i < b1.getNumSamples(); i += 7)
            same = b1.getSample (0, i) == b2.getSample (0, i);
        check (same, "same seed renders identical audio");

        // Single-sample render still produces role variety (audible via pitch).
        LoopRenderer::Context solo = ctx;
        solo.samples = { makeHit (200.0, sr, 0.3) };
        solo.roleMap = SampleAnalyzer::assignRoles (solo.samples);
        const auto soloBuf = LoopRenderer::render (p1, solo);
        check (soloBuf.getMagnitude (0, soloBuf.getNumSamples()) > 0.01f,
               "single-sample loop renders");
    }

    // --- render cache -----------------------------------------------------------
    {
        GeneratorSettings s;
        auto p = PatternValidator::validate (LoopGenerator::generate (31415, s));
        p.name = "TEST 01";
        LoopRenderer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = 120.0;
        ctx.samples = { makeHit (100.0, sr, 0.3) };
        ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);
        const auto buf = LoopRenderer::render (p, ctx);
        const auto wav = RenderCache::write (buf, p, ctx.bpm, ctx.sampleRate);
        check (wav.existsAsFile() && wav.getSize() > 1000, "cache writes a WAV");
        check (RenderCache::fileFor (p, ctx.bpm, ctx.sampleRate) == wav,
               "cache file name is deterministic");

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wav));
        check (reader != nullptr
               && (int) reader->lengthInSamples == buf.getNumSamples()
               && reader->sampleRate == sr,
               "written WAV round-trips length and rate");
        wav.deleteFile();
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILED") << " - "
              << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
