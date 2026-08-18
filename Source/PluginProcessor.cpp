#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/SampleLoader.h"
#include "Engine/SampleAnalyzer.h"
#include "Engine/LoopGenerator.h"
#include "Engine/PatternValidator.h"
#include "Engine/LoopRenderer.h"
#include "Engine/RenderCache.h"
#include "Engine/MidiExporter.h"
#include "Engine/MusicalScorer.h"
#include "Engine/SilencePlanner.h"

namespace orcha
{

// What each role's sample can carry, for the symbolic stage (Phase 3:
// sample-aware role use). Derived from the existing analysis, deterministic.
static TraitsByRole deriveTraits (const std::vector<InputSample::Ptr>& samples,
                                  const RoleMap& map)
{
    TraitsByRole traits {};
    for (Role role : { Role::LOW, Role::MID, Role::HIGH, Role::FX })
    {
        const int slot = map.slotFor (role);
        if (slot < 0 || slot >= (int) samples.size()
            || samples[(size_t) slot] == nullptr)
            continue;
        const auto& a = samples[(size_t) slot]->analysis;
        auto& t = traits[(size_t) role];
        t.sustained = ! a.isOneShot || a.durationSeconds > 0.6;
        t.lowHeavy = a.lowEnergyRatio > 0.4f;
        t.brightShort = a.spectralCentroidHz > 2500.0f && a.durationSeconds < 0.4;
        t.weakTransient = a.transientStrength < 0.3f;
    }
    return traits;
}

OrchaAudioProcessor::OrchaAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    startTimer (150);
    pool.addJob ([] { RenderCache::cleanupStale(); });
}

OrchaAudioProcessor::~OrchaAudioProcessor()
{
    generation.fetch_add (1);           // orphan any in-flight results
    pool.removeAllJobs (true, 4000);
    stopTimer();
}

// --- samples -------------------------------------------------------------------

void OrchaAudioProcessor::loadSampleAsync (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= numSlots || ! SampleLoader::isSupported (file))
        return;

    juce::WeakReference<OrchaAudioProcessor> self (this);
    pool.addJob ([self, slot, file]
    {
        auto loaded = SampleLoader::load (file);   // may be nullptr (corrupt)
        juce::MessageManager::callAsync ([self, slot, loaded]
        {
            if (self == nullptr)
                return;
            if (loaded != nullptr)
            {
                self->rawSamples[(size_t) slot] = loaded;
                self->samples[(size_t) slot] =
                    SampleTransform::apply (*loaded, self->transforms[(size_t) slot]);
                self->rolesChanged();
            }
            self->notifyModel();       // even on failure, so the UI un-busies
        });
    });
}

void OrchaAudioProcessor::clearSample (int slot)
{
    if (slot < 0 || slot >= numSlots)
        return;
    samples[(size_t) slot] = nullptr;
    rawSamples[(size_t) slot] = nullptr;
    transforms[(size_t) slot] = {};
    rolesChanged();
    notifyModel();
}

void OrchaAudioProcessor::setTransform (int slot, SampleTransform::Settings t)
{
    if (slot < 0 || slot >= numSlots)
        return;
    transforms[(size_t) slot] = t;
    if (rawSamples[(size_t) slot] != nullptr)
    {
        // Cheap enough to do synchronously - a copy, a crop, an analysis.
        samples[(size_t) slot] = SampleTransform::apply (*rawSamples[(size_t) slot], t);
        rolesChanged();
    }
    notifyModel();
}

bool OrchaAudioProcessor::canSliceAsKit (int slot) const
{
    if (slot < 0 || slot >= numSlots || rawSamples[(size_t) slot] == nullptr)
        return false;
    const auto& a = rawSamples[(size_t) slot]->analysis;
    return ! a.isOneShot && a.durationSeconds > 1.0;
}

bool OrchaAudioProcessor::sliceAsKit (int slot)
{
    if (! canSliceAsKit (slot))
        return false;
    const auto src = rawSamples[(size_t) slot];
    const auto slices = SampleAnalyzer::chooseKitSlices (src->buffer,
                                                         src->sourceSampleRate);
    if (slices.size() < 2)
        return false;   // low confidence: nothing changes, no pretending

    for (int t = 0; t < numSlots && t < (int) slices.size(); ++t)
    {
        rawSamples[(size_t) t] = src;
        transforms[(size_t) t] = {};
        transforms[(size_t) t].start = slices[(size_t) t].start;
        transforms[(size_t) t].end = slices[(size_t) t].end;
        samples[(size_t) t] = SampleTransform::apply (*src, transforms[(size_t) t]);
    }
    rolesChanged();
    notifyModel();
    return true;
}

void OrchaAudioProcessor::setUserRole (int slot, Role role)
{
    if (auto& s = samples[(size_t) slot])
    {
        auto copy = std::make_shared<InputSample> (*s);
        copy->userRole = role;
        samples[(size_t) slot] = std::move (copy);
        rolesChanged();
        notifyModel();
    }
}

bool OrchaAudioProcessor::anySampleLoaded() const
{
    return std::any_of (samples.begin(), samples.end(),
                        [] (const auto& s) { return s != nullptr; });
}

void OrchaAudioProcessor::rolesChanged()
{
    roleMap = SampleAnalyzer::assignRoles (samples);

    // Existing options (including ones restored from state that could not
    // render yet) get rebuilt with the new role map.
    std::vector<int> toBuild;
    for (int i = 0; i < numOptions; ++i)
        if (pendingSeeds[(size_t) i].motif != 0)
            toBuild.push_back (i);
    if (! toBuild.empty() && anySampleLoaded())
        enqueueBuild (std::move (toBuild));
}

// --- generation ----------------------------------------------------------------

