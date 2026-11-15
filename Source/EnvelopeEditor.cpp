#include "EnvelopeEditor.h"
#include "PastelLookAndFeel.h"

using namespace ParamMath;

EnvelopeEditor::EnvelopeEditor (SimpleSamplerProcessor& p) : processor (p)
{
    setPad (0);
}

void EnvelopeEditor::setPad (int padIndex)
{
    pad = padIndex;
    static const char* const names[4] = { id::attack, id::decay, id::sustain, id::release };

    for (int i = 0; i < 4; ++i)
    {
        attachments[i].reset();
        params[i] = processor.getPadParameter (pad, names[i]);
        jassert (params[i] != nullptr);
        attachments[i] = std::make_unique<juce::ParameterAttachment> (*params[i], [this] (float) { repaint(); });
    }
    repaint();
}

float EnvelopeEditor::norm (int which) const
{
    return params[which] != nullptr ? params[which]->getValue() : 0.0f;
}

void EnvelopeEditor::setNorm (int which, float n)
{
    if (params[which] != nullptr && attachments[which] != nullptr)
        attachments[which]->setValueAsPartOfGesture (params[which]->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
}

EnvelopeEditor::Geometry EnvelopeEditor::getGeometry() const
{
    Geometry g;
    g.area = getLocalBounds().toFloat().reduced (8.0f, 8.0f);
    g.hold = g.area.getWidth() * 0.16f;
    g.segment = (g.area.getWidth() - g.hold) / 3.0f;

    // every stage keeps a minimum width so the handles never collapse onto each other
    auto stageWidth = [&] (int which) { return g.segment * (0.08f + 0.92f * std::sqrt (norm (which))); };

    const float top = g.area.getY(), bottom = g.area.getBottom();
    const float sustainY = bottom - (bottom - top) * norm (2);

    g.start        = { g.area.getX(), bottom };
    g.peak         = { g.start.x + stageWidth (0), top };
    g.sustainStart = { g.peak.x + stageWidth (1), sustainY };
    g.sustainEnd   = { g.sustainStart.x + g.hold, sustainY };
    g.end          = { g.sustainEnd.x + stageWidth (3), bottom };
    return g;
}

EnvelopeEditor::Handle EnvelopeEditor::handleAt (juce::Point<float> p) const
{
    const auto g = getGeometry();
    const juce::Point<float> points[3] = { g.peak, g.sustainStart, g.end };

    Handle best = none;
    float bestDistance = 16.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = points[i].getDistanceFrom (p);
        if (d < bestDistance) { bestDistance = d; best = (Handle) i; }
    }
    return best;
}

void EnvelopeEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (PastelLookAndFeel::track());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    const auto geo = getGeometry();

    juce::Path curve;
    curve.startNewSubPath (geo.start);
    // slightly curved stages (exponential feel)
    curve.quadraticTo ({ geo.start.x + (geo.peak.x - geo.start.x) * 0.35f, geo.peak.y + (geo.start.y - geo.peak.y) * 0.25f }, geo.peak);
    curve.quadraticTo ({ geo.peak.x + (geo.sustainStart.x - geo.peak.x) * 0.3f, geo.sustainStart.y }, geo.sustainStart);
    curve.lineTo (geo.sustainEnd);
    curve.quadraticTo ({ geo.sustainEnd.x + (geo.end.x - geo.sustainEnd.x) * 0.3f, geo.end.y }, geo.end);

    juce::Path fill (curve);
    fill.lineTo (geo.start);
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (PastelLookAndFeel::accent().withAlpha (0.28f), 0.0f, geo.area.getY(),
                                             PastelLookAndFeel::accent().withAlpha (0.03f), 0.0f, geo.area.getBottom(), false));
    g.fillPath (fill);

    g.setColour (PastelLookAndFeel::accent());
    g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const juce::Point<float> points[3] = { geo.peak, geo.sustainStart, geo.end };
    for (int i = 0; i < 3; ++i)
    {
        const bool hot = (active == i) || (active == none && hover == i);
        const float r = hot ? 6.0f : 4.5f;
        if (hot)
        {
            g.setColour (PastelLookAndFeel::accent().withAlpha (0.18f));
            g.fillEllipse (juce::Rectangle<float> (r * 3.6f, r * 3.6f).withCentre (points[i]));
        }
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (points[i]));
        g.setColour (PastelLookAndFeel::accent());
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (points[i]), 1.8f);
    }
}

void EnvelopeEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto h = handleAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h == none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
        repaint();
    }
}

void EnvelopeEditor::mouseExit (const juce::MouseEvent&)
{
    if (hover != none) { hover = none; repaint(); }
}

void EnvelopeEditor::mouseDown (const juce::MouseEvent& e)
{
    active = handleAt (e.position);
    if (active == none)
        return;

    const int first = active == attackHandle ? 0 : active == decayHandle ? 1 : 3;
    attachments[first]->beginGesture();
    if (active == decayHandle)
        attachments[2]->beginGesture();
    repaint();
}

void EnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (active == none)
        return;

    const auto geo = getGeometry();
    // inverse of stageWidth(): width = segment * (0.08 + 0.92 * sqrt(n))
    auto widthToNorm = [&] (float width)
    {
        const float s = juce::jlimit (0.0f, 1.0f, (width / geo.segment - 0.08f) / 0.92f);
        return s * s;
    };

    switch (active)
    {
        case attackHandle:
            setNorm (0, widthToNorm (e.position.x - geo.start.x));
            break;
        case decayHandle:
            setNorm (1, widthToNorm (e.position.x - geo.peak.x));
            setNorm (2, (geo.area.getBottom() - e.position.y) / geo.area.getHeight());
            break;
        case releaseHandle:
            setNorm (3, widthToNorm (e.position.x - geo.sustainEnd.x));
            break;
        case none:
        default:
            break;
    }
}

void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    if (active == none)
        return;

    const int first = active == attackHandle ? 0 : active == decayHandle ? 1 : 3;
    attachments[first]->endGesture();
    if (active == decayHandle)
        attachments[2]->endGesture();
    active = none;
    repaint();
}
