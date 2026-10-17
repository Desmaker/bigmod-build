#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

//==============================================================================
// A breakpoint curve used by the LFO.
//  - nodes:    (x = time 0..1, y = amplitude -1..+1), sorted by x
//  - segments: one per gap between nodes, each with a `bend` (-1..+1) and the
//              `pos` (0..1 along the segment) where it was grabbed. Bending near
//              the middle of a segment bends all of it; bending near a node only
//              bends the part of the line close to that node (localised control).
//==============================================================================
struct CurveNode { float x = 0.0f, y = 0.0f; };
struct CurveSeg  { float pos = 0.5f, bend = 0.0f; };

struct Curve
{
    std::vector<CurveNode> nodes;
    std::vector<CurveSeg>  segs;

    static float segShape (float t, const CurveSeg& s) noexcept
    {
        const float b = juce::jlimit (-1.0f, 1.0f, s.bend);
        if (std::abs (b) < 1.0e-4f) return t;

        // The bend acts on a window around the grabbed point: grabbed in the middle the
        // whole segment curves, grabbed near a node only the part next to that node does.
        const float p = juce::jlimit (0.02f, 0.98f, s.pos);
        const float r0 = p <= 0.5f ? 0.0f : 2.0f * p - 1.0f;
        const float r1 = p <= 0.5f ? 2.0f * p : 1.0f;
        if (t <= r0 || t >= r1) return t;

        const float w = r1 - r0;
        const float x = (t - r0) / w;
        const float c = std::exp (4.0f * std::abs (b)) - 1.0f;     // curve tension
        const float y = b > 0.0f ? x * (1.0f + c) / (1.0f + c * x)
                                 : 1.0f - (1.0f - x) * (1.0f + c) / (1.0f + c * (1.0f - x));
        return r0 + w * y;
    }

    // Index of the segment that contains x, or -1 if none (x past the end)
    int segmentAt (float x) const noexcept
    {
        for (int i = 0; i + 1 < (int) nodes.size(); ++i)
            if (x >= nodes[(size_t) i].x && x < nodes[(size_t) i + 1].x)
                return i;
        return -1;
    }

    float evaluate (float x) const noexcept
    {
        if (nodes.empty()) return 0.0f;
        if (x <= nodes.front().x) return nodes.front().y;
        if (x >= nodes.back().x)  return nodes.back().y;

        const int i = segmentAt (x);
        if (i < 0) return nodes.back().y;

        const auto& n0 = nodes[(size_t) i];
        const auto& n1 = nodes[(size_t) i + 1];
        const float w = n1.x - n0.x;
        if (w <= 0.0f) return n1.y;
        const float t = (x - n0.x) / w;
        return n0.y + (n1.y - n0.y) * segShape (t, segs[(size_t) i]);
    }

    void fixSegments()
    {
        const size_t want = nodes.size() > 1 ? nodes.size() - 1 : 0;
        segs.resize (want);
    }

    void addNode (float x, float y)
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        y = juce::jlimit (-1.0f, 1.0f, y);
        const int seg = segmentAt (x);
        if (seg < 0) return;
        nodes.insert (nodes.begin() + seg + 1, { x, y });
        segs.insert  (segs.begin()  + seg + 1, CurveSeg {});
        segs[(size_t) seg] = CurveSeg {};
    }

    bool removeNode (int index)
    {
        if (index <= 0 || index >= (int) nodes.size() - 1) return false; // keep first & last
        nodes.erase (nodes.begin() + index);
        segs.erase (segs.begin() + index);
        segs[(size_t) index - 1] = CurveSeg {};
        return true;
    }

    //==========================================================================
    static juce::StringArray presetNames()
    {
        return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Pulse 25%",
                 "Half Sine", "Exp Rise", "Exp Fall", "Steps", "Ramp Hold" };
    }

    static Curve makePreset (int index)
    {
        Curve c;
        auto n = [&c] (float x, float y) { c.nodes.push_back ({ x, y }); };
        switch (index)
        {
            default:
            case 0: n (0, 0); n (0.25f, 1); n (0.5f, 0); n (0.75f, -1); n (1, 0);
                    c.fixSegments();
                    c.segs = { { 0.5f, 0.23f }, { 0.5f, -0.23f }, { 0.5f, 0.23f }, { 0.5f, -0.23f } };
                    return c;
            case 1: n (0, 0); n (0.25f, 1); n (0.75f, -1); n (1, 0); break;
            case 2: n (0, -1); n (1, 1); break;
            case 3: n (0, 1); n (1, -1); break;
            case 4: n (0, 1); n (0.5f, 1); n (0.5f, -1); n (1, -1); break;
            case 5: n (0, 1); n (0.25f, 1); n (0.25f, -1); n (1, -1); break;
            case 6: n (0, -1); n (0.5f, 1); n (1, -1);
                    c.fixSegments(); c.segs = { { 0.5f, 0.23f }, { 0.5f, -0.23f } };
                    return c;
            case 7: n (0, -1); n (1, 1); c.fixSegments(); c.segs = { { 0.5f, -0.6f } }; return c;
            case 8: n (0, 1); n (1, -1); c.fixSegments(); c.segs = { { 0.5f, 0.6f } }; return c;
            case 9: n (0, -1); n (0.25f, -1); n (0.25f, -0.333f); n (0.5f, -0.333f);
                    n (0.5f, 0.333f); n (0.75f, 0.333f); n (0.75f, 1); n (1, 1); break;
            case 10: n (0, -1); n (0.5f, 1); n (1, 1); break;
        }
        c.fixSegments();
        return c;
    }

    //==========================================================================
    juce::String toString() const
    {
        juce::StringArray s;
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            juce::String item = juce::String (nodes[i].x, 5) + "," + juce::String (nodes[i].y, 5);
            if (i < segs.size())
                item << "," << juce::String (segs[i].pos, 5) << "," << juce::String (segs[i].bend, 5);
            s.add (item);
        }
        return s.joinIntoString (";");
    }

    static Curve fromString (const juce::String& str)
    {
        Curve c;
        for (auto& item : juce::StringArray::fromTokens (str, ";", {}))
        {
            auto v = juce::StringArray::fromTokens (item, ",", {});
            if (v.size() < 2) continue;
            c.nodes.push_back ({ juce::jlimit (0.0f, 1.0f, v[0].getFloatValue()),
                                 juce::jlimit (-1.0f, 1.0f, v[1].getFloatValue()) });
            if (v.size() >= 4)
                c.segs.push_back ({ v[2].getFloatValue(), v[3].getFloatValue() });
        }
        if (c.nodes.size() < 2) return makePreset (0);
        c.fixSegments();
        return c;
    }
};
