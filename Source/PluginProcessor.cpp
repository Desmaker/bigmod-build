#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
BigModShared::BigModShared()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "BigMod";
    o.filenameSuffix      = ".settings";
    o.folderName          = "BigMod";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    props = std::make_unique<juce::PropertiesFile> (o);

    formatManager.addDefaultFormats();

    if (auto xml = props->getXmlValue ("pluginList"))
        knownList.recreateFromXml (*xml);
    lastSeenModTime = props->getFile().getLastModificationTime();
    knownList.addChangeListener (this);
}

BigModShared::~BigModShared()
{
    knownList.removeChangeListener (this);
    props->saveIfNeeded();
}

juce::File BigModShared::getDeadMansPedalFile() const
{
    return props->getFile().getSiblingFile ("RecentlyCrashedPluginsList");
}

juce::File BigModShared::getPresetFolder() const
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("BigMod").getChildFile ("Presets");
    f.createDirectory();
    return f;
}

void BigModShared::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (reloading) return;
    if (auto xml = knownList.createXml())
    {
        props->setValue ("pluginList", xml.get());
        props->saveIfNeeded();
        lastSeenModTime = props->getFile().getLastModificationTime();
    }
}

void BigModShared::reloadListIfChangedOnDisk()
{
    // another BigMod (or another DAW) may have rescanned since we loaded the list
    auto t = props->getFile().getLastModificationTime();
    if (t == lastSeenModTime) return;
    lastSeenModTime = t;
    props->reload();
    if (auto xml = props->getXmlValue ("pluginList"))
    {
        const juce::ScopedValueSetter<bool> svs (reloading, true);
        knownList.recreateFromXml (*xml);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BigModProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using P = juce::AudioParameterFloat;
    auto attrs = [] (const juce::String& unit) { return juce::AudioParameterFloatAttributes().withLabel (unit); };

    for (int i = 0; i < PoolIDs::numMacros; ++i)
        layout.add (std::make_unique<P> (juce::ParameterID { PoolIDs::macro (i), 1 }, "Macro " + juce::String (i + 1),
                                         juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f, attrs ("%")));
    for (int i = 0; i < PoolIDs::numXY; ++i)
    {
        layout.add (std::make_unique<P> (juce::ParameterID { PoolIDs::xyX (i), 1 }, "XY " + juce::String (i + 1) + " X",
                                         juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<P> (juce::ParameterID { PoolIDs::xyY (i), 1 }, "XY " + juce::String (i + 1) + " Y",
                                         juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    }
    for (int i = 0; i < PoolIDs::numBlenders; ++i)
    {
        layout.add (std::make_unique<P> (juce::ParameterID { PoolIDs::blendRange (i), 1 }, "Blender " + juce::String (i + 1) + " Range",
                                         juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<P> (juce::ParameterID { PoolIDs::blendAngle (i), 1 }, "Blender " + juce::String (i + 1) + " Angle",
                                         juce::NormalisableRange<float> (0.0f, 360.0f), 0.0f, attrs ("deg")));
    }
    return layout;
}

juce::AudioProcessor::BusesProperties BigModProcessor::makeBuses()
{
    if constexpr (BigModProcessor::isInstrument)
        return juce::AudioProcessor::BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    else
        return juce::AudioProcessor::BusesProperties()
                   .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

BigModProcessor::BigModProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    engine.pool = &apvts;
    engine.instrumentMode = isInstrument;
    resolution.store (shared->getDefaultResolution());
    work.setSize (2, maxSubBlock);
    lastOut.setSize (2, maxSubBlock);
    subMidi.ensureSize (2048);
}

BigModProcessor::~BigModProcessor()
{
    cancelPendingUpdate();
    onBeforePluginChange = nullptr;
    onAfterPluginChange = nullptr;
    const juce::ScopedLock sw (swapLock);
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (hostLock);
        engine.setHostParameters ({});
        old = std::move (hosted);
    }
    if (old != nullptr)
    {
        old->removeListener (this);
        old->releaseResources();
    }
}

//==============================================================================
void BigModProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    subPlayHead.sampleRate = sampleRate;
    engine.prepare (sampleRate);

    const juce::ScopedLock sl (hostLock);
    if (hosted != nullptr)
    {
        hosted->setRateAndBufferSizeDetails (sampleRate, maxSubBlock);
        hosted->prepareToPlay (sampleRate, maxSubBlock);
        setLatencySamples (hosted->getLatencySamples());
    }
    work.setSize (juce::jmax (2, hostIn, hostOut), maxSubBlock, false, true, true);
    lastOut.setSize (2, maxSubBlock, false, true, true);
    lastOut.clear();
    prepared = true;
}

void BigModProcessor::releaseResources()
{
    const juce::ScopedLock sl (hostLock);
    if (hosted != nullptr) hosted->releaseResources();
    prepared = false;
}

bool BigModProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if constexpr (isInstrument)
        return true;
    else
        return layouts.getMainInputChannelSet() == out;
}

double BigModProcessor::getTailLengthSeconds() const
{
    if (auto* h = hosted.get()) return h->getTailLengthSeconds();
    return 0.0;
}

//==============================================================================
void BigModProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int ourIn  = getTotalNumInputChannels();
    const int ourOut = getTotalNumOutputChannels();

    for (int ch = ourIn; ch < ourOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    // transport
    bool playing = false, hasTempo = false;
    double bpm = 120.0, ppq = 0.0;
    subPlayHead.position = {};
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            subPlayHead.position = pos;
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) { bpm = *b; hasTempo = true; }
            if (auto q = pos->getPpqPosition()) ppq = *q;
            else hasTempo = false;
        }
    }
    bool justStarted = playing && ! wasPlaying;
    wasPlaying = playing;

    const juce::ScopedTryLock hostTry (hostLock);
    if (! hostTry.isLocked())
    {
        if (isInstrument) buffer.clear();     // a plug-in is being swapped right now
        return;
    }

    auto* plug = hosted.get();
    const juce::ScopedTryLock engineTry (engine.lock);
    const bool engineOK = engineTry.isLocked();

    if (plug != nullptr)
        plug->setNonRealtime (isNonRealtime());

    const int step = juce::jlimit (8, maxSubBlock, resolution.load());
    const int wc = work.getNumChannels();

    for (int start = 0; start < numSamples; start += step)
    {
        const int len = juce::jmin (step, numSamples - start);

        // MIDI for this slice
        subMidi.clear();
        bool noteOn = false;
        for (auto it = midi.findNextSamplePosition (start); it != midi.cend(); ++it)
        {
            const auto meta = *it;
            if (meta.samplePosition >= start + len) break;
            subMidi.addEvent (meta.data, meta.numBytes, meta.samplePosition - start);
            if (meta.getMessage().isNoteOn()) noteOn = true;
        }

        // audio going into the hosted plug-in
        const int inCh = plug != nullptr ? hostIn : wc;
        for (int c = 0; c < wc; ++c)
        {
            if (! isInstrument && ourIn > 0 && c < inCh)
                work.copyFrom (c, 0, buffer, juce::jmin (c, ourIn - 1), start, len);
            else
                work.clear (c, 0, len);
        }

        if (engineOK)
        {
            ModContext ctx;
            ctx.sampleRate = currentSampleRate;
            ctx.numSamples = len;
            ctx.hasTempo = hasTempo;
            ctx.isPlaying = playing;
            ctx.transportJustStarted = justStarted && start == 0;
            ctx.bpm = bpm;
            ctx.ppq = ppq + (double) start / currentSampleRate * bpm / 60.0;
            ctx.noteOn = noteOn;
            ctx.midi = &subMidi;
            ctx.inputAudio = work.getArrayOfReadPointers();
            ctx.inputChannels = isInstrument ? 0 : juce::jmin (juce::jmax (1, ourIn), wc);
            ctx.outputAudio = lastOut.getArrayOfReadPointers();
            ctx.outputChannels = juce::jmin (2, ourOut);
            engine.processSubBlock (ctx);
        }

        if (plug != nullptr)
        {
            subPlayHead.offset = start;
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), wc, len);
            plug->processBlock (view, subMidi);

            for (int c = 0; c < ourOut; ++c)
            {
                if (hostOut > 0)
                    buffer.copyFrom (c, start, work, juce::jmin (c, hostOut - 1), 0, len);
                else
                    buffer.clear (c, start, len);
            }
        }
        else if (isInstrument)
        {
            buffer.clear (start, len);
        }

        for (int c = 0; c < juce::jmin (2, ourOut); ++c)
            lastOut.copyFrom (c, 0, buffer, c, start, len);
    }
}

