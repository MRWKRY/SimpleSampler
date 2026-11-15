#include "WaveformView.h"
#include "PastelLookAndFeel.h"

using namespace ParamMath;

WaveformView::WaveformView (SimpleSamplerProcessor& p) : processor (p)
{
    noteField.setRange (0, 127, 1, 1);
    noteField.setPixelsPerStep (6.0);
    noteField.textFromValue = [] (double v)
    {
        const int n = juce::roundToInt (v);
        return noteName (n) + "  (" + juce::String (n) + ")";
    };
    noteField.valueFromText = [] (const juce::String& t)
    {
        const int n = parseNote (t.upToFirstOccurrenceOf ("(", false, false));
        return n <= -1000 ? std::numeric_limits<double>::quiet_NaN() : (double) n;
    };
    addAndMakeVisible (noteField);

    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    loadButton.onClick  = [this] { if (onBrowse) onBrowse (pad); };
    clearButton.onClick = [this] { processor.clearPad (pad); };

    setPad (0);
}

void WaveformView::setPad (int padIndex)
{
    pad = padIndex;

    noteParam.reset();
    noteParam = std::make_unique<ParamField> (*processor.getPadParameter (pad, id::note), noteField);

    loopAttachment.reset(); startAttachment.reset(); endAttachment.reset();
    loopParam  = processor.getPadParameter (pad, id::loop);
    startParam = processor.getPadParameter (pad, id::loopStart);
    endParam   = processor.getPadParameter (pad, id::loopEnd);
    loopAttachment  = std::make_unique<juce::ParameterAttachment> (*loopParam,  [this] (float) { repaint(); });
    startAttachment = std::make_unique<juce::ParameterAttachment> (*startParam, [this] (float) { repaint(); });
    endAttachment   = std::make_unique<juce::ParameterAttachment> (*endParam,   [this] (float) { repaint(); });

    playPosition = -1.0f;
    refreshContent();
}

void WaveformView::refreshContent()
{
    const auto& slot = processor.getPad (pad);
    sample   = slot.getSample();
    fileName = slot.getFileName();
    missing  = slot.isMissing();
    loading  = slot.loading.load();
    clearButton.setEnabled (slot.hasFile());
    cacheDirty = true;
    repaint();
}

//==============================================================================
juce::Rectangle<int> WaveformView::innerArea() const  { return getLocalBounds().reduced (15, 11); }
juce::Rectangle<int> WaveformView::headerArea() const { return innerArea().removeFromTop (30); }
juce::Rectangle<int> WaveformView::rulerArea() const  { return innerArea().withTrimmedTop (38).removeFromTop (18); }
juce::Rectangle<int> WaveformView::waveArea() const   { return innerArea().withTrimmedTop (56); }

float WaveformView::xForNorm (double n) const
{
    const auto w = waveArea();
    return (float) w.getX() + (float) (n * w.getWidth());
}

double WaveformView::normForX (float x) const
{
    const auto w = waveArea();
    return juce::jlimit (0.0, 1.0, (double) (x - (float) w.getX()) / juce::jmax (1, w.getWidth()));
}

void WaveformView::resized()
{
    auto header = headerArea();
    clearButton.setBounds (header.removeFromRight (58).reduced (0, 2));
    header.removeFromRight (6);
    loadButton.setBounds (header.removeFromRight (58).reduced (0, 2));
    header.removeFromRight (10);
    noteField.setBounds (header.removeFromRight (86).reduced (0, 2));
    cacheDirty = true;
}

