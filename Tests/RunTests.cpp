// Automated end-to-end tests: hosts a real VST3 inside BigMod and checks every module.
//   BigModTests <path to test .vst3> <output folder> [--gui]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>
#include <set>

static int failures = 0, passes = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    ok ? ++passes : ++failures;
}

static bool near (float a, float b, float tol = 0.02f) { return std::abs (a - b) <= tol; }

struct FakePlayHead : public juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
};

constexpr double SR = 48000.0;
constexpr int BS = 512;

struct Rig
{
    std::unique_ptr<BigModProcessor> proc;
    juce::AudioBuffer<float> buffer { 2, BS };
    juce::MidiBuffer midi;
    float inputLevel = 0.5f;

    explicit Rig (const juce::PluginDescription& d)
    {
        proc = std::make_unique<BigModProcessor>();
        proc->setPlayConfigDetails (BigModProcessor::isInstrument ? 0 : 2, 2, SR, BS);
        proc->prepareToPlay (SR, BS);
        juce::String err;
        const bool ok = proc->loadPlugin (d, err);
        if (! ok) std::cout << "    load error: " << err << std::endl;
    }

    juce::AudioProcessorParameter* param (int i) { return proc->engine.getHostParam (i); }

    void block()
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < BS; ++i)
                buffer.setSample (c, i, inputLevel);
        proc->processBlock (buffer, midi);
        midi.clear();
    }

    void blocks (int n) { for (int i = 0; i < n; ++i) block(); }
};

//==============================================================================
static void testBasics (const juce::PluginDescription& d)
{
    std::cout << "\n[hosting + LFO]" << std::endl;
    Rig r (d);
    auto& e = r.proc->engine;
    check (r.proc->getHostedPlugin() != nullptr, "plug-in loads inside BigMod");
    check (e.getNumHostParams() >= 4, "hosted parameters exposed (" + juce::String (e.getNumHostParams()) + ")");
    check (r.proc->getHostedParamName (0) == "Gain", "parameter names read back");

    auto* lfo = dynamic_cast<LfoModule*> (e.addModule (ModType::LFO));
    check (lfo != nullptr && lfo->getName() == "LFO 1", "LFO module added and named");
    lfo->params[0]->setFromText ("5");
    check (lfo->params[0]->textForReal (lfo->params[0]->getBaseReal()) == "5.000",
           "rate typed as 5 Hz reads back as " + lfo->params[0]->textForReal (lfo->params[0]->getBaseReal()));

    auto* a = e.addHostAssignment (lfo->uid, 0, 0, 0.4f);
    check (a != nullptr && e.isHostParamManaged (0), "LFO assigned to Gain");

    float mn = 1.0f, mx = 0.0f, outMin = 10.0f, outMax = -10.0f;
    for (int b = 0; b < 60; ++b)
    {
        r.block();
        const float v = r.param (0)->getValue();
        mn = juce::jmin (mn, v); mx = juce::jmax (mx, v);
        outMin = juce::jmin (outMin, r.buffer.getSample (0, BS - 1));
        outMax = juce::jmax (outMax, r.buffer.getSample (0, BS - 1));
    }
    check (mx - mn > 0.6f, "LFO moves the hosted parameter (" + juce::String (mn, 2) + " .. " + juce::String (mx, 2) + ")");
    check (mn > 0.08f && mx < 0.92f, "modulation range = base 0.5 +/- depth 0.4");
    check (near (e.getHostBase (0), 0.5f, 0.001f), "user's base value is preserved under modulation");
    check (outMax - outMin > 0.25f, "audio really changes: output " + juce::String (outMin, 2) + " .. " + juce::String (outMax, 2));

    // the user turns the knob in the plug-in's own window while it's modulated
    r.param (0)->setValue (0.8f);
    r.block();
    check (near (e.getHostBase (0), 0.8f, 0.001f), "moving the knob in the plug-in sets a new base (0.8)");

    lfo->enabled.store (false);
    r.blocks (2);
    check (near (r.param (0)->getValue(), 0.8f, 0.001f), "bypassed module -> parameter rests at its base");
    lfo->enabled.store (true);

    e.removeAssignment (a->uid);
    r.block();
    check (! e.isHostParamManaged (0) && near (r.param (0)->getValue(), 0.8f, 0.001f), "removing the assignment restores the base value");

    // negative depth inverts
    a = e.addHostAssignment (lfo->uid, 0, 0, -0.3f);
    r.blocks (20);
    check (a != nullptr, "negative (inverted) depth accepted");

    // learn: the plug-in's UI starts a gesture on parameter 2
    e.learnSourceUid.store (lfo->uid);
    e.learnHostHit.store (-1);
    r.param (2)->beginChangeGesture();
    r.param (2)->endChangeGesture();
    check (e.learnHostHit.load() == 2, "Assign/learn catches the parameter touched in the plug-in UI");
    e.learnSourceUid.store (0);
}

