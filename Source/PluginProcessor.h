#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Model/InputSample.h"
#include "Engine/Pattern.h"
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
    };

    InputSample::Ptr getSample (int slot) const   { return samples[(size_t) slot]; }
    void loadSampleAsync (int slot, const juce::File& file);
    void clearSample (int slot);
    void setUserRole (int slot, Role role);

    GeneratorSettings settings;         // editor edits directly, then generates

    const Option& option (int index) const        { return options[(size_t) index]; }
    void generateAll();                 // fresh seeds; favorites keep theirs
    void regenerateOption (int index);  // new seed for one card
    void toggleFavorite (int index)     { options[(size_t) index].favorite = ! options[(size_t) index].favorite; notifyModel(); }
    void togglePlay (int index);
    bool anySampleLoaded() const;
    bool isGenerating() const           { return pendingJobs.load() > 0; }
    // A build job is on its way to filling this slot.
    bool optionBusy (int index) const   { return pendingSeeds[(size_t) index] != 0
                                              && ! options[(size_t) index].ready
                                              && isGenerating(); }
    int  playingOption() const          { return preview.playingOption(); }

    // For drag-out: the option's WAV, written if the cache lost it.
    juce::File ensureWavFor (int index);

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
    // already be stored in pendingSeeds for those indices.
    void enqueueBuild (std::vector<int> indices);
    void rerenderAtCurrentTempo();

    std::vector<InputSample::Ptr> samples { numSlots, nullptr };
    RoleMap roleMap;
    std::array<Option, numOptions> options;
    std::array<juce::uint64, numOptions> pendingSeeds {};

    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (2)
                                                     .withThreadName ("ORCHA worker") };
    std::atomic<int> generation { 0 };      // bumps cancel stale job results
    std::atomic<int> pendingJobs { 0 };

    PreviewPlayer preview;
    std::atomic<double> bpmAtomic { 120.0 };
    std::atomic<double> ppqAtomic { -1.0 };
    std::atomic<bool> playingAtomic { false };
    double lastRenderBpm = 0.0;             // message thread
    double lastSampleRate = 48000.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (OrchaAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrchaAudioProcessor)
};

} // namespace orcha
