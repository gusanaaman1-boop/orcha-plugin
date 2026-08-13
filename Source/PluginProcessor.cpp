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
    // ornament seed re-rolls. A never-generated card starts from scratch.
    pendingSeeds[(size_t) index] = {
        opt.present ? opt.pattern.seed : LoopGenerator::deriveSeed (fresh, index),
        LoopGenerator::deriveSeed (~fresh, index) };
    opt.ready = false;
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
    };
    std::vector<BuildInput> inputs;
    for (int i : indices)
    {
        juce::String name;
        name << modeName (settings.mode) << ' '
             << juce::String (i + 1).paddedLeft ('0', 2);
        inputs.push_back ({ i, pendingSeeds[(size_t) i], name });
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

            // Reseed until this option differs meaningfully from everything
            // already on screen - "12 clearly different results" is a promise.
            // Only the ornament seed re-rolls, so a variation request never
            // loses its motif to the diversity check.
            juce::uint64 orn = in.seeds.orn;
            Pattern pattern;
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                pattern = PatternValidator::validate (
                    LoopGenerator::generate (in.seeds.motif, orn, settingsCopy));
                if (! existingSigs.contains (pattern.signature()))
                    break;
                orn = LoopGenerator::deriveSeed (orn, 7777 + attempt);
            }
            existingSigs.add (pattern.signature());
            pattern.name = in.name;

            auto loop = PreviewPlayer::Loop::Ptr (new PreviewPlayer::Loop());
            loop->buffer = LoopRenderer::render (pattern, ctx);
            loop->bpm = ctx.bpm;
            loop->bars = pattern.settings.bars;
            loop->optionIndex = in.index;
            const auto wav = RenderCache::write (loop->buffer, pattern, ctx.bpm, ctx.sampleRate);

            const int index = in.index;
            juce::MessageManager::callAsync ([self, index, pattern, loop, wav, gen]
            {
                if (self == nullptr || self->generation.load() != gen)
                    return;
                auto& opt = self->options[(size_t) index];
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