void OrchaAudioProcessor::generateAll()
{
    if (! anySampleLoaded())
        return;

    auto& rnd = juce::Random::getSystemRandom();
    const juce::uint64 master = ((juce::uint64) (juce::uint32) rnd.nextInt() << 32)
                              ^ (juce::uint64) juce::Time::getHighResolutionTicks();
    std::vector<int> toBuild;
    for (int i = 0; i < numOptions; ++i)
    {
        auto& opt = options[(size_t) i];
        if (opt.favorite && opt.present)
            continue;                   // favorites survive GENERATE MORE
        pendingSeeds[(size_t) i] = { LoopGenerator::deriveSeed (master, i),
                                     LoopGenerator::deriveSeed (~master, i), 2 };
        opt.ready = false;
        // A replaced card is a NEW card. Without this, any card the user had
        // edited (step edits, CLEAN, ENDING, TRANSITION all set the flag)
        // took the useExisting path in enqueueBuild and re-rendered its OLD
        // pattern - it looked frozen under GENERATE, forever.
        opt.edited = false;
        opt.endingOverride = -1;
        opt.fxReverb = opt.fxDelay = opt.fxPump = 0.0f;
        toBuild.push_back (i);
    }
    enqueueBuild (std::move (toBuild), {}, false, true);
}

void OrchaAudioProcessor::generateSet()
{
    if (! anySampleLoaded())
        return;
    auto& rnd = juce::Random::getSystemRandom();
    const juce::uint64 master = ((juce::uint64) (juce::uint32) rnd.nextInt() << 32)
                              ^ (juce::uint64) juce::Time::getHighResolutionTicks();
    // ONE motif for the whole set: same skeleton, lead voice and cell -
    // the same musical world, stated as groove, build, drop and break.
    const juce::uint64 setMotif = LoopGenerator::deriveSeed (master, 777);
    static const Mode groupModes[4] = { Mode::GROOVE, Mode::BUILD,
                                        Mode::DROP, Mode::BREAK };
    std::vector<int> toBuild;
    for (int i = 0; i < numOptions; ++i)
    {
        auto& opt = options[(size_t) i];
        if (opt.favorite && opt.present)
            continue;
        pendingSeeds[(size_t) i] = { setMotif,
                                     LoopGenerator::deriveSeed (~master, i), 2,
                                     0, (int) groupModes[i / 3] };
        opt.ready = false;
        // Same rule as generateAll: a replaced card is a NEW card, so the
        // edited flag must clear or enqueueBuild re-renders the OLD pattern.
        opt.edited = false;
        opt.endingOverride = -1;
        opt.fxReverb = opt.fxDelay = opt.fxPump = 0.0f;
        toBuild.push_back (i);
    }
    enqueueBuild (std::move (toBuild));   // no pool: cohesion IS the point
}

bool OrchaAudioProcessor::exportAll (const juce::File& directory)
{
    if (! directory.isDirectory() && ! directory.createDirectory())
        return false;
    juce::var manifest;
    auto* arr = new juce::DynamicObject();
    juce::Array<juce::var> cards;
    int exported = 0;
    for (int i = 0; i < numOptions; ++i)
    {
        const auto& opt = options[(size_t) i];
        if (! opt.present || ! opt.ready || opt.loop == nullptr)
            continue;
        const auto base = juce::String ("ORCHA_")
            + familyName (opt.pattern.settings.family) + "_"
            + opt.pattern.name.replaceCharacter (' ', '_') + "_"
            + juce::String (juce::roundToInt (opt.loop->bpm)) + "bpm_"
            + juce::String (opt.pattern.settings.bars) + "bars_card"
            + juce::String (i + 1).paddedLeft ('0', 2);
        const auto wav = ensureWavFor (i);
        const auto mid = ensureMidiFor (i);
        if (wav.existsAsFile())
            wav.copyFileTo (directory.getChildFile (
                (base + ".wav").replaceCharacter (' ', '_')));
        if (mid.existsAsFile())
            mid.copyFileTo (directory.getChildFile (
                (base + ".mid").replaceCharacter (' ', '_')));
        auto* card = new juce::DynamicObject();
        card->setProperty ("card", i + 1);
        card->setProperty ("name", opt.pattern.name);
        card->setProperty ("family", familyName (opt.pattern.settings.family));
        card->setProperty ("mode", modeName (opt.pattern.settings.mode));
        card->setProperty ("bars", opt.pattern.settings.bars);
        card->setProperty ("bpm", opt.loop->bpm);
        card->setProperty ("motifSeed", juce::String::toHexString ((juce::int64) opt.pattern.seed));
        card->setProperty ("ornamentSeed", juce::String::toHexString ((juce::int64) opt.pattern.ornamentSeed));
        card->setProperty ("algo", opt.pattern.algo);
        card->setProperty ("destination", (int) opt.pattern.destination);
        cards.add (juce::var (card));
        ++exported;
    }
    // The favorites chain, when there is one, ships as audio AND as MIDI so
    // the whole phrase can be rebuilt in the host with the user's own sounds.
    std::vector<const Pattern*> chainPatterns;
    for (const auto& opt : options)
        if (opt.favorite && opt.present && opt.ready && opt.loop != nullptr)
            chainPatterns.push_back (&opt.pattern);
    if (chainPatterns.size() >= 2)
    {
        const auto chainBase = "ORCHA_CHAIN_"
            + juce::String ((int) chainPatterns.size()) + "cards_"
            + juce::String (juce::roundToInt (bpmAtomic.load())) + "bpm";
        const auto chainWav = ensureChainWav();
        if (chainWav.existsAsFile())
            chainWav.copyFileTo (directory.getChildFile (chainBase + ".wav"));
        MidiExporter::writeChain (chainPatterns, bpmAtomic.load(),
                                  directory.getChildFile (chainBase + ".mid"));
        arr->setProperty ("chainCards", (int) chainPatterns.size());
    }

    arr->setProperty ("plugin", "ORCHA");
    arr->setProperty ("cards", cards);
    directory.getChildFile ("manifest.json")
        .replaceWithText (juce::JSON::toString (juce::var (arr), false));
    return exported > 0;
}

