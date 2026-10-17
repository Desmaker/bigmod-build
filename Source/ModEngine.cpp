#include "ModEngine.h"

thread_local bool ModEngine::writingFromEngine = false;

//==============================================================================
juce::String modTypeMenuName (ModType t)
{
    switch (t)
    {
        case ModType::Macro:       return "Macro";
        case ModType::LFO:         return "LFO";
        case ModType::XYPad:       return "XY Pad";
        case ModType::Blender:     return "Preset Blender";
        case ModType::EnvFollower: return "Envelope Follower";
        case ModType::Randomizer:  return "Randomizer";
        case ModType::MidiMacro:   return "Midi Macro";
        default: break;
    }
    return {};
}

juce::String modTypeShortName (ModType t)
{
    switch (t)
    {
        case ModType::Macro:       return "MACRO";
        case ModType::LFO:         return "LFO";
        case ModType::XYPad:       return "XY";
        case ModType::Blender:     return "BLENDER";
        case ModType::EnvFollower: return "ENV FOL";
        case ModType::Randomizer:  return "RANDOM";
        case ModType::MidiMacro:   return "MIDI";
        default: break;
    }
    return {};
}

juce::String modTypeAbbrev (ModType t)
{
    switch (t)
    {
        case ModType::Macro:       return "M";
        case ModType::LFO:         return "L";
        case ModType::XYPad:       return "XY";
        case ModType::Blender:     return "B";
        case ModType::EnvFollower: return "E";
        case ModType::Randomizer:  return "R";
        case ModType::MidiMacro:   return "C";
        default: break;
    }
    return {};
}

//==============================================================================
namespace
{
    struct Div { const char* name; double beats; };
    const Div divisions[] = {
        { "8 bars", 32.0 }, { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1/1", 4.0 },
        { "1/2D", 3.0 }, { "1/2", 2.0 }, { "1/2T", 4.0 / 3.0 },
        { "1/4D", 1.5 }, { "1/4", 1.0 }, { "1/4T", 2.0 / 3.0 },
        { "1/8D", 0.75 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 },
        { "1/16D", 0.375 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 },
        { "1/32", 0.125 }, { "1/64", 0.0625 }
    };

    juce::String pct (float v)    { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String deg (float v)    { return juce::String (juce::roundToInt (v)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); }
    juce::String ms (float v)     { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String db (float v)     { return juce::String (juce::roundToInt (v)) + " dB"; }
    float parseNum (const juce::String& s) { return s.retainCharacters ("-0123456789.").getFloatValue(); }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    // A rate parameter whose meaning depends on the module's Hz / tempo-sync switch.
    std::unique_ptr<ModParam> makeRateParam (std::atomic<bool>* syncFlag, juce::NormalisableRange<float> hzRange, float defaultHz)
    {
        auto toText = [syncFlag, hzRange] (float norm) -> juce::String
        {
            if (syncFlag->load())
                return SyncDivisions::name (SyncDivisions::indexFromNorm (norm));
            return juce::String (hzRange.convertFrom0to1 (norm), 3);
        };
        auto fromText = [syncFlag, hzRange] (const juce::String& t) -> float
        {
            if (syncFlag->load())
            {
                auto i = SyncDivisions::find (t);
                return i >= 0 ? SyncDivisions::normFromIndex (i) : 0.5f;
            }
            return hzRange.convertTo0to1 (juce::jlimit (hzRange.start, hzRange.end, parseNum (t)));
        };
        return std::make_unique<ModParam> ("rate", "Rate", juce::NormalisableRange<float> (0.0f, 1.0f),
                                           hzRange.convertTo0to1 (defaultHz), toText, fromText);
    }

    const juce::NormalisableRange<float> lfoHz    = skewed (0.01f, 40.0f, 1.0f);
    const juce::NormalisableRange<float> randomHz = skewed (0.05f, 50.0f, 2.0f);

    float frac (double v) { return (float) (v - std::floor (v)); }
}

int SyncDivisions::size()                 { return (int) std::size (divisions); }
juce::String SyncDivisions::name (int i)  { return divisions[juce::jlimit (0, size() - 1, i)].name; }
double SyncDivisions::beats (int i)       { return divisions[juce::jlimit (0, size() - 1, i)].beats; }
int SyncDivisions::find (const juce::String& s)
{
    auto t = s.trim().toLowerCase();
    for (int i = 0; i < size(); ++i)
        if (t == juce::String (divisions[i].name).toLowerCase())
            return i;
    return -1;
}

//==============================================================================
void ModParam::setBaseNorm (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (hostParam != nullptr)
        hostParam->setValueNotifyingHost (v);
    else
        baseNorm.store (v);
}

bool ModParam::setFromText (const juce::String& t)
{
    if (t.trim().isEmpty()) return false;
    if (fromText)
    {
        // fromText returns a value in this param's real range
        setBaseNorm (range.convertTo0to1 (juce::jlimit (range.start, range.end, fromText (t))));
        return true;
    }
    setBaseNorm (range.convertTo0to1 (juce::jlimit (range.start, range.end, parseNum (t))));
    return true;
}

int Module::findParamIndex (const juce::String& pid) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i]->id == pid)
            return (int) i;
    return -1;
}

