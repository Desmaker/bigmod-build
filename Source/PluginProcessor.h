#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "ModEngine.h"

#ifndef BIGMOD_IS_SYNTH
 #if defined (JucePlugin_IsSynth)
  #define BIGMOD_IS_SYNTH JucePlugin_IsSynth
 #else
  #define BIGMOD_IS_SYNTH 0
 #endif
#endif

//==============================================================================
// Settings + the scanned plug-in list, shared by every BigMod instance in the
// process and stored in  <user app data>/BigMod/BigMod.settings
//==============================================================================
class BigModShared : private juce::ChangeListener
{
public:
    BigModShared();
    ~BigModShared() override;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;
    std::unique_ptr<juce::PropertiesFile> props;

    juce::File getDeadMansPedalFile() const;
    juce::File getPresetFolder() const;
    void reloadListIfChangedOnDisk();

    bool getSortByManufacturer() const      { return props->getBoolValue ("sortByManufacturer", false); }
    void setSortByManufacturer (bool b)     { props->setValue ("sortByManufacturer", b); props->saveIfNeeded(); }
    int  getDefaultResolution() const       { return props->getIntValue ("resolution", 32); }
    void setDefaultResolution (int r)       { props->setValue ("resolution", r); props->saveIfNeeded(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::Time lastSeenModTime;
    bool reloading = false;
};

//==============================================================================
// Hands the hosted plug-in a play position that is correct for each sub-block
//==============================================================================
struct SubBlockPlayHead : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> position;
    int offset = 0;
    double sampleRate = 44100.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! position.hasValue() || offset == 0) return position;
        auto p = *position;
        if (auto t = p.getTimeInSamples()) p.setTimeInSamples (*t + offset);
        if (auto s = p.getTimeInSeconds()) p.setTimeInSeconds (*s + offset / sampleRate);
        if (auto q = p.getPpqPosition())
            if (auto b = p.getBpm())
                p.setPpqPosition (*q + offset / sampleRate * *b / 60.0);
        return p;
    }
};

//==============================================================================
class BigModProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorListener,
                        private juce::AsyncUpdater
{
public:
    BigModProcessor();
    ~BigModProcessor() override;

    static constexpr bool isInstrument = BIGMOD_IS_SYNTH != 0;
    static constexpr int maxSubBlock = 512;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }
    const juce::String getName() const override         { return isInstrument ? "BigMod Instrument" : "BigMod"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Hosting (message thread only)
    bool loadPlugin (const juce::PluginDescription&, juce::String& error, const juce::MemoryBlock* state = nullptr);
    void unloadPlugin();
    juce::AudioPluginInstance* getHostedPlugin() const noexcept { return hosted.get(); }
    juce::String getHostedName() const;
    juce::String getHostedParamName (int index) const;
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }

    // Presets (.bigmod files)
    std::unique_ptr<juce::XmlElement> createStateXml();
    void applyStateXml (const juce::XmlElement&);
    bool savePresetToFile (const juce::File&);
    bool loadPresetFromFile (const juce::File&);
    void clearRack()                                    { engine.clearAll(); }

    // Preset Blender helpers
    void captureSnapshot (int blenderUid, int slot);
    void captureProgram (int blenderUid, int slot, int programIndex);
    void clearBlendSlot (int blenderUid, int slot);
    juce::StringArray getHostedProgramNames() const;

    //==========================================================================
    ModEngine engine;
    juce::AudioProcessorValueTreeState apvts;
    juce::SharedResourcePointer<BigModShared> shared;
    std::atomic<int> resolution { 32 };       // samples per modulation update
    juce::String lastError;                   // shown in the footer

    std::function<void()> onBeforePluginChange, onAfterPluginChange;   // set by the editor

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();
    void configureBuses (juce::AudioPluginInstance&);

    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float newValue) override;
    void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override;
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override;
    void handleAsyncUpdate() override;

    juce::CriticalSection hostLock;   // audio thread try-locks this; held only while swapping
    juce::CriticalSection swapLock;   // serialises load/unload/state on non-audio threads
    std::unique_ptr<juce::AudioPluginInstance> hosted;
    juce::PluginDescription loadedDesc;   // as scanned (instances can report an inner binary path)
    std::unique_ptr<juce::XmlElement> missingPluginXml;
    int hostIn = 0, hostOut = 0;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    bool prepared = false, wasPlaying = false;

    juce::AudioBuffer<float> work, lastOut;
    juce::MidiBuffer subMidi;
    SubBlockPlayHead subPlayHead;

    JUCE_DECLARE_WEAK_REFERENCEABLE (BigModProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigModProcessor)
};