//==============================================================================
static void testLfoModes (const juce::PluginDescription& d)
{
    std::cout << "\n[LFO: tempo sync, one-shot, curve editor]" << std::endl;
    Rig r (d);
    auto& e = r.proc->engine;
    FakePlayHead ph;
    r.proc->setPlayHead (&ph);

    auto* lfo = dynamic_cast<LfoModule*> (e.addModule (ModType::LFO));
    lfo->tempoSync.store (true);
    lfo->params[0]->setFromText ("1/4");
    check (lfo->params[0]->textForReal (lfo->params[0]->getBaseReal()) == "1/4", "sync division set to 1/4");

    ph.playing = true;
    ph.ppq = 0.0;     r.block();
    ph.ppq = 10.25;   r.block();
    check (near (lfo->uiPos.load(), 0.25f, 0.03f), "synced LFO locks to the DAW beat (ppq 10.25 -> phase 0.25)");
    ph.ppq = 33.5;    r.block();
    check (near (lfo->uiPos.load(), 0.5f, 0.03f), "... and follows it (ppq 33.5 -> phase 0.5)");

    lfo->params[1]->setFromText ("90");
    r.block();
    check (near (lfo->uiPos.load(), 0.75f, 0.03f), "phase offset of 90 deg shifts the start by a quarter");
    lfo->params[1]->setFromText ("0");

    lfo->playMode.store (LfoModule::PingPong);
    ph.ppq = 1.25; r.block();
    check (near (lfo->uiPos.load(), 0.75f, 0.03f), "ping-pong runs backwards on odd cycles");

    // one-shot forward, triggered by a MIDI note
    ph.playing = false;
    lfo->tempoSync.store (false);
    lfo->params[0]->setFromText ("2");      // 0.5 s per pass
    lfo->playMode.store (LfoModule::OneShotForward);
    r.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    r.block();
    const float p1 = lfo->uiPos.load();
    r.blocks (10);
    const float p2 = lfo->uiPos.load();
    r.blocks (60);
    const float p3 = lfo->uiPos.load();
    check (p1 < 0.1f && p2 > p1 && near (p3, 1.0f, 0.001f), "one-shot restarts on a note, runs once and holds ("
           + juce::String (p1, 2) + " -> " + juce::String (p2, 2) + " -> " + juce::String (p3, 2) + ")");

    lfo->playMode.store (LfoModule::OneShotReverse);
    lfo->retriggerRequest.store (true);
    r.block();
    check (lfo->uiPos.load() > 0.9f, "one-shot reverse starts at the end");

    // curve editing
    Curve c = Curve::makePreset (2);   // saw up
    check (near (c.evaluate (0.5f), 0.0f) && near (c.evaluate (0.25f), -0.5f), "linear segment evaluates correctly");
    c.segs[0].bend = 1.0f;
    check (c.evaluate (0.25f) > -0.3f, "bending a segment curves it (" + juce::String (c.evaluate (0.25f), 2) + ")");
    c.segs[0] = { 0.1f, 1.0f };
    const float nearNode = c.evaluate (0.05f), farAway = c.evaluate (0.75f);
    c.segs[0] = { 0.5f, 0.0f };
    check (nearNode > c.evaluate (0.05f) + 0.1f && near (farAway, c.evaluate (0.75f), 0.001f),
           "bending close to a node only changes the curve near that node");
    c.addNode (0.5f, 0.8f);
    check (c.nodes.size() == 3 && near (c.evaluate (0.5f), 0.8f), "double-click adds a node");
    check (c.removeNode (1) && c.nodes.size() == 2, "double-click on a node removes it");
    check (! c.removeNode (0), "first/last nodes can't be removed");
    auto sq = Curve::makePreset (4);
    check (near (sq.evaluate (0.25f), 1.0f) && near (sq.evaluate (0.75f), -1.0f), "square preset (vertical jump) evaluates");
    auto round = Curve::fromString (Curve::makePreset (0).toString());
    check (round.toString() == Curve::makePreset (0).toString(), "curve survives save/load");
    r.proc->setPlayHead (nullptr);
}