//==============================================================================
MacroModule::MacroModule (int u, int n) : Module (ModType::Macro, u, n)
{
    addParam (std::make_unique<ModParam> ("value", "Value", juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f,
                                          [] (float v) { return juce::String (v, 1) + " %"; }));
}

void MacroModule::process (const ModContext&)
{
    out[0] = effReal (0) / 100.0f;
}

//==============================================================================
LfoModule::LfoModule (int u, int n) : Module (ModType::LFO, u, n)
{
    curve = Curve::makePreset (0);
    addParam (makeRateParam (&tempoSync, lfoHz, 1.0f));
    addParam (std::make_unique<ModParam> ("phase", "Phase", juce::NormalisableRange<float> (0.0f, 360.0f), 0.0f,
                                          [] (float v) { return deg (v); }));
}

void LfoModule::prepare (double) { freePhase = 0.0; oneShotPos = 1.0; }

void LfoModule::process (const ModContext& c)
{
    const double dt = c.numSamples / c.sampleRate;
    const bool sync = tempoSync.load();
    const double bpm = c.bpm > 0.0 ? c.bpm : 120.0;
    double beatsLen = 1.0, cps;

    if (sync)
    {
        beatsLen = SyncDivisions::beats (SyncDivisions::indexFromNorm (eff[0]));
        cps = (bpm / 60.0) / beatsLen;
    }
    else
    {
        cps = lfoHz.convertFrom0to1 (eff[0]);
    }

    const float phaseOffset = effReal (1) / 360.0f;
    const bool manual = retriggerRequest.exchange (false);

    if (manual || c.transportJustStarted) freePhase = 0.0;
    if (manual || c.noteOn || c.transportJustStarted) oneShotPos = 0.0;

    const int mode = playMode.load();
    float pos;

    if (mode == Loop || mode == PingPong)
    {
        double raw;
        if (sync && c.isPlaying && c.hasTempo)
            raw = c.ppq / beatsLen;
        else
        {
            freePhase = std::fmod (freePhase + cps * dt, 2.0);
            raw = freePhase;
        }

        float p;
        if (mode == Loop) p = frac (raw);
        else { const double q = std::fmod (raw, 2.0); p = (float) (q < 1.0 ? q : 2.0 - q); }

        pos = frac ((double) p + phaseOffset);
    }
    else
    {
        oneShotPos = juce::jmin (1.0, oneShotPos + cps * dt);
        const float p = phaseOffset + (1.0f - phaseOffset) * (float) oneShotPos;
        pos = mode == OneShotForward ? p : 1.0f - p;
        pos = juce::jlimit (0.0f, 1.0f, pos);
    }

    out[0] = curve.evaluate (pos);
    uiPos.store (pos);
}

void LfoModule::saveExtra (juce::XmlElement& e) const
{
    e.setAttribute ("curve", curve.toString());
    e.setAttribute ("preset", presetIndex);
    e.setAttribute ("sync", tempoSync.load());
    e.setAttribute ("mode", playMode.load());
}

void LfoModule::loadExtra (const juce::XmlElement& e)
{
    curve = Curve::fromString (e.getStringAttribute ("curve"));
    presetIndex = e.getIntAttribute ("preset", -1);
    tempoSync.store (e.getBoolAttribute ("sync"));
    playMode.store (e.getIntAttribute ("mode", Loop));
}

//==============================================================================
XYModule::XYModule (int u, int n) : Module (ModType::XYPad, u, n)
{
    auto t = [] (float v) { return juce::String (v, 2); };
    addParam (std::make_unique<ModParam> ("x", "X", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, t));
    addParam (std::make_unique<ModParam> ("y", "Y", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, t));
}

void XYModule::process (const ModContext&)
{
    out[0] = effReal (0);
    out[1] = effReal (1);
}

//==============================================================================
BlenderModule::BlenderModule (int u, int n) : Module (ModType::Blender, u, n)
{
    for (int i = 0; i < 4; ++i) { slots[(size_t) i].name = "Empty " + juce::String (i + 1); uiWeights[(size_t) i].store (0.0f); }
    addParam (std::make_unique<ModParam> ("range", "Range", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                          [] (float v) { return juce::String (v, 2); }));
    addParam (std::make_unique<ModParam> ("angle", "Angle", juce::NormalisableRange<float> (0.0f, 360.0f), 0.0f,
                                          [] (float v) { return deg (v); }));
}

bool BlenderModule::computeWeights (float range, float angleDeg)
{
    const float a = juce::degreesToRadians (angleDeg);
    const float px = range * std::sin (a), py = -range * std::cos (a);
    const float sx[4] = { 0.0f, 1.0f, 0.0f, -1.0f };
    const float sy[4] = { -1.0f, 0.0f, 1.0f, 0.0f };

    float sum = 0.0f;
    for (size_t i = 0; i < 4; ++i)
    {
        weights[i] = 0.0f;
        if (! slots[i].filled()) continue;
        const float d2 = (px - sx[i]) * (px - sx[i]) + (py - sy[i]) * (py - sy[i]);
        weights[i] = 1.0f / (d2 + 1.0e-4f);
        sum += weights[i];
    }

    for (size_t i = 0; i < 4; ++i)
    {
        if (sum > 0.0f) weights[i] /= sum;
        uiWeights[i].store (weights[i]);
    }
    return sum > 0.0f;
}