//==============================================================================
void WaveformView::rebuildCache (float scale)
{
    const auto area = waveArea();
    const int w = juce::jmax (1, juce::roundToInt ((float) area.getWidth() * scale));
    const int h = juce::jmax (1, juce::roundToInt ((float) area.getHeight() * scale));

    cache = juce::Image (juce::Image::ARGB, w, h, true);
    cacheScale = scale;
    cacheDirty = false;

    if (sample == nullptr || sample->peakMax.empty())
        return;

    juce::Graphics g (cache);
    const float mid = (float) h * 0.5f;
    const float amp = (float) h * 0.46f;
    const int numPeaks = (int) sample->peakMax.size();

    juce::Path shape;
    std::vector<float> lows ((size_t) w);

    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((juce::int64) x * numPeaks / w);
        const int b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * numPeaks / w));
        float mn = 0.0f, mx = 0.0f;
        for (int i = a; i < b && i < numPeaks; ++i)
        {
            mn = juce::jmin (mn, sample->peakMin[(size_t) i]);
            mx = juce::jmax (mx, sample->peakMax[(size_t) i]);
        }
        // keep a hairline visible even for silence
        const float top = mid - juce::jmax (0.6f, juce::jlimit (0.0f, 1.0f, mx) * amp);
        lows[(size_t) x] = mid + juce::jmax (0.6f, juce::jlimit (0.0f, 1.0f, -mn) * amp);

        if (x == 0) shape.startNewSubPath (0.0f, top);
        else        shape.lineTo ((float) x, top);
    }
    for (int x = w; --x >= 0;)
        shape.lineTo ((float) x, lows[(size_t) x]);
    shape.closeSubPath();

    const auto colours = PastelLookAndFeel::padColours (pad);
    juce::ColourGradient gradient (colours.a, 0.0f, 0.0f, colours.b, (float) w, 0.0f, false);
    gradient.addColour (0.5, colours.a.interpolatedWith (colours.b, 0.5f).brighter (0.08f));
    g.setGradientFill (gradient);
    g.fillPath (shape);
}

void WaveformView::paintRuler (juce::Graphics& g)
{
    const auto ruler = rulerArea();
    const auto wave = waveArea();

    g.setColour (PastelLookAndFeel::track());
    g.fillRect (ruler.getX(), ruler.getBottom() - 1, ruler.getWidth(), 1);

    if (sample == nullptr || sample->lengthSeconds <= 0.0)
        return;

    const double length = sample->lengthSeconds;
    static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 30, 60, 120, 300 };
    double step = steps[std::size (steps) - 1];
    for (double s : steps)
        if (s / length * wave.getWidth() >= 64.0) { step = s; break; }

    const int decimals = step < 0.01 ? 3 : step < 0.1 ? 2 : step < 1.0 ? 1 : 0;
    g.setFont (PastelLookAndFeel::font (10.5f));

    for (int i = 0; i * step <= length + 1.0e-9; ++i)
    {
        const double t = i * step;
        const int x = juce::roundToInt (xForNorm (t / length));
        g.setColour (PastelLookAndFeel::textSoft().withAlpha (0.7f));
        g.fillRect (x, ruler.getBottom() - 6, 1, 6);

        if (x + 44 <= ruler.getRight())
        {
            g.setColour (PastelLookAndFeel::textSoft());
            g.drawText (juce::String (t, decimals) + (i == 0 ? " s" : ""), x + 4, ruler.getY(), 44, ruler.getHeight() - 3,
                        juce::Justification::centredLeft, false);
        }

        // minor ticks
        for (int k = 1; k < 4; ++k)
        {
            const double mt = t + step * k / 4.0;
            if (mt > length) break;
            g.setColour (PastelLookAndFeel::textSoft().withAlpha (0.35f));
            g.fillRect (juce::roundToInt (xForNorm (mt / length)), ruler.getBottom() - 3, 1, 3);
        }
    }
}