//==============================================================================
void BigModProcessor::configureBuses (juce::AudioPluginInstance& inst)
{
    auto mainOut = getBusesLayout().getMainOutputChannelSet();
    if (mainOut.isDisabled()) mainOut = juce::AudioChannelSet::stereo();

    auto layout = inst.getBusesLayout();
    for (int i = 0; i < layout.outputBuses.size(); ++i)
        layout.outputBuses.getReference (i) = i == 0 ? mainOut : juce::AudioChannelSet::disabled();
    for (int i = 0; i < layout.inputBuses.size(); ++i)
        layout.inputBuses.getReference (i) = (i == 0 && ! isInstrument) ? mainOut : juce::AudioChannelSet::disabled();

    if (inst.setBusesLayout (layout))
        return;

    // fall back: only set the main buses, leave anything else the plug-in insists on
    auto l2 = inst.getBusesLayout();
    if (! l2.outputBuses.isEmpty()) l2.outputBuses.getReference (0) = mainOut;
    if (! l2.inputBuses.isEmpty() && ! isInstrument) l2.inputBuses.getReference (0) = mainOut;
    inst.setBusesLayout (l2);
}

bool BigModProcessor::loadPlugin (const juce::PluginDescription& desc, juce::String& error, const juce::MemoryBlock* state)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const juce::ScopedLock sw (swapLock);

    auto inst = shared->formatManager.createPluginInstance (desc, currentSampleRate, maxSubBlock, error);
    if (inst == nullptr)
    {
        if (error.isEmpty()) error = "Could not load " + desc.name;
        return false;
    }

    configureBuses (*inst);
    if (state != nullptr && state->getSize() > 0)
        inst->setStateInformation (state->getData(), (int) state->getSize());

    inst->setPlayHead (&subPlayHead);
    inst->setRateAndBufferSizeDetails (currentSampleRate, maxSubBlock);
    if (prepared)
        inst->prepareToPlay (currentSampleRate, maxSubBlock);

    if (onBeforePluginChange) onBeforePluginChange();

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (hostLock);
        old = std::move (hosted);
        hosted = std::move (inst);
        loadedDesc = desc;
        hostIn  = hosted->getTotalNumInputChannels();
        hostOut = hosted->getTotalNumOutputChannels();
        work.setSize (juce::jmax (2, hostIn, hostOut), maxSubBlock, false, true, true);
        engine.setHostParameters (hosted->getParameters());
    }

    if (old != nullptr)
    {
        old->removeListener (this);
        old->releaseResources();
        old.reset();
    }

    hosted->addListener (this);
    missingPluginXml.reset();
    lastError.clear();
    setLatencySamples (hosted->getLatencySamples());
    updateHostDisplay();

    if (onAfterPluginChange) onAfterPluginChange();
    return true;
}