void BlenderModule::saveExtra (juce::XmlElement& e) const
{
    for (size_t i = 0; i < 4; ++i)
    {
        auto* s = e.createNewChildElement ("SLOT");
        s->setAttribute ("name", slots[i].name);
        if (slots[i].filled())
        {
            juce::MemoryBlock mb (slots[i].values.data(), slots[i].values.size() * sizeof (float));
            s->setAttribute ("values", mb.toBase64Encoding());
        }
    }
}

void BlenderModule::loadExtra (const juce::XmlElement& e)
{
    size_t i = 0;
    for (auto* s : e.getChildWithTagNameIterator ("SLOT"))
    {
        if (i >= 4) break;
        slots[i].name = s->getStringAttribute ("name", "Empty " + juce::String ((int) i + 1));
        slots[i].values.clear();
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (s->getStringAttribute ("values")) && mb.getSize() >= sizeof (float))
        {
            slots[i].values.resize (mb.getSize() / sizeof (float));
            std::memcpy (slots[i].values.data(), mb.getData(), slots[i].values.size() * sizeof (float));
        }
        ++i;
    }
    ++slotsVersion;
    primed = false;
}

//==============================================================================
EnvFollowerModule::EnvFollowerModule (int u, int n, bool defaultToOutput) : Module (ModType::EnvFollower, u, n)
{
    followOutput.store (defaultToOutput);
    addParam (std::make_unique<ModParam> ("input", "Input", juce::NormalisableRange<float> (-24.0f, 36.0f), 0.0f,
                                          [] (float v) { return db (v); }));
    addParam (std::make_unique<ModParam> ("attack", "Attack", skewed (0.1f, 1000.0f, 30.0f), 10.0f,
                                          [] (float v) { return ms (v); }));
    addParam (std::make_unique<ModParam> ("release", "Release", skewed (1.0f, 3000.0f, 150.0f), 150.0f,
                                          [] (float v) { return ms (v); }));
}

void EnvFollowerModule::prepare (double s) { sr = s; env = 0.0f; rmsState = 0.0f; }

void EnvFollowerModule::process (const ModContext& c)
{
    const float gain = juce::Decibels::decibelsToGain (effReal (0));
    const float atk = std::exp (-1.0f / (effReal (1) * 0.001f * (float) sr));
    const float rel = std::exp (-1.0f / (effReal (2) * 0.001f * (float) sr));
    const float rmsCoef = std::exp (-1.0f / (0.03f * (float) sr));   // 30 ms RMS window
    const bool rms = rmsMode.load();

    const bool useOut = followOutput.load();
    const float* const* audio = useOut ? c.outputAudio : c.inputAudio;
    const int chans = useOut ? c.outputChannels : c.inputChannels;

    for (int i = 0; i < c.numSamples; ++i)
    {
        float x = 0.0f;
        if (audio != nullptr)
            for (int ch = 0; ch < chans; ++ch)
                x = juce::jmax (x, std::abs (audio[ch][i]));
        x *= gain;

        if (rms)
        {
            rmsState = rmsCoef * rmsState + (1.0f - rmsCoef) * x * x;
            x = std::sqrt (rmsState) * 1.41421356f;
        }

        const float k = x > env ? atk : rel;
        env = k * env + (1.0f - k) * x;
    }

    if (! std::isfinite (env)) env = 0.0f;
    out[0] = juce::jlimit (0.0f, 1.0f, env);
}

void EnvFollowerModule::saveExtra (juce::XmlElement& e) const
{
    e.setAttribute ("rms", rmsMode.load());
    e.setAttribute ("followOutput", followOutput.load());
}

void EnvFollowerModule::loadExtra (const juce::XmlElement& e)
{
    rmsMode.store (e.getBoolAttribute ("rms"));
    followOutput.store (e.getBoolAttribute ("followOutput", followOutput.load()));
}

//==============================================================================
RandomModule::RandomModule (int u, int n) : Module (ModType::Randomizer, u, n), rng (juce::Random::getSystemRandom().nextInt64())
{
    addParam (std::make_unique<ModParam> ("amount", "Amount", juce::NormalisableRange<float> (0.0f, 100.0f), 50.0f, pct));
    addParam (std::make_unique<ModParam> ("smooth", "Smooth", juce::NormalisableRange<float> (0.0f, 100.0f), 30.0f, pct));
    addParam (std::make_unique<ModParam> ("jitter", "Jitter", juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, pct));
    addParam (makeRateParam (&tempoSync, randomHz, 3.0f));
}

void RandomModule::newTarget (double period, float jitter)
{
    // the previous segment always finishes on its target, so start the new one from there
    prevTarget = target;
    target = rng.nextFloat() * 2.0f - 1.0f;
    segLen = period * (1.0 + jitter * (rng.nextFloat() * 1.6 - 0.8));
    segLen = juce::jmax (1.0e-3, segLen);
}

