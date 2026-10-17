// Two tiny plug-ins used only by the automated tests:
//   TEST_SYNTH=0 -> "BigMod TestFx"   : gain/cutoff/drive/mode, 3 factory programs
//   TEST_SYNTH=1 -> "BigMod TestSynth": a sine voice playing MIDI notes, level/pitch params
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

class TestPlugin : public juce::AudioProcessor
{
public:
    TestPlugin()
        : AudioProcessor (TEST_SYNTH ? BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())
                                     : BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                        .withOutput ("Out", juce::AudioChannelSet::stereo())),
          apvts (*this, nullptr, "P", layout())
    {}

    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "cutoff", 1 }, "Cutoff", 0.0f, 1.0f, 0.3f));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "drive", 1 }, "Drive", 0.0f, 1.0f, 0.0f));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode", juce::StringArray { "A", "B", "C" }, 0));
        return l;
    }

    const juce::String getName() const override { return TEST_SYNTH ? "BigMod TestSynth" : "BigMod TestFx"; }
    void prepareToPlay (double sr, int) override { sampleRate = sr; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        const float gain = apvts.getRawParameterValue ("gain")->load();
       #if TEST_SYNTH
        for (const auto m : midi)
        {
            auto msg = m.getMessage();
            if (msg.isNoteOn()) { freq = juce::MidiMessage::getMidiNoteInHertz (msg.getNoteNumber()); on = true; }
            if (msg.isNoteOff()) on = false;
        }
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float s = on ? std::sin (phase) * gain : 0.0f;
            phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
            for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, s);
        }
       #else
        juce::ignoreUnused (midi);
        b.applyGain (gain);
       #endif
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return TEST_SYNTH; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }

    int getNumPrograms() override { return 3; }
    int getCurrentProgram() override { return current; }
    void setCurrentProgram (int i) override
    {
        static const float values[3][3] = { { 0.2f, 0.1f, 0.0f }, { 0.5f, 0.5f, 0.5f }, { 0.9f, 0.9f, 1.0f } };
        current = juce::jlimit (0, 2, i);
        const char* ids[] = { "gain", "cutoff", "drive" };
        for (int p = 0; p < 3; ++p)
            apvts.getParameter (ids[p])->setValueNotifyingHost (values[current][p]);
    }
    const juce::String getProgramName (int i) override { return juce::StringArray { "Soft", "Mid", "Hard" }[i]; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& d) override
    {
        if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size)) apvts.replaceState (juce::ValueTree::fromXml (*x));
    }

private:
    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 48000.0, phase = 0.0, freq = 440.0;
    bool on = false;
    int current = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestPlugin(); }