void OrchaAudioProcessor::regenerateOption (int index)
{
    if (index < 0 || index >= numOptions || ! anySampleLoaded())
        return;
    auto& rnd = juce::Random::getSystemRandom();
    const juce::uint64 fresh = ((juce::uint64) (juce::uint32) rnd.nextInt() << 32)
                             ^ (juce::uint64) juce::Time::getHighResolutionTicks();
    auto& opt = options[(size_t) index];
    // Same groove, another take: the motif seed survives, only the
    // ornament seed re-rolls. A never-generated card starts from scratch,
    // and manual edits make way for the fresh take.
    // Regeneration is a user action: it upgrades the card to engine 2 even
    // when the motif came from an old project.
    pendingSeeds[(size_t) index] = {
        opt.present ? opt.pattern.seed : LoopGenerator::deriveSeed (fresh, index),
        LoopGenerator::deriveSeed (~fresh, index), 2 };
    opt.ready = false;
    opt.edited = false;
    // The new take must differ from the one it replaces - a card with a
    // simple motif can otherwise re-roll into the identical pattern and look
    // like the button did nothing.
    juce::StringArray mustDiffer;
    if (opt.present)
        mustDiffer.add (opt.pattern.signature());
    enqueueBuild ({ index }, std::move (mustDiffer));
}

void OrchaAudioProcessor::applyEditedPattern (int index, Pattern edited)
{
    if (index < 0 || index >= numOptions || ! options[(size_t) index].present)
        return;

    // Light cleanup only - no DROP-downbeat insertion, no BREAK thinning:
    // the user's word is final here, silence included.
    const double lastAllowed = edited.stepCount() - 0.25;
    edited.events.erase (std::remove_if (edited.events.begin(), edited.events.end(),
        [lastAllowed] (const Event& e)
        { return e.pos < 0.0 || e.pos > lastAllowed || e.velocity <= 0.0f; }),
        edited.events.end());
    std::sort (edited.events.begin(), edited.events.end(), eventBefore);

    auto& opt = options[(size_t) index];
    opt.pattern = std::move (edited);
    opt.edited = true;
    opt.ready = false;
    enqueueBuild ({ index });
}

void OrchaAudioProcessor::resetOptionEdits (int index)
{
    if (index < 0 || index >= numOptions || ! options[(size_t) index].edited)
        return;
    options[(size_t) index].edited = false;
    options[(size_t) index].ready = false;
    enqueueBuild ({ index });
}

void OrchaAudioProcessor::makeTransition (int index, int target)
{
    if (index < 0 || index >= numOptions || target < 0 || target >= numOptions
        || index == target)
        return;
    const auto& src = options[(size_t) index];
    const auto& dst = options[(size_t) target];
    if (! src.present || ! dst.present)
        return;

    // Read both sides: the target's section chooses the ending grammar, the
    // tension delta chooses how hard the transition pushes.
    const auto dstMode = dst.pattern.settings.mode;
    const auto destination = dstMode == Mode::DROP ? Destination::ToDrop
                           : dstMode == Mode::BREAK ? Destination::ToBreak
                           : Destination::LoopBack;
    auto tensionOf = [] (const Pattern& p)
    {
        float sum = 0.0f;
        for (float t : TensionModel::measure (p))
            sum += t;
        return sum;
    };
    const float delta = tensionOf (dst.pattern) - tensionOf (src.pattern);

    auto s = src.pattern.settings;
    s.bars = juce::jmax (1, s.bars / 2);          // a transition is shorter
    s.energy = juce::jlimit (0.0f, 1.0f, s.energy + juce::jlimit (-0.25f, 0.35f,
                                                                  delta * 0.5f));
    // Same motif = recognizable source material; the ornament stream is
    // derived from BOTH cards, so the fill belongs to the pair.
    auto transition = LoopGenerator::generateV2 (
        src.pattern.seed,
        LoopGenerator::deriveSeed (src.pattern.ornamentSeed ^ dst.pattern.seed, 13),
        s, deriveTraits (samples, roleMap), destination);
    transition = PatternValidator::validate (std::move (transition));
    transition.name = src.pattern.name + ">" + juce::String (target + 1);
    transition.fxReverb = src.pattern.fxReverb;
    transition.fxDelay = src.pattern.fxDelay;
    transition.fxPump = src.pattern.fxPump;
    applyEditedPattern (index, std::move (transition));
}

OrchaAudioProcessor::ChainJob OrchaAudioProcessor::currentChain() const
{
    // The hearts define the chain: every favorited, ready card in slot
    // order becomes one seamless phrase.
    ChainJob job;
    juce::String id;
    for (const auto& opt : options)
        if (opt.favorite && opt.present && opt.ready && opt.loop != nullptr)
        {
            job.patterns.push_back (opt.pattern);
            id << juce::String::toHexString ((juce::int64) opt.pattern.seed) << '_'
               << juce::String::toHexString ((juce::int64) opt.pattern.ornamentSeed)
               << '_' << opt.fxReverb << '_' << opt.fxDelay << '_' << opt.fxPump << '_';
        }
    if (job.patterns.size() < 2)
        return {};

    id << juce::String (bpmAtomic.load(), 3) << '_' << srAtomic.load()
       << (pitchEnabled ? "_p" : "_n");
    // CHAIN2: the phrase is rendered as one timeline so each card's reverb
    // and delay tail crosses into the card after it. Older CHAIN_ files were
    // plain concatenations - a different sound, so a different name.
    job.file = RenderCache::cacheDirectory().getChildFile (
        "ORCHA_CHAIN2_" + juce::String::toHexString (id.hashCode64()) + ".wav");
    job.ctx.sampleRate = srAtomic.load();
    job.ctx.bpm = bpmAtomic.load();
    job.ctx.samples = samples;
    job.ctx.roleMap = roleMap;
    job.ctx.pitchEnabled = pitchEnabled;
    return job;
}