void RandomModule::process (const ModContext& c)
{
    const float amount = effReal (0) / 100.0f;
    const float smooth = effReal (1) / 100.0f;
    const float jitter = effReal (2) / 100.0f;
    const bool sync = tempoSync.load();
    const double bpm = c.bpm > 0.0 ? c.bpm : 120.0;
    const double dt = c.numSamples / c.sampleRate;

    if (sync && c.isPlaying && c.hasTempo)
    {
        const double beatsLen = SyncDivisions::beats (SyncDivisions::indexFromNorm (eff[3]));
        const auto idx = (long long) std::floor (c.ppq / beatsLen);
        if (idx != lastGridIndex)
        {
            lastGridIndex = idx;
            newTarget (beatsLen * 60.0 / bpm, 0.0f);
        }
        segLen = beatsLen * 60.0 / bpm;
        segPos = (c.ppq - (double) idx * beatsLen) * 60.0 / bpm;
    }
    else
    {
        lastGridIndex = -1;
        const double period = sync ? SyncDivisions::beats (SyncDivisions::indexFromNorm (eff[3])) * 60.0 / bpm
                                   : 1.0 / randomHz.convertFrom0to1 (eff[3]);
        segPos += dt;
        if (segPos >= segLen)
        {
            segPos = segLen > 0.0 ? std::fmod (segPos - segLen, juce::jmax (segLen, 1.0e-3)) : 0.0;
            newTarget (period, jitter);
        }
    }

    float v;
    if (smooth < 0.001f)
        v = target;
    else
    {
        const float t = (float) juce::jlimit (0.0, 1.0, segPos / (smooth * segLen));
        v = prevTarget + (target - prevTarget) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t));
    }

    noise = 0.7f * noise + 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
    v += noise * jitter * 0.25f;

    out[0] = juce::jlimit (-1.0f, 1.0f, v) * amount;
}

void RandomModule::saveExtra (juce::XmlElement& e) const { e.setAttribute ("sync", tempoSync.load()); }
void RandomModule::loadExtra (const juce::XmlElement& e) { tempoSync.store (e.getBoolAttribute ("sync")); }

//==============================================================================
MidiMacroModule::MidiMacroModule (int u, int n) : Module (ModType::MidiMacro, u, n)
{
    addParam (std::make_unique<ModParam> ("value", "Value", juce::NormalisableRange<float> (0.0f, 127.0f), 0.0f,
                                          [] (float v) { return juce::String (juce::roundToInt (v)); }));
}

void MidiMacroModule::process (const ModContext& c)
{
    if (c.midi != nullptr)
    {
        for (const auto meta : *c.midi)
        {
            const auto msg = meta.getMessage();
            if (! msg.isController()) continue;

            if (learning.load())
            {
                ccNumber.store (msg.getControllerNumber());
                learning.store (false);
            }

            if (msg.getControllerNumber() == ccNumber.load())
                params[0]->baseNorm.store ((float) msg.getControllerValue() / 127.0f);
        }
    }

    auto& p = *params[0];
    const float e = juce::jlimit (0.0f, 1.0f, p.baseNorm.load() + p.modSum);
    eff[0] = e;
    p.effNorm.store (e);
    out[0] = e;
}

void MidiMacroModule::saveExtra (juce::XmlElement& e) const { e.setAttribute ("cc", ccNumber.load()); }
void MidiMacroModule::loadExtra (const juce::XmlElement& e) { ccNumber.store (e.getIntAttribute ("cc", -1)); }

//==============================================================================
ModEngine::ModEngine() = default;
ModEngine::~ModEngine() = default;

std::unique_ptr<Module> ModEngine::createModule (ModType t, int uid, int number)
{
    switch (t)
    {
        case ModType::Macro:       return std::make_unique<MacroModule> (uid, number);
        case ModType::LFO:         return std::make_unique<LfoModule> (uid, number);
        case ModType::XYPad:       return std::make_unique<XYModule> (uid, number);
        case ModType::Blender:     return std::make_unique<BlenderModule> (uid, number);
        case ModType::EnvFollower: return std::make_unique<EnvFollowerModule> (uid, number, instrumentMode);
        case ModType::Randomizer:  return std::make_unique<RandomModule> (uid, number);
        case ModType::MidiMacro:   return std::make_unique<MidiMacroModule> (uid, number);
        default: break;
    }
    return {};
}

int ModEngine::nextNumber (ModType t) const
{
    for (int n = 1;; ++n)
    {
        bool used = false;
        for (auto& m : modules)
            if (m->type == t && m->number == n) { used = true; break; }
        if (! used) return n;
    }
}

int ModEngine::claimSlot (ModType t, int forced)
{
    auto claim = [forced] (auto& arr) -> int
    {
        if (juce::isPositiveAndBelow (forced, (int) arr.size()) && ! arr[(size_t) forced]) { arr[(size_t) forced] = true; return forced; }
        for (size_t i = 0; i < arr.size(); ++i)
            if (! arr[i]) { arr[i] = true; return (int) i; }
        return -1;
    };

    switch (t)
    {
        case ModType::Macro:   return claim (macroSlots);
        case ModType::XYPad:   return claim (xySlots);
        case ModType::Blender: return claim (blendSlots);
        default: break;
    }
    return -1;
}