void BigModProcessor::unloadPlugin()
{
    JUCE_ASSERT_MESSAGE_THREAD
    const juce::ScopedLock sw (swapLock);
    if (hosted == nullptr) return;

    if (onBeforePluginChange) onBeforePluginChange();

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (hostLock);
        engine.setHostParameters ({});
        old = std::move (hosted);
        hostIn = hostOut = 0;
    }
    old->removeListener (this);
    old->releaseResources();
    old.reset();

    setLatencySamples (0);
    updateHostDisplay();
    if (onAfterPluginChange) onAfterPluginChange();
}

juce::String BigModProcessor::getHostedName() const
{
    if (auto* h = hosted.get()) return h->getName();
    return {};
}

juce::String BigModProcessor::getHostedParamName (int index) const
{
    if (auto* p = engine.getHostParam (index))
    {
        auto n = p->getName (64);
        return n.isNotEmpty() ? n : "Param " + juce::String (index + 1);
    }
    return "Param " + juce::String (index + 1);
}

//==============================================================================
void BigModProcessor::audioProcessorParameterChanged (juce::AudioProcessor*, int index, float newValue)
{
    if (ModEngine::writingFromEngine || ! engine.isLearning()) return;
    // only edits made in the plug-in's window count; output/meter parameters change on the audio thread
    if (! juce::MessageManager::existsAndIsCurrentThread()) return;
    const float lw = engine.getHostLastWritten (index);
    if (lw >= 0.0f && std::abs (newValue - lw) < 1.0e-4f) return;
    engine.learnHostHit.store (index);
}

