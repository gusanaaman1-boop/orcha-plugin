#include "RenderCache.h"

namespace orcha
{

juce::File RenderCache::cacheDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("ORCHA");
    dir.createDirectory();
    return dir;
}

juce::File RenderCache::fileFor (const Pattern& p, double bpm, double sampleRate)
{
    // The name carries the full identity: seed, settings, tempo, rate, bars.
    juce::String id;
    id << juce::String::toHexString ((juce::int64) p.seed) << '_'
       << modeName (p.settings.mode) << '_'
       << familyName (p.settings.family) << '_'
       << p.settings.bars << "bars_"
       << juce::roundToInt (bpm * 100.0) << '_'
       << juce::roundToInt (sampleRate) << '_'
       << juce::roundToInt (p.settings.energy * 100.0f) << '_'
       << juce::roundToInt (p.settings.density * 100.0f) << '_'
       << juce::roundToInt (p.settings.randomness * 100.0f);

    // Cubase shows the file name in the pool - keep it meaningful first.
    return cacheDirectory().getChildFile ("ORCHA_" + juce::String (modeName (p.settings.mode))
                                          + "_" + p.name.replaceCharacter (' ', '_')
                                          + "_" + juce::String::toHexString (id.hashCode64())
                                          + ".wav");
}

juce::File RenderCache::write (const juce::AudioBuffer<float>& buffer, const Pattern& p,
                               double bpm, double sampleRate)
{
    const auto file = fileFor (p, bpm, sampleRate);
    if (file.existsAsFile())
        return file;

    const auto tmp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".part");
    tmp.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (tmp.createOutputStream());
    if (stream == nullptr)
        return {};

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate,
                             (unsigned int) buffer.getNumChannels(), 24, {}, 0));
    if (writer == nullptr)
        return {};
    stream.release();   // writer owns it now

    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
    {
        writer.reset();
        tmp.deleteFile();
        return {};
    }
    writer.reset();     // flush before rename

    // Atomic-ish publish: a drag can never pick up a half-written file.
    if (! tmp.moveFileTo (file))
    {
        tmp.deleteFile();
        return file.existsAsFile() ? file : juce::File();
    }
    return file;
}

void RenderCache::cleanupStale()
{
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (7);
    for (const auto& f : cacheDirectory().findChildFiles (juce::File::findFiles, false, "*.wav"))
        if (f.getLastModificationTime() < cutoff)
            f.deleteFile();
}

} // namespace orcha
