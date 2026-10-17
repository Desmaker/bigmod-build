#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Curve.h"
#include <atomic>
#include <memory>
#include <vector>
#include <array>
#include <map>

//==============================================================================
//  BigMod modulation engine
//
//  Everything here is shared between the message thread (UI) and the audio
//  thread. Structural changes (adding/removing modules or assignments, editing
//  LFO curves, blender snapshots) happen on the message thread while holding
//  ModEngine::lock. The audio thread only *tries* that lock, so it never waits.
//  Simple values that the UI drags (knob bases, depths, toggles) are atomics.
//==============================================================================

enum class ModType { Macro = 0, LFO, XYPad, Blender, EnvFollower, Randomizer, MidiMacro, NumTypes };

juce::String modTypeMenuName (ModType);   // "Envelope Follower"
juce::String modTypeShortName (ModType);  // "ENV FOL"
juce::String modTypeAbbrev (ModType);     // "E"

namespace PoolIDs
{
    constexpr int numMacros = 8, numXY = 4, numBlenders = 4;
    inline juce::String macro (int slot)        { return "macro" + juce::String (slot + 1); }
    inline juce::String xyX (int slot)          { return "xy" + juce::String (slot + 1) + "x"; }
    inline juce::String xyY (int slot)          { return "xy" + juce::String (slot + 1) + "y"; }
    inline juce::String blendRange (int slot)   { return "blend" + juce::String (slot + 1) + "range"; }
    inline juce::String blendAngle (int slot)   { return "blend" + juce::String (slot + 1) + "angle"; }
}

//==============================================================================
struct ModContext
{
    double sampleRate = 44100.0;
    int    numSamples = 0;
    bool   hasTempo = false, isPlaying = false, transportJustStarted = false;
    double bpm = 120.0, ppq = 0.0;
    bool   noteOn = false;
    const juce::MidiBuffer* midi = nullptr;
    const float* const* inputAudio  = nullptr; int inputChannels  = 0;  // audio entering the hosted plug-in
    const float* const* outputAudio = nullptr; int outputChannels = 0;  // what the plug-in produced last sub-block
};

//==============================================================================
struct ModParam
{
    using ToText   = std::function<juce::String (float)>;
    using FromText = std::function<float (const juce::String&)>;

    ModParam (juce::String pid, juce::String displayName, juce::NormalisableRange<float> r,
              float defaultReal, ToText tt, FromText ft = {}, bool canModulate = true)
        : id (std::move (pid)), name (std::move (displayName)), range (r),
          defaultValue (defaultReal), toText (std::move (tt)), fromText (std::move (ft)),
          modulatable (canModulate)
    {
        baseNorm.store (range.convertTo0to1 (defaultReal));
        effNorm.store (baseNorm.load());
    }

    juce::String id, name;
    juce::NormalisableRange<float> range;
    float defaultValue;
    ToText toText;
    FromText fromText;
    bool modulatable;
    juce::RangedAudioParameter* hostParam = nullptr;   // set for DAW-automatable pool parameters

    std::atomic<float> baseNorm { 0.0f };   // the user's setting (0..1)
    std::atomic<float> effNorm  { 0.0f };   // base + modulation, for display
    float modSum = 0.0f;                    // audio thread scratch

    float getBaseNorm() const               { return hostParam != nullptr ? hostParam->getValue() : baseNorm.load(); }
    void  setBaseNorm (float v);
    float getBaseReal() const               { return range.convertFrom0to1 (getBaseNorm()); }
    float getEffReal() const                { return range.convertFrom0to1 (effNorm.load()); }
    juce::String textForReal (float real) const { return toText ? toText (real) : juce::String (real, 2); }
    bool  setFromText (const juce::String&);
    void  beginGesture()                    { if (hostParam != nullptr) hostParam->beginChangeGesture(); }
    void  endGesture()                      { if (hostParam != nullptr) hostParam->endChangeGesture(); }
};

//==============================================================================
class Module
{
public:
    Module (ModType t, int uidIn, int numberIn) : type (t), uid (uidIn), number (numberIn)
    {
        uiOut[0].store (0.0f); uiOut[1].store (0.0f);
    }
    virtual ~Module() = default;

    const ModType type;
    const int uid;
    const int number;          // "LFO 2" -> 2
    int poolSlot = -1;         // for Macro/XY/Blender: index into the DAW-automatable pool
    std::atomic<bool> enabled { true };

    std::vector<std::unique_ptr<ModParam>> params;
    std::vector<float> eff;                      // effective normalised param values (audio thread)
    std::array<float, 2> out { 0.0f, 0.0f };     // modulation outputs
    std::array<std::atomic<float>, 2> uiOut;      // copies for the UI