void BigModProcessor::audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index)
{
    if (engine.isLearning())
        engine.learnHostHit.store (index);
}

void BigModProcessor::audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails& d)
{
    if (d.latencyChanged) triggerAsyncUpdate();
}

void BigModProcessor::handleAsyncUpdate()
{
    const juce::ScopedLock sw (swapLock);
    if (hosted != nullptr) setLatencySamples (hosted->getLatencySamples());
}

//==============================================================================
std::unique_ptr<juce::XmlElement> BigModProcessor::createStateXml()
{
    auto root = std::make_unique<juce::XmlElement> ("BIGMOD");
    root->setAttribute ("version", 1);
    root->setAttribute ("resolution", resolution.load());

    {
        const juce::ScopedLock sw (swapLock);
        if (hosted != nullptr)
        {
            auto* pe = root->createNewChildElement ("PLUGIN");
            pe->addChildElement (loadedDesc.createXml().release());
            juce::MemoryBlock mb;
            hosted->getStateInformation (mb);
            pe->setAttribute ("state", mb.toBase64Encoding());
        }
        else if (missingPluginXml != nullptr)
        {
            root->addChildElement (new juce::XmlElement (*missingPluginXml));   // keep it for when it's installed again
        }
    }

    engine.saveState (*root);
    if (auto st = apvts.copyState().createXml())
        root->addChildElement (st.release());
    return root;
}

void BigModProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = createStateXml())
        copyXmlToBinary (*xml, dest);
}

void BigModProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("BIGMOD")) return;

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        applyStateXml (*xml);
        return;
    }

    std::shared_ptr<juce::XmlElement> copy (xml.release());
    juce::WeakReference<BigModProcessor> weak (this);
    juce::MessageManager::callAsync ([weak, copy]
    {
        if (auto* p = weak.get())
            p->applyStateXml (*copy);
    });
}

void BigModProcessor::applyStateXml (const juce::XmlElement& xml)
{
    JUCE_ASSERT_MESSAGE_THREAD
    resolution.store (xml.getIntAttribute ("resolution", resolution.load()));

    if (auto* pe = xml.getChildByName ("PLUGIN"))
    {
        juce::PluginDescription desc;
        juce::MemoryBlock state;
        state.fromBase64Encoding (pe->getStringAttribute ("state"));

        if (auto* de = pe->getFirstChildElement(); de != nullptr && desc.loadFromXml (*de))
        {
            const bool same = hosted != nullptr && loadedDesc.createIdentifierString() == desc.createIdentifierString();
            if (same)
            {
                const juce::ScopedLock sw (swapLock);
                engine.setHostParameters (hosted->getParameters());
                if (state.getSize() > 0)
                    hosted->setStateInformation (state.getData(), (int) state.getSize());
                lastError.clear();
            }
            else
            {
                shared->reloadListIfChangedOnDisk();
                juce::String err;
                bool ok = loadPlugin (desc, err, &state);
                if (! ok)
                {
                    // the plug-in may have moved since this was saved: look it up in the scanned list
                    for (auto& t : shared->knownList.getTypes())
                        if (t.uniqueId == desc.uniqueId && t.pluginFormatName == desc.pluginFormatName && t.name == desc.name
                             && t.fileOrIdentifier != desc.fileOrIdentifier)
                        {
                            desc = t;
                            ok = loadPlugin (desc, err, &state);
                            break;
                        }
                }

                if (! ok)
                {
                    unloadPlugin();
                    lastError = "Couldn't load \"" + desc.name + "\" - scan your plug-ins in Settings."
                                 + (err.isNotEmpty() ? " (" + err + ")" : juce::String());
                    DBG ("BigMod: load failed for " << desc.fileOrIdentifier << ": " << err);
                    missingPluginXml = std::make_unique<juce::XmlElement> (*pe);
                }
            }
        }
    }
    else
    {
        unloadPlugin();
        missingPluginXml.reset();
    }

    engine.loadState (xml);

    if (auto* p = xml.getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*p));

    if (onAfterPluginChange) onAfterPluginChange();
}