//==============================================================================
static void testMacroXYCross (const juce::PluginDescription& d)
{
    std::cout << "\n[Macro, XY pad, cross-modulation]" << std::endl;
    Rig r (d);
    auto& e = r.proc->engine;

    r.param (2)->setValue (0.5f);   // drive
    auto* macro = e.addModule (ModType::Macro);
    check (macro != nullptr && macro->params[0]->hostParam != nullptr, "macro is exposed to the DAW for automation ("
           + (macro->params[0]->hostParam != nullptr ? macro->params[0]->hostParam->getName (32) : juce::String()) + ")");
    e.addHostAssignment (macro->uid, 0, 1, 0.5f);    // cutoff up
    e.addHostAssignment (macro->uid, 0, 2, -0.5f);   // drive down

    macro->params[0]->setBaseNorm (1.0f);  // +100 %
    r.block();
    check (near (r.param (1)->getValue(), 0.8f) && near (r.param (2)->getValue(), 0.0f),
           "macro +100%: cutoff up (0.80), drive down (0.00) - one knob, opposite directions");
    macro->params[0]->setBaseNorm (0.0f);  // -100 %
    r.block();
    check (near (r.param (1)->getValue(), 0.0f) && near (r.param (2)->getValue(), 1.0f),
           "macro -100%: cutoff 0.00, drive 1.00 (bipolar)");
    macro->params[0]->setBaseNorm (0.5f);
    r.block();
    check (near (r.param (1)->getValue(), 0.3f) && near (r.param (2)->getValue(), 0.5f), "macro at 0 -> both at their base");

    // XY pad
    auto* xy = e.addModule (ModType::XYPad);
    xy->params[0]->setBaseNorm (xy->params[0]->range.convertTo0to1 (0.5f));
    xy->params[1]->setBaseNorm (xy->params[1]->range.convertTo0to1 (-0.5f));
    e.addHostAssignment (xy->uid, 0, 0, 0.5f);   // X -> gain
    r.block();
    check (near (xy->out[0], 0.5f) && near (xy->out[1], -0.5f), "XY pad outputs X and Y independently");
    check (near (r.param (0)->getValue(), 0.75f), "XY X axis drives gain (0.5 + 0.5*0.5 = 0.75)");

    // cross modulation: macro 2 -> LFO rate, LFO -> randomizer amount
    auto* macro2 = e.addModule (ModType::Macro);
    auto* lfo = e.addModule (ModType::LFO);
    auto* rnd = e.addModule (ModType::Randomizer);
    const float rateBase = lfo->params[0]->getBaseNorm();
    check (e.addModuleAssignment (macro2->uid, 0, lfo->uid, "rate", 0.3f) != nullptr, "macro 2 -> LFO rate assignment");
    check (e.addModuleAssignment (lfo->uid, 0, lfo->uid, "phase", 0.3f) == nullptr, "a module can't modulate itself");
    macro2->params[0]->setBaseNorm (1.0f);
    r.block();
    check (near (lfo->params[0]->effNorm.load(), rateBase + 0.3f, 0.001f), "cross-mod: macro raises the LFO's rate");
    check (e.getIncoming (lfo->uid, "rate").size() == 1, "incoming indicator data for the LFO rate");

    e.addModuleAssignment (lfo->uid, 0, rnd->uid, "amount", 0.5f);
    float mn = 2.0f, mx = -2.0f;
    for (int b = 0; b < 80; ++b) { r.block(); mn = juce::jmin (mn, rnd->params[0]->getEffReal()); mx = juce::jmax (mx, rnd->params[0]->getEffReal()); }
    check (mx - mn > 30.0f, "cross-mod: LFO sweeps the randomizer's Amount (" + juce::String (mn, 0) + "% .. " + juce::String (mx, 0) + "%)");

    // chains are evaluated without feedback blow-ups
    e.addModuleAssignment (rnd->uid, 0, lfo->uid, "phase", 0.5f);
    r.blocks (20);
    check (std::isfinite (lfo->out[0]) && std::abs (lfo->out[0]) <= 1.0f, "modulation loops stay stable");

    // pool limits
    juce::String why;
    for (int i = 0; i < 6; ++i) e.addModule (ModType::Macro);
    const bool refused = ! e.canAddModule (ModType::Macro, why);
    check (refused, "9th macro refused: " + why);

    // removing a module removes its routings
    const auto before = e.assignments.size();
    e.removeModule (lfo->uid);
    check (e.assignments.size() < before && e.findModule (lfo->uid) == nullptr, "removing a module removes its routings");
}

