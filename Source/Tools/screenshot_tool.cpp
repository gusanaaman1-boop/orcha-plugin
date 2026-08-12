// Drives the real processor + editor the way a host would, saves PNGs of the
// UI, and checks the state round-trip. Usage:
//   OrchaShot <outDir> [sample1.wav [sample2.wav [sample3.wav]]]

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../PluginEditor.h"

using namespace orcha;

static void pumpUntil (std::function<bool()> done, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! done() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::String ("ui-shots"));
    outDir.createDirectory();

    auto processor = std::make_unique<OrchaAudioProcessor>();
    processor->prepareToPlay (48000.0, 512);

    // Feed a few audio blocks so the tempo/transport atomics settle.
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    processor->processBlock (block, midi);

    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (processor->createEditor());
    editor->setSize (1100, 700);

    auto shoot = [&] (const juce::String& name)
    {
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::PNGImageFormat png;
        const auto file = outDir.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (out.openedOk())
            png.writeImageToStream (image, out);
        std::cout << "wrote " << file.getFullPathName() << "\n";
    };

    shoot ("01-empty.png");

    int loaded = 0;
    for (int i = 0; i < juce::jmin (3, argc - 2); ++i)
    {
        const juce::File f { juce::String (argv[2 + i]) };
        if (f.existsAsFile())
        {
            processor->loadSampleAsync (i, f);
            ++loaded;
        }
    }
    pumpUntil ([&]
    {
        int have = 0;
        for (int i = 0; i < 3; ++i)
            if (processor->getSample (i) != nullptr)
                ++have;
        return have >= loaded;
    }, 15000);
    shoot ("02-samples.png");

    if (loaded > 0)
    {
        processor->generateAll();
        pumpUntil ([&]
        {
            if (processor->isGenerating())
                return false;
            for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
                if (! processor->option (i).ready)
                    return false;
            return true;
        }, 30000);
        pumpUntil ([] { return false; }, 300);   // let repaints land
        shoot ("03-generated.png");

        // --- state round-trip: seeds, favorites, settings must survive ---------
        processor->toggleFavorite (2);
        processor->toggleFavorite (7);
        juce::MemoryBlock state;
        processor->getStateInformation (state);

        auto restored = std::make_unique<OrchaAudioProcessor>();
        restored->prepareToPlay (48000.0, 512);
        restored->setStateInformation (state.getData(), (int) state.getSize());
        pumpUntil ([&]
        {
            if (restored->isGenerating())
                return false;
            for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
                if (! restored->option (i).ready)
                    return false;
            return true;
        }, 30000);

        int failures = 0;
        for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
        {
            const auto& a = processor->option (i);
            const auto& b = restored->option (i);
            if (a.pattern.seed != b.pattern.seed)
            {
                std::cout << "STATE FAIL: option " << i << " seed mismatch\n";
                ++failures;
            }
            if (a.favorite != b.favorite)
            {
                std::cout << "STATE FAIL: option " << i << " favorite mismatch\n";
                ++failures;
            }
            if (a.ready && b.ready
                && a.pattern.signature() != b.pattern.signature())
            {
                std::cout << "STATE FAIL: option " << i << " pattern mismatch\n";
                ++failures;
            }
        }
        if (restored->settings.bars != processor->settings.bars
            || restored->settings.mode != processor->settings.mode)
        {
            std::cout << "STATE FAIL: settings mismatch\n";
            ++failures;
        }

        std::cout << (failures == 0 ? "STATE OK" : "STATE FAILED") << "\n";
        editor.reset();
        processor.reset();
        restored.reset();
        return failures == 0 ? 0 : 1;
    }

    editor.reset();
    processor.reset();
    return 0;
}
