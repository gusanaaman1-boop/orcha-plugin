#include "SampleLoader.h"
#include "SampleAnalyzer.h"

namespace orcha
{

bool SampleLoader::isSupported (const juce::File& file)
{
    return file.hasFileExtension ("wav;aiff;aif;flac");
}

InputSample::Ptr SampleLoader::load (const juce::File& file)
{
    if (! file.existsAsFile() || ! isSupported (file))
        return nullptr;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    // 60 s is far beyond any rhythm one-shot or short loop; refuse silently
    // huge files instead of eating memory.
    const juce::int64 maxLen = (juce::int64) (reader->sampleRate * 60.0);
    const int numSamples = (int) juce::jmin (reader->lengthInSamples, maxLen);
    const int numChannels = juce::jmin (2, (int) reader->numChannels);

    auto sample = std::make_shared<InputSample>();
    sample->buffer.setSize (numChannels, numSamples);
    if (! reader->read (&sample->buffer, 0, numSamples, 0, true, numChannels > 1))
        return nullptr;

    sample->file = file;
    sample->name = file.getFileName();
    sample->sourceSampleRate = reader->sampleRate;
    sample->analysis = SampleAnalyzer::analyze (sample->buffer, reader->sampleRate);
    return sample;
}

} // namespace orcha