//==============================================================================
static void testRandomEnvMidi (const juce::PluginDescription& d)
{
    std::cout << "\n[Randomizer, Envelope Follower, MIDI Macro]" << std::endl;
    Rig r (d);
    auto& e = r.proc->engine;

    auto* rnd = dynamic_cast<RandomModule*> (e.addModule (ModType::Randomizer));
    rnd->params[0]->setFromText ("80");
    rnd->params[1]->setFromText ("0");
    rnd->params[3]->setFromText ("20");
    std::set<int> distinct;
    float mx = 0.0f;
    for (int b = 0; b < 100; ++b) { r.block(); distinct.insert (juce::roundToInt (rnd->out[0] * 1000.0f)); mx = juce::jmax (mx, std::abs (rnd->out[0])); }
    check (distinct.size() > 10, "randomizer produces changing values (" + juce::String ((int) distinct.size()) + " distinct)");
    check (mx <= 0.8001f, "randomizer stays inside +/- Amount (max " + juce::String (mx, 2) + ")");

    // smoothing: compare block-to-block jumps
    auto jumpiness = [&] (const char* smooth)
    {
        rnd->params[1]->setFromText (smooth);
        float prev = rnd->out[0], total = 0.0f;
        for (int b = 0; b < 200; ++b) { r.block(); total += std::abs (rnd->out[0] - prev); prev = rnd->out[0]; }
        return total;
    };
    const float stepped = jumpiness ("0"), smooth = jumpiness ("100");
    juce::ignoreUnused (stepped, smooth);
    // count of large single-block jumps is the real signature of stepped motion
    auto bigJumps = [&] (const char* sm)
    {
        rnd->params[1]->setFromText (sm);
        rnd->params[3]->setFromText ("5");
        float prev = rnd->out[0]; int n = 0;
        for (int b = 0; b < 400; ++b) { r.block(); if (std::abs (rnd->out[0] - prev) > 0.2f) ++n; prev = rnd->out[0]; }
        return n;
    };
    const int jStep = bigJumps ("0"), jSmooth = bigJumps ("100");
    check (jStep > jSmooth * 3, "Smooth 0% = stepped jumps (" + juce::String (jStep) + "), 100% = gliding (" + juce::String (jSmooth) + ")");

    // envelope follower
    auto* env = dynamic_cast<EnvFollowerModule*> (e.addModule (ModType::EnvFollower));
    env->params[1]->setFromText ("1");
    env->params[2]->setFromText ("20");
    e.addHostAssignment (env->uid, 0, 1, 0.5f);
    r.inputLevel = 0.0f; r.blocks (20);
    const float quiet = env->out[0];
    r.inputLevel = 0.8f; r.blocks (5);
    const float loud = env->out[0];
    check (quiet < 0.01f && near (loud, 0.8f, 0.05f), "envelope follower tracks input level (" + juce::String (quiet, 2) + " -> " + juce::String (loud, 2) + ")");
    check (near (r.param (1)->getValue(), 0.3f + 0.5f * loud, 0.03f), "... and drives the plug-in parameter with it");
    env->params[0]->setFromText ("12");
    r.inputLevel = 0.1f; r.blocks (10);
    check (env->out[0] > 0.3f, "Input gain makes quiet signals register (" + juce::String (env->out[0], 2) + ")");
    env->rmsMode.store (true);
    r.blocks (10);
    check (env->out[0] > 0.0f, "RMS mode works");
    r.inputLevel = 0.5f;

    // MIDI macro learn
    auto* mm = dynamic_cast<MidiMacroModule*> (e.addModule (ModType::MidiMacro));
    mm->learning.store (true);
    r.midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 10);
    r.block();
    check (mm->ccNumber.load() == 74 && ! mm->learning.load(), "MIDI Learn grabs the first CC (CC 74)");
    check (near (mm->params[0]->getBaseReal(), 100.0f, 0.5f) && near (mm->out[0], 100.0f / 127.0f), "MIDI macro follows the CC value (100)");
    r.midi.addEvent (juce::MidiMessage::controllerEvent (1, 7, 5), 0);
    r.block();
    check (near (mm->params[0]->getBaseReal(), 100.0f, 0.5f), "other CC numbers are ignored");
    e.addHostAssignment (mm->uid, 0, 0, -0.5f);
    r.block();
    check (near (r.param (0)->getValue(), 0.5f - 0.5f * 100.0f / 127.0f, 0.01f), "one hardware knob drives the plug-in");
}

