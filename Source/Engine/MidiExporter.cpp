#include "MidiExporter.h"
#include "RenderCache.h"

namespace orcha
{

int MidiExporter::noteForRole (Role role)
{
    switch (role)
    {
        case Role::LOW:  return 36;
        case Role::MID:  return 38;
        case Role::HIGH: return 42;
        case Role::FX:   return 49;
        case Role::AUTO: break;
    }
    return 36;
}

juce::File MidiExporter::fileFor (const Pattern& p, double bpm)
{
    return RenderCache::fileFor (p, bpm, 0.0).withFileExtension (".mid");
}

juce::File MidiExporter::write (const Pattern& p, double bpm)
{
    const auto file = fileFor (p, bpm);
    if (file.existsAsFile())
        return file;

    constexpr int ppq = 960;                 // ticks per quarter
    const double ticksPerStep = ppq / 4.0;   // one 16th

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (
        (int) std::round (60'000'000.0 / bpm)), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    const double msToTicks = (bpm / 60.0) * ppq / 1000.0;
    for (const auto& e : p.events)
    {
        const bool oddStep = (juce::roundToInt (std::floor (e.pos)) % 2) == 1
                             && std::abs (e.pos - std::floor (e.pos)) < 0.01;
        double ticks = e.pos * ticksPerStep
                     + (oddStep ? p.swing * ticksPerStep * 0.5 : 0.0)
                     + e.microMs * msToTicks;
        if (ticks < 0.0)
            ticks = 0.0;

        const double lenSteps = e.gateSteps > 0.0 ? e.gateSteps : 1.0;
        const int note = noteForRole (e.role);
        const auto velocity = (juce::uint8) juce::jlimit (1, 127,
            juce::roundToInt (e.velocity * 127.0f));

        seq.addEvent (juce::MidiMessage::noteOn (10, note, velocity), ticks);
        seq.addEvent (juce::MidiMessage::noteOff (10, note),
                      ticks + lenSteps * ticksPerStep * 0.9);
    }
    seq.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ppq);
    midi.addTrack (seq);

    const auto tmp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".part");
    tmp.deleteFile();
    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk() || ! midi.writeTo (out))
        {
            tmp.deleteFile();
            return {};
        }
    }
    if (! tmp.moveFileTo (file))
    {
        tmp.deleteFile();
        return file.existsAsFile() ? file : juce::File();
    }
    return file;
}

} // namespace orcha