void ModEngine::releaseSlot (ModType t, int slot)
{
    if (slot < 0) return;
    switch (t)
    {
        case ModType::Macro:   if (slot < PoolIDs::numMacros)   macroSlots[(size_t) slot] = false; break;
        case ModType::XYPad:   if (slot < PoolIDs::numXY)       xySlots[(size_t) slot] = false; break;
        case ModType::Blender: if (slot < PoolIDs::numBlenders) blendSlots[(size_t) slot] = false; break;
        default: break;
    }
}

void ModEngine::linkPool (Module& m)
{
    if (pool == nullptr || m.poolSlot < 0) return;
    auto get = [this] (const juce::String& id) { return pool->getParameter (id); };

    switch (m.type)
    {
        case ModType::Macro:   m.params[0]->hostParam = get (PoolIDs::macro (m.poolSlot)); break;
        case ModType::XYPad:   m.params[0]->hostParam = get (PoolIDs::xyX (m.poolSlot));
                               m.params[1]->hostParam = get (PoolIDs::xyY (m.poolSlot)); break;
        case ModType::Blender: m.params[0]->hostParam = get (PoolIDs::blendRange (m.poolSlot));
                               m.params[1]->hostParam = get (PoolIDs::blendAngle (m.poolSlot)); break;
        default: break;
    }
}

bool ModEngine::canAddModule (ModType t, juce::String& why) const
{
    if (modules.size() >= 32) { why = "The rack is full (32 modules)."; return false; }
    auto free = [] (const auto& arr) { for (bool b : arr) if (! b) return true; return false; };
    if (t == ModType::Macro && ! free (macroSlots))     { why = "All 8 macros are in use."; return false; }
    if (t == ModType::XYPad && ! free (xySlots))        { why = "All 4 XY pads are in use."; return false; }
    if (t == ModType::Blender && ! free (blendSlots))   { why = "All 4 preset blenders are in use."; return false; }
    return true;
}

Module* ModEngine::addModule (ModType t, int forcedUid, int forcedNumber, int forcedSlot)
{
    const juce::ScopedLock sl (lock);

    int slot = -1;
    const bool pooled = (t == ModType::Macro || t == ModType::XYPad || t == ModType::Blender);
    if (pooled)
    {
        slot = claimSlot (t, forcedSlot);
        if (slot < 0) return nullptr;
    }
    else if (modules.size() >= 32)
        return nullptr;

    const int uid = forcedUid > 0 ? forcedUid : nextUid;
    nextUid = juce::jmax (nextUid, uid + 1);
    const int number = pooled ? slot + 1 : (forcedNumber > 0 ? forcedNumber : nextNumber (t));

    auto m = createModule (t, uid, number);
    m->poolSlot = slot;
    linkPool (*m);

    if (forcedUid == 0)   // brand-new module: reset its DAW parameters to defaults
        for (auto& p : m->params)
            if (p->hostParam != nullptr)
                p->hostParam->setValueNotifyingHost (p->range.convertTo0to1 (p->defaultValue));

    for (size_t i = 0; i < m->params.size(); ++i)
        m->eff[i] = m->params[i]->getBaseNorm();

    m->prepare (sampleRate);
    auto* raw = m.get();
    modules.push_back (std::move (m));
    rebuildRouting();
    bumpVersion();
    return raw;
}

void ModEngine::removeModule (int uid)
{
    const juce::ScopedLock sl (lock);

    if (learnSourceUid.load() == uid) learnSourceUid.store (0);

    assignments.erase (std::remove_if (assignments.begin(), assignments.end(), [uid] (auto& a)
    {
        return a->sourceUid == uid || (a->target == Assignment::Target::ModuleParam && a->targetUid == uid);
    }), assignments.end());

    for (auto it = modules.begin(); it != modules.end(); ++it)
    {
        if ((*it)->uid != uid) continue;
        for (auto& p : (*it)->params)
            if (p->hostParam != nullptr)
                p->hostParam->setValueNotifyingHost (p->range.convertTo0to1 (p->defaultValue));
        releaseSlot ((*it)->type, (*it)->poolSlot);
        modules.erase (it);
        break;
    }

    rebuildRouting();
    bumpVersion();
}

void ModEngine::clearAll()
{
    const juce::ScopedLock sl (lock);
    learnSourceUid.store (0);
    assignments.clear();
    for (auto& m : modules)
        releaseSlot (m->type, m->poolSlot);
    modules.clear();
    rebuildRouting();
    bumpVersion();
}

Module* ModEngine::findModule (int uid) const
{
    for (auto& m : modules)
        if (m->uid == uid) return m.get();
    return nullptr;
}

Assignment* ModEngine::findAssignment (int uid) const
{
    for (auto& a : assignments)
        if (a->uid == uid) return a.get();
    return nullptr;
}

std::vector<Assignment*> ModEngine::getOutgoing (int moduleUid, int output) const
{
    std::vector<Assignment*> r;
    for (auto& a : assignments)
        if (a->sourceUid == moduleUid && a->sourceOutput == output) r.push_back (a.get());
    return r;
}