//==============================================================================
static void testBlender (const juce::PluginDescription& d)
{
    std::cout << "\n[Preset Blender]" << std::endl;
    Rig r (d);
    auto& e = r.proc->engine;
    auto* bl = dynamic_cast<BlenderModule*> (e.addModule (ModType::Blender));

    r.param (0)->setValue (0.1f); r.param (1)->setValue (0.2f);
    r.proc->captureSnapshot (bl->uid, 0);
    r.param (0)->setValue (0.9f); r.param (1)->setValue (0.8f);
    r.proc->captureSnapshot (bl->uid, 1);
    check (bl->slots[0].filled() && bl->slots[1].filled() && bl->slots[0].name == "Snapshot 1", "snapshots captured into slots 1 and 2");

    auto setPos = [&] (float range, float angle)
    {
        bl->params[0]->setBaseNorm (bl->params[0]->range.convertTo0to1 (range));
        bl->params[1]->setBaseNorm (bl->params[1]->range.convertTo0to1 (angle));
        r.block();
    };

    setPos (1.0f, 0.0f);
    check (near (r.param (0)->getValue(), 0.1f) && near (r.param (1)->getValue(), 0.2f), "node on slot 1 -> plug-in = snapshot 1");
    setPos (1.0f, 90.0f);
    check (near (r.param (0)->getValue(), 0.9f) && near (r.param (1)->getValue(), 0.8f), "node on slot 2 -> plug-in = snapshot 2");
    setPos (0.0f, 0.0f);
    check (near (r.param (0)->getValue(), 0.5f) && near (r.param (1)->getValue(), 0.5f), "centre -> even mix of the filled slots");
    setPos (0.5f, 45.0f);
    check (near (r.param (0)->getValue(), 0.5f), "halfway between 1 and 2 -> 50/50 morph");
    setPos (0.6f, 0.0f);
    const float g = r.param (0)->getValue();
    check (g > 0.1f && g < 0.5f, "every parameter is interpolated continuously (gain " + juce::String (g, 3) + ")");

    // Blender + LFO on the same parameter: the blend sets the base, the LFO moves around it
    auto* lfo = e.addModule (ModType::LFO);
    e.addHostAssignment (lfo->uid, 0, 0, 0.05f);
    setPos (1.0f, 90.0f);
    check (near (e.getHostBase (0), 0.9f, 0.01f), "blender sets the base of a modulated parameter");

    // factory preset capture (switches program, reads, restores)
    const float before = r.param (2)->getValue();
    r.proc->captureProgram (bl->uid, 2, 2);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
    check (bl->slots[2].name == "Hard" && bl->slots[2].filled() && near (bl->slots[2].values[0], 0.9f),
           "factory preset 'Hard' captured into slot 3");
    check (near (r.param (2)->getValue(), before, 0.001f) && ! e.writesSuspended.load(), "plug-in restored after reading the preset");
    check (r.proc->getHostedProgramNames().size() == 3, "factory preset names listed");

    r.proc->clearBlendSlot (bl->uid, 2);
    check (! bl->slots[2].filled(), "slot can be cleared");
}

