// Renders one example loop per (family, mode) pair from real samples, for
// listening sessions. Usage:
//   OrchaAudition <outDir> <bpm> sample1.wav [sample2.wav [sample3.wav]]

#include <JuceHeader.h>
#include <map>
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

// Phase A5/A6: turns the listener's filled ratings into the protocol's
// metrics. Reads <kitDir>/.key/key.csv + a ratings JSONL (one object per
// line: {"case":N,"choice":"X"|"Y"|"tie","confidence":1-5,"tags":[],"note":""}),
// prints win rates overall / per family / per mode, repeat-consistency, and
// tag counts, and writes metrics.json next to the ratings. Empty choices are
// skipped and reported - never counted as data.
static int tallyRatings (const juce::File& kitDir, const juce::File& ratingsFile)
{
    const auto keyFile = kitDir.getChildFile (".key").getChildFile ("key.csv");
    if (! keyFile.existsAsFile())
    {
        std::cout << "no key at " << keyFile.getFullPathName() << "\n";
        return 1;
    }
    if (! ratingsFile.existsAsFile())
    {
        std::cout << "no ratings file at " << ratingsFile.getFullPathName() << "\n";
        return 1;
    }

    struct KeyRow { juce::String x, y, family, mode; int repeatOf = 0; };
    std::map<int, KeyRow> key;
    {
        juce::StringArray lines;
        keyFile.readLines (lines);
        for (int i = 1; i < lines.size(); ++i)   // skip header
        {
            auto cols = juce::StringArray::fromTokens (lines[i], ",", "");
            if (cols.size() < 11)
                continue;
            key[cols[0].getIntValue()] =
                { cols[1], cols[2], cols[3], cols[4], cols[10].getIntValue() };
        }
    }

    struct Rating { juce::String engine; int confidence = 0; juce::StringArray tags; };
    std::map<int, Rating> picks;          // case -> resolved pick
    int filled = 0, empty = 0, ties = 0, unknown = 0;
    std::map<juce::String, int> tagCounts;
    {
        juce::StringArray lines;
        ratingsFile.readLines (lines);
        for (const auto& line : lines)
        {
            if (line.trim().isEmpty())
                continue;
            const auto v = juce::JSON::parse (line);
            if (! v.isObject())
                { ++unknown; continue; }
            const int caseNum = (int) v.getProperty ("case", 0);
            const auto choice = v.getProperty ("choice", "").toString()
                                 .trim().toUpperCase();
            const auto it = key.find (caseNum);
            if (it == key.end())
                { ++unknown; continue; }
            if (choice.isEmpty())
                { ++empty; continue; }
            Rating r;
            r.confidence = (int) v.getProperty ("confidence", 0);
            if (const auto* tagArr = v.getProperty ("tags", juce::var()).getArray())
                for (const auto& t : *tagArr)
                {
                    r.tags.add (t.toString());
                    ++tagCounts[t.toString()];
                }
            if (choice == "X")       r.engine = it->second.x;
            else if (choice == "Y")  r.engine = it->second.y;
            else                     { ++ties; continue; }
            picks[caseNum] = std::move (r);
            ++filled;
        }
    }

    struct Bucket { int wins2 = 0, total = 0; };
    Bucket overall;
    std::map<juce::String, Bucket> byFamily, byMode;
    double confWeightedWins = 0.0, confWeightTotal = 0.0;
    int repeatPairs = 0, repeatConsistent = 0;
    for (const auto& [caseNum, r] : picks)
    {
        const auto& k = key[caseNum];
        if (k.repeatOf > 0)
        {
            // Hidden repeat: consistency check only, never counted twice.
            const auto orig = picks.find (k.repeatOf);
            if (orig != picks.end())
            {
                ++repeatPairs;
                if (orig->second.engine == r.engine)
                    ++repeatConsistent;
            }
            continue;
        }
        const bool wins2 = r.engine == "engine2";
        ++overall.total;            overall.wins2 += wins2 ? 1 : 0;
        ++byFamily[k.family].total; byFamily[k.family].wins2 += wins2 ? 1 : 0;
        ++byMode[k.mode].total;     byMode[k.mode].wins2 += wins2 ? 1 : 0;
        const double w = juce::jlimit (1, 5, r.confidence);
        confWeightTotal += w;
        confWeightedWins += wins2 ? w : 0.0;
    }

    auto pct = [] (int a, int b)
    { return b > 0 ? juce::String (100.0 * a / b, 1) + "%" : juce::String ("-"); };

    std::cout << "ORCHA blind tally\n"
              << "  rated: " << filled << "  empty: " << empty
              << "  ties: " << ties << "  unmatched: " << unknown << "\n"
              << "  engine2 wins: " << overall.wins2 << "/" << overall.total
              << "  (" << pct (overall.wins2, overall.total) << ")\n";
    if (confWeightTotal > 0.0)
        std::cout << "  confidence-weighted: "
                  << juce::String (100.0 * confWeightedWins / confWeightTotal, 1)
                  << "%\n";
    if (repeatPairs > 0)
        std::cout << "  repeat consistency: " << repeatConsistent << "/"
                  << repeatPairs << "  (" << pct (repeatConsistent, repeatPairs)
                  << ")  - below 70% means the answers are noise\n";
    std::cout << "  by family:\n";
    for (const auto& [name, b] : byFamily)
        std::cout << "    " << name.paddedRight (' ', 15) << b.wins2 << "/"
                  << b.total << "  (" << pct (b.wins2, b.total) << ")\n";
    std::cout << "  by mode:\n";
    for (const auto& [name, b] : byMode)
        std::cout << "    " << name.paddedRight (' ', 15) << b.wins2 << "/"
                  << b.total << "  (" << pct (b.wins2, b.total) << ")\n";
    if (! tagCounts.empty())
    {
        std::cout << "  tags:\n";
        for (const auto& [tag, n] : tagCounts)
            std::cout << "    " << tag.paddedRight (' ', 15) << n << "\n";
    }

    auto* m = new juce::DynamicObject();
    m->setProperty ("rated", filled);
    m->setProperty ("empty", empty);
    m->setProperty ("ties", ties);
    m->setProperty ("engine2Wins", overall.wins2);
    m->setProperty ("scoredCases", overall.total);
    m->setProperty ("repeatConsistent", repeatConsistent);
    m->setProperty ("repeatPairs", repeatPairs);
    if (confWeightTotal > 0.0)
        m->setProperty ("confidenceWeightedWinRate",
                        confWeightedWins / confWeightTotal);
    juce::Array<juce::var> fams;
    for (const auto& [name, b] : byFamily)
    {
        auto* f = new juce::DynamicObject();
        f->setProperty ("family", name);
        f->setProperty ("wins2", b.wins2);
        f->setProperty ("total", b.total);
        fams.add (juce::var (f));
    }
    m->setProperty ("byFamily", fams);
    ratingsFile.getSiblingFile ("metrics.json")
        .replaceWithText (juce::JSON::toString (juce::var (m), false));
    std::cout << "metrics.json written next to the ratings\n";
    return 0;
}

int main (int argc, char* argv[])
{
    if (argc >= 3 && juce::String (argv[1]) == "tally")
    {
        const juce::File kitDir { juce::String (argv[2]) };
        const juce::File ratings = argc >= 4
            ? juce::File { juce::String (argv[3]) }
            : kitDir.getChildFile ("ratings.jsonl");
        return tallyRatings (kitDir, ratings);
    }
    if (argc < 4)
    {
        std::cout << "usage: OrchaAudition <outDir> <bpm> <sample1> [sample2 [sample3]]\n"
                     "       OrchaAudition blind <outDir> <bpm> <samples...>\n"
                     "       OrchaAudition tally <kitDir> [ratings.jsonl]\n";
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
