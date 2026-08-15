// ORCHA engine checks: determinism, diversity, musical guard-rails, render
// correctness. No UI, no plug-in wrapper - a broken editor cannot hide a
// broken generator.

#include <JuceHeader.h>
#include "../Engine/SampleAnalyzer.h"
#include "../Engine/LoopGenerator.h"
#include "../Engine/PatternValidator.h"
#include "../Engine/LoopRenderer.h"
#include "../Engine/RenderCache.h"
#include "../Engine/MidiExporter.h"
#include "../Engine/SampleTransform.h"
#include "../Engine/PhrasePlanner.h"
#include "../Engine/FeelVector.h"
#include "../Engine/Motif.h"
#include "../Engine/SilencePlanner.h"
#include "../Engine/MusicalScorer.h"
#include "../Playback/PreviewPlayer.h"
#include <map>

using namespace orcha;

// The timing budgets below are Release numbers. A Debug or sanitizer build
// runs the same work 10-30x slower - under ASan+UBSan the chain benchmark
// takes 3.6 s against a 2 s budget - so asserting there would fail honest
// builds and teach us to ignore the check. The benchmarks still RUN and
// still print, so a sanitizer pass reports real timings; only the ceiling
// is lifted.
#if defined(__has_feature)
 #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) \
     || __has_feature(memory_sanitizer) || __has_feature(undefined_behavior_sanitizer)
  #define ORCHA_SANITIZED 1
 #endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
 #undef ORCHA_SANITIZED
 #define ORCHA_SANITIZED 1
#endif
#ifndef ORCHA_SANITIZED
 #define ORCHA_SANITIZED 0
#endif
#if defined(JUCE_DEBUG) && JUCE_DEBUG
 #define ORCHA_DEBUG_BUILD 1
#else
 #define ORCHA_DEBUG_BUILD 0