//==============================================================================
static void testState (const juce::PluginDescription& d)
{
    std::cout << "\n[Save / load (DAW project + presets)]" << std::endl;
    Rig r (d);
    auto& e = r.proc->engine;

    auto* lfo = dynamic_cast<LfoModule*> (e.addModule (ModType::LFO));
    Curve c = Curve::makePreset (1);
    c.addNode (0.6f, 0.3f);
    c.segs[0].bend = 0.7f;
    lfo->curve = c; lfo->presetIndex = -1;
    lfo->tempoSync.store (true);
    lfo->playMode.store (LfoModule::PingPong);
    auto* macro = e.addModule (ModType::Macro);
    macro->params[0]->setBaseNorm (0.8f);
    auto* bl = dynamic_cast<BlenderModule*> (e.addModule (ModType::Blender));
    r.proc->captureSnapshot (bl->uid, 1);
    auto* mm = dynamic_cast<MidiMacroModule*> (e.addModule (ModType::MidiMacro));
    mm->ccNumber.store (21);
    e.addHostAssignment (lfo->uid, 0, 1, -0.33f);
    e.addModuleAssignment (macro->uid, 0, lfo->uid, "rate", 0.25f);
    r.blocks (1);                   // blender applies its blend once
    r.param (1)->setValue (0.42f);  // then the user tweaks the knob in the plug-in
    r.blocks (3);

    juce::MemoryBlock state;
    r.proc->getStateInformation (state);
    if (auto x = r.proc->createStateXml()) if (auto* pe = x->getChildByName ("PLUGIN")) std::cout << "    " << pe->getFirstChildElement()->toString (juce::XmlElement::TextFormat().singleLine()) << std::endl;
    check (state.getSize() > 100, "state saved (" + juce::String ((int) state.getSize()) + " bytes)");

    BigModProcessor p2;
    p2.setPlayConfigDetails (2, 2, SR, BS);
    p2.prepareToPlay (SR, BS);
    p2.setStateInformation (state.getData(), (int) state.getSize());
    auto& e2 = p2.engine;
    check (p2.getHostedPlugin() != nullptr && p2.getHostedName() == r.proc->getHostedName(), "hosted plug-in reloaded " + p2.lastError);
    check (e2.modules.size() == 4, "all modules restored");
    check (e2.assignments.size() == 2, "all assignments restored");
    auto* lfo2 = dynamic_cast<LfoModule*> (e2.modules[0].get());
    check (lfo2 != nullptr && lfo2->curve.toString() == c.toString(), "custom LFO curve restored exactly");
    check (lfo2 != nullptr && lfo2->tempoSync.load() && lfo2->playMode.load() == LfoModule::PingPong, "LFO sync + playback mode restored");
    check (near (e2.modules[1]->params[0]->getBaseNorm(), 0.8f, 0.001f), "macro position restored (DAW parameter)");
    auto* bl2 = dynamic_cast<BlenderModule*> (e2.modules[2].get());
    check (bl2 != nullptr && bl2->slots[1].filled() && bl2->slots[1].values.size() == bl->slots[1].values.size(), "blender snapshots restored");
    check (dynamic_cast<MidiMacroModule*> (e2.modules[3].get())->ccNumber.load() == 21, "MIDI CC mapping restored");
    check (near (e2.getHostBase (1), 0.42f, 0.001f), "un-modulated base of the plug-in knob restored (" + juce::String (e2.getHostBase (1), 3) + ", orig " + juce::String (e.getHostBase (1), 3) + ")");
    check (std::abs (e2.assignments[0]->depth.load() + 0.33f) < 0.001f, "modulation depth restored");

    // preset file round trip
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bigmod_test.bigmod");
    check (r.proc->savePresetToFile (f), "preset file written");
    r.proc->clearRack();
    check (r.proc->engine.modules.empty(), "rack cleared");
    check (r.proc->loadPresetFromFile (f) && r.proc->engine.modules.size() == 4, "preset file loaded back");
    f.deleteFile();
}