std::vector<Assignment*> ModEngine::getIncoming (int moduleUid, const juce::String& pid) const
{
    std::vector<Assignment*> r;
    for (auto& a : assignments)
        if (a->target == Assignment::Target::ModuleParam && a->targetUid == moduleUid && a->targetParamId == pid)
            r.push_back (a.get());
    return r;
}

Assignment* ModEngine::addHostAssignment (int srcUid, int srcOut, int hostIndex, float depth)
{
    const juce::ScopedLock sl (lock);
    if (findModule (srcUid) == nullptr || ! juce::isPositiveAndBelow (hostIndex, numHost)) return nullptr;

    for (auto& a : assignments)
        if (a->target == Assignment::Target::HostParam && a->sourceUid == srcUid
             && a->sourceOutput == srcOut && a->hostIndex == hostIndex)
            return a.get();

    auto a = std::make_unique<Assignment>();
    a->uid = nextUid++;
    a->sourceUid = srcUid; a->sourceOutput = srcOut;
    a->target = Assignment::Target::HostParam;
    a->hostIndex = hostIndex;
    a->depth.store (depth);
    auto* raw = a.get();
    assignments.push_back (std::move (a));
    rebuildRouting();
    bumpVersion();
    return raw;
}

Assignment* ModEngine::addModuleAssignment (int srcUid, int srcOut, int dstUid, const juce::String& pid, float depth)
{
    const juce::ScopedLock sl (lock);
    auto* src = findModule (srcUid);
    auto* dst = findModule (dstUid);
    if (src == nullptr || dst == nullptr || src == dst) return nullptr;
    auto* p = dst->getParam (pid);
    if (p == nullptr || ! p->modulatable) return nullptr;

    for (auto& a : assignments)
        if (a->target == Assignment::Target::ModuleParam && a->sourceUid == srcUid && a->sourceOutput == srcOut
             && a->targetUid == dstUid && a->targetParamId == pid)
            return a.get();

    auto a = std::make_unique<Assignment>();
    a->uid = nextUid++;
    a->sourceUid = srcUid; a->sourceOutput = srcOut;
    a->target = Assignment::Target::ModuleParam;
    a->targetUid = dstUid; a->targetParamId = pid;
    a->depth.store (depth);
    auto* raw = a.get();
    assignments.push_back (std::move (a));
    rebuildRouting();
    bumpVersion();
    return raw;
}

void ModEngine::removeAssignment (int uid)
{
    const juce::ScopedLock sl (lock);
    assignments.erase (std::remove_if (assignments.begin(), assignments.end(),
                                       [uid] (auto& a) { return a->uid == uid; }), assignments.end());
    rebuildRouting();
    bumpVersion();
}

void ModEngine::rebuildRouting()
{
    // resolve pointers, drop dangling routes
    for (auto& a : assignments)
    {
        a->src = findModule (a->sourceUid);
        a->dst = nullptr; a->dstParamIndex = -1;
        if (a->target == Assignment::Target::ModuleParam)
        {
            a->dst = findModule (a->targetUid);
            if (a->dst != nullptr) a->dstParamIndex = a->dst->findParamIndex (a->targetParamId);
        }
    }

    assignments.erase (std::remove_if (assignments.begin(), assignments.end(), [this] (auto& a)
    {
        if (a->src == nullptr || a->sourceOutput >= juce::jmax (1, a->src->getNumOutputs())) return true;
        if (a->target == Assignment::Target::HostParam) return ! juce::isPositiveAndBelow (a->hostIndex, numHost);
        return a->dst == nullptr || a->dstParamIndex < 0;
    }), assignments.end());

    // which host parameters are now under modulation?
    std::vector<bool> now ((size_t) numHost, false);
    for (auto& a : assignments)
        if (a->target == Assignment::Target::HostParam)
            now[(size_t) a->hostIndex] = true;

    managedList.clear();
    for (int i = 0; i < numHost; ++i)
    {
        auto& st = hostState[(size_t) i];
        const bool was = st.managed.load();
        if (now[(size_t) i] && ! was)
        {
            const float v = hostPtrs[i]->getValue();
            st.base.store (v);
            st.lastWritten.store (v);
        }
        else if (! now[(size_t) i] && was)
        {
            writeHost (i, st.base.load());   // modulation removed: put the knob back where the user left it
        }
        st.managed.store (now[(size_t) i]);
        if (now[(size_t) i]) managedList.push_back (i);
    }
}

//==============================================================================
void ModEngine::setHostParameters (const juce::Array<juce::AudioProcessorParameter*>& ps)
{
    const juce::ScopedLock sl (lock);

    assignments.erase (std::remove_if (assignments.begin(), assignments.end(),
                                       [] (auto& a) { return a->target == Assignment::Target::HostParam; }), assignments.end());
    for (auto& m : modules)
        if (auto* b = dynamic_cast<BlenderModule*> (m.get()))
        {
            for (size_t i = 0; i < 4; ++i) { b->slots[i].values.clear(); b->slots[i].name = "Empty " + juce::String ((int) i + 1); }
            ++b->slotsVersion;
        }

    numHost = 0;   // so rebuildRouting doesn't touch the old pointers
    managedList.clear();
    hostPtrs = ps;
    numHost = ps.size();
    hostState.reset (numHost > 0 ? new HostParamState[(size_t) numHost] : nullptr);
    for (int i = 0; i < numHost; ++i)
        hostState[(size_t) i].base.store (hostPtrs[i]->getValue());

    rebuildRouting();
    bumpVersion();
}