// Worker or message thread; pure function of the job.
juce::File OrchaAudioProcessor::renderChainJob (const ChainJob& job)
{
    if (job.patterns.size() < 2 || job.file == juce::File())
        return {};
    if (job.file.existsAsFile())
        return job.file;

    std::vector<const Pattern*> ptrs;
    for (const auto& p : job.patterns)
        ptrs.push_back (&p);
    auto joined = LoopRenderer::renderChain (ptrs, job.ctx);
    if (joined.getNumSamples() == 0)
        return {};

    // Written to a sibling first: a half-written chain must never be picked
    // up as a cache hit by the next drag. The scratch name is unique per
    // render because the background pass and a drag that overtook it can be
    // building the same phrase on two threads at once - whoever lands first
    // wins the move, and the loser finds the finished file and uses it.
    static std::atomic<int> scratchCounter { 0 };
    const auto tmp = job.file.getSiblingFile (
        job.file.getFileNameWithoutExtension() + "_"
        + juce::String (scratchCounter.fetch_add (1)) + ".part");
    tmp.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream (tmp.createOutputStream());
        if (stream == nullptr)
            return {};
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), job.ctx.sampleRate, 2, 24, {}, 0));
        if (writer == nullptr)
            return {};
        stream.release();
        writer->writeFromAudioSampleBuffer (joined, 0, joined.getNumSamples());
    }
    if (! tmp.moveFileTo (job.file))
    {
        tmp.deleteFile();
        return job.file.existsAsFile() ? job.file : juce::File();
    }
    return job.file;
}

void OrchaAudioProcessor::prepareChainAsync()
{
    // The phrase is rendered the moment the favorites change, not when the
    // user starts dragging: twelve wet four-bar cards take ~115 ms, and a
    // stall that lands on mouse-down can lose the drag gesture itself.
    auto job = currentChain();
    if (job.patterns.size() < 2 || job.file.existsAsFile())
        return;
    juce::WeakReference<OrchaAudioProcessor> self (this);
    pool.addJob ([self, job] { if (self != nullptr) renderChainJob (job); });
}

juce::File OrchaAudioProcessor::ensureChainWav()
{
    // Normally the background pass already left the file on disk; if the drag
    // beat it there, render it now rather than hand back nothing.
    return renderChainJob (currentChain());
}

void OrchaAudioProcessor::cleanOption (int index, int strength)
{
    if (index < 0 || index >= numOptions || ! options[(size_t) index].present)
        return;
    Pattern cleaned = options[(size_t) index].pattern;
    LoopGenerator::cleanPattern (cleaned, strength);
    applyEditedPattern (index, std::move (cleaned));
}

void OrchaAudioProcessor::setOptionEnding (int index, int endingOverride)
{
    if (index < 0 || index >= numOptions)
        return;
    auto& opt = options[(size_t) index];
    opt.endingOverride = juce::jlimit (-1, 3, endingOverride);
    if (! opt.present)
        return;
    opt.ready = false;
    if (opt.edited)
    {
        // Edited cards keep every user decision: only the transition region
        // is rewritten, deterministically from the card's own seeds.
        if (opt.endingOverride >= 0)
            LoopGenerator::applyEnding (opt.pattern,
                (Destination) opt.endingOverride,
                deriveTraits (samples, roleMap),
                opt.pattern.ornamentSeed ^ 0xE4D1E4D1ull);
        enqueueBuild ({ index }, {}, true);
    }
    else
    {
        // Generated cards re-generate from their stored seeds with the
        // forced destination - same identity, new ending.
        enqueueBuild ({ index });
    }
    notifyModel();
}

void OrchaAudioProcessor::setOptionFx (int index, float reverb, float delay, float pump)
{
    if (index < 0 || index >= numOptions)
        return;
    auto& opt = options[(size_t) index];
    opt.fxReverb = juce::jlimit (0.0f, 1.0f, reverb);
    opt.fxDelay = juce::jlimit (0.0f, 1.0f, delay);
    opt.fxPump = juce::jlimit (0.0f, 1.0f, pump);
    if (! opt.present)
        return;
    // Same pattern, new polish: render as-is, no regeneration.
    opt.ready = false;
    enqueueBuild ({ index }, {}, true);
}