void WaveformView::paintLoop (juce::Graphics& g)
{
    if (sample == nullptr || startParam == nullptr || endParam == nullptr || loopParam == nullptr)
        return;

    const auto wave = waveArea();
    const bool loopOn = loopParam->getValue() > 0.5f;
    const float xs = xForNorm (juce::jmin (startParam->getValue(), endParam->getValue()));
    const float xe = xForNorm (juce::jmax (startParam->getValue(), endParam->getValue()));

    if (loopOn)
    {
        // fade everything outside the loop, tint the loop region
        g.setColour (juce::Colours::white.withAlpha (0.62f));
        g.fillRect (juce::Rectangle<float> ((float) wave.getX(), (float) wave.getY(), xs - (float) wave.getX(), (float) wave.getHeight()));
        g.fillRect (juce::Rectangle<float> (xe, (float) wave.getY(), (float) wave.getRight() - xe, (float) wave.getHeight()));
        g.setColour (PastelLookAndFeel::accent().withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float> (xs, (float) wave.getY(), xe - xs, (float) wave.getHeight()));
    }

    const auto colour = loopOn ? PastelLookAndFeel::accent() : PastelLookAndFeel::textSoft().withAlpha (0.7f);

    auto drawHandle = [&] (float x, bool isStart, bool hot)
    {
        g.setColour (colour);
        g.fillRect (juce::Rectangle<float> (x - 0.75f, (float) wave.getY(), 1.5f, (float) wave.getHeight()));

        // little flag at the top, pointing into the loop
        const float fw = hot ? 13.0f : 11.0f, fh = 15.0f;
        juce::Rectangle<float> flag (isStart ? x : x - fw, (float) wave.getY(), fw, fh);
        juce::Path p;
        p.addRoundedRectangle (flag.getX(), flag.getY(), flag.getWidth(), flag.getHeight(), 4.0f, 4.0f,
                               false, false, ! isStart, isStart);
        g.fillPath (p);

        g.setColour (juce::Colours::white);
        const float cx = flag.getCentreX(), cy = flag.getCentreY();
        juce::Path arrow;
        const float d = isStart ? 1.0f : -1.0f;
        arrow.addTriangle (cx - 2.0f * d, cy - 3.0f, cx - 2.0f * d, cy + 3.0f, cx + 2.5f * d, cy);
        g.fillPath (arrow);
    };

    drawHandle (xs, true, dragging == startHandle);
    drawHandle (xe, false, dragging == endHandle);
}

void WaveformView::paint (juce::Graphics& g)
{
    PastelLookAndFeel::drawCard (g, getLocalBounds().toFloat().reduced (1.0f));

    // ---- header: pad badge + file name
    auto header = headerArea();
    header.setRight (noteField.getX() - 44);

    const auto colours = PastelLookAndFeel::padColours (pad);
    auto badge = header.removeFromLeft (64).reduced (0, 4).toFloat();
    g.setGradientFill (juce::ColourGradient (colours.a, badge.getX(), 0.0f, colours.b, badge.getRight(), 0.0f, false));
    g.fillRoundedRectangle (badge, badge.getHeight() * 0.5f);
    g.setColour (juce::Colours::white);
    g.setFont (PastelLookAndFeel::font (12.0f, true));
    g.drawText ("PAD " + juce::String (pad + 1).paddedLeft ('0', 2), badge, juce::Justification::centred, false);

    header.removeFromLeft (10);
    g.setFont (PastelLookAndFeel::font (14.0f, true));
    g.setColour (missing ? juce::Colour (0xffe0607e) : PastelLookAndFeel::text());
    juce::String nameText = fileName.isNotEmpty() ? fileName : juce::String ("No sample");
    if (missing) nameText = "Missing: " + nameText;
    g.drawText (nameText, header, juce::Justification::centredLeft, true);

    if (sample != nullptr)
    {
        // file facts, right-aligned in the ruler row
        g.setFont (PastelLookAndFeel::font (10.5f));
        g.setColour (PastelLookAndFeel::textSoft());
        const auto info = juce::String (sample->lengthSeconds, 2) + " s  |  "
                        + juce::String (sample->sourceSampleRate / 1000.0, 1) + " kHz  |  "
                        + (sample->sourceChannels == 1 ? "mono" : "stereo");
        const auto ruler = rulerArea();
        g.drawText (info, ruler.withY (ruler.getY() - 9).withHeight (12), juce::Justification::centredRight, false);
    }

    g.setFont (PastelLookAndFeel::font (11.5f));
    g.setColour (PastelLookAndFeel::textSoft());
    g.drawText ("Note", noteField.getX() - 40, noteField.getY(), 34, noteField.getHeight(), juce::Justification::centredRight, false);

    // ---- waveform
    paintRuler (g);

    const auto wave = waveArea();
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRoundedRectangle (wave.toFloat(), 8.0f);
    g.setColour (PastelLookAndFeel::track().withAlpha (0.8f));
    g.fillRect (wave.getX(), wave.getCentreY(), wave.getWidth(), 1);

    if (sample != nullptr)
    {
        const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (cacheDirty || ! cache.isValid() || std::abs (scale - cacheScale) > 0.01f)
            rebuildCache (scale);

        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (cache, wave.toFloat());
        paintLoop (g);

        if (playPosition >= 0.0f)
        {
            const float x = xForNorm (playPosition);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (x - 1.5f, (float) wave.getY(), 3.0f, (float) wave.getHeight()));
            g.setColour (PastelLookAndFeel::text());
            g.fillRect (juce::Rectangle<float> (x - 0.5f, (float) wave.getY(), 1.0f, (float) wave.getHeight()));
        }
    }
    else
    {
        g.setFont (PastelLookAndFeel::font (14.0f));
        g.setColour (PastelLookAndFeel::textSoft());
        const juce::String message = loading ? "Loading..."
                                   : missing ? "The sample file was not found. Click to locate it."
                                             : "Drop an audio file here, or click to choose one";
        g.drawText (message, wave, juce::Justification::centred, true);
    }

    if (dragOver)
    {
        g.setColour (PastelLookAndFeel::accent().withAlpha (0.10f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 14.0f);
        g.setColour (PastelLookAndFeel::accent());
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 14.0f, 2.0f);
    }
}

