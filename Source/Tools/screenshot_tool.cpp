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

        if (auto* orchaEditor = dynamic_cast<OrchaAudioProcessorEditor*> (editor.get()))
        {
            orchaEditor->openEditPanelFor (2);
            pumpUntil ([] { return false; }, 200);
            shoot ("04-edit-panel.png");

            // The sample cutting room, with a real crop + fades visible.
            auto cutSet = processor->getTransform (0);
            cutSet.start = 0.15f;
            cutSet.end = 0.8f;
            cutSet.fadeIn = 0.1f;
            cutSet.fadeOut = 0.2f;
            processor->setTransform (0, cutSet);
            orchaEditor->openSamplePanelFor (0);
            pumpUntil ([] { return false; }, 300);
            shoot ("05-sample-cut.png");
            cutSet = {};
            processor->setTransform (0, cutSet);
            orchaEditor->openSamplePanelFor (0);   // leave it consistent
            pumpUntil ([] { return false; }, 200);
        }

        int failuresEarly = 0;
        // --- regenerate must visibly change the card (the DROP 02 bug) ---------
        for (int idx : { 1, 5 })
        {
            const auto before = processor->option (idx).pattern.signature();
            processor->regenerateOption (idx);
            pumpUntil ([&] { return processor->option (idx).ready; }, 15000);
            if (processor->option (idx).pattern.signature() == before)
            {
                std::cout << "REGEN FAIL: option " << idx << " did not change\n";
                ++failuresEarly;
            }
        }
        std::cout << (failuresEarly == 0 ? "REGEN OK" : "REGEN FAILED") << "\n";

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

        int failures = failuresEarly;
        for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
        {
            const auto& a = processor->option (i);
            const auto& b = restored->option (i);
            if (a.pattern.seed != b.pattern.seed
                || a.pattern.ornamentSeed != b.pattern.ornamentSeed)
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

        // --- edited pattern round-trip -----------------------------------------
        // A manual edit must survive save/restore verbatim, not be regenerated.
        {
            auto edited = processor->option (0).pattern;
            Event extra;
            extra.pos = 3.0;
            extra.role = Role::HIGH;
            extra.velocity = 0.42f;
            edited.events.push_back (extra);
            processor->applyEditedPattern (0, edited);
            pumpUntil ([&] { return processor->option (0).ready; }, 15000);

            juce::MemoryBlock editState;
            processor->getStateInformation (editState);
            auto restored2 = std::make_unique<OrchaAudioProcessor>();
            restored2->prepareToPlay (48000.0, 512);
            restored2->setStateInformation (editState.getData(), (int) editState.getSize());
            pumpUntil ([&] { return restored2->option (0).ready; }, 30000);

            if (! restored2->option (0).edited)
            {
                std::cout << "STATE FAIL: edited flag lost\n";
                ++failures;
            }
            if (restored2->option (0).pattern.signature()
                    != processor->option (0).pattern.signature())
            {
                std::cout << "STATE FAIL: edited pattern not restored verbatim\n";
                ++failures;
            }
            restored2.reset();
        }

        std::cout << (failures == 0 ? "STATE OK" : "STATE FAILED") << "\n";

        // --- F5: corrupted / truncated / garbage state must fail SAFELY --------
        {
            juce::MemoryBlock good;
            processor->getStateInformation (good);
            auto fuzzTarget = std::make_unique<OrchaAudioProcessor>();
            fuzzTarget->prepareToPlay (48000.0, 512);
            // Truncated at every eighth of its length.
            for (int cut = 1; cut < 8; ++cut)
                fuzzTarget->setStateInformation (good.getData(),
                    (int) (good.getSize() * (size_t) cut / 8));
            // Bit-flipped copy.
            juce::MemoryBlock bad (good);
            auto* bytes = static_cast<juce::uint8*> (bad.getData());
            for (size_t i = 0; i < bad.getSize(); i += 7)
                bytes[i] ^= 0x5A;
            fuzzTarget->setStateInformation (bad.getData(), (int) bad.getSize());
            // Pure garbage.
            juce::MemoryBlock junk (513);
            auto* j = static_cast<juce::uint8*> (junk.getData());
            for (size_t i = 0; i < junk.getSize(); ++i)
                j[i] = (juce::uint8) (i * 37 + 11);
            fuzzTarget->setStateInformation (junk.getData(), (int) junk.getSize());
            fuzzTarget->setStateInformation (nullptr, 0);
            pumpUntil ([] { return false; }, 300);
            fuzzTarget.reset();
            std::cout << "FUZZ OK\n";   // reaching here = no crash, no hang
        }
        editor.reset();
        processor.reset();
        restored.reset();
        return failures == 0 ? 0 : 1;
    }

    editor.reset();
    processor.reset();
    return 0;
}