    juce::String getName() const          { return modTypeShortName (type) + " " + juce::String (number); }
    juce::String getAbbrev() const        { return modTypeAbbrev (type) + juce::String (number); }
    virtual int  getNumOutputs() const    { return 1; }
    virtual bool isBipolar() const        { return true; }
    virtual juce::String getOutputName (int) const { return {}; }

    virtual void prepare (double /*sampleRate*/) {}
    virtual void process (const ModContext&) = 0;
    virtual void saveExtra (juce::XmlElement&) const {}
    virtual void loadExtra (const juce::XmlElement&) {}

    ModParam* addParam (std::unique_ptr<ModParam> p) { params.push_back (std::move (p)); eff.push_back (params.back()->baseNorm.load()); return params.back().get(); }
    int  findParamIndex (const juce::String& pid) const;
    ModParam* getParam (const juce::String& pid) const { auto i = findParamIndex (pid); return i >= 0 ? params[(size_t) i].get() : nullptr; }
    float effReal (int i) const { return params[(size_t) i]->range.convertFrom0to1 (eff[(size_t) i]); }
};

//==============================================================================
// Note-value table used by every tempo-synced module
struct SyncDivisions
{
    static int size();
    static juce::String name (int index);
    static double beats (int index);          // length in quarter notes
    static int indexFromNorm (float norm)     { return juce::jlimit (0, size() - 1, juce::roundToInt (norm * (float) (size() - 1))); }
    static float normFromIndex (int index)    { return (float) index / (float) (size() - 1); }
    static int find (const juce::String&);
};

//==============================================================================
class MacroModule : public Module
{
public:
    MacroModule (int uid, int number);
    void process (const ModContext&) override;
};

class LfoModule : public Module
{
public:
    enum PlayMode { OneShotForward = 0, OneShotReverse, Loop, PingPong };
    LfoModule (int uid, int number);
    void prepare (double) override;
    void process (const ModContext&) override;
    void saveExtra (juce::XmlElement&) const override;
    void loadExtra (const juce::XmlElement&) override;

    Curve curve;                         // guarded by ModEngine::lock
    int presetIndex = 0;                 // -1 = custom
    std::atomic<bool> tempoSync { false };
    std::atomic<int>  playMode { Loop };
    std::atomic<bool> retriggerRequest { false };
    std::atomic<float> uiPos { 0.0f };

private:
    double freePhase = 0.0, oneShotPos = 1.0;
};

class XYModule : public Module
{
public:
    XYModule (int uid, int number);
    int getNumOutputs() const override { return 2; }
    juce::String getOutputName (int i) const override { return i == 0 ? "X" : "Y"; }
    void process (const ModContext&) override;
};

struct BlendSlot
{
    juce::String name;
    std::vector<float> values;
    bool filled() const { return ! values.empty(); }
};

class BlenderModule : public Module
{
public:
    BlenderModule (int uid, int number);
    int getNumOutputs() const override { return 0; }
    void process (const ModContext&) override {}
    void saveExtra (juce::XmlElement&) const override;
    void loadExtra (const juce::XmlElement&) override;

    std::array<BlendSlot, 4> slots;      // guarded by ModEngine::lock
    int slotsVersion = 0;
    std::array<float, 4> weights { 0.25f, 0.25f, 0.25f, 0.25f };
    std::array<std::atomic<float>, 4> uiWeights;

    // audio thread bookkeeping
    float lastRange = -1.0f, lastAngle = -1.0f; int lastVersion = -1;
    bool primed = true;   // false after loading: the plug-in state already holds the blend
    bool computeWeights (float range, float angleDeg);   // returns false if no slot is filled
};

class EnvFollowerModule : public Module
{
public:
    EnvFollowerModule (int uid, int number, bool defaultToOutput);
    bool isBipolar() const override { return false; }
    void prepare (double) override;
    void process (const ModContext&) override;
    void saveExtra (juce::XmlElement&) const override;
    void loadExtra (const juce::XmlElement&) override;

    std::atomic<bool> rmsMode { false };
    std::atomic<bool> followOutput { false };
private:
    double sr = 44100.0;
    float env = 0.0f, rmsState = 0.0f;
};

class RandomModule : public Module
{
public:
    RandomModule (int uid, int number);
    void process (const ModContext&) override;
    void saveExtra (juce::XmlElement&) const override;
    void loadExtra (const juce::XmlElement&) override;

