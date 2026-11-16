#pragma once

#include "../ControlCards.h"
#include "Knob.h"
#include "Widgets.h"

//==============================================================================
/** Shows the selected modulator: an editable 16-step pattern for "Step", a preview of the wave otherwise. */
class ModDisplay : public juce::Component,
                   private juce::Timer
{
public:
    explicit ModDisplay (SimpleSamplerProcessor&);
    ~ModDisplay() override;

    void setMod (int modIndex);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void editStep (const juce::MouseEvent&);
    int currentShape() const;

    SimpleSamplerProcessor& processor;
    int mod = 0;
    juce::RangedAudioParameter* typeParam = nullptr;
    juce::RangedAudioParameter* stepParams[TempoSync::numSteps] = {};
    float lastPosition = -1.0f;
    int lastShape = -1;
    float lastSteps[TempoSync::numSteps] = {};
};

//==============================================================================
class ModulatorCard : public Card
{
public:
    explicit ModulatorCard (SimpleSamplerProcessor&);
    void setPad (int) override {}
    void setMod (int modIndex);
    void resized() override;
    static constexpr int height = 33 + fieldRowHeight + 6 + 76 + 12;

private:
    void syncChanged (bool isSynced);

    int mod = 0;
    CaptionCombo type { "Type" }, rate { "Rate (sync)" }, mode { "Phase" };
    NumberField hz;
    juce::ToggleButton syncToggle { "Sync" };
    ModDisplay display;
    std::unique_ptr<ParamField> hzParam;
    std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment;
    std::unique_ptr<juce::ParameterAttachment> syncListener;
};

//==============================================================================
/** The modulation matrix as a list: source, target, depth, remove. */
class MatrixCard : public Card
{
public:
    explicit MatrixCard (SimpleSamplerProcessor&);
    void setPad (int) override {}
    void resized() override;
    void paint (juce::Graphics&) override;
    /** Rebuilds the rows if the assignments changed. */
    void refresh();

private:
    class Row : public juce::Component
    {
    public:
        Row (SimpleSamplerProcessor&, const ModMatrix::SlotInfo&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void update (const ModMatrix::SlotInfo&);
        ModMatrix::SlotInfo info;
    private:
        SimpleSamplerProcessor& processor;
        NumberField depth;
        juce::TextButton remove { "x" };
        juce::String targetName;
    };

    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Row> rows;
    uint32_t matrixVersion = 0xffffffffu;
    static constexpr int rowHeight = 28;
};

//==============================================================================
class RandomizerCard : public Card
{
public:
    explicit RandomizerCard (SimpleSamplerProcessor&);
    void setPad (int) override {}
    void resized() override;
    void refresh();
    static constexpr int height = 33 + 28 + 6 + fieldRowHeight + 12;

private:
    juce::TextButton randomizeButton { "Randomize" }, undoButton { "Undo" };
    juce::ToggleButton autoToggle { "Auto" };
    CaptionCombo scope { "Scope" }, interval { "Auto every" };
    NumberField amount, tempo;
    std::unique_ptr<ParamField> amountParam, tempoParam;
    std::unique_ptr<juce::ButtonParameterAttachment> autoAttachment;
    juce::String lastSummary;
};

//==============================================================================
class ModPanel : public juce::Component,
                 private juce::Timer
{
public:
    explicit ModPanel (SimpleSamplerProcessor&);
    ~ModPanel() override;

    void setMod (int modIndex);
    int getMod() const noexcept                             { return mod; }
    void resized() override;

private:
    void timerCallback() override;

    int mod = 0;
    ModulatorCard modulator;
    MatrixCard matrix;
    RandomizerCard randomizer;
};