float ModEngine::getHostBase (int i) const
{
    if (! juce::isPositiveAndBelow (i, numHost)) return 0.0f;
    return hostState[(size_t) i].managed.load() ? hostState[(size_t) i].base.load() : hostPtrs[i]->getValue();
}

bool ModEngine::isHostParamManaged (int i) const
{
    return juce::isPositiveAndBelow (i, numHost) && hostState[(size_t) i].managed.load();
}

float ModEngine::getHostLastWritten (int i) const
{
    return juce::isPositiveAndBelow (i, numHost) ? hostState[(size_t) i].lastWritten.load() : -1.0f;
}

std::vector<float> ModEngine::captureHostState() const
{
    std::vector<float> v ((size_t) numHost);
    for (int i = 0; i < numHost; ++i)
        v[(size_t) i] = getHostBase (i);
    return v;
}

void ModEngine::writeHost (int i, float v)
{
    auto* p = hostPtrs[i];
    writingFromEngine = true;
    p->setValue (v);
    writingFromEngine = false;
    hostState[(size_t) i].lastWritten.store (p->getValue());   // read back: some plug-ins quantise
}

//==============================================================================
void ModEngine::prepare (double sr)
{
    const juce::ScopedLock sl (lock);
    sampleRate = sr;
    for (auto& m : modules) m->prepare (sr);
}

void ModEngine::processSubBlock (const ModContext& c)
{
    const bool canWrite = ! writesSuspended.load();

    // 1) did the user (or DAW automation inside the plug-in) move a modulated knob?
    if (canWrite)
    {
        for (int idx : managedList)
        {
            auto& st = hostState[(size_t) idx];
            const float v = hostPtrs[idx]->getValue();
            const float lw = st.lastWritten.load();
            if (lw < 0.0f || std::abs (v - lw) > 1.0e-4f)
            {
                st.base.store (v);
                st.lastWritten.store (v);
            }
        }
    }

    // 2) run the modules in rack order; earlier modules feed later ones in the same sub-block
    for (auto& m : modules)
    {
        for (auto& p : m->params) p->modSum = 0.0f;

        for (auto& a : assignments)
            if (a->dst == m.get() && a->src != nullptr && a->src->enabled.load())
                m->params[(size_t) a->dstParamIndex]->modSum += a->depth.load() * a->src->out[(size_t) a->sourceOutput];

        for (size_t i = 0; i < m->params.size(); ++i)
        {
            auto& p = *m->params[i];
            const float e = juce::jlimit (0.0f, 1.0f, p.getBaseNorm() + p.modSum);
            m->eff[i] = e;
            p.effNorm.store (e);
        }

        if (m->enabled.load())
            m->process (c);
        else
            m->out = { 0.0f, 0.0f };

        m->uiOut[0].store (m->out[0]);
        m->uiOut[1].store (m->out[1]);
    }

    // 3) preset blenders rewrite the base values of every parameter when their node moves
    for (auto& m : modules)
    {
        if (m->type != ModType::Blender || ! m->enabled.load()) continue;
        auto& b = static_cast<BlenderModule&> (*m);
        const float r = b.effReal (0), a = b.effReal (1);
        if (r == b.lastRange && a == b.lastAngle && b.slotsVersion == b.lastVersion) continue;

        b.lastRange = r; b.lastAngle = a; b.lastVersion = b.slotsVersion;
        const bool hasSlots = b.computeWeights (r, a);
        if (! b.primed) { b.primed = true; continue; }   // just restored: don't overwrite the saved plug-in state
        if (! hasSlots || ! canWrite) continue;

        for (int j = 0; j < numHost; ++j)
        {
            float v = 0.0f, wsum = 0.0f;
            for (size_t s = 0; s < 4; ++s)
            {
                if (b.weights[s] <= 0.0f || (size_t) j >= b.slots[s].values.size()) continue;
                v += b.weights[s] * b.slots[s].values[(size_t) j];
                wsum += b.weights[s];
            }
            if (wsum <= 0.0f) continue;
            v /= wsum;

            auto& st = hostState[(size_t) j];
            if (st.managed.load())
                st.base.store (v);
            else if (std::abs (hostPtrs[j]->getValue() - v) > 1.0e-6f)
                writeHost (j, v);
        }
    }

    // 4) write modulated values into the hosted plug-in
    for (int idx : managedList) hostState[(size_t) idx].modSum = 0.0f;

    for (auto& a : assignments)
        if (a->target == Assignment::Target::HostParam && a->src != nullptr && a->src->enabled.load())
            hostState[(size_t) a->hostIndex].modSum += a->depth.load() * a->src->out[(size_t) a->sourceOutput];

    if (canWrite)
    {
        for (int idx : managedList)
        {
            auto& st = hostState[(size_t) idx];
            const float e = juce::jlimit (0.0f, 1.0f, st.base.load() + st.modSum);
            if (std::abs (e - st.lastWritten.load()) > 1.0e-6f)
                writeHost (idx, e);
        }
    }
}

