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
#include "../Playback/PreviewPlayer.h"

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
        int rollingSeeds = 0;
        for (int i = 0; i < 60; ++i)
        {
            const auto p = PatternValidator::validate (LoopGenerator::generate (
                LoopGenerator::deriveSeed (13579, i), s));
            for (const auto& e : p.events)
                if (e.role == Role::LOW && e.gateSteps >= 1.0 && e.velocity < 0.6f)
                {
                    ++rollingSeeds;
                    break;
                }
        }
        check (rollingSeeds >= 4, "melodic rolling skeleton appears in the pool");
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

    std::cout << (failures == 0 ? "ALL OK" : "FAILED") << " - "
              << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