//==============================================================================
void WaveformView::repaintCursor (float normalised)
{
    if (normalised < 0.0f)
        return;
    const auto wave = waveArea();
    repaint (juce::Rectangle<int> (juce::roundToInt (xForNorm (normalised)) - 4, wave.getY(), 8, wave.getHeight()));
}

void WaveformView::setPlayPosition (float normalised)
{
    if (sample == nullptr)
        normalised = -1.0f;

    if (std::abs (normalised - playPosition) < 1.0e-5f)
        return;

    repaintCursor (playPosition);
    playPosition = normalised;
    repaintCursor (playPosition);
}

WaveformView::Handle WaveformView::handleAt (juce::Point<int> p) const
{
    if (sample == nullptr || startParam == nullptr || endParam == nullptr)
        return none;

    const auto wave = waveArea().expanded (6, 0);
    if (! wave.contains (p))
        return none;

    const float ds = std::abs ((float) p.x - xForNorm (startParam->getValue()));
    const float de = std::abs ((float) p.x - xForNorm (endParam->getValue()));
    const float reach = 9.0f;

    if (ds > reach && de > reach)
        return none;

    if (std::abs (ds - de) < 0.5f)   // both on the same spot: pick by side
        return (float) p.x < xForNorm (startParam->getValue()) ? startHandle : endHandle;

    return ds < de ? startHandle : endHandle;
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (handleAt (e.getPosition()) != none ? juce::MouseCursor::LeftRightResizeCursor
                    : (sample == nullptr && waveArea().contains (e.getPosition())) ? juce::MouseCursor::PointingHandCursor
                                                                                  : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (sample == nullptr)
    {
        if (waveArea().contains (e.getPosition()) && ! loading && onBrowse)
            onBrowse (pad);
        return;
    }

    dragging = handleAt (e.getPosition());
    if (dragging == startHandle)      startAttachment->beginGesture();
    else if (dragging == endHandle)   endAttachment->beginGesture();
    repaint();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == none)
        return;

    const double minGap = 0.002;
    const double n = normForX ((float) e.x);

    if (dragging == startHandle)
        startAttachment->setValueAsPartOfGesture ((float) juce::jlimit (0.0, juce::jmax (0.0, (double) endParam->getValue() - minGap), n));
    else
        endAttachment->setValueAsPartOfGesture ((float) juce::jlimit (juce::jmin (1.0, (double) startParam->getValue() + minGap), 1.0, n));
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (dragging == startHandle)      startAttachment->endGesture();
    else if (dragging == endHandle)   endAttachment->endGesture();
    dragging = none;
    repaint();
}

//==============================================================================
bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (processor.getLoader().canRead (juce::File (f)))
            return true;
    return false;
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (processor.getLoader().canRead (file))
        {
            processor.loadSampleIntoPad (pad, file, true);
            break;
        }
    }
    repaint();
}
