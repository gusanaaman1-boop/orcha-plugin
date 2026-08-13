#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/SampleLoader.h"
#include "Engine/SampleAnalyzer.h"
#include "Engine/LoopGenerator.h"
#include "Engine/PatternValidator.h"
#include "Engine/LoopRenderer.h"
#include "Engine/RenderCache.h"
#include "Engine/MidiExporter.h"

namespace orcha
{

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
                self->samples[(size_t) slot] = loaded;
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
    rolesChanged();
    notifyModel();
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
                                     LoopGenerator::deriveSeed (~master, i) };
        opt.ready = false;
        toBuild.push_back (i);
    }
    enqueueBuild (std::move (toBuild));
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
    pendingSeeds[(size_t) index] = {
        opt.present ? opt.pattern.seed : LoopGenerator::deriveSeed (fresh, index),
        LoopGenerator::deriveSeed (~fresh, index) };
    opt.ready = false;
    opt.edited = false;
    enqueueBuild ({ index });
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
    std::sort (edited.events.begin(), edited.events.end(),
               [] (const Event& a, const Event& b) { return a.pos < b.pos; });

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

void OrchaAudioProcessor::enqueueBuild (std::vector<int> indices)
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
    };
    std::vector<BuildInput> inputs;
    for (int i : indices)
    {
        juce::String name;
        name << modeName (settings.mode) << ' '
             << juce::String (i + 1).paddedLeft ('0', 2);
        BuildInput in { i, pendingSeeds[(size_t) i], name, false, {} };
        if (options[(size_t) i].edited)
        {
            in.useExisting = true;
            in.existing = options[(size_t) i].pattern;
            in.name = in.existing.name.isNotEmpty() ? in.existing.name : name;
        }
        inputs.push_back (std::move (in));
    }

    // Signatures the new batch must differ from: kept favorites that are NOT
    // being rebuilt here. An option must never collide with itself, or a
    // state restore would reseed it away from its saved pattern.
    juce::StringArray existingSigs;
    for (int i = 0; i < numOptions; ++i)
    {
        const auto& opt = options[(size_t) i];
        if (opt.favorite && opt.present
            && std::find (indices.begin(), indices.end(), i) == indices.end())
            existingSigs.add (opt.pattern.signature());
    }

    LoopRenderer::Context ctx;
    ctx.sampleRate = srAtomic.load();
    ctx.bpm = bpmAtomic.load();
    ctx.samples = samples;
    ctx.roleMap = roleMap;
    lastRenderBpm = ctx.bpm;
    lastRenderSr = ctx.sampleRate;

    const auto settingsCopy = settings;
    const int gen = generation.load();
    juce::WeakReference<OrchaAudioProcessor> self (this);
    pendingJobs.fetch_add (1);
    notifyModel();

    pool.addJob ([self, inputs, existingSigs, ctx, settingsCopy, gen, this]() mutable
    {
        // `this` is only touched through atomics here; object lifetime is
        // guarded by removeAllJobs in the destructor.
        for (const auto& in : inputs)
        {
            if (generation.load() != gen)
                break;

            Pattern pattern;
            if (in.useExisting)
            {
                // A user edit renders exactly as edited - no generation, no
                // diversity reseed, no validator second-guessing.
                pattern = in.existing;
            }
            else
            {
                // Reseed until this option differs meaningfully from everything
                // already on screen - "12 clearly different results" is a
                // promise. Only the ornament seed re-rolls, so a variation
                // request never loses its motif to the diversity check.
                juce::uint64 orn = in.seeds.orn;
                for (int attempt = 0; attempt < 8; ++attempt)
                {
                    pattern = PatternValidator::validate (
                        LoopGenerator::generate (in.seeds.motif, orn, settingsCopy));
                    if (! existingSigs.contains (pattern.signature()))
                        break;
                    orn = LoopGenerator::deriveSeed (orn, 7777 + attempt);
                }
                existingSigs.add (pattern.signature());
            }
            pattern.name = in.name;

            auto loop = PreviewPlayer::Loop::Ptr (new PreviewPlayer::Loop());
            loop->buffer = LoopRenderer::render (pattern, ctx);
            loop->bpm = ctx.bpm;
            loop->bars = pattern.settings.bars;
            loop->optionIndex = in.index;
            const auto wav = RenderCache::write (loop->buffer, pattern, ctx.bpm, ctx.sampleRate);

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
                self->pendingSeeds[(size_t) index] = { pattern.seed, pattern.ornamentSeed };
                // A playing card follows its refreshed audio seamlessly.
                if (self->preview.playingOption() == index)
                    self->preview.play (loop);
                self->notifyModel();
            });
        }

        pendingJobs.fetch_sub (1);
        juce::MessageManager::callAsync ([self]
        {
            if (self != nullptr)
                self->notifyModel();
        });
    });
}

void OrchaAudioProcessor::rerenderAtCurrentTempo()
{
    // Patterns stay, audio re-renders: tempo or sample-rate moved.
    std::vector<int> present;
    for (int i = 0; i < numOptions; ++i)
        if (options[(size_t) i].present)
        {
            pendingSeeds[(size_t) i] = { options[(size_t) i].pattern.seed,
                                         options[(size_t) i].pattern.ornamentSeed };
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
                                      opt.loop->bpm, srAtomic.load());
    return opt.wavFile;
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
    root.setProperty ("schema", 2, nullptr);

    juce::ValueTree st ("settings");
    st.setProperty ("mode", modeName (settings.mode), nullptr);
    st.setProperty ("family", familyName (settings.family), nullptr);
    st.setProperty ("energy", settings.energy, nullptr);
    st.setProperty ("density", settings.density, nullptr);
    st.setProperty ("randomness", settings.randomness, nullptr);
    st.setProperty ("bars", settings.bars, nullptr);
    root.appendChild (st, nullptr);

    juce::ValueTree ss ("samples");
    for (int i = 0; i < numSlots; ++i)
        if (const auto& s = samples[(size_t) i])
        {
            juce::ValueTree v ("sample");
            v.setProperty ("slot", i, nullptr);
            v.setProperty ("path", s->file.getFullPathName(), nullptr);
            v.setProperty ("role", roleName (s->userRole), nullptr);
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
        const juce::String f = st.getProperty ("family", "EDM");
        settings.family = f == "ARABIC" ? Family::ARABIC
                        : f == "MEDITERRANEAN" ? Family::MEDITERRANEAN
                        : f == "AFRO" ? Family::AFRO
                        : f == "HYBRID" ? Family::HYBRID : Family::EDM;
        settings.energy = (float) (double) st.getProperty ("energy", 0.6);
        settings.density = (float) (double) st.getProperty ("density", 0.5);
        settings.randomness = (float) (double) st.getProperty ("randomness", 0.3);
        settings.bars = juce::jlimit (1, 4, (int) st.getProperty ("bars", 1));
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
            pendingSeeds[(size_t) i] = { motif, orn };
            options[(size_t) i].favorite = v.getProperty ("favorite", false);

            if ((bool) v.getProperty ("edited", false))
            {
                Pattern p;
                p.seed = motif;
                p.ornamentSeed = orn;
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
    if (auto ss = root.getChildWithName ("samples"); ss.isValid())
        for (const auto& v : ss)
        {
            const int slot = v.getProperty ("slot", -1);
            const juce::File file ((juce::String) v.getProperty ("path", ""));
            if (slot >= 0 && slot < numSlots && file.existsAsFile())
                loadSampleAsync (slot, file);   // completion triggers the rebuild
        }

    notifyModel();
}

} // namespace orcha

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orcha::OrchaAudioProcessor();
}