//==============================================================================
static juce::String hostParamId (juce::AudioProcessorParameter* p)
{
    if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*> (p))
        return hp->getParameterID();
    return {};
}

void ModEngine::saveState (juce::XmlElement& parent) const
{
    const juce::ScopedLock sl (lock);

    auto* mods = parent.createNewChildElement ("MODULES");
    for (auto& m : modules)
    {
        auto* e = mods->createNewChildElement ("MODULE");
        e->setAttribute ("uid", m->uid);
        e->setAttribute ("type", (int) m->type);
        e->setAttribute ("number", m->number);
        e->setAttribute ("slot", m->poolSlot);
        e->setAttribute ("enabled", m->enabled.load());
        for (auto& p : m->params)
        {
            auto* pe = e->createNewChildElement ("P");
            pe->setAttribute ("id", p->id);
            pe->setAttribute ("v", p->getBaseNorm());
        }
        m->saveExtra (*e);
    }

    auto* as = parent.createNewChildElement ("ASSIGNMENTS");
    for (auto& a : assignments)
    {
        auto* e = as->createNewChildElement ("A");
        e->setAttribute ("uid", a->uid);
        e->setAttribute ("src", a->sourceUid);
        e->setAttribute ("out", a->sourceOutput);
        e->setAttribute ("depth", a->depth.load());
        if (a->target == Assignment::Target::HostParam)
        {
            e->setAttribute ("host", a->hostIndex);
            e->setAttribute ("hostId", hostParamId (hostPtrs[a->hostIndex]));
        }
        else
        {
            e->setAttribute ("dst", a->targetUid);
            e->setAttribute ("param", a->targetParamId);
        }
    }

    auto* bases = parent.createNewChildElement ("BASES");
    for (int idx : managedList)
    {
        auto* e = bases->createNewChildElement ("B");
        e->setAttribute ("i", idx);
        e->setAttribute ("v", hostState[(size_t) idx].base.load());
    }
}

void ModEngine::loadState (const juce::XmlElement& parent)
{
    const juce::ScopedLock sl (lock);
    clearAll();

    if (auto* mods = parent.getChildByName ("MODULES"))
    {
        for (auto* e : mods->getChildWithTagNameIterator ("MODULE"))
        {
            const auto t = (ModType) e->getIntAttribute ("type", -1);
            if ((int) t < 0 || (int) t >= (int) ModType::NumTypes) continue;
            auto* m = addModule (t, e->getIntAttribute ("uid"), e->getIntAttribute ("number"), e->getIntAttribute ("slot", -1));
            if (m == nullptr) continue;

            m->enabled.store (e->getBoolAttribute ("enabled", true));
            for (auto* pe : e->getChildWithTagNameIterator ("P"))
                if (auto* p = m->getParam (pe->getStringAttribute ("id")))
                    if (p->hostParam == nullptr)      // DAW-visible ones come back with the parameter state
                        p->baseNorm.store ((float) pe->getDoubleAttribute ("v"));
            m->loadExtra (*e);
        }
    }

    std::map<juce::String, int> idToIndex;
    for (int i = 0; i < numHost; ++i)
    {
        auto id = hostParamId (hostPtrs[i]);
        if (id.isNotEmpty()) idToIndex[id] = i;
    }

    if (auto* as = parent.getChildByName ("ASSIGNMENTS"))
    {
        for (auto* e : as->getChildWithTagNameIterator ("A"))
        {
            auto a = std::make_unique<Assignment>();
            a->uid = e->getIntAttribute ("uid", nextUid);
            nextUid = juce::jmax (nextUid, a->uid + 1);
            a->sourceUid = e->getIntAttribute ("src");
            a->sourceOutput = e->getIntAttribute ("out");
            a->depth.store ((float) e->getDoubleAttribute ("depth", 0.25));

            if (e->hasAttribute ("host"))
            {
                a->target = Assignment::Target::HostParam;
                a->hostIndex = e->getIntAttribute ("host", -1);
                auto it = idToIndex.find (e->getStringAttribute ("hostId"));
                if (it != idToIndex.end()) a->hostIndex = it->second;
            }
            else
            {
                a->target = Assignment::Target::ModuleParam;
                a->targetUid = e->getIntAttribute ("dst");
                a->targetParamId = e->getStringAttribute ("param");
            }
            assignments.push_back (std::move (a));
        }
    }

    rebuildRouting();

    if (auto* bases = parent.getChildByName ("BASES"))
    {
        for (auto* e : bases->getChildWithTagNameIterator ("B"))
        {
            const int i = e->getIntAttribute ("i", -1);
            if (! isHostParamManaged (i)) continue;
            hostState[(size_t) i].base.store ((float) e->getDoubleAttribute ("v"));
            hostState[(size_t) i].lastWritten.store (hostPtrs[i]->getValue());
        }
    }

    bumpVersion();
}