//==============================================================================
static void testInstrument (const juce::PluginDescription& d)
{
    std::cout << "\n[Instrument mode]" << std::endl;
    Rig r (d);
    auto& e = r.proc->engine;
    check (r.proc->getHostedPlugin() != nullptr, "synth loads inside BigMod Instrument");
    r.midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
    r.block();
    check (r.buffer.getMagnitude (0, 0, BS) > 0.2f, "MIDI reaches the synth and it plays (" + juce::String (r.buffer.getMagnitude (0, 0, BS), 2) + ")");

    auto* lfo = e.addModule (ModType::LFO);
    lfo->params[0]->setFromText ("4");
    e.addHostAssignment (lfo->uid, 0, 0, 0.45f);
    float mn = 10.0f, mx = 0.0f;
    for (int b = 0; b < 40; ++b) { r.block(); const float m = r.buffer.getMagnitude (0, 0, BS); mn = juce::jmin (mn, m); mx = juce::jmax (mx, m); }
    check (mx - mn > 0.4f, "LFO tremolo on the synth's level (" + juce::String (mn, 2) + " .. " + juce::String (mx, 2) + ")");

    auto* env = dynamic_cast<EnvFollowerModule*> (e.addModule (ModType::EnvFollower));
    check (env->followOutput.load(), "envelope follower listens to the synth's output by default");
    r.blocks (10);
    check (env->out[0] > 0.05f, "envelope follower reacts to the synth (" + juce::String (env->out[0], 2) + ")");
}

//==============================================================================
static void populateForScreenshot (BigModProcessor& p)
{
    auto& e = p.engine;
    auto* lfo = e.addModule (ModType::LFO);
    auto* macro = e.addModule (ModType::Macro);
    auto* rnd = e.addModule (ModType::Randomizer);
    auto* xy = e.addModule (ModType::XYPad);
    auto* env = e.addModule (ModType::EnvFollower);
    auto* bl = e.addModule (ModType::Blender);
    auto* mm = e.addModule (ModType::MidiMacro);
    e.addHostAssignment (lfo->uid, 0, 0, 0.15f);
    e.addHostAssignment (macro->uid, 0, 1, 0.6f);
    e.addHostAssignment (macro->uid, 0, 2, -0.4f);
    e.addModuleAssignment (macro->uid, 0, lfo->uid, "rate", 0.3f);
    e.addHostAssignment (xy->uid, 0, 1, 0.5f);
    e.addHostAssignment (xy->uid, 1, 2, -0.5f);
    e.addHostAssignment (rnd->uid, 0, 2, 0.2f);
    e.addModuleAssignment (env->uid, 0, rnd->uid, "amount", 0.4f);
    p.captureSnapshot (bl->uid, 0);
    p.captureSnapshot (bl->uid, 1);
    macro->params[0]->setBaseNorm (0.75f);
    xy->params[0]->setBaseNorm (0.7f);
    xy->params[1]->setBaseNorm (0.35f);
    bl->params[0]->setBaseNorm (0.45f);
    bl->params[1]->setBaseNorm (40.0f / 360.0f);
    dynamic_cast<MidiMacroModule*> (mm)->ccNumber.store (74);
    mm->params[0]->baseNorm.store (0.6f);
}

