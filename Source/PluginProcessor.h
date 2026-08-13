#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Model/InputSample.h"
#include "Engine/Pattern.h"
#include "Engine/SampleTransform.h"
#include "Playback/PreviewPlayer.h"

namespace orcha
{

// ORCHA: up to three samples in, twelve seeded loop options out. All heavy
// work (decode, analysis, generation, rendering, file I/O) runs on the
// ThreadPool; processBlock only mixes the active preview loop.
class OrchaAudioProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    static constexpr int numSlots = 3;
    static constexpr int numOptions = 12;

    OrchaAudioProcessor();
    ~OrchaAudioProcessor() override;

    // --- model, message thread only -------------------------------------------
    struct Option
    {
        Pattern pattern;
        juce::File wavFile;
        PreviewPlayer::Loop::Ptr loop;  // rendered preview audio (also waveform)
        bool favorite = false;
        bool ready = false;             // false while a job is rebuilding it
        bool present = false;           // slot has ever been generated
        bool edited = false;            // user edits win over the generator
        float fxReverb = 0.0f;          // card-level polish amounts (0 = off),
        float fxDelay = 0.0f;           // survive regeneration
    };

    InputSample::Ptr getSample (int slot) const   { return samples[(size_t) slot]; }
    // The pristine decode, for the sample-cut editor's full waveform.
    InputSample::Ptr getRawSample (int slot) const { return rawSamples[(size_t) slot]; }
    // Which loaded slot serves this role right now (-1 when nothing loaded).
    int slotForRole (Role r) const                { return roleMap.slotFor (r); }
    void loadSampleAsync (int slot, const juce::File& file);
    void clearSample (int slot);
    void setUserRole (int slot, Role role);

    // Non-destructive input edits: the raw decode is kept, toggling is exact.
    SampleTransform::Settings getTransform (int slot) const { return transforms[(size_t) slot]; }
    void setTransform (int slot, SampleTransform::Settings t);

    GeneratorSettings settings;         // editor edits directly, then generates

    const Option& option (int index) const        { return options[(size_t) index]; }
    void generateAll();                 // fresh seeds; favorites keep theirs
    // Variation: keeps the card's motif seed (same groove) and re-rolls only
    // the ornament seed. A card that was never generated gets both fresh.
    // Discards manual edits - a fresh take starts from the generator.
    void regenerateOption (int index);

    // Step-editor support: replace the option's pattern with the user's
    // edit and re-render it. Edited patterns survive tempo/sample-rate
    // re-renders and are stored verbatim in the plug-in state.
    void applyEditedPattern (int index, Pattern edited);
    // Back to the generated version (same seeds), dropping the edits.
    void resetOptionEdits (int index);
    // Baked-in reverb/delay amounts (0..1) for one card; re-renders the same
    // pattern without regenerating it.
    void setOptionFx (int index, float reverb, float delay);
    void toggleFavorite (int index)     { options[(size_t) index].favorite = ! options[(size_t) index].favorite; notifyModel(); }
    void togglePlay (int index);
    bool anySampleLoaded() const;
    bool isGenerating() const           { return pendingJobs.load() > 0; }

    // Global PITCH switch: off re-renders everything without pitch moves.
    bool isPitchEnabled() const         { return pitchEnabled; }
    void setPitchEnabled (bool enabled);
    // Playhead of the playing card, 0..1.
    float previewFraction() const       { return preview.playbackFraction(); }
    // A build job is on its way to filling this slot.
    bool optionBusy (int index) const   { return pendingSeeds[(size_t) index].motif != 0
                                              && ! options[(size_t) index].ready
                                              && isGenerating(); }
    int  playingOption() const          { return preview.playingOption(); }

    // For drag-out: the option's WAV / MIDI, written if the cache lost them.
    juce::File ensureWavFor (int index);
    juce::File ensureMidiFor (int index);

    std::function<void()> onModelChanged;   // editor hook, message thread

    double currentBpm() const           { return bpmAtomic.load(); }

    // --- juce::AudioProcessor -------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override               { return true; }
    const juce::String getName() const override   { return "ORCHA"; }
    bool acceptsMidi() const override             { return false; }
    bool producesMidi() const override            { return false; }
    double getTailLengthSeconds() const override  { return 0.0; }
    int getNumPrograms() override                 { return 1; }
    int getCurrentProgram() override              { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    void timerCallback() override;
    void notifyModel();
    void rolesChanged();
    // Generates + renders the given option indices on the pool. Seeds must
    // already be stored in pendingSeeds for those indices. extraSigs are
    // signatures the new results must additionally differ from (a variation
    // must differ from its own previous take).
    // forceExisting renders each option's current pattern as-is (FX toggles),
    // without requiring the edited flag. freshBatch marks a NEW generation:
    // only then does the Phase 5 candidate pool + scored selection run -
    // restores and re-renders reproduce their stored seeds exactly.
    void enqueueBuild (std::vector<int> indices,
                       juce::StringArray extraSigs = {},
                       bool forceExisting = false,
                       bool freshBatch = false);
    void rerenderAtCurrentTempo();

    // algo: which generator rebuilt this slot's pattern. Restored v0.9.0
    // projects keep 1 (the frozen engine); new generations use 2.
    struct SeedPair { juce::uint64 motif = 0, orn = 0; int algo = 2; int dest = 0; };

    std::vector<InputSample::Ptr> samples { numSlots, nullptr };   // transformed
    std::vector<InputSample::Ptr> rawSamples { numSlots, nullptr };
    std::array<SampleTransform::Settings, numSlots> transforms {};
    RoleMap roleMap;
    std::array<Option, numOptions> options;
    std::array<SeedPair, numOptions> pendingSeeds {};

    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (2)
                                                     .withThreadName ("ORCHA worker") };
    std::atomic<int> generation { 0 };      // bumps cancel stale job results
    std::atomic<int> pendingJobs { 0 };

    PreviewPlayer preview;
    std::atomic<double> bpmAtomic { 120.0 };
    std::atomic<double> ppqAtomic { -1.0 };
    std::atomic<bool> playingAtomic { false };
    double lastRenderBpm = 0.0;             // message thread
    double lastRenderSr = 0.0;              // message thread
    bool pitchEnabled = true;               // message thread; renders honor it
    std::atomic<double> srAtomic { 48000.0 };   // written by prepareToPlay

    JUCE_DECLARE_WEAK_REFERENCEABLE (OrchaAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrchaAudioProcessor)
};

} // namespace orcha