    std::atomic<bool> tempoSync { false };
private:
    juce::Random rng;
    float prevTarget = 0.0f, target = 0.0f, noise = 0.0f;
    double segPos = 0.0, segLen = 0.0;
    long long lastGridIndex = -1;
    void newTarget (double periodSeconds, float jitter);
};

class MidiMacroModule : public Module
{
public:
    MidiMacroModule (int uid, int number);
    bool isBipolar() const override { return false; }
    void process (const ModContext&) override;
    void saveExtra (juce::XmlElement&) const override;
    void loadExtra (const juce::XmlElement&) override;

    std::atomic<int>  ccNumber { -1 };
    std::atomic<bool> learning { false };
};

//==============================================================================
struct Assignment
{
    enum class Target { HostParam, ModuleParam };

    int uid = 0;
    int sourceUid = 0, sourceOutput = 0;
    Target target = Target::HostParam;
    int hostIndex = -1;                  // Target::HostParam
    int targetUid = 0;                   // Target::ModuleParam
    juce::String targetParamId;
    std::atomic<float> depth { 0.25f };  // -1..+1

    // cached by ModEngine::rebuildRouting()
    Module* src = nullptr;
    Module* dst = nullptr;
    int dstParamIndex = -1;
};

//==============================================================================
class ModEngine
{
public:
    ModEngine();
    ~ModEngine();

    juce::CriticalSection lock;
    std::atomic<int> structureVersion { 0 };
    juce::AudioProcessorValueTreeState* pool = nullptr;
    bool instrumentMode = false;

    std::vector<std::unique_ptr<Module>> modules;
    std::vector<std::unique_ptr<Assignment>> assignments;

    //--- message thread API (takes the lock internally) -----------------------
    Module* addModule (ModType, int forcedUid = 0, int forcedNumber = 0, int forcedSlot = -1);
    void removeModule (int uid);
    void clearAll();
    Assignment* addHostAssignment (int srcUid, int srcOut, int hostIndex, float depth);
    Assignment* addModuleAssignment (int srcUid, int srcOut, int dstUid, const juce::String& paramId, float depth);
    void removeAssignment (int uid);
    bool canAddModule (ModType, juce::String& whyNot) const;

    Module* findModule (int uid) const;
    Assignment* findAssignment (int uid) const;
    std::vector<Assignment*> getOutgoing (int moduleUid, int output) const;
    std::vector<Assignment*> getIncoming (int moduleUid, const juce::String& paramId) const;
    void bumpVersion() { structureVersion.fetch_add (1); }

    //--- hosted plug-in parameters ------------------------------------------------
    void setHostParameters (const juce::Array<juce::AudioProcessorParameter*>&);
    int getNumHostParams() const { return numHost; }
    juce::AudioProcessorParameter* getHostParam (int i) const { return juce::isPositiveAndBelow (i, numHost) ? hostPtrs[i] : nullptr; }
    float getHostBase (int i) const;
    bool isHostParamManaged (int i) const;
    float getHostLastWritten (int i) const;
    std::vector<float> captureHostState() const;          // un-modulated values of every parameter
    std::atomic<bool> writesSuspended { false };

    //--- learn ("Assign") state -------------------------------------------------
    std::atomic<int> learnSourceUid { 0 }, learnSourceOutput { 0 }, learnHostHit { -1 };
    bool isLearning() const { return learnSourceUid.load() != 0; }

    //--- audio thread -----------------------------------------------------------
    void prepare (double sampleRate);
    void processSubBlock (const ModContext&);              // call with lock held

    //--- persistence (message thread) -------------------------------------------
    void saveState (juce::XmlElement& parent) const;
    void loadState (const juce::XmlElement& parent);       // after the hosted plug-in is loaded

    static thread_local bool writingFromEngine;

private:
    struct HostParamState
    {
        std::atomic<float> base { 0.0f };
        std::atomic<float> lastWritten { -1.0f };
        std::atomic<bool>  managed { false };
        float modSum = 0.0f;
    };

    std::unique_ptr<HostParamState[]> hostState;
    juce::Array<juce::AudioProcessorParameter*> hostPtrs;
    int numHost = 0;
    std::vector<int> managedList;
    int nextUid = 1;
    double sampleRate = 44100.0;
    std::array<bool, PoolIDs::numMacros> macroSlots {};
    std::array<bool, PoolIDs::numXY> xySlots {};
    std::array<bool, PoolIDs::numBlenders> blendSlots {};

    void rebuildRouting();                                 // lock held
    void writeHost (int index, float value);
    int  claimSlot (ModType, int forced);
    void releaseSlot (ModType, int slot);
    void linkPool (Module&);
    int  nextNumber (ModType) const;
    std::unique_ptr<Module> createModule (ModType, int uid, int number);
};