static void saveSnapshot (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << "  wrote " << f.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")" << std::endl;
}

static void testGui (const juce::PluginDescription& d, const juce::File& outDir)
{
    std::cout << "\n[Editor]" << std::endl;
    Rig r (d);
    populateForScreenshot (*r.proc);

    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditorIfNeeded());
    auto* big = dynamic_cast<BigModEditor*> (ed.get());
    check (big != nullptr, "editor created");
    check (ed->getWidth() >= 860 && ed->getHeight() > RackMetrics::rackH, "editor snaps the rack under the plug-in ("
           + juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()) + ")");

    juce::DocumentWindow win ("BigMod test", juce::Colours::black, 0);
    win.setUsingNativeTitleBar (false);
    win.setTitleBarHeight (0);
    win.setContentNonOwned (ed.get(), true);
    win.setTopLeftPosition (0, 0);
    win.setVisible (true);

    juce::Thread::sleep (10);
    for (int i = 0; i < 40; ++i) { r.block(); juce::MessageManager::getInstance()->runDispatchLoopUntil (25); }
    saveSnapshot (*ed, outDir.getChildFile ("editor.png"));
    for (auto* child : ed->getChildren())
        if (auto* vp = dynamic_cast<juce::Viewport*> (child))
            if (auto* content = vp->getViewedComponent())
                saveSnapshot (*content, outDir.getChildFile ("rack_full.png"));

    juce::ChildProcess cp;
    if (cp.start ("import -window root " + outDir.getChildFile ("screen.png").getFullPathName().quoted()))
        std::cout << cp.readAllProcessOutput() << std::endl;

    // learn mode look
    auto& e = r.proc->engine;
    e.learnSourceUid.store (e.modules[1]->uid);
    for (int i = 0; i < 8; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    saveSnapshot (*ed, outDir.getChildFile ("editor_learn.png"));
    e.learnSourceUid.store (0);

    // resize follows the hosted editor
    if (auto* hosted = ed->getChildComponent (0))
        juce::ignoreUnused (hosted);

    // unload: rack only
    r.proc->unloadPlugin();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    check (ed->getHeight() == RackMetrics::rackH + RackMetrics::footerH, "without a plug-in the window is just the rack");
    saveSnapshot (*ed, outDir.getChildFile ("editor_empty.png"));

    win.clearContentComponent();
    ed.reset();
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 3) { std::cout << "usage: BigModTests <plugin.vst3> <outdir> [--gui]" << std::endl; return 2; }
    const juce::File pluginFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::File outDir (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    outDir.createDirectory();
    bool gui = false;
    for (int i = 3; i < argc; ++i) if (juce::String (argv[i]) == "--gui") gui = true;

    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> found;
    fmt.findAllTypesForFile (found, pluginFile.getFullPathName());
    std::cout << "BigMod tests (" << (BigModProcessor::isInstrument ? "instrument" : "effect") << " build)" << std::endl;
    check (found.size() == 1, "test plug-in discovered by the VST3 scanner: " + pluginFile.getFileName());
    if (found.isEmpty()) return 1;
    const auto desc = *found[0];

    if constexpr (BigModProcessor::isInstrument)
    {
        testInstrument (desc);
    }
    else
    {
        testBasics (desc);
        testLfoModes (desc);
        testMacroXYCross (desc);
        testRandomEnvMidi (desc);
        testBlender (desc);
        testState (desc);
    }

    if (gui) testGui (desc, outDir);

    std::cout << "\n" << passes << " passed, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