void OrchaAudioProcessor::enqueueBuild (std::vector<int> indices,
                                        juce::StringArray extraSigs,
                                        bool forceExisting,
                                        bool freshBatch)
{
    if (indices.empty())
        return;

    // Everything a job needs, copied now on the message thread. The samples
    // are immutable shared pointers, so the copies are cheap and safe.
    struct BuildInput
    {
        int index;
        SeedPair seeds;
        juce::String name;
        bool useExisting = false;   // edited pattern: render as-is, no generate
        Pattern existing;
        float fxReverb = 0.0f, fxDelay = 0.0f, fxPump = 0.0f;
        int endingOverride = -1;                  // A8: -1 AUTO
    };
    std::vector<BuildInput> inputs;
    for (int i : indices)
    {
        juce::String name;
        const auto cardMode = pendingSeeds[(size_t) i].modeOv >= 0
            ? (Mode) pendingSeeds[(size_t) i].modeOv : settings.mode;
        name << modeName (cardMode) << ' '
             << juce::String (i + 1).paddedLeft ('0', 2);
        BuildInput in { i, pendingSeeds[(size_t) i], name, false, {} };
        if (options[(size_t) i].edited
            || (forceExisting && options[(size_t) i].present))
        {
            in.useExisting = true;
            in.existing = options[(size_t) i].pattern;
            in.name = in.existing.name.isNotEmpty() ? in.existing.name : name;
        }
        in.fxReverb = options[(size_t) i].fxReverb;
        in.fxDelay = options[(size_t) i].fxDelay;
        in.fxPump = options[(size_t) i].fxPump;
        in.endingOverride = options[(size_t) i].endingOverride;
        inputs.push_back (std::move (in));
    }

    // Signatures the new batch must differ from: every present option that is
    // NOT being rebuilt here (not just favorites - DROP 02 once re-rolled
    // into a twin of DROP 05 and looked frozen), plus the caller's extras.
    // An option must never collide with its own stored signature, or a state
    // restore would reseed it away from its saved pattern.
    juce::StringArray existingSigs = std::move (extraSigs);
    for (int i = 0; i < numOptions; ++i)
    {
        const auto& opt = options[(size_t) i];
        if (opt.present
            && std::find (indices.begin(), indices.end(), i) == indices.end())
            existingSigs.add (opt.pattern.signature());
    }

    LoopRenderer::Context ctx;
    ctx.sampleRate = srAtomic.load();
    ctx.bpm = bpmAtomic.load();
    ctx.samples = samples;
    ctx.roleMap = roleMap;
    ctx.pitchEnabled = pitchEnabled;
    lastRenderBpm = ctx.bpm;
    lastRenderSr = ctx.sampleRate;

    const auto settingsCopy = settings;
    const auto traits = deriveTraits (samples, roleMap);
    const int gen = generation.load();
    juce::WeakReference<OrchaAudioProcessor> self (this);
    pendingJobs.fetch_add (1);
    notifyModel();

    pool.addJob ([self, inputs, existingSigs, ctx, settingsCopy, traits, gen,
                  freshBatch, this]() mutable
    {
        // `this` is only touched through atomics here; object lifetime is
        // guarded by removeAllJobs in the destructor.

        // Phase 5: a FRESH batch does not accept the first valid random
        // pattern. A pool of symbolic candidates (6 per card) is generated,
        // hard-validated, scored, and the cards are selected jointly for
        // quality and diversity. Only the selected ones render. Restores and
        // re-renders never come here - their seeds are already chosen.
        std::vector<Pattern> chosen;
        if (freshBatch && inputs.size() >= 4)
        {
            // Phase A1: ONE source of truth for pool sizing, deterministic
            // prefix-stable expansion - the first 96 of a 192 run are the
            // same candidates as a 96 run.
            std::vector<Pattern> candidatePool;
            std::vector<MusicalScorer::Features> feats;
            std::vector<MusicalScorer::ScoreBreakdown> scores;
            const auto m0 = inputs[0].seeds.motif;
            const auto o0 = inputs[0].seeds.orn;
            auto growPoolTo = [&] (int n)
            {
                for (int j = (int) candidatePool.size();
                     j < n && generation.load() == gen; ++j)
                {
                    // Every 8th candidate is a transition (throw / deflate /
                    // stop), scored WITH its ending, so the dramatic persona
                    // slots can pick real transition cards.
                    const auto dest = j % 8 == 5 ? Destination::ToDrop
                                    : j % 8 == 6 ? Destination::ToBreak
                                    : j % 8 == 7 ? Destination::ToStop
                                                 : Destination::LoopBack;
                    auto pat = PatternValidator::validate (LoopGenerator::generateV2 (
                        LoopGenerator::deriveSeed (m0 ^ 0xB00B5ull, j),
                        LoopGenerator::deriveSeed (o0 ^ 0xCAFEull, j),
                        settingsCopy, traits, dest));
                    feats.push_back (MusicalScorer::extract (pat));
                    scores.push_back (MusicalScorer::score (pat, settingsCopy));
                    candidatePool.push_back (std::move (pat));
                }
            };
            std::vector<int> sel;
            for (int size : { CandidatePoolConfig::initialPoolSize,
                              CandidatePoolConfig::firstExpansionSize,
                              CandidatePoolConfig::finalExpansionSize })
            {
                growPoolTo (size);
                sel = MusicalScorer::selectDiverse (candidatePool, feats, scores,
                                                    (int) inputs.size(),
                                                    settingsCopy);
                if ((int) sel.size() >= (int) inputs.size())
                    break;
            }
            for (int idx : sel)
                chosen.push_back (candidatePool[(size_t) idx]);
        }

        size_t slotInBatch = 0;
        for (const auto& in : inputs)
        {
            const size_t mySlot = slotInBatch++;
            if (generation.load() != gen)
                break;

            Pattern pattern;
            if (in.useExisting)
            {
                // A user edit renders exactly as edited - no generation, no
                // diversity reseed, no validator second-guessing.
                pattern = in.existing;
            }
            else if (mySlot < chosen.size())
            {
                // Fresh batch: this card's pattern was already selected from
                // the scored pool; its seeds are stored for exact restore.
                pattern = chosen[mySlot];
            }
            else
            {
                // Reseed until this option differs meaningfully from everything
                // already on screen - "12 clearly different results" is a
                // promise. Only the ornament seed re-rolls, so a variation
                // request never loses its motif to the diversity check.
                auto makePattern = [&] (juce::uint64 m, juce::uint64 o)
                {
                    const auto dest = (Destination) juce::jlimit (0, 3,
                        in.endingOverride >= 0 ? in.endingOverride
                                               : in.seeds.dest);
                    auto local = settingsCopy;
                    if (in.seeds.modeOv >= 0)
                        local.mode = (Mode) juce::jlimit (0, 3, in.seeds.modeOv);
                    return in.seeds.algo >= 2
                        ? LoopGenerator::generateV2 (m, o, local, traits, dest)
                        : LoopGenerator::generate (m, o, local);
                };
                juce::uint64 orn = in.seeds.orn;
                for (int attempt = 0; attempt < 8; ++attempt)
                {
                    pattern = PatternValidator::validate (makePattern (in.seeds.motif, orn));
                    if (! existingSigs.contains (pattern.signature()))
                        break;
                    orn = LoopGenerator::deriveSeed (orn, 7777 + attempt);
                }
                // A motif so simple that no ornament seed can change it (the
                // "frozen card" report): as a last resort the motif itself
                // re-rolls, which always lands somewhere new.
                juce::uint64 motif = in.seeds.motif;
                for (int attempt = 0;
                     existingSigs.contains (pattern.signature()) && attempt < 8;
                     ++attempt)
                {
                    motif = LoopGenerator::deriveSeed (motif, 31337 + attempt);
                    pattern = PatternValidator::validate (makePattern (motif, orn));
                }
                existingSigs.add (pattern.signature());
            }
            pattern.name = in.name;
            pattern.fxReverb = in.fxReverb;
            pattern.fxDelay = in.fxDelay;
            pattern.fxPump = in.fxPump;

            auto loop = PreviewPlayer::Loop::Ptr (new PreviewPlayer::Loop());
            loop->buffer = LoopRenderer::render (pattern, ctx);
            loop->bpm = ctx.bpm;
            loop->bars = pattern.settings.bars;
            loop->optionIndex = in.index;
            const auto wav = RenderCache::write (loop->buffer, pattern, ctx.bpm,
                                                 ctx.sampleRate, ctx.pitchEnabled);

            const int index = in.index;
            const bool fromEdit = in.useExisting;
            juce::MessageManager::callAsync ([self, index, pattern, loop, wav, gen, fromEdit]
            {
                if (self == nullptr || self->generation.load() != gen)
                    return;
                auto& opt = self->options[(size_t) index];
                // A generated result must never clobber an edit the user made
                // while the batch was still in flight.
                if (opt.edited && ! fromEdit)
                    return;
                opt.pattern = pattern;
                opt.loop = loop;
                opt.wavFile = wav;
                opt.ready = true;
                opt.present = true;
                self->pendingSeeds[(size_t) index] = { pattern.seed, pattern.ornamentSeed,
                                                      pattern.algo,
                                                      (int) pattern.destination };
                // A playing card follows its refreshed audio seamlessly.
                if (self->preview.playingOption() == index)
                    self->preview.play (loop);
                self->notifyModel();
            });
        }

        pendingJobs.fetch_sub (1);
        juce::MessageManager::callAsync ([self]
        {
            if (self == nullptr)
                return;
            self->notifyModel();
            // The batch is in: if it touched a favorite, the phrase changed.
            if (! self->isGenerating())
                self->prepareChainAsync();
        });
    });
}