bool BigModProcessor::savePresetToFile (const juce::File& f)
{
    if (auto xml = createStateXml())
        return xml->writeTo (f);
    return false;
}

bool BigModProcessor::loadPresetFromFile (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("BIGMOD")) return false;
    applyStateXml (*xml);
    return true;
}

//==============================================================================
juce::StringArray BigModProcessor::getHostedProgramNames() const
{
    juce::StringArray names;
    if (auto* h = hosted.get())
        for (int i = 0; i < h->getNumPrograms(); ++i)
        {
            auto n = h->getProgramName (i).trim();
            names.add (n.isNotEmpty() ? n : "Program " + juce::String (i + 1));
        }
    return names;
}

void BigModProcessor::captureSnapshot (int blenderUid, int slot)
{
    const juce::ScopedLock sl (engine.lock);
    if (auto* b = dynamic_cast<BlenderModule*> (engine.findModule (blenderUid)))
    {
        b->slots[(size_t) slot].values = engine.captureHostState();
        b->slots[(size_t) slot].name = "Snapshot " + juce::String (slot + 1);
        ++b->slotsVersion;
        engine.bumpVersion();
    }
}

void BigModProcessor::clearBlendSlot (int blenderUid, int slot)
{
    const juce::ScopedLock sl (engine.lock);
    if (auto* b = dynamic_cast<BlenderModule*> (engine.findModule (blenderUid)))
    {
        b->slots[(size_t) slot].values.clear();
        b->slots[(size_t) slot].name = "Empty " + juce::String (slot + 1);
        ++b->slotsVersion;
        engine.bumpVersion();
    }
}

void BigModProcessor::captureProgram (int blenderUid, int slot, int programIndex)
{
    auto* h = hosted.get();
    if (h == nullptr || ! juce::isPositiveAndBelow (programIndex, h->getNumPrograms())) return;

    // Switch the plug-in to the factory preset, read every parameter, then put the
    // plug-in back exactly as it was. Modulation writes pause while this happens.
    auto saved = std::make_shared<juce::MemoryBlock>();
    h->getStateInformation (*saved);
    const auto name = getHostedProgramNames()[programIndex];

    engine.writesSuspended.store (true);
    h->setCurrentProgram (programIndex);

    juce::WeakReference<BigModProcessor> weak (this);
    juce::Timer::callAfterDelay (300, [weak, h, saved, name, blenderUid, slot]
    {
        auto* p = weak.get();
        if (p == nullptr) return;
        if (p->hosted.get() != h) { p->engine.writesSuspended.store (false); return; }

        std::vector<float> values;
        for (auto* prm : h->getParameters())
            values.push_back (prm->getValue());

        if (saved->getSize() > 0)
            h->setStateInformation (saved->getData(), (int) saved->getSize());
        p->engine.writesSuspended.store (false);

        const juce::ScopedLock sl (p->engine.lock);
        if (auto* b = dynamic_cast<BlenderModule*> (p->engine.findModule (blenderUid)))
        {
            b->slots[(size_t) slot].values = std::move (values);
            b->slots[(size_t) slot].name = name;
            ++b->slotsVersion;
            p->engine.bumpVersion();
        }
    });
}

//==============================================================================
juce::AudioProcessorEditor* BigModProcessor::createEditor()
{
    return new BigModEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BigModProcessor();
}
