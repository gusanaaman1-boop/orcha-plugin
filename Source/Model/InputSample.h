#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace orcha
{

// Rhythmic job a sample can do inside a generated loop. AUTO means "let the
// analyzer decide"; the resolved role is stored separately so the user's
// choice survives re-analysis.
enum class Role { AUTO = 0, LOW, MID, HIGH, FX };

inline const char* roleName (Role r)
{
    switch (r)
    {
        case Role::AUTO: return "AUTO";
        case Role::LOW:  return "LOW";
        case Role::MID:  return "MID";
        case Role::HIGH: return "HIGH";
        case Role::FX:   return "FX";
    }
    return "AUTO";
}

inline Role roleFromName (const juce::String& s)
{
    if (s == "LOW")  return Role::LOW;
    if (s == "MID")  return Role::MID;
    if (s == "HIGH") return Role::HIGH;
    if (s == "FX")   return Role::FX;
    return Role::AUTO;
}

// What SampleAnalyzer learned about a file. All values are host-independent.
struct SampleAnalysis
{
    double durationSeconds = 0.0;
    float  transientStrength = 0.0f;  // 0 soft .. 1 very percussive attack
    float  spectralCentroidHz = 0.0f;
    float  lowEnergyRatio = 0.0f;     // energy below 200 Hz / total energy
    bool   isOneShot = true;          // short decaying hit vs. loop-like file
};

// An immutable loaded sample. Instances are shared by pointer between the
// message thread, worker jobs and the renderer; nobody mutates one after
// construction, which is what makes the handoff real-time- and thread-safe.
struct InputSample
{
    juce::File file;
    juce::String name;
    juce::AudioBuffer<float> buffer;   // resampled to nothing - kept at source rate
    double sourceSampleRate = 44100.0;
    SampleAnalysis analysis;
    Role userRole = Role::AUTO;        // what the user picked on the card
    Role resolvedRole = Role::LOW;     // what the analyzer assigned

    using Ptr = std::shared_ptr<const InputSample>;
};

} // namespace orcha