void OrchaAudioProcessor::toggleFavorite (int index)
{
    if (index < 0 || index >= numOptions)
        return;
    auto& opt = options[(size_t) index];
    opt.favorite = ! opt.favorite;
    notifyModel();
    prepareChainAsync();
}

void OrchaAudioProcessor::rerenderAtCurrentTempo()
{
    // Patterns stay, audio re-renders: tempo or sample-rate moved.
    std::vector<int> present;
    for (int i = 0; i < numOptions; ++i)
        if (options[(size_t) i].present)
        {
            pendingSeeds[(size_t) i] = { options[(size_t) i].pattern.seed,
                                         options[(size_t) i].pattern.ornamentSeed,
                                         options[(size_t) i].pattern.algo,
                                         (int) options[(size_t) i].pattern.destination,
                                         (int) options[(size_t) i].pattern.settings.mode
                                             == (int) settings.mode ? -1
                                             : (int) options[(size_t) i].pattern.settings.mode };
            present.push_back (i);
        }
    if (! present.empty() && anySampleLoaded())
        enqueueBuild (std::move (present));
}

// --- preview / drag ------------------------------------------------------------

void OrchaAudioProcessor::togglePlay (int index)
{
    if (index < 0 || index >= numOptions)
        return;
    if (preview.playingOption() == index)
        preview.stop();
    else if (options[(size_t) index].ready && options[(size_t) index].loop != nullptr)
        preview.play (options[(size_t) index].loop);
    notifyModel();
}

juce::File OrchaAudioProcessor::ensureWavFor (int index)
{
    if (index < 0 || index >= numOptions)
        return {};
    auto& opt = options[(size_t) index];
    if (! opt.present || opt.loop == nullptr)
        return {};
    if (opt.wavFile.existsAsFile())
        return opt.wavFile;
    // Cache was evicted: the render is tiny, write it back synchronously.
    opt.wavFile = RenderCache::write (opt.loop->buffer, opt.pattern,
                                      opt.loop->bpm, srAtomic.load(), pitchEnabled);
    return opt.wavFile;
}

void OrchaAudioProcessor::setPitchEnabled (bool enabled)
{
    if (pitchEnabled == enabled)
        return;
    pitchEnabled = enabled;
    // Same patterns, new render: pitch is a render-time decision.
    std::vector<int> present;
    for (int i = 0; i < numOptions; ++i)
        if (options[(size_t) i].present)
        {
            options[(size_t) i].ready = false;
            present.push_back (i);
        }
    if (! present.empty() && anySampleLoaded())
        enqueueBuild (std::move (present), {}, true);
    notifyModel();
}

juce::File OrchaAudioProcessor::ensureMidiFor (int index)
{
    if (index < 0 || index >= numOptions)
        return {};
    const auto& opt = options[(size_t) index];
    if (! opt.present || ! opt.ready || opt.loop == nullptr)
        return {};
    return MidiExporter::write (opt.pattern, opt.loop->bpm);
}

// --- audio ---------------------------------------------------------------------

void OrchaAudioProcessor::prepareToPlay (double sampleRate, int)
{
    // The timer notices the change and re-renders; prepareToPlay itself may
    // run off the message thread, so it only publishes the value.
    srAtomic.store (sampleRate);
}

