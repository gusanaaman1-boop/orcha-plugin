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

        // --- the internal sample browser: open, list, pick ---------------------
        if (auto* orchaEditor = dynamic_cast<OrchaAudioProcessorEditor*> (editor.get()))
        {
            const auto lib = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getChildFile ("orcha-library");
            lib.getChildFile ("kicks").createDirectory();
            juce::File (juce::String (argv[2]))
                .copyFileTo (lib.getChildFile ("kicks").getChildFile ("library-kick.wav"));
            orchaEditor->browser().setRoot (lib, false);
            orchaEditor->openBrowserFor (1);
            pumpUntil ([] { return false; }, 300);
            shoot ("06-browser.png");

            const auto before = processor->getSample (1)->file.getFileName();
            orchaEditor->browser().pickFile (
                lib.getChildFile ("kicks").getChildFile ("library-kick.wav"));
            pumpUntil ([&]
            {
                return processor->getSample (1) != nullptr
                    && processor->getSample (1)->file.getFileName() == "library-kick.wav";
            }, 15000);
            const bool picked = processor->getSample (1) != nullptr
                && processor->getSample (1)->file.getFileName() == "library-kick.wav";
            std::cout << (picked ? "BROWSER OK" : "BROWSER FAILED") << "\n";
            if (! picked)
                return 1;
            juce::ignoreUnused (before);
            // Put the original snare back so later sections see the real kit.
            processor->loadSampleAsync (1, juce::File (juce::String (argv[3])));
            pumpUntil ([&]
            {
                return processor->getSample (1) != nullptr
                    && processor->getSample (1)->file.getFileName() != "library-kick.wav";
            }, 15000);
            lib.deleteRecursively();
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

        // --- GENERATE must replace EVERY non-favorite, edited or not -----------
        // The user's report: cards did not change under GENERATE. Cause: an
        // edited card kept its flag, and enqueueBuild re-rendered the OLD
        // pattern via the useExisting path. Edit a card, force an ending on
        // another, favorite a third - then GENERATE. The edited ones must
        // change, the favorite must not.
        {
            int genFailures = 0;
            auto edited = processor->option (3).pattern;
            Event extra;
            extra.pos = 5.0;
            extra.role = Role::MID;
            extra.velocity = 0.9f;
            edited.events.push_back (extra);
            processor->applyEditedPattern (3, edited);
            pumpUntil ([&] { return processor->option (3).ready; }, 15000);
            processor->setOptionEnding (4, 3);   // force ToStop on card 5
            pumpUntil ([&] { return processor->option (4).ready; }, 15000);
            if (! processor->option (6).favorite)
                processor->toggleFavorite (6);

            juce::StringArray before;
            for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
                before.add (processor->option (i).pattern.signature());

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

            for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
            {
                const bool changed =
                    processor->option (i).pattern.signature() != before[i];
                if (i == 6 && changed)
                {
                    std::cout << "GENERATE FAIL: favorite card 7 was replaced\n";
                    ++genFailures;
                }
                if (i != 6 && ! changed)
                {
                    std::cout << "GENERATE FAIL: card " << (i + 1)
                              << " did not change\n";
                    ++genFailures;
                }
                if (i != 6 && processor->option (i).edited)
                {
                    std::cout << "GENERATE FAIL: card " << (i + 1)
                              << " still flagged edited\n";
                    ++genFailures;
                }
            }
            processor->toggleFavorite (6);   // leave no favorites behind
            failuresEarly += genFailures;
            std::cout << (genFailures == 0 ? "GENERATE OK" : "GENERATE FAILED") << "\n";
        }

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

        // --- B4: the favorites chain, end to end -------------------------------
        {
            int chainFailures = 0;
            // Exactly two hearts, one of them wet, so the phrase exercises
            // the tail that has to cross the card boundary. Earlier sections
            // favorited other cards; the chain is whatever is hearted NOW.
            for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
                if (processor->option (i).favorite)
                    processor->toggleFavorite (i);
            processor->setOptionFx (0, 0.7f, 0.3f, 0.0f);
            pumpUntil ([&] { return processor->option (0).ready; }, 15000);
            for (int i : { 0, 1 })
                processor->toggleFavorite (i);
            pumpUntil ([&] { return ! processor->isGenerating(); }, 15000);

            const auto chainFile = processor->ensureChainWav();
            if (! chainFile.existsAsFile())
            {
                std::cout << "CHAIN FAIL: no chain file\n";
                ++chainFailures;
            }
            else
            {
                juce::AudioFormatManager fm;
                fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (
                    fm.createReaderFor (chainFile));
                if (reader == nullptr)
                {
                    std::cout << "CHAIN FAIL: chain wav unreadable\n";
                    ++chainFailures;
                }
                else
                {
                    const int expected =
                        processor->option (0).loop->buffer.getNumSamples()
                        + processor->option (1).loop->buffer.getNumSamples();
                    if ((int) reader->lengthInSamples != expected)
                    {
                        std::cout << "CHAIN FAIL: length " << reader->lengthInSamples
                                  << " expected " << expected << "\n";
                        ++chainFailures;
                    }
                    juce::AudioBuffer<float> chainBuf (2, (int) reader->lengthInSamples);
                    reader->read (&chainBuf, 0, (int) reader->lengthInSamples, 0, true, true);
                    if (chainBuf.getMagnitude (0, chainBuf.getNumSamples()) <= 0.0f)
                        { std::cout << "CHAIN FAIL: silent chain\n"; ++chainFailures; }
                }
            }
            // Un-hearting one card must invalidate the phrase, not reuse it.
            processor->toggleFavorite (1);
            if (processor->ensureChainWav().existsAsFile())
            {
                std::cout << "CHAIN FAIL: one favorite still produced a chain\n";
                ++chainFailures;
            }
            processor->toggleFavorite (1);
            processor->setOptionFx (0, 0.0f, 0.0f, 0.0f);
            pumpUntil ([&] { return processor->option (0).ready; }, 15000);
            failures += chainFailures;
            std::cout << (chainFailures == 0 ? "CHAIN OK" : "CHAIN FAILED") << "\n";
        }

        // --- F3: reliability scenarios the mac can prove -----------------------
        {
            int relFailures = 0;
            const auto scratch = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getChildFile ("orcha-reliability");
            scratch.createDirectory();

            // Hebrew / unicode in the sample path - the user's real world.
            const juce::File source { juce::String (argv[2]) };
            const auto hebrew = scratch.getChildFile (
                juce::String::fromUTF8 ("\xd7\xaa\xd7\x95\xd7\xa3 \xd7\x91\xd7\xa2\xd7\x91\xd7\xa8\xd7\x99\xd7\xaa.wav"));
            hebrew.deleteFile();
            if (source.copyFileTo (hebrew))
            {
                auto uni = std::make_unique<OrchaAudioProcessor>();
                uni->prepareToPlay (48000.0, 512);
                uni->loadSampleAsync (0, hebrew);
                pumpUntil ([&] { return uni->getSample (0) != nullptr; }, 15000);
                if (uni->getSample (0) == nullptr)
                {
                    std::cout << "RELIABILITY FAIL: unicode path did not load\n";
                    ++relFailures;
                }
                else
                {
                    // Save while the sample file exists, then restore AFTER it
                    // vanished: no crash, and the slot reports missing rather
                    // than pretending.
                    uni->generateAll();
                    pumpUntil ([&] { return ! uni->isGenerating(); }, 30000);
                    juce::MemoryBlock uniState;
                    uni->getStateInformation (uniState);
                    uni.reset();
                    hebrew.deleteFile();
                    auto missing = std::make_unique<OrchaAudioProcessor>();
                    missing->prepareToPlay (48000.0, 512);
                    missing->setStateInformation (uniState.getData(),
                                                  (int) uniState.getSize());
                    pumpUntil ([&] { return ! missing->isGenerating(); }, 15000);
                    if (missing->getSample (0) != nullptr)
                    {
                        std::cout << "RELIABILITY FAIL: missing sample restored as loaded\n";
                        ++relFailures;
                    }
                    missing.reset();
                }
            }
            else
            {
                std::cout << "RELIABILITY FAIL: could not stage unicode copy\n";
                ++relFailures;
            }

            // Two live instances generating at once - one shared render cache.
            {
                auto a = std::make_unique<OrchaAudioProcessor>();
                auto b = std::make_unique<OrchaAudioProcessor>();
                for (auto* p : { a.get(), b.get() })
                {
                    p->prepareToPlay (48000.0, 512);
                    for (int i = 0; i < juce::jmin (3, argc - 2); ++i)
                        p->loadSampleAsync (i, juce::File { juce::String (argv[2 + i]) });
                }
                pumpUntil ([&]
                {
                    return a->getSample (0) != nullptr && b->getSample (0) != nullptr;
                }, 15000);
                a->generateAll();
                b->generateAll();
                pumpUntil ([&]
                {
                    return ! a->isGenerating() && ! b->isGenerating();
                }, 60000);
                int readyA = 0, readyB = 0;
                for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
                {
                    readyA += a->option (i).ready ? 1 : 0;
                    readyB += b->option (i).ready ? 1 : 0;
                }
                if (readyA < OrchaAudioProcessor::numOptions
                    || readyB < OrchaAudioProcessor::numOptions)
                {
                    std::cout << "RELIABILITY FAIL: dual instances ready "
                              << readyA << "/" << readyB << "\n";
                    ++relFailures;
                }

                // Sample-rate change mid-flight: the drift timer must rerender
                // every card at the new rate without being asked.
                a->prepareToPlay (44100.0, 512);
                a->processBlock (block, midi);
                pumpUntil ([&]
                {
                    if (a->isGenerating())
                        return false;
                    for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
                        if (! a->option (i).ready)
                            return false;
                    return true;
                }, 30000);
                int ready44 = 0;
                for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
                    ready44 += a->option (i).ready ? 1 : 0;
                if (ready44 < OrchaAudioProcessor::numOptions)
                {
                    std::cout << "RELIABILITY FAIL: 44.1k rerender ready "
                              << ready44 << "/12\n";
                    ++relFailures;
                }
                a.reset();
                b.reset();
            }

            scratch.deleteRecursively();
            failures += relFailures;
            std::cout << (relFailures == 0 ? "RELIABILITY OK"
                                           : "RELIABILITY FAILED") << "\n";
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