#endif
static constexpr bool timingIsMeaningful = ! ORCHA_SANITIZED && ! ORCHA_DEBUG_BUILD;

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
    for (auto family : { Family::EDM, Family::MELODIC_TECHNO, Family::PSYTRANCE,
                         Family::ARABIC, Family::MEDITERRANEAN, Family::AFRO,
                         Family::CINEMATIC, Family::HYBRID })
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

    // --- BUILD rolls accelerate -------------------------------------------------
    {
        GeneratorSettings s;
        s.mode = Mode::BUILD;
        s.energy = 0.8f;
        int rolls = 0, accelerating = 0;
        for (int i = 0; i < 48; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (606, i), s));
            std::vector<double> rollPos;
            for (const auto& e : p.events)
                if (e.roll)
                    rollPos.push_back (e.pos);
            if (rollPos.size() < 4)
                continue;
            ++rolls;
            std::sort (rollPos.begin(), rollPos.end());
            const double first = rollPos[1] - rollPos[0];
            const double last = rollPos.back() - rollPos[rollPos.size() - 2];
            bool monotonic = true;
            for (size_t k = 2; k < rollPos.size(); ++k)
                monotonic = monotonic
                    && rollPos[k] - rollPos[k - 1] <= rollPos[k - 1] - rollPos[k - 2] + 1.0e-9;
            if (monotonic && last < first - 0.05)
                ++accelerating;
        }
        check (rolls >= 10, "BUILD produces rolls to measure");
        check (accelerating == rolls,
               "every BUILD roll accelerates - spacing shrinks hit by hit");
    }

    // --- hat choke ----------------------------------------------------------------
    {
        // A sustained tone that would ring for a full second, marked one-shot
        // so the loop-slice auto-gate stays out of the way: only the choke can
        // stop it.
        auto ring = std::make_shared<InputSample>();
        ring->buffer.setSize (1, (int) sr);
        for (int i = 0; i < (int) sr; ++i)
            ring->buffer.setSample (0, i,
                0.5f * std::sin (juce::MathConstants<float>::twoPi * 900.0f * (float) i / (float) sr));
        ring->sourceSampleRate = sr;
        ring->analysis.isOneShot = true;

        LoopRenderer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = 120.0;                      // 1 bar = 2 s, one step = 0.125 s
        ctx.samples = { ring };
        ctx.roleMap = { 0, 0, 0, 0 };

        Pattern p;
        p.settings.bars = 1;
        Event open;
        open.pos = 0.0;
        open.role = Role::HIGH;
        open.velocity = 1.0f;
        Event closed;
        closed.pos = 1.0;
        closed.role = Role::HIGH;
        closed.velocity = 0.05f;
        p.events = { open, closed };

        const auto buf = LoopRenderer::render (p, ctx);
        auto magAt = [&buf] (double t0, double t1, double rate)
        {
            return buf.getMagnitude ((int) (t0 * rate),
                                     juce::jmax (1, (int) ((t1 - t0) * rate)));
        };
        check (magAt (0.02, 0.10, sr) > 0.15, "open hit sounds before the choke");
        // Without the choke the first hit rings at ~0.35 here; with it, only
        // the quiet second hit remains.
        check (magAt (0.40, 0.50, sr) < 0.15, "next HIGH hit chokes the ringing one");
    }

    // --- accent = brightness ------------------------------------------------------
    {
        // Low tone plus a quiet high partial. A single sine cannot show the
        // shelf - its derivative is the same frequency - but the balance
        // between these two partials can: the snap lifts the 5 kHz part.
        auto dull = std::make_shared<InputSample>();
        dull->buffer.setSize (1, (int) (sr * 0.3));
        for (int i = 0; i < dull->buffer.getNumSamples(); ++i)
        {
            const float t = (float) i / (float) sr;
            dull->buffer.setSample (0, i,
                0.45f * std::sin (juce::MathConstants<float>::twoPi * 200.0f * t)
                + 0.1f * std::sin (juce::MathConstants<float>::twoPi * 5000.0f * t));
        }
        dull->sourceSampleRate = sr;
        dull->analysis.isOneShot = true;

        LoopRenderer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = 120.0;
        ctx.samples = { dull };
        ctx.roleMap = { 0, 0, 0, 0 };

        auto hfRatio = [&] (float velocity)
        {
            Pattern p;
            p.settings.bars = 1;
            Event e;
            e.pos = 0.0;
            e.role = Role::LOW;
            e.velocity = velocity;
            p.events = { e };
            const auto buf = LoopRenderer::render (p, ctx);
            double vv = 0.0, dd = 0.0;
            const float* d = buf.getReadPointer (0);
            const int n = (int) (sr * 0.02);
            for (int i = 1; i < n; ++i)
            {
                vv += (double) d[i] * d[i];
                const double df = d[i] - d[i - 1];
                dd += df * df;
            }
            return vv > 0.0 ? dd / vv : 0.0;
        };
        check (hfRatio (1.0f) > hfRatio (0.5f) * 1.1,
               "accented hit is brighter, not just louder");
    }

    // --- preview waits for the bar line -------------------------------------------
    {
        auto makeLoop = [&] (int optionIndex)
        {
            PreviewPlayer::Loop::Ptr loop (new PreviewPlayer::Loop());
            loop->buffer.setSize (2, (int) sr);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < (int) sr; ++i)
                    loop->buffer.setSample (ch, i, 0.5f);
            loop->bpm = 120.0;
            loop->bars = 1;
            loop->optionIndex = optionIndex;
            return loop;
        };
        juce::AudioBuffer<float> out (2, 512);
        auto mag = [&out] { return out.getMagnitude (0, out.getNumSamples()); };

        PreviewPlayer pp;
        pp.play (makeLoop (0));
        out.clear();
        pp.process (out, 1.3, true, sr);
        check (mag() == 0.0f, "mid-bar start holds for the bar line");
        out.clear();
        pp.process (out, 3.9, true, sr);
        check (mag() == 0.0f, "still holding just before the bar");
        out.clear();
        pp.process (out, 4.01, true, sr);
        check (mag() > 0.1f, "enters on the bar line");

        // A re-render of the SAME option must not drop out.
        pp.play (makeLoop (0));
        pp.releaseRetired();
        out.clear();
        pp.process (out, 5.3, true, sr);
        check (mag() > 0.1f, "re-render of the playing option continues seamlessly");

        // Stopped host: immediate.
        PreviewPlayer stopped;
        stopped.play (makeLoop (3));
        out.clear();
        stopped.process (out, -1.0, false, sr);
        check (mag() > 0.1f, "stopped host previews immediately");
    }

    // --- the groove layer ----------------------------------------------------------
    {
        // Accent map: "e" positions (pos%4==1) must sit under the beats.
        GeneratorSettings s;
        s.mode = Mode::GROOVE;
        s.family = Family::EDM;
        s.randomness = 0.0f;
        double beatVel = 0.0, eVel = 0.0;
        int beatN = 0, eN = 0, ghosts = 0, laidBackOk = 0, laidBackTotal = 0;
        for (int i = 0; i < 24; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (2468, i), s));
            for (const auto& e : p.events)
            {
                if (e.roll)
                    continue;
                const int step = juce::roundToInt (std::floor (e.pos)) % 4;
                if (std::abs (e.pos - std::round (e.pos)) > 0.01)
                    continue;
                if (step == 0) { beatVel += e.velocity; ++beatN; }
                if (step == 1) { eVel += e.velocity; ++eN; }
                if (e.velocity <= 0.28f)
                    ++ghosts;
                // Family feel: at r=0 every non-anchor HIGH sits exactly
                // highFeelMs behind the grid.
                if (! e.protectedAnchor && e.role == Role::HIGH)
                {
                    ++laidBackTotal;
                    if (std::abs (e.microMs - 3.0f) < 0.01f)
                        ++laidBackOk;
                }
            }
        }
        check (beatN > 0 && eN > 0, "groove test has data on beats and e's");
        check (eVel / juce::jmax (1, eN) < beatVel / juce::jmax (1, beatN) * 0.85,
               "accent map ducks the e's under the beats");
        check (ghosts >= 24, "designed ghost layer puts quiet ticks in the pocket");
        check (laidBackTotal > 0 && laidBackOk == laidBackTotal,
               "EDM hats sit laid back by the family feel");

        // Melodic-techno rolling cell is reachable: choked low ghosts offbeat.
        GeneratorSettings mel = s;
        mel.family = Family::MELODIC_TECHNO;
        int rollingSeeds = 0;
        for (int i = 0; i < 60; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (13579, i), mel));
            for (const auto& e : p.events)
                if (e.role == Role::LOW && e.gateSteps >= 1.0 && e.velocity < 0.6f)
                {
                    ++rollingSeeds;
                    break;
                }
        }
        check (rollingSeeds >= 12, "melodic rolling cell appears often in its family");

        // PSYTRANCE: the bass fills the space between kicks - plenty of
        // off-beat LOW events, and every one of them gated tight.
        GeneratorSettings psy = s;
        psy.family = Family::PSYTRANCE;
        int offbeatBass = 0, ungated = 0, psyPatterns = 0;
        for (int i = 0; i < 24; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (30303, i), psy));
            ++psyPatterns;
            for (const auto& e : p.events)
                if (e.role == Role::LOW && ! e.roll
                    && juce::roundToInt (std::floor (e.pos)) % 4 != 0
                    && std::abs (e.pos - std::round (e.pos)) < 0.01)
                {
                    ++offbeatBass;
                    if (e.gateSteps <= 0.0)
                        ++ungated;
                }
        }
        check (offbeatBass >= psyPatterns * 4, "psytrance rolls its off-beat bass");
        check (ungated == 0, "psytrance bass notes are all gated");

        // CINEMATIC: meaningfully sparser than EDM at the same settings.
        GeneratorSettings cin = s;
        cin.family = Family::CINEMATIC;
        double cinEvents = 0.0, edmEvents = 0.0;
        for (int i = 0; i < 24; ++i)
        {
            const auto seed = LoopGenerator::deriveSeed (40404, i);
            cinEvents += (double) PatternValidator::validate (
                LoopGenerator::generate (seed, cin)).events.size();
            edmEvents += (double) PatternValidator::validate (
                LoopGenerator::generate (seed, s)).events.size();
        }
        check (cinEvents < edmEvents * 0.75, "CINEMATIC keeps its air");
    }

    // --- sample transforms ----------------------------------------------------------
    {
        // Silence padding around a burst: trim must remove it; reverse must
        // mirror the audio exactly. Both non-destructive by construction.
        auto padded = std::make_shared<InputSample>();
        const int pad = (int) (sr * 0.2);
        const int body = (int) (sr * 0.1);
        padded->buffer.setSize (1, pad + body + pad);
        padded->buffer.clear();
        for (int i = 0; i < body; ++i)
            padded->buffer.setSample (0, pad + i,
                0.6f * std::sin (juce::MathConstants<float>::twoPi * 500.0f * (float) i / (float) sr));
        padded->sourceSampleRate = sr;

        const auto trimmed = orcha::SampleTransform::apply (*padded, { false, true });
        check (trimmed->buffer.getNumSamples() < padded->buffer.getNumSamples() / 2,
               "trim removes the silence padding");
        check (trimmed->buffer.getMagnitude (0, trimmed->buffer.getNumSamples()) > 0.4f,
               "trim keeps the audio itself");

        // Cut from BOTH sides: keep the middle half.
        const auto half = orcha::SampleTransform::apply (*padded,
            { false, false, 0.25f, 0.75f });
        check (std::abs (half->buffer.getNumSamples()
                         - padded->buffer.getNumSamples() / 2) <= 1,
               "two-sided cut keeps exactly the chosen region");

        // A light fade-in tames the head without touching the length.
        orcha::SampleTransform::Settings fadeSet;
        fadeSet.fadeIn = 0.3f;
        const auto faded = orcha::SampleTransform::apply (*padded, fadeSet);
        check (faded->buffer.getNumSamples() == padded->buffer.getNumSamples(),
               "fade does not change the length");
        const int probe = (int) ((float) padded->buffer.getNumSamples() * 0.05f);
        check (std::abs (faded->buffer.getSample (0, probe))
                   <= std::abs (padded->buffer.getSample (0, probe)) + 1.0e-6f,
               "fade-in reduces the head");

        const auto reversed = orcha::SampleTransform::apply (*padded, { true, false });
        bool mirrored = reversed->buffer.getNumSamples() == padded->buffer.getNumSamples();
        const int n = padded->buffer.getNumSamples();
        for (int i = 0; mirrored && i < n; i += 997)
            mirrored = reversed->buffer.getSample (0, i)
                     == padded->buffer.getSample (0, n - 1 - i);
        check (mirrored, "reverse mirrors the buffer exactly");
    }

    // --- per-card FX ----------------------------------------------------------------
    {
        LoopRenderer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = 124.0;
        ctx.samples = { makeHit (100.0, sr, 0.3) };
        ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);

        GeneratorSettings s;
        auto dry = PatternValidator::validate (LoopGenerator::generate (515, s));
        auto wet = dry;
        wet.fxReverb = true;
        wet.fxDelay = true;

        const auto dryBuf = LoopRenderer::render (dry, ctx);
        const auto wetBuf = LoopRenderer::render (wet, ctx);
        const auto wetBuf2 = LoopRenderer::render (wet, ctx);
        check (wetBuf.getNumSamples() == dryBuf.getNumSamples(),
               "FX does not change the loop length");
        bool differs = false;
        for (int i = 0; i < dryBuf.getNumSamples() && ! differs; i += 31)
            differs = dryBuf.getSample (0, i) != wetBuf.getSample (0, i);
        check (differs, "FX audibly changes the render");
        bool sameTwice = true;
        for (int i = 0; i < wetBuf.getNumSamples() && sameTwice; i += 31)
            sameTwice = wetBuf.getSample (0, i) == wetBuf2.getSample (0, i);
        check (sameTwice, "FX render is deterministic");
        check (wetBuf.getMagnitude (0, wetBuf.getNumSamples())
                   <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-4f,
               "FX render still honors the -1 dBFS ceiling");
    }

    // --- URBAN / BREAKS carry their documented signatures ---------------------------
    {
        // Dembow: the tresillo snare on the "a" of 1 (step 3) shows up often.
        GeneratorSettings urb;
        urb.family = Family::URBAN;
        urb.mode = Mode::GROOVE;
        int tresillo = 0;
        for (int i = 0; i < 24; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (50505, i), urb));
            for (const auto& e : p.events)
                if (e.role == Role::MID && std::abs (e.pos - 3.0) < 0.26)
                {
                    ++tresillo;
                    break;
                }
        }
        check (tresillo >= 6, "URBAN speaks the tresillo snare");

        // BREAKS: ghost snares (quiet MID hits) are the aesthetic.
        GeneratorSettings brk;
        brk.family = Family::BREAKS;
        brk.mode = Mode::GROOVE;
        int ghostSnares = 0;
        for (int i = 0; i < 24; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (60606, i), brk));
            for (const auto& e : p.events)
                if (e.role == Role::MID && e.velocity <= 0.4f)
                    ++ghostSnares;
        }
        check (ghostSnares >= 24, "BREAKS keeps its ghost snares");
    }

    // --- ENGINE 1 CHARACTERIZATION (Phase 0 of the Engine 2.0 upgrade) -------------
    // Freezes the v1 generator bit-for-bit: a fixed seed grid across every
    // family, mode and length, hashed. If this hash moves, v1 changed and old
    // projects would silently regenerate differently. v2 lives beside v1 and
    // is NOT covered by this hash.
    {
        juce::String all;
        // The same content with each pattern's events sorted as text, so it
        // cannot see the ORDER events are stored in - only which events exist.
        // The two hashes answer different questions, and when a change moves
        // one but not the other, that difference is the whole answer.
        juce::String contentOnly;
        std::map<juce::String, juce::String> perFamily;
        for (auto family : { Family::EDM, Family::MELODIC_TECHNO, Family::PSYTRANCE,
                             Family::URBAN, Family::BREAKS, Family::ARABIC,
                             Family::MEDITERRANEAN, Family::AFRO, Family::CINEMATIC,
                             Family::HYBRID })
            for (auto mode : { Mode::DROP, Mode::BREAK, Mode::BUILD, Mode::GROOVE })
                for (int bars : { 1, 2, 4 })
                    for (int i = 0; i < 3; ++i)
                    {
                        GeneratorSettings s;
                        s.family = family;
                        s.mode = mode;
                        s.bars = bars;
                        const auto p = PatternValidator::validate (
                            LoopGenerator::generate (
                                LoopGenerator::deriveSeed (0xF0F0, i),
                                LoopGenerator::deriveSeed (0x0E0E, i), s));
                        all << p.signature() << '|';
                        juce::StringArray perEvent;
                        for (const auto& e : p.events)
                        {
                            const auto desc = juce::String (e.pos, 4) + ","
                                + juce::String ((int) e.role) + ","
                                + juce::String (e.velocity, 4) + ","
                                + juce::String (e.microMs, 3) + ","
                                + juce::String (e.pitchSemis) + ","
                                + juce::String ((int) e.gateSteps);
                            all << juce::String (e.velocity, 4) << ','
                                << juce::String (e.microMs, 3) << ','
                                << e.pitchSemis << ',' << (int) e.gateSteps << ';';
                            perEvent.add (desc);
                        }
                        // No signature() here: it walks the events in stored
                        // order, which is exactly what this hash must not see.
                        perEvent.sort (true);
                        const auto line = perEvent.joinIntoString (";") + "\n";
                        contentOnly << line;
                        perFamily[familyName (family)] << line;
                    }
        const juce::int64 hash = all.hashCode64();
        const juce::int64 contentHash = contentOnly.hashCode64();
        // Both values re-recorded 2026-08-15. They are now the SAME on macOS
        // and on Windows, which is the point - the first Windows CI run this
        // code ever had proved they were not. Two separate causes, both fixed
        // in the same session:
        //
        //  1. Sorting events on position alone left simultaneous hits - a kick
        //     and a hat on one step - comparing equal, and std::sort is not
        //     stable. libc++ and the MSVC STL ordered them differently, and the
        //     validator's de-duplication and event-count trim each keep
        //     whichever event they meet first. Fixed by eventBefore(), a total
        //     order. Measured before and after: this changed storage order ONLY,
        //     the content hash held at 2010191849257386728.
        //
        //  2. Floating-point contraction. clang fused a*b+c into a single FMA
        //     where MSVC did not, so a threshold comparison in the generator
        //     could land on the other side. Fixed by -ffp-contract=off and
        //     /fp:strict. This one DID change the music slightly on macOS
        //     (content 2010191849257386728 -> 6994967397226463169): a handful
        //     of last-bit decisions now resolve the same way everywhere.
        //     Deliberate, and done before a single Windows user exists - the
        //     alternative is one project sounding like two.
        const juce::int64 golden = -7885527166888537179LL;
        const juce::int64 goldenContent = 6994967397226463169LL;
        if (hash != golden)
            std::cout << "  v1 characterization hash: " << hash << "\n";
        if (contentHash != goldenContent)
            std::cout << "  v1 CONTENT hash: " << contentHash << "\n";
        // When the hash moves, say WHERE. A per-family breakdown turns "the
        // engine differs on Windows" into "these two families differ", which
        // is the difference between a guess and a place to look.
        if (hash != golden || contentHash != goldenContent)
            for (const auto& [fam, text] : perFamily)
                std::cout << "    family " << fam << " content "
                          << text.hashCode64() << "\n";
        check (hash == golden, "ENGINE 1 IS FROZEN - characterization hash unchanged");
        check (contentHash == goldenContent,
               "ENGINE 1 IS FROZEN - pattern content unchanged");
    }

    // --- transition drama: BUILD countdown + gap, BREAK reverse swell ---------------
    {
        GeneratorSettings bld;
        bld.mode = Mode::BUILD;
        bld.energy = 0.8f;
        int walks = 0, gaps = 0;
        for (int i = 0; i < 48; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (70707, i), bld));
            // Countdown: >= 3 LOW anchors whose pitch strictly walks one way.
            std::vector<int> pitches;
            for (const auto& e : p.events)
                if (e.role == Role::LOW && e.protectedAnchor
                    && std::fmod (e.pos, 4.0) < 0.01)
                    pitches.push_back (e.pitchSemis);
            int down = 0, up = 0;
            for (size_t k = 1; k < pitches.size(); ++k)
            {
                if (pitches[k] < pitches[k - 1]) ++down;
                if (pitches[k] > pitches[k - 1]) ++up;
            }
            if (down >= 2 || up >= 2)
                ++walks;
            // Breath ending: nothing in the final 2 steps except one pickup.
            int tailEvents = 0;
            bool pickup = false;
            for (const auto& e : p.events)
                if (e.pos >= p.stepCount() - 2.0)
                {
                    ++tailEvents;
                    pickup = pickup || (e.role == Role::LOW && e.velocity >= 0.99f);
                }
            if (tailEvents <= 1 && (tailEvents == 0 || pickup))
                ++gaps;
        }
        check (walks >= 20, "BUILD pitch-walk countdown appears often");
        check (gaps >= 8, "BUILD breath ending (1, 2... boom) appears");

        GeneratorSettings brk;
        brk.mode = Mode::BREAK;
        int swells = 0;
        for (int i = 0; i < 48; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (80808, i), brk));
            for (const auto& e : p.events)
                if (e.reverse && e.role == Role::HIGH
                    && e.pos > p.stepCount() - 4.5)
                {
                    ++swells;
                    break;
                }
        }
        check (swells >= 12, "BREAK reverse swell pulls into the loop point");

        // The swell must survive rendering unchoked: audio present right
        // before the loop end even when hats follow it.
        LoopRenderer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = 120.0;
        ctx.samples = { makeHit (60.0, sr, 0.4), makeHit (800.0, sr, 0.2),
                        makeHit (4000.0, sr, 1.0) };
        ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);
        Pattern sp;
        sp.settings.bars = 1;
        Event hat;
        hat.pos = 14.0;
        hat.role = Role::HIGH;
        hat.velocity = 0.5f;
        Event swell;
        swell.pos = 12.25;
        swell.role = Role::HIGH;
        swell.reverse = true;
        swell.gateSteps = 3.45;
        swell.velocity = 0.7f;
        sp.events = { swell, hat };
        const auto sbuf = LoopRenderer::render (sp, ctx);
        const int probeAt = (int) (sr * 1.9);   // step ~15.2 of a 2 s bar
        check (sbuf.getMagnitude (probeAt, (int) (sr * 0.05)) > 0.02f,
               "reverse swell rides past later hats unchoked");
    }

    // --- variation preserves the motif ---------------------------------------------
    {
        // Same motif seed + different ornament seeds = same groove, another
        // take: every protected anchor stays put, but the takes still differ.
        GeneratorSettings s;
        s.mode = Mode::GROOVE;   // no DROP hole, so anchors are orn-independent
        s.family = Family::ARABIC;
        auto anchorsOf = [] (const Pattern& p)
        {
            juce::String a;
            for (const auto& e : p.events)
                if (e.protectedAnchor)
                    a << juce::roundToInt (e.pos * 4.0) << ':' << (int) e.role << ';';
            return a;
        };
        int differing = 0;
        for (int i = 0; i < 24; ++i)
        {
            const auto motif = LoopGenerator::deriveSeed (777, i);
            const auto a = PatternValidator::validate (LoopGenerator::generate (
                motif, LoopGenerator::deriveSeed (111, i), s));
            const auto b = PatternValidator::validate (LoopGenerator::generate (
                motif, LoopGenerator::deriveSeed (222, i), s));
            check (anchorsOf (a) == anchorsOf (b),
                   "variation keeps every protected anchor");
            check (a.swing == b.swing, "variation keeps the swing feel");
            if (a.signature() != b.signature())
                ++differing;
        }
        check (differing >= 12, "variations still differ in their ornaments");

        // And full two-seed determinism.
        const auto x = PatternValidator::validate (LoopGenerator::generate (9, 8, s));
        const auto y = PatternValidator::validate (LoopGenerator::generate (9, 8, s));
        check (x.signature() == y.signature() && x.events.size() == y.events.size(),
               "two-seed generate is deterministic");
    }

    // --- MIDI export ---------------------------------------------------------------
    {
        GeneratorSettings s;
        s.bars = 2;
        auto p = PatternValidator::validate (LoopGenerator::generate (271828, s));
        p.name = "MIDI 01";
        const double bpm = 126.0;
        const auto mid = MidiExporter::write (p, bpm);
        check (mid.existsAsFile() && mid.getSize() > 50, "MIDI file written");

        juce::FileInputStream in (mid);
        juce::MidiFile midi;
        check (in.openedOk() && midi.readFrom (in), "MIDI file parses back");
        check (midi.getNumTracks() == 1, "one track");

        int noteOns = 0;
        double tempoUs = 0.0;
        double maxTick = 0.0;
        if (midi.getNumTracks() == 1)
            for (const auto* ev : *midi.getTrack (0))
            {
                if (ev->message.isNoteOn())
                {
                    ++noteOns;
                    maxTick = juce::jmax (maxTick, ev->message.getTimeStamp());
                }
                if (ev->message.isTempoMetaEvent())
                    tempoUs = ev->message.getTempoSecondsPerQuarterNote() * 1.0e6;
            }
        check (noteOns == (int) p.events.size(), "one note per event");
        check (std::abs (tempoUs - 60'000'000.0 / bpm) < 1.0, "tempo written");
        // 2 bars at 960 ppq = 7680 ticks; nothing may start beyond the loop.
        check (maxTick < 2 * 4 * 960, "no note starts past the loop end");
        mid.deleteFile();
    }

    // === ENGINE 2 / PHASE 1: PhrasePlanner + FeelVector + generateV2 ===============
    {
        // --- plan legality + determinism for every mode x length ---------------
        for (auto mode : { Mode::DROP, Mode::BREAK, Mode::BUILD, Mode::GROOVE })
            for (int bars : { 1, 2, 4 })
                for (int i = 0; i < 8; ++i)
                {
                    const auto seed = LoopGenerator::deriveSeed (0x9101, i);
                    const auto a = PhrasePlanner::plan (mode, bars, seed);
                    const auto b = PhrasePlanner::plan (mode, bars, seed);
                    check (a.roles == b.roles, "phrase plan is deterministic");
                    check (a.segments() == (bars > 1 ? bars : 4),
                           "plan covers every segment");
                    check (a.beatLevel == (bars == 1), "1-bar plans are beat-level");
                }
        // Grammar spot checks.
        check (PhrasePlanner::plan (Mode::BREAK, 4, 7).roles.back() == PhraseRole::Breath,
               "4-bar BREAK ends in Breath");
        check (PhrasePlanner::plan (Mode::DROP, 4, 7).roles.front() == PhraseRole::Impact,
               "4-bar DROP opens with Impact");
        check (PhrasePlanner::plan (Mode::GROOVE, 4, 7).roles[2] == PhraseRole::Contrast,
               "4-bar GROOVE has its B section");

        // --- FeelVector: section-aware macro mapping ---------------------------
        {
            GeneratorSettings hiE;
            hiE.energy = 1.0f;
            hiE.mode = Mode::BREAK;
            const auto fBreak = FeelVector::derive (hiE, 42);
            hiE.mode = Mode::DROP;
            const auto fDrop = FeelVector::derive (hiE, 42);
            check (fBreak.space > fDrop.space,
                   "high energy keeps space in BREAK but spends it in DROP");
            check (fDrop.aggression > fBreak.aggression,
                   "DROP energy is aggression, BREAK energy is tension");
            GeneratorSettings psy;
            psy.family = Family::PSYTRANCE;
            psy.randomness = 1.0f;
            check (FeelVector::derive (psy, 1).looseness < 0.1f,
                   "PSYTRANCE randomness never buys looseness");
        }

        // --- v2 determinism + core musical guarantees --------------------------
        for (auto family : { Family::EDM, Family::ARABIC, Family::PSYTRANCE })
            for (auto mode : { Mode::DROP, Mode::BREAK, Mode::BUILD, Mode::GROOVE })
            {
                GeneratorSettings s;
                s.family = family;
                s.mode = mode;
                s.bars = 4;
                const auto a = PatternValidator::validate (
                    LoopGenerator::generateV2 (999, 111, s));
                const auto b = PatternValidator::validate (
                    LoopGenerator::generateV2 (999, 111, s));
                check (a.signature() == b.signature() && a.algo == 2,
                       "v2 is deterministic and tagged");
                check (! a.events.empty(), "v2 never returns silence");
                for (const auto& e : a.events)
                    check (e.pos <= 64 - 0.25 + 1.0e-9, "v2 respects the boundary");
            }
        {
            GeneratorSettings s;
            double dropN = 0, breakN = 0;
            for (int i = 0; i < 24; ++i)
            {
                s.mode = Mode::DROP;
                dropN += (double) PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (11, i), LoopGenerator::deriveSeed (22, i), s))
                    .events.size();
                s.mode = Mode::BREAK;
                breakN += (double) PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (11, i), LoopGenerator::deriveSeed (22, i), s))
                    .events.size();
            }
            check (breakN < dropN * 0.7, "v2 BREAK stays meaningfully sparser than DROP");
        }

        // --- the phrase actually has shape -------------------------------------
        auto eventsInBar = [] (const Pattern& p, int bar)
        {
            int n = 0;
            for (const auto& e : p.events)
                if (e.pos >= bar * 16 && e.pos < (bar + 1) * 16)
                    ++n;
            return n;
        };
        {
            // BREAK: the Breath bar (4th) is emptier than the Call bar (2nd).
            GeneratorSettings s;
            s.mode = Mode::BREAK;
            s.bars = 4;
            int breathThinner = 0;
            for (int i = 0; i < 24; ++i)
            {
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (333, i), LoopGenerator::deriveSeed (444, i), s));
                if (eventsInBar (p, 3) < eventsInBar (p, 1))
                    ++breathThinner;
            }
            check (breathThinner >= 16, "v2 BREAK: the Breath bar is the emptiest");

            // BUILD: density rises across the phrase (bar 3 vs bar 1),
            // measured before the final bar where vacuum may empty it.
            s.mode = Mode::BUILD;
            double early = 0, late = 0;
            for (int i = 0; i < 24; ++i)
            {
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (555, i), LoopGenerator::deriveSeed (666, i), s));
                early += eventsInBar (p, 0);
                late += eventsInBar (p, 2);
            }
            check (late > early * 1.1, "v2 BUILD: the Accelerate bar outweighs Establish");

            // GROOVE: the Contrast bar differs from Establish for most seeds.
            s.mode = Mode::GROOVE;
            int contrasts = 0;
            for (int i = 0; i < 24; ++i)
            {
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (777, i), LoopGenerator::deriveSeed (888, i), s));
                juce::String barSig[2];
                for (const auto& e : p.events)
                {
                    const int bar = (int) (e.pos / 16.0);
                    if (bar == 0 || bar == 2)
                        barSig[bar == 0 ? 0 : 1]
                            << juce::roundToInt (std::fmod (e.pos, 16.0) * 4.0)
                            << ':' << (int) e.role << ';';
                }
                if (barSig[0] != barSig[1])
                    ++contrasts;
            }
            check (contrasts >= 18, "v2 GROOVE: the B section is a real contrast");
        }

        // --- "same groove, another take" holds in v2 ---------------------------
        {
            GeneratorSettings s;
            s.mode = Mode::GROOVE;
            s.family = Family::ARABIC;
            auto anchorsOf = [] (const Pattern& p)
            {
                juce::String a;
                for (const auto& e : p.events)
                    if (e.protectedAnchor)
                        a << juce::roundToInt (e.pos * 4.0) << ':' << (int) e.role << ';';
                return a;
            };
            for (int i = 0; i < 12; ++i)
            {
                const auto motif = LoopGenerator::deriveSeed (1212, i);
                const auto a = PatternValidator::validate (
                    LoopGenerator::generateV2 (motif, LoopGenerator::deriveSeed (1, i), s));
                const auto b = PatternValidator::validate (
                    LoopGenerator::generateV2 (motif, LoopGenerator::deriveSeed (2, i), s));
                check (anchorsOf (a) == anchorsOf (b),
                       "v2 variation keeps every protected anchor");
                check (a.swing == b.swing, "v2 variation keeps the swing");
            }
        }

        // --- performance budget: symbolic generation is effectively free -------
        {
            GeneratorSettings s;
            s.bars = 4;
            const auto t0 = juce::Time::getHighResolutionTicks();
            int total = 0;
            for (int i = 0; i < 96; ++i)
            {
                s.family = (Family) (i % 10);
                s.mode = (Mode) (i % 4);
                total += (int) PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (4242, i),
                    LoopGenerator::deriveSeed (2424, i), s)).events.size();
            }
            const double ms = juce::Time::highResolutionTicksToSeconds (
                juce::Time::getHighResolutionTicks() - t0) * 1000.0;
            check (total > 0, "benchmark generated real patterns");
            check (! timingIsMeaningful || ms < 250.0, "96 symbolic v2 candidates well inside budget");
            std::cout << "  v2 benchmark: 96 candidates in "
                      << juce::String (ms, 2) << " ms\n";
        }
    }

    // === ENGINE 2 / PHASE 2: MotifGrammar + CallResponsePlan =======================
    {
        // A known cell to transform: four hits across a bar.
        Motif::Cell base;
        base.segLen = 16.0;
        for (double pos : { 2.0, 5.0, 10.0, 14.0 })
        {
            Event e;
            e.pos = pos;
            e.role = pos < 8.0 ? Role::HIGH : Role::MID;
            e.velocity = 0.6f;
            e.gateSteps = 0.75;
            base.events.push_back (e);
        }

        using T = Motif::Transform;
        juce::Random tr (42);
        // --- every transformation behaves as documented ------------------------
        check (Motif::apply (base, T::ExactRepeat, tr).events.size() == 4,
               "ExactRepeat keeps everything");
        check (Motif::apply (base, T::Truncate, tr).events.size() == 2,
               "Truncate keeps only the first half");
        check (Motif::apply (base, T::OmitLast, tr).events.size() == 3,
               "OmitLast drops the final event");
        {
            const auto d = Motif::apply (base, T::DelayedRepeat, tr);
            check (! d.events.empty() && d.events.front().pos == 2.5,
                   "DelayedRepeat lands half a step later");
        }
        {
            const auto a = Motif::apply (base, T::Anticipation, tr);
            check (! a.events.empty() && a.events.front().pos == 1.5,
                   "Anticipation pulls the cell ahead");
        }
        {
            const auto e2 = Motif::apply (base, T::EchoSofter, tr);
            bool softer = true;
            for (size_t i = 0; i < e2.events.size(); ++i)
                softer = softer && e2.events[i].velocity < base.events[i].velocity;
            check (softer, "EchoSofter softens every event");
        }
        {
            const auto d = Motif::apply (base, T::DensifyEnd, tr);
            check (d.events.size() > base.events.size(),
                   "DensifyEnd doubles the ending into itself");
        }
        {
            const auto q = Motif::apply (base, T::QuestionSilence, tr);
            bool tailSilent = true;
            for (const auto& e : q.events)
                tailSilent = tailSilent && e.pos < 12.0;
            check (tailSilent && ! q.events.empty(),
                   "QuestionSilence withholds the ending");
        }
        {
            const auto ans = Motif::apply (base, T::AnswerLowResolve, tr);
            const bool hasLowResolve = std::any_of (ans.events.begin(), ans.events.end(),
                [] (const Event& e) { return e.role == Role::LOW && e.pos == 12.0; });
            check (hasLowResolve,
                   "AnswerLowResolve lands a LOW on the last strong beat");
        }
        {
            const auto disp = Motif::apply (base, T::Displace, tr);
            check (! disp.events.empty() && disp.events.front().pos == 3.0,
                   "Displace shifts the whole cell one step");
        }

        // --- forbidden family/style combinations -------------------------------
        auto allows = [] (Family f, T t)
        {
            const auto& a = Motif::allowedFor (f);
            return std::find (a.begin(), a.end(), t) != a.end();
        };
        check (! allows (Family::PSYTRANCE, T::Displace)
               && ! allows (Family::PSYTRANCE, T::Anticipation),
               "psytrance precision forbids displacement");
        check (! allows (Family::ARABIC, T::Displace),
               "arabic iqa' grammar forbids displacement");
        check (allows (Family::AFRO, T::Displace),
               "afro interlock welcomes displacement");
        check (! allows (Family::CINEMATIC, T::DensifyEnd),
               "cinematic space forbids densified endings");
        // And choose() can never escape the family's allowed set.
        for (int i = 0; i < 200; ++i)
        {
            juce::Random cr (i);
            const auto t = Motif::choose (PhraseRole::Develop, Family::PSYTRANCE,
                                          1.0f, cr);
            check (allows (Family::PSYTRANCE, t),
                   "choose() stays inside the family grammar");
        }

        // --- meter-aware similarity --------------------------------------------
        check (Motif::similarity (base, base) > 0.99f, "a cell equals itself");
        {
            juce::Random sr (7);
            const auto echo = Motif::apply (base, T::EchoSofter, sr);
            const auto trunc = Motif::apply (base, T::Truncate, sr);
            check (Motif::similarity (base, echo)
                       > Motif::similarity (base, trunc),
                   "an echo is closer than a truncation");
        }

        // --- call/response in real v2 BREAK phrases ----------------------------
        {
            GeneratorSettings s;
            s.mode = Mode::BREAK;
            s.bars = 4;
            s.randomness = 0.3f;
            int related = 0, notIdentical = 0, plans = 0;
            for (int i = 0; i < 24; ++i)
            {
                const auto motif = LoopGenerator::deriveSeed (2468, i);
                const auto cr = Motif::callResponseOf (
                    PhrasePlanner::plan (Mode::BREAK, 4, motif),
                    s.family, s.randomness, motif);
                if (! cr.valid())
                    continue;
                ++plans;
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    motif, LoopGenerator::deriveSeed (1357, i), s));
                const auto callCell = Motif::extract (p, cr.callSegment, 16.0);
                const auto respCell = Motif::extract (p, cr.responseSegment, 16.0);
                if (callCell.events.empty())
                    continue;
                const float sim = Motif::similarity (callCell, respCell);
                if (sim > 0.2f)
                    ++related;
                if (sim < 0.995f)
                    ++notIdentical;
            }
            check (plans >= 20, "BREAK plans carry call/response");
            check (related >= plans / 2,
                   "the response reuses recognizable call material");
            check (notIdentical >= plans / 2,
                   "the response is a development, not a copy");
        }

        // --- ornament-only regeneration preserves the motif identity -----------
        {
            GeneratorSettings s;
            s.mode = Mode::GROOVE;
            s.randomness = 0.0f;
            s.bars = 2;
            for (int i = 0; i < 12; ++i)
            {
                const auto motif = LoopGenerator::deriveSeed (9876, i);
                const auto a = PatternValidator::validate (LoopGenerator::generateV2 (
                    motif, LoopGenerator::deriveSeed (10, i), s));
                const auto b = PatternValidator::validate (LoopGenerator::generateV2 (
                    motif, LoopGenerator::deriveSeed (20, i), s));
                // The decorated cell (away from the roll-prone tail) must be
                // identical at r=0: the motif now belongs to the motif seed.
                juce::String sigA, sigB;
                for (const auto& e : a.events)
                    if (! e.protectedAnchor && ! e.roll && e.gateSteps == 0.75
                        && e.pos < a.stepCount() - 4)
                        sigA << juce::roundToInt (e.pos * 4.0) << ':'
                             << (int) e.role << ';';
                for (const auto& e : b.events)
                    if (! e.protectedAnchor && ! e.roll && e.gateSteps == 0.75
                        && e.pos < b.stepCount() - 4)
                        sigB << juce::roundToInt (e.pos * 4.0) << ':'
                             << (int) e.role << ';';
                check (sigA == sigB,
                       "another take keeps the motif, not only the anchors");
            }
        }

        // --- low randomness = tighter identity across the phrase ---------------
        {
            GeneratorSettings tame, wild;
            tame.mode = wild.mode = Mode::GROOVE;
            tame.bars = wild.bars = 4;
            tame.randomness = 0.0f;
            wild.randomness = 1.0f;
            double tameSim = 0.0, wildSim = 0.0;
            int n = 0;
            for (int i = 0; i < 24; ++i)
            {
                const auto motif = LoopGenerator::deriveSeed (555, i);
                const auto orn = LoopGenerator::deriveSeed (666, i);
                const auto pt = PatternValidator::validate (
                    LoopGenerator::generateV2 (motif, orn, tame));
                const auto pw = PatternValidator::validate (
                    LoopGenerator::generateV2 (motif, orn, wild));
                const auto t0 = Motif::extract (pt, 0, 16.0);
                const auto t1 = Motif::extract (pt, 1, 16.0);
                const auto w0 = Motif::extract (pw, 0, 16.0);
                const auto w1 = Motif::extract (pw, 1, 16.0);
                if (t0.events.empty() || w0.events.empty())
                    continue;
                tameSim += Motif::similarity (t0, t1);
                wildSim += Motif::similarity (w0, w1);
                ++n;
            }
            check (n >= 12 && tameSim > wildSim,
                   "low randomness preserves stronger motif identity");
        }
    }

    // === ENGINE 2 / PHASE 3: StyleDNA + RoleInteraction + sample awareness =========
    {
        // --- role interaction: no off-beat pile-ups ----------------------------
        for (auto family : { Family::EDM, Family::AFRO, Family::URBAN })
            for (int i = 0; i < 16; ++i)
            {
                GeneratorSettings s;
                s.family = family;
                s.density = 0.9f;
                s.bars = 2;
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (31001, i),
                    LoopGenerator::deriveSeed (31002, i), s));
                for (const auto& e : p.events)
                {
                    if (std::fmod (e.pos, 4.0) < 0.01 || e.roll
                        || std::abs (e.pos - std::round (e.pos)) > 0.01)
                        continue;
                    int rolesHere = 0;
                    for (int rr = 0; rr < 5; ++rr)
                    {
                        for (const auto& o : p.events)
                            if ((int) o.role == rr && std::abs (o.pos - e.pos) < 0.26)
                            {
                                ++rolesHere;
                                break;
                            }
                    }
                    check (rolesHere <= RhythmStyle::get (family).maxOffbeatStack,
                           "off-beat slots never stack more voices than the style allows");
                }
            }

        // --- arabic ghosts hug a real hit --------------------------------------
        {
            GeneratorSettings s;
            s.family = Family::ARABIC;
            s.mode = Mode::GROOVE;
            s.density = 0.8f;
            for (int i = 0; i < 16; ++i)
            {
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (32001, i),
                    LoopGenerator::deriveSeed (32002, i), s));
                for (const auto& g : p.events)
                {
                    if (! (g.velocity <= 0.28f && g.gateSteps == 0.5 && ! g.roll))
                        continue;
                    bool neighbored = false;
                    for (const auto& o : p.events)
                        if (&o != &g && std::abs (o.pos - g.pos) <= 1.01)
                            neighbored = true;
                    check (neighbored, "an arabic ka never floats alone");
                }
            }
        }

        // --- sample-aware symbolic generation ----------------------------------
        {
            TraitsByRole sustainedHigh {};
            sustainedHigh[(size_t) Role::HIGH].sustained = true;
            GeneratorSettings s;
            s.density = 0.9f;
            s.bars = 2;
            for (int i = 0; i < 16; ++i)
            {
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (33001, i),
                    LoopGenerator::deriveSeed (33002, i), s, sustainedHigh));
                double last = -99.0;
                for (const auto& e : p.events)
                {
                    if (e.role != Role::HIGH)
                        continue;
                    check (e.gateSteps > 0.0, "a sustained sample is always gated");
                    if (! e.protectedAnchor && ! e.roll)
                    {
                        check (e.pos - last >= 1.0 - 1.0e-9,
                               "a sustained sample never re-fires inside a step");
                        last = e.pos;
                    }
                }
            }

            TraitsByRole weakMid {};
            weakMid[(size_t) Role::MID].weakTransient = true;
            s.mode = Mode::BUILD;
            s.energy = 0.9f;
            for (int i = 0; i < 24; ++i)
            {
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (34001, i),
                    LoopGenerator::deriveSeed (34002, i), s, weakMid));
                for (const auto& e : p.events)
                    check (! (e.roll && e.role == Role::MID),
                           "a weak-transient sample never carries a roll");
            }

            // Determinism with traits, and traits change the outcome.
            s.mode = Mode::GROOVE;
            const auto a = PatternValidator::validate (LoopGenerator::generateV2 (
                77, 88, s, sustainedHigh));
            const auto b = PatternValidator::validate (LoopGenerator::generateV2 (
                77, 88, s, sustainedHigh));
            const auto c = PatternValidator::validate (LoopGenerator::generateV2 (
                77, 88, s, TraitsByRole {}));
            check (a.signature() == b.signature(),
                   "traits-aware generation is deterministic");
            check (a.events.size() <= c.events.size(),
                   "sustained traits never ADD events");
        }

        // --- StyleDNA carries scorer targets -----------------------------------
        check (RhythmStyle::get (Family::PSYTRANCE).syncopationTargetLo
                   <= RhythmStyle::get (Family::AFRO).syncopationTargetLo,
               "afro aims higher syncopation than psytrance");
        check (RhythmStyle::get (Family::ARABIC).ghostsNeedNeighbor
               && ! RhythmStyle::get (Family::EDM).ghostsNeedNeighbor,
               "ka adjacency is an arabic rule, not a global one");
    }

    // === ENGINE 2 / PHASE 4: SilencePlanner + tension model ========================
    {
        // Planned silence survives MAXIMUM density and ornamentation.
        for (auto family : { Family::CINEMATIC, Family::EDM })
            for (int i = 0; i < 16; ++i)
            {
                GeneratorSettings s;
                s.family = family;
                s.mode = family == Family::CINEMATIC ? Mode::BREAK : Mode::GROOVE;
                s.density = 1.0f;
                s.randomness = 0.8f;
                s.bars = 2;
                const auto motif = LoopGenerator::deriveSeed (41001, i);
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    motif, LoopGenerator::deriveSeed (41002, i), s));
                const auto regions = SilencePlanner::plan (
                    s.mode, s.family, s.bars,
                    PhrasePlanner::plan (s.mode, s.bars, motif),
                    FeelVector::derive (s, motif), motif);
                for (const auto& reg : regions)
                {
                    if (! reg.allRoles)
                        continue;   // lead-only regions need the lead role
                    for (const auto& e : p.events)
                        if (! e.protectedAnchor && ! e.roll)
                            check (! reg.contains (e.pos),
                                   "planned silence cannot be refilled");
                }
            }

        // The four sections carry visibly different tension shapes.
        auto meanTension = [] (Mode mode, int segIndex)
        {
            GeneratorSettings s;
            s.mode = mode;
            s.bars = 4;
            double sum = 0.0;
            for (int i = 0; i < 24; ++i)
            {
                const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                    LoopGenerator::deriveSeed (42001, i),
                    LoopGenerator::deriveSeed (42002, i), s));
                sum += TensionModel::measure (p)[(size_t) segIndex];
            }
            return sum / 24.0;
        };
        check (meanTension (Mode::BUILD, 2) > meanTension (Mode::BUILD, 0) * 1.1,
               "BUILD tension rises toward the destination");
        check (meanTension (Mode::BREAK, 3) < meanTension (Mode::BREAK, 1),
               "BREAK ends with less tension than its call");
        check (meanTension (Mode::BREAK, 1) < meanTension (Mode::DROP, 1),
               "BREAK carries less tension than DROP overall");
    }

    // === ENGINE 2 / PHASE 5: candidate pool + MusicalScorer + joint selection ======
    {
        GeneratorSettings s;
        s.mode = Mode::GROOVE;
        s.family = Family::EDM;

        // Build a real pool the way the processor does.
        std::vector<Pattern> pool;
        std::vector<MusicalScorer::Features> feats;
        std::vector<MusicalScorer::ScoreBreakdown> scores;
        for (int j = 0; j < 72; ++j)
        {
            auto pat = PatternValidator::validate (LoopGenerator::generateV2 (
                LoopGenerator::deriveSeed (0xB00B5, j),
                LoopGenerator::deriveSeed (0xCAFE, j), s));
            feats.push_back (MusicalScorer::extract (pat));
            scores.push_back (MusicalScorer::score (pat, s));
            pool.push_back (std::move (pat));
        }

        // A known-bad pattern scores below every generated one: no downbeat,
        // one isolated stack in the middle of nowhere.
        {
            Pattern bad;
            bad.settings = s;
            for (int k = 0; k < 3; ++k)
            {
                Event e;
                e.pos = 7.0;
                e.role = (Role) (k + 1);
                e.velocity = 0.9f;
                bad.events.push_back (e);
            }
            const float badScore = MusicalScorer::score (bad, s).total;
            int beaten = 0;
            for (const auto& sc : scores)
                if (sc.total > badScore)
                    ++beaten;
            check (beaten >= 68, "a broken pattern loses to real candidates");
        }

        // Joint selection: deterministic, floor-respecting, more diverse
        // than taking the first twelve.
        const auto sel1 = MusicalScorer::selectDiverse (pool, feats, scores, 12, s);
        const auto sel2 = MusicalScorer::selectDiverse (pool, feats, scores, 12, s);
        check (sel1 == sel2 && sel1.size() == 12, "selection is deterministic");
        {
            std::set<int> unique (sel1.begin(), sel1.end());
            check (unique.size() == 12, "twelve distinct candidates selected");

            float bestQ = 0.0f;
            for (const auto& sc : scores)
                bestQ = juce::jmax (bestQ, sc.total);
            int aboveFloor = 0;
            for (int idx : sel1)
                if (scores[(size_t) idx].total >= bestQ * 0.55f)
                    ++aboveFloor;
            check (aboveFloor >= 10, "the quality floor holds for the batch");

            auto meanPairDist = [&] (const std::vector<int>& idxs)
            {
                float sum = 0.0f;
                int n = 0;
                for (size_t a = 0; a < idxs.size(); ++a)
                    for (size_t b2 = a + 1; b2 < idxs.size(); ++b2)
                    {
                        sum += MusicalScorer::distance (
                            feats[(size_t) idxs[a]], feats[(size_t) idxs[b2]]);
                        ++n;
                    }
                return n > 0 ? sum / (float) n : 0.0f;
            };
            std::vector<int> firstTwelve;
            for (int j = 0; j < 12; ++j)
                firstTwelve.push_back (j);
            check (meanPairDist (sel1) > meanPairDist (firstTwelve) * 1.05f,
                   "joint selection is more diverse than first-come");
        }

        // Ghost-only changes must not masquerade as diversity.
        {
            auto a = pool[0];
            auto b2 = a;
            Event ghost;
            ghost.pos = 5.0;
            ghost.role = Role::HIGH;
            ghost.velocity = 0.12f;
            ghost.gateSteps = 0.5;
            b2.events.push_back (ghost);
            const float d = MusicalScorer::distance (MusicalScorer::extract (a),
                                                     MusicalScorer::extract (b2));
            check (d < 0.1f, "one ghost is not perceived diversity");
        }

        // The debug breakdown explains itself.
        check (MusicalScorer::score (pool[0], s).describe().contains ("pulse="),
               "scores stay inspectable");
    }

    // === ENGINE 2 / PHASE 6: correlated performance ================================
    {
        GeneratorSettings s;
        s.mode = Mode::GROOVE;
        s.family = Family::EDM;
        s.randomness = 0.8f;
        double accentMicro = 0.0, ghostMicro = 0.0;
        int aN = 0, gN = 0;
        float maxAbs = 0.0f;
        for (int i = 0; i < 24; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                LoopGenerator::deriveSeed (61001, i),
                LoopGenerator::deriveSeed (61002, i), s));
            for (const auto& e : p.events)
            {
                if (e.protectedAnchor)
                    continue;
                maxAbs = juce::jmax (maxAbs, std::abs (e.microMs));
                if (e.role != Role::HIGH)
                    continue;
                if (e.velocity >= 0.55f) { accentMicro += e.microMs; ++aN; }
                if (e.velocity <= 0.28f) { ghostMicro += e.microMs; ++gN; }
            }
        }
        check (aN > 10 && gN > 10, "performance test has accents and ghosts");
        check (ghostMicro / juce::jmax (1, gN) > accentMicro / juce::jmax (1, aN),
               "confident hits arrive earlier, ghosts hang behind");
        check (maxAbs <= 12.001f, "human timing stays bounded");

        // Psytrance stays surgical even at maximum randomness.
        s.family = Family::PSYTRANCE;
        s.randomness = 1.0f;
        float psyMax = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                LoopGenerator::deriveSeed (62001, i),
                LoopGenerator::deriveSeed (62002, i), s));
            for (const auto& e : p.events)
                if (! e.protectedAnchor)
                    psyMax = juce::jmax (psyMax, std::abs (e.microMs));
        }
        check (psyMax <= 1.0f, "psytrance core precision survives max randomness");
    }

    // === ENGINE 2 / PHASE 7: destination-aware endings =============================
    {
        GeneratorSettings s;
        s.mode = Mode::GROOVE;
        s.bars = 2;
        const TraitsByRole none {};
        auto gen = [&] (Destination d, int i)
        {
            return PatternValidator::validate (LoopGenerator::generateV2 (
                LoopGenerator::deriveSeed (71001, i),
                LoopGenerator::deriveSeed (71002, i), s, none, d));
        };

        int stopsClean = 0, dropsThrow = 0, breaksDeflate = 0, breakSwells = 0;
        for (int i = 0; i < 12; ++i)
        {
            const auto pStop = gen (Destination::ToStop, i);
            bool tailEmpty = true;
            bool finalAccent = false;
            for (const auto& e : pStop.events)
            {
                if (e.pos > 28.26)
                    tailEmpty = false;
                if (std::abs (e.pos - 28.0) < 0.26 && e.velocity >= 0.99f)
                    finalAccent = true;
            }
            if (tailEmpty && finalAccent)
                ++stopsClean;

            const auto pDrop = gen (Destination::ToDrop, i);
            for (const auto& e : pDrop.events)
                if (std::abs (e.pos - 31.0) < 0.26 && e.role == Role::LOW
                    && e.velocity >= 0.99f)
                {
                    ++dropsThrow;
                    break;
                }

            const auto pBreak = gen (Destination::ToBreak, i);
            bool deflated = true;
            for (const auto& e : pBreak.events)
            {
                if (e.pos >= 30.0 && ! e.protectedAnchor && ! e.reverse)
                    deflated = false;
                if (e.reverse)
                    ++breakSwells;
            }
            if (deflated)
                ++breaksDeflate;

            // Determinism per destination.
            check (gen (Destination::ToStop, i).signature()
                       == pStop.signature(),
                   "destination endings are deterministic");
        }
        check (stopsClean == 12, "ToStop: one final accent, then nothing");
        check (dropsThrow == 12, "ToDrop: the pickup always promises the impact");
        check (breaksDeflate == 12, "ToBreak: the tail deflates");
        check (breakSwells >= 6, "ToBreak: the reverse swell usually pulls in");
        check (gen (Destination::ToStop, 0).signature()
                   != gen (Destination::LoopBack, 0).signature(),
               "different destinations produce different endings");
    }

    // === PHASE A (v0.10 -> completion): pool config, floors, CLEAN, ENDING =========
    {
        GeneratorSettings s;
        s.mode = Mode::GROOVE;
        s.family = Family::EDM;

        // Pool prefix stability: the first 96 of a 192 run are the same
        // candidates as a 96 run (A1).
        for (int j : { 0, 47, 95 })
        {
            const auto a = LoopGenerator::generateV2 (
                LoopGenerator::deriveSeed (0xB00B5, j),
                LoopGenerator::deriveSeed (0xCAFE, j), s);
            const auto b = LoopGenerator::generateV2 (
                LoopGenerator::deriveSeed (0xB00B5, j),
                LoopGenerator::deriveSeed (0xCAFE, j), s);
            check (a.signature() == b.signature(),
                   "pool candidate streams are prefix-stable");
        }
        check (CandidatePoolConfig::initialPoolSize == 96
               && CandidatePoolConfig::finalExpansionSize == 192,
               "one source of truth for pool sizing");

        // HardValidator (A2): a broken pattern is rejected absolutely, a
        // generated one is not.
        {
            Pattern broken;
            broken.settings = s;
            Event lone;
            lone.pos = 30.0;
            lone.role = Role::HIGH;
            lone.velocity = 0.9f;
            broken.events = { lone };
            check (MusicalScorer::hardReject (broken, s),
                   "hard validity rejects the functionally empty");
            const auto real = PatternValidator::validate (
                LoopGenerator::generateV2 (5, 6, s));
            check (! MusicalScorer::hardReject (real, s),
                   "hard validity passes real candidates");
        }

        // CLEAN (A7): monotonic strengths, anchors always survive,
        // deterministic.
        {
            const auto p0 = PatternValidator::validate (
                LoopGenerator::generateV2 (99, 44, s));
            Pattern l1 = p0, l2 = p0, l3 = p0, l1b = p0;
            LoopGenerator::cleanPattern (l1, 1);
            LoopGenerator::cleanPattern (l1b, 1);
            LoopGenerator::cleanPattern (l2, 2);
            LoopGenerator::cleanPattern (l3, 3);
            check (l1.signature() == l1b.signature(), "CLEAN is deterministic");
            check (l1.events.size() <= p0.events.size()
                   && l2.events.size() <= l1.events.size()
                   && l3.events.size() <= l2.events.size(),
                   "CLEAN strengths strip monotonically");
            auto anchors = [] (const Pattern& p)
            {
                int n = 0;
                for (const auto& e : p.events)
                    if (e.protectedAnchor)
                        ++n;
                return n;
            };
            check (anchors (l3) == anchors (p0),
                   "CLEAN hard keeps every anchor");
        }

        // ENDING override on an existing pattern (A8): only the transition
        // region changes; deterministic.
        {
            auto p0 = PatternValidator::validate (
                LoopGenerator::generateV2 (123, 456, s));
            p0.settings.bars = s.bars;
            Pattern stop = p0, stop2 = p0;
            LoopGenerator::applyEnding (stop, Destination::ToStop, {}, 777);
            LoopGenerator::applyEnding (stop2, Destination::ToStop, {}, 777);
            check (stop.signature() == stop2.signature(),
                   "applyEnding is deterministic");
            bool tailClear = true, headSame = true;
            for (const auto& e : stop.events)
                if (e.pos > stop.stepCount() - 4 + 0.26)
                    tailClear = false;
            // The head (first half) is untouched by an ending override.
            juce::String h0, h1;
            for (const auto& e : p0.events)
                if (e.pos < p0.stepCount() / 2)
                    h0 << juce::roundToInt (e.pos * 4.0) << ':' << (int) e.role << ';';
            for (const auto& e : stop.events)
                if (e.pos < stop.stepCount() / 2)
                    h1 << juce::roundToInt (e.pos * 4.0) << ':' << (int) e.role << ';';
            headSame = h0 == h1;
            check (tailClear && headSame,
                   "ENDING override rewrites only the transition region");
        }
    }

    // === F1: the pump =============================================================
    {
        LoopRenderer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = 124.0;
        ctx.samples = { makeHit (100.0, sr, 0.3), makeHit (800.0, sr, 0.2),
                        makeHit (4000.0, sr, 0.15) };
        ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);
        GeneratorSettings s;
        auto dry = PatternValidator::validate (LoopGenerator::generateV2 (31, 32, s));
        auto pumped = dry;
        pumped.fxPump = 1.0f;
        const auto a = LoopRenderer::render (dry, ctx);
        const auto b = LoopRenderer::render (pumped, ctx);
        const auto b2 = LoopRenderer::render (pumped, ctx);
        bool differs = false, sameTwice = true;
        for (int i = 0; i < a.getNumSamples(); i += 37)
        {
            differs = differs || a.getSample (0, i) != b.getSample (0, i);
            sameTwice = sameTwice && b.getSample (0, i) == b2.getSample (0, i);
        }
        check (differs, "the pump audibly ducks");
        check (sameTwice, "the pump is deterministic");
        check (b.getNumSamples() == a.getNumSamples(), "the pump keeps the length");
        check (b.getMagnitude (0, b.getNumSamples())
                   <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-4f,
               "the pump honors the ceiling");
    }

    // === PHASE E: kit slicing =====================================================
    {
        // A synthetic "loop": kick, snare, hat, kick - with silence between.
        juce::AudioBuffer<float> loopBuf (1, (int) (sr * 2.0));
        loopBuf.clear();
        auto putHit = [&] (double atSec, double freq, double lenSec)
        {
            const int at = (int) (atSec * sr);
            for (int i = 0; i < (int) (lenSec * sr)
                 && at + i < loopBuf.getNumSamples(); ++i)
            {
                const double t = i / sr;
                loopBuf.addSample (0, at + i,
                    (float) (std::sin (juce::MathConstants<double>::twoPi * freq * t)
                             * std::exp (-t * 22.0) * 0.8));
            }
        };
        putHit (0.05, 70.0, 0.35);
        putHit (0.55, 900.0, 0.25);
        putHit (1.05, 5000.0, 0.12);
        putHit (1.55, 70.0, 0.35);

        const auto onsets = SampleAnalyzer::detectOnsets (loopBuf, sr);
        check ((int) onsets.size() >= 3, "onset detection finds the hits");
        const auto slices = SampleAnalyzer::chooseKitSlices (loopBuf, sr);
        check ((int) slices.size() >= 2, "kit slicing reaches confidence");
        if (slices.size() >= 2)
        {
            // LOW slice must be darker than the HIGH slice.
            auto centroidOf = [&] (const SampleAnalyzer::KitSlice& k)
            {
                const int from = (int) (k.start * (float) loopBuf.getNumSamples());
                const int to = (int) (k.end * (float) loopBuf.getNumSamples());
                juce::AudioBuffer<float> sub (1, juce::jmax (16, to - from));
                sub.copyFrom (0, 0, loopBuf, 0, from, sub.getNumSamples());
                return SampleAnalyzer::analyze (sub, sr).spectralCentroidHz;
            };
            check (centroidOf (slices.front()) < centroidOf (slices.back()),
                   "kit slices order dark to bright");
        }
        // Low confidence input changes nothing: pure noise floor.
        juce::AudioBuffer<float> flat (1, (int) sr);
        flat.clear();
        check (SampleAnalyzer::chooseKitSlices (flat, sr).empty(),
               "no onsets means no pretended kit");
    }

    // === F4: render performance budget ============================================
    {
        LoopRenderer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = 125.0;
        ctx.samples = { makeHit (60.0, sr, 0.4), makeHit (800.0, sr, 0.2),
                        makeHit (4000.0, sr, 0.15) };
        ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);
        GeneratorSettings s;
        s.bars = 2;
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < 12; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                LoopGenerator::deriveSeed (0xF4, i),
                LoopGenerator::deriveSeed (0x4F, i), s));
            const auto buf = LoopRenderer::render (p, ctx);
            check (buf.getNumSamples() > 0, "render benchmark produced audio");
        }
        const double ms = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - t0) * 1000.0;
        std::cout << "  render benchmark: 12 cards in "
                  << juce::String (ms, 1) << " ms\n";
        check (! timingIsMeaningful || ms < 4000.0, "12-card render inside budget");
    }

    // === B4: chained phrase - FX tails must cross the card boundary ==============
    {
        LoopRenderer::Context ctx;
        ctx.sampleRate = sr;
        ctx.bpm = 125.0;
        ctx.samples = { makeHit (60.0, sr, 0.4), makeHit (800.0, sr, 0.2),
                        makeHit (4000.0, sr, 0.15) };
        ctx.roleMap = SampleAnalyzer::assignRoles (ctx.samples);
        GeneratorSettings s;
        s.bars = 1;

        auto makeCard = [&] (int i, float reverb)
        {
            auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                LoopGenerator::deriveSeed (0xB4C4, i),
                LoopGenerator::deriveSeed (0x4C4B, i), s));
            p.fxReverb = reverb;
            return p;
        };

        // Card one drenched, card two bone dry: any wet energy at the top of
        // card two can only have come across the boundary.
        const auto wetCard = makeCard (0, 0.9f);
        const auto dryCard = makeCard (1, 0.0f);
        const std::vector<const Pattern*> pair { &wetCard, &dryCard };
        const auto chained = LoopRenderer::renderChain (pair, ctx);
        const int cardLen = LoopRenderer::render (dryCard, ctx).getNumSamples();
        check (chained.getNumSamples() == cardLen * 2, "chain length is the sum");
        check (LoopRenderer::renderChain ({ &wetCard }, ctx).getNumSamples() == 0,
               "a chain needs at least two cards");

        // The same phrase with card one dry as well. Card two is identical
        // audio in both, so the tail is measured where card two is quietest -
        // a gap between its own hits, where anything audible arrived from the
        // card before it. Each phrase is normalized against its own peak,
        // which is all the -1 dBFS ceiling leaves free to differ.
        const auto dryFirst = makeCard (0, 0.0f);
        const std::vector<const Pattern*> dryPair { &dryFirst, &dryCard };
        const auto allDry = LoopRenderer::renderChain (dryPair, ctx);
        const auto plainSecond = LoopRenderer::render (dryCard, ctx);
        const int probe = (int) (sr * 0.02);
        int gapAt = 0;
        float quietest = 1.0f;
        for (int at = 0; at + probe < juce::jmin (cardLen, (int) (sr * 0.5)); at += probe)
            if (const float m = plainSecond.getMagnitude (at, probe); m < quietest)
            {
                quietest = m;
                gapAt = at;
            }
        auto levelInGap = [&] (const juce::AudioBuffer<float>& b)
        {
            const float peak = b.getMagnitude (0, b.getNumSamples());
            return peak > 0.0f ? b.getMagnitude (cardLen + gapAt, probe) / peak : 0.0f;
        };
        check (levelInGap (chained) > levelInGap (allDry) * 1.5f,
               "reverb tail crosses into the next card");

        // ...and the phrase still loops: the last card's tail wrapped round to
        // the top, so the chain start differs from the lone first card.
        const auto plainFirst = LoopRenderer::render (wetCard, ctx);
        float headDiff = 0.0f;
        for (int i = 0; i < juce::jmin (1000, cardLen); ++i)
            headDiff = juce::jmax (headDiff,
                std::abs (chained.getSample (0, i) - plainFirst.getSample (0, i)));
        check (headDiff > 0.0f, "chain start carries the wrapped tail");

        // Ceiling holds for the whole phrase, tails included.
        check (chained.getMagnitude (0, chained.getNumSamples())
                   <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-4f,
               "chained phrase respects the -1 dBFS ceiling");

        // Determinism: same cards, same context, byte-identical phrase.
        const auto again = LoopRenderer::renderChain (pair, ctx);
        float drift = 0.0f;
        for (int i = 0; i < chained.getNumSamples(); ++i)
            drift = juce::jmax (drift,
                std::abs (chained.getSample (0, i) - again.getSample (0, i)));
        check (drift == 0.0f, "chain render is deterministic");

        // Worst case a user can build: all twelve cards favorited, four bars
        // each, every one of them wet. This runs when DRAG CHAIN is pressed.
        GeneratorSettings big = s;
        big.bars = 4;
        std::vector<Pattern> heavy;
        for (int i = 0; i < 12; ++i)
        {
            auto p = PatternValidator::validate (LoopGenerator::generateV2 (
                LoopGenerator::deriveSeed (0xB16, i),
                LoopGenerator::deriveSeed (0x61B, i), big));
            p.fxReverb = 0.6f;
            p.fxDelay = 0.5f;
            heavy.push_back (std::move (p));
        }
        std::vector<const Pattern*> heavyPtrs;
        for (const auto& p : heavy)
            heavyPtrs.push_back (&p);
        const auto t1 = juce::Time::getHighResolutionTicks();
        const auto longChain = LoopRenderer::renderChain (heavyPtrs, ctx);
        const double chainMs = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - t1) * 1000.0;
        std::cout << "  chain benchmark: 12 wet 4-bar cards in "
                  << juce::String (chainMs, 1) << " ms\n";
        check (longChain.getNumSamples() > 0, "long chain rendered");
        check (! timingIsMeaningful || chainMs < 2000.0, "worst-case chain inside budget");
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILED") << " - "
              << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