bool OrchaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void OrchaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    double ppq = -1.0;
    bool hostPlaying = false;
    if (auto* playhead = getPlayHead())
    {
        if (auto pos = playhead->getPosition())
        {
            if (auto bpm = pos->getBpm())
                bpmAtomic.store (*bpm, std::memory_order_relaxed);
            if (auto q = pos->getPpqPosition())
                ppq = *q;
            hostPlaying = pos->getIsPlaying();
        }
    }
    ppqAtomic.store (ppq, std::memory_order_relaxed);
    playingAtomic.store (hostPlaying, std::memory_order_relaxed);

    preview.process (buffer, ppq, hostPlaying, getSampleRate());
}

// --- housekeeping --------------------------------------------------------------

void OrchaAudioProcessor::timerCallback()
{
    preview.releaseRetired();

    // Host tempo or sample rate drifted from the rendered ones: rebuild the
    // audio (same patterns, same seeds) once the current batch is done. A
    // 44.1->48 kHz change without this played every loop at the wrong pitch
    // and length.
    if (! isGenerating() && lastRenderBpm > 0.0
        && (std::abs (bpmAtomic.load() - lastRenderBpm) > 0.5
            || std::abs (srAtomic.load() - lastRenderSr) > 1.0))
        rerenderAtCurrentTempo();
}

void OrchaAudioProcessor::notifyModel()
{
    if (onModelChanged != nullptr)
        onModelChanged();
}

// --- state ---------------------------------------------------------------------

void OrchaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("ORCHA");
    root.setProperty ("schema", 4, nullptr);

    juce::ValueTree st ("settings");
    st.setProperty ("mode", modeName (settings.mode), nullptr);
    st.setProperty ("family", familyName (settings.family), nullptr);
    st.setProperty ("energy", settings.energy, nullptr);
    st.setProperty ("density", settings.density, nullptr);
    st.setProperty ("randomness", settings.randomness, nullptr);
    st.setProperty ("bars", settings.bars, nullptr);
    st.setProperty ("pitch", pitchEnabled, nullptr);
    root.appendChild (st, nullptr);

    juce::ValueTree ss ("samples");
    for (int i = 0; i < numSlots; ++i)
        if (const auto& s = samples[(size_t) i])
        {
            juce::ValueTree v ("sample");
            v.setProperty ("slot", i, nullptr);
            v.setProperty ("path", s->file.getFullPathName(), nullptr);
            v.setProperty ("role", roleName (s->userRole), nullptr);
            v.setProperty ("rev", transforms[(size_t) i].reverse, nullptr);
            v.setProperty ("trim", transforms[(size_t) i].trimTail, nullptr);
            v.setProperty ("cs", (double) transforms[(size_t) i].start, nullptr);
            v.setProperty ("ce", (double) transforms[(size_t) i].end, nullptr);
            v.setProperty ("fi", (double) transforms[(size_t) i].fadeIn, nullptr);
            v.setProperty ("fo", (double) transforms[(size_t) i].fadeOut, nullptr);
            ss.appendChild (v, nullptr);
        }
    root.appendChild (ss, nullptr);

    juce::ValueTree os ("options");
    os.setProperty ("tempo", lastRenderBpm > 0.0 ? lastRenderBpm : bpmAtomic.load(), nullptr);
    for (int i = 0; i < numOptions; ++i)
    {
        const auto& opt = options[(size_t) i];
        if (! opt.present && pendingSeeds[(size_t) i].motif == 0)
            continue;
        juce::ValueTree v ("option");
        v.setProperty ("index", i, nullptr);
        v.setProperty ("seed", juce::String::toHexString (
            (juce::int64) (opt.present ? opt.pattern.seed : pendingSeeds[(size_t) i].motif)), nullptr);
        v.setProperty ("orn", juce::String::toHexString (
            (juce::int64) (opt.present ? opt.pattern.ornamentSeed : pendingSeeds[(size_t) i].orn)), nullptr);
        v.setProperty ("name", opt.present ? opt.pattern.name
                                           : juce::String (modeName (settings.mode)) + " "
                                             + juce::String (i + 1).paddedLeft ('0', 2), nullptr);
        v.setProperty ("favorite", opt.favorite, nullptr);
        v.setProperty ("rvb", (double) opt.fxReverb, nullptr);
        v.setProperty ("dly", (double) opt.fxDelay, nullptr);
        v.setProperty ("pmp", (double) opt.fxPump, nullptr);
        v.setProperty ("algo", opt.present ? opt.pattern.algo
                                           : pendingSeeds[(size_t) i].algo, nullptr);
        v.setProperty ("dest", opt.present ? (int) opt.pattern.destination
                                           : pendingSeeds[(size_t) i].dest, nullptr);
        v.setProperty ("endov", opt.endingOverride, nullptr);
        if (pendingSeeds[(size_t) i].modeOv >= 0)
            v.setProperty ("modeov", pendingSeeds[(size_t) i].modeOv, nullptr);

        // A user-edited pattern cannot be rebuilt from seeds - store it whole.
        if (opt.edited)
        {
            v.setProperty ("edited", true, nullptr);
            v.setProperty ("swing", opt.pattern.swing, nullptr);
            v.setProperty ("bars", opt.pattern.settings.bars, nullptr);
            v.setProperty ("emode", modeName (opt.pattern.settings.mode), nullptr);
            v.setProperty ("efamily", familyName (opt.pattern.settings.family), nullptr);
            for (const auto& e : opt.pattern.events)
            {
                juce::ValueTree ev ("e");
                ev.setProperty ("p", e.pos, nullptr);
                ev.setProperty ("r", (int) e.role, nullptr);
                ev.setProperty ("v", e.velocity, nullptr);
                ev.setProperty ("m", e.microMs, nullptr);
                ev.setProperty ("ps", e.pitchSemis, nullptr);
                ev.setProperty ("g", e.gateSteps, nullptr);
                ev.setProperty ("a", e.protectedAnchor, nullptr);
                if (e.reverse)
                    ev.setProperty ("rev", true, nullptr);
                if (e.roll)
                    ev.setProperty ("roll", true, nullptr);
                v.appendChild (ev, nullptr);
            }
        }
        os.appendChild (v, nullptr);
    }
    root.appendChild (os, nullptr);

    juce::MemoryOutputStream mos (destData, false);
    root.writeToStream (mos);
}

void OrchaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! root.hasType ("ORCHA"))
        return;

    generation.fetch_add (1);   // anything in flight is now stale

    if (auto st = root.getChildWithName ("settings"); st.isValid())
    {
        const juce::String m = st.getProperty ("mode", "DROP");
        settings.mode = m == "BREAK" ? Mode::BREAK : m == "BUILD" ? Mode::BUILD
                      : m == "GROOVE" ? Mode::GROOVE : Mode::DROP;
        settings.family = familyFromName (st.getProperty ("family", "EDM").toString());
        settings.energy = (float) (double) st.getProperty ("energy", 0.6);
        settings.density = (float) (double) st.getProperty ("density", 0.5);
        settings.randomness = (float) (double) st.getProperty ("randomness", 0.3);
        settings.bars = juce::jlimit (1, 4, (int) st.getProperty ("bars", 1));
        pitchEnabled = st.getProperty ("pitch", true);
    }

    for (auto& opt : options)
        opt = {};
    pendingSeeds.fill ({});

    if (auto os = root.getChildWithName ("options"); os.isValid())
        for (const auto& v : os)
        {
            const int i = v.getProperty ("index", -1);
            if (i < 0 || i >= numOptions)
                continue;
            const auto motif = (juce::uint64) v.getProperty ("seed", "0")
                                   .toString().getHexValue64();
            // Schema 1 states carry one seed; the single-seed generate()
            // derived its ornament stream exactly like this, so old projects
            // reproduce their loops bit for bit.
            const juce::String ornHex = v.getProperty ("orn", "");
            const auto orn = ornHex.isNotEmpty()
                ? (juce::uint64) ornHex.getHexValue64()
                : LoopGenerator::deriveSeed (motif, 4242);
            // Projects saved before engine 2 carry no algo attribute: they
            // restore through the frozen v1 path, bit-for-bit, forever.
            pendingSeeds[(size_t) i] = { motif, orn, (int) v.getProperty ("algo", 1),
                                         (int) v.getProperty ("dest", 0),
                                         (int) v.getProperty ("modeov", -1) };
            options[(size_t) i].favorite = v.getProperty ("favorite", false);
            // 0.6.0 saved these as bools; a bool var reads back as 0/1.
            options[(size_t) i].fxReverb = (float) (double) v.getProperty ("rvb", 0.0);
            options[(size_t) i].fxDelay = (float) (double) v.getProperty ("dly", 0.0);
            options[(size_t) i].fxPump = (float) (double) v.getProperty ("pmp", 0.0);
            options[(size_t) i].endingOverride = v.getProperty ("endov", -1);

            if ((bool) v.getProperty ("edited", false))
            {
                Pattern p;
                p.seed = motif;
                p.ornamentSeed = orn;
                p.algo = pendingSeeds[(size_t) i].algo;
                p.name = v.getProperty ("name", "");
                p.swing = v.getProperty ("swing", 0.0);
                p.settings = settings;
                p.settings.bars = juce::jlimit (1, 4, (int) v.getProperty ("bars", 1));
                const juce::String em = v.getProperty ("emode", "DROP");
                p.settings.mode = em == "BREAK" ? Mode::BREAK : em == "BUILD" ? Mode::BUILD
                                : em == "GROOVE" ? Mode::GROOVE : Mode::DROP;
                for (const auto& ev : v)
                {
                    if (! ev.hasType ("e"))
                        continue;
                    Event e;
                    e.pos = ev.getProperty ("p", 0.0);
                    e.role = (Role) juce::jlimit (0, 4, (int) ev.getProperty ("r", 1));
                    e.velocity = (float) (double) ev.getProperty ("v", 0.7);
                    e.microMs = (float) (double) ev.getProperty ("m", 0.0);
                    e.pitchSemis = ev.getProperty ("ps", 0);
                    e.gateSteps = ev.getProperty ("g", 0.0);
                    e.protectedAnchor = ev.getProperty ("a", false);
                    e.reverse = ev.getProperty ("rev", false);
                    e.roll = ev.getProperty ("roll", false);
                    p.events.push_back (e);
                }
                options[(size_t) i].pattern = std::move (p);
                options[(size_t) i].edited = true;
            }
        }

    for (auto& s : samples)
        s = nullptr;
    for (auto& s : rawSamples)
        s = nullptr;
    transforms.fill ({});
    if (auto ss = root.getChildWithName ("samples"); ss.isValid())
        for (const auto& v : ss)
        {
            const int slot = v.getProperty ("slot", -1);
            // toString(), not a cast to String: MSVC rejects the cast outright
            // ("cannot convert from juce::var to juce::String") where clang
            // quietly picks var's conversion operator.
            const juce::File file (v.getProperty ("path", "").toString());
            if (slot >= 0 && slot < numSlots && file.existsAsFile())
            {
                auto& t = transforms[(size_t) slot];
                t.reverse = v.getProperty ("rev", false);
                t.trimTail = v.getProperty ("trim", false);
                t.start = (float) (double) v.getProperty ("cs", 0.0);
                // 0.7.0 stored a single "len" (kept head fraction).
                t.end = (float) (double) v.getProperty ("ce",
                            (double) v.getProperty ("len", 1.0));
                t.fadeIn = (float) (double) v.getProperty ("fi", 0.0);
                t.fadeOut = (float) (double) v.getProperty ("fo", 0.0);
                loadSampleAsync (slot, file);   // completion applies the transform
            }
        }

    notifyModel();
}

} // namespace orcha

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orcha::OrchaAudioProcessor();
}
