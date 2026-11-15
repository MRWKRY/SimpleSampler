#include "PresetBar.h"
#include "PastelLookAndFeel.h"

namespace
{
    enum MenuIds
    {
        saveId = 1, saveAsId, loadFileId, openFolderId, initId,
        deleteId = 50,
        firstPresetId = 100
    };
}

void PresetBar::ChevronButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (3.0f);
    if (highlighted || down)
    {
        g.setColour (PastelLookAndFeel::accent().withAlpha (down ? 0.22f : 0.12f));
        g.fillEllipse (b.withSizeKeepingCentre (juce::jmin (b.getWidth(), b.getHeight()), juce::jmin (b.getWidth(), b.getHeight())));
    }

    const float cx = b.getCentreX(), cy = b.getCentreY(), d = right ? 1.0f : -1.0f;
    juce::Path p;
    p.startNewSubPath (cx - 2.5f * d, cy - 5.0f);
    p.lineTo (cx + 2.5f * d, cy);
    p.lineTo (cx - 2.5f * d, cy + 5.0f);
    g.setColour (highlighted ? PastelLookAndFeel::accent() : PastelLookAndFeel::text().withAlpha (0.75f));
    g.strokePath (p, juce::PathStrokeType (1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
PresetBar::PresetBar (SimpleSamplerProcessor& p) : processor (p), presets (p)
{
    addAndMakeVisible (previousButton);
    addAndMakeVisible (nextButton);
    previousButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");

    previousButton.onClick = [this] { if (! presets.loadPrevious()) flash ("No presets yet"); };
    nextButton.onClick     = [this] { if (! presets.loadNext())     flash ("No presets yet"); };

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refresh();
}

PresetBar::~PresetBar()
{
    stopTimer();
}

void PresetBar::refresh()
{
    const auto name = processor.getPresetName();
    if (name != currentName)
    {
        currentName = name;
        repaint();
    }
}

juce::Rectangle<int> PresetBar::nameArea() const
{
    return getLocalBounds().reduced (34, 0);
}

void PresetBar::resized()
{
    auto r = getLocalBounds().reduced (3, 0);
    previousButton.setBounds (r.removeFromLeft (30));
    nextButton.setBounds (r.removeFromRight (30));
    if (nameEditor != nullptr)
        nameEditor->setBounds (nameArea().reduced (2, 5));
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    PastelLookAndFeel::drawCard (g, bounds, bounds.getHeight() * 0.5f, isMouseOver (true) ? 0.82f : 0.66f);

    if (nameEditor != nullptr)
        return;

    auto area = nameArea();

    if (flashText.isNotEmpty())
    {
        g.setColour (PastelLookAndFeel::accent());
        g.setFont (PastelLookAndFeel::font (14.0f, true));
        g.drawText (flashText, area, juce::Justification::centred, true);
        return;
    }

    // small chevron showing that the name opens a menu
    auto arrowArea = area.removeFromRight (16).toFloat();
    juce::Path arrow;
    arrow.startNewSubPath (arrowArea.getCentreX() - 4.0f, arrowArea.getCentreY() - 2.0f);
    arrow.lineTo (arrowArea.getCentreX(), arrowArea.getCentreY() + 2.5f);
    arrow.lineTo (arrowArea.getCentreX() + 4.0f, arrowArea.getCentreY() - 2.0f);
    g.setColour (PastelLookAndFeel::textSoft());
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto labelArea = area.removeFromLeft (52);
    g.setFont (PastelLookAndFeel::font (10.5f));
    g.drawText ("PRESET", labelArea, juce::Justification::centredLeft, false);

    g.setColour (PastelLookAndFeel::text());
    g.setFont (PastelLookAndFeel::font (14.5f, true));
    g.drawText (currentName.isNotEmpty() ? currentName : juce::String ("Init"), area, juce::Justification::centredLeft, true);
}

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    if (nameEditor == nullptr && nameArea().contains (e.getPosition()))
        showMenu();
}

//==============================================================================
void PresetBar::showMenu()
{
    const auto names = presets.getPresetNames();
    const bool currentExists = presets.presetExists (currentName);

    juce::PopupMenu menu;
    menu.addItem (saveId, currentExists ? "Save \"" + currentName + "\"" : juce::String ("Save..."));
    menu.addItem (saveAsId, "Save as...");
    menu.addSeparator();
    menu.addItem (loadFileId, "Load from file...");
    menu.addItem (openFolderId, "Show preset folder");
    menu.addItem (initId, "New (clear all pads)");

    if (currentExists)
    {
        juce::PopupMenu del;
        del.addItem (deleteId, "Yes, delete \"" + currentName + "\"");
        menu.addSubMenu ("Delete preset", del);
    }

    menu.addSeparator();
    menu.addSectionHeader (names.isEmpty() ? "No saved presets" : "Presets");
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (firstPresetId + i, names[i], true, names[i].equalsIgnoreCase (currentName));

    juce::Component::SafePointer<PresetBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withMaximumNumColumns (1),
                        [safe, names] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& self = *safe;

        if (result == saveId)
        {
            if (self.presets.presetExists (self.currentName)) self.saveAs (self.currentName);
            else                                              self.beginNaming();
        }
        else if (result == saveAsId)
        {
            self.beginNaming();
        }
        else if (result == loadFileId)
        {
            self.chooser = std::make_unique<juce::FileChooser> ("Load preset", self.presets.getFolder(),
                                                                juce::String ("*") + PresetManager::extension);
            self.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [safe] (const juce::FileChooser& fc)
                                       {
                                           if (safe == nullptr) return;
                                           const auto file = fc.getResult();
                                           if (file.existsAsFile() && ! safe->presets.loadPresetFile (file))
                                               safe->flash ("Could not load preset");
                                       });
        }
        else if (result == openFolderId)
        {
            self.presets.getFolder().createDirectory();
            self.presets.getFolder().revealToUser();
        }
        else if (result == initId)
        {
            self.processor.resetToInit();
        }
        else if (result == deleteId)
        {
            if (self.presets.deletePreset (self.currentName))
                self.flash ("Deleted");
        }
        else if (result >= firstPresetId && result - firstPresetId < names.size())
        {
            if (! self.presets.loadPreset (names[result - firstPresetId]))
                self.flash ("Could not load preset");
        }
    });
}

void PresetBar::beginNaming()
{
    if (nameEditor != nullptr)
        return;

    nameEditor = std::make_unique<juce::TextEditor>();
    nameEditor->setFont (PastelLookAndFeel::font (14.0f, true));
    nameEditor->setJustification (juce::Justification::centredLeft);
    nameEditor->setTextToShowWhenEmpty ("Preset name, then Enter", PastelLookAndFeel::textSoft());
    nameEditor->setText (currentName == "Init" ? juce::String() : currentName, false);
    nameEditor->setSelectAllWhenFocused (true);
    nameEditor->setInputRestrictions (60);
    addAndMakeVisible (*nameEditor);
    nameEditor->setBounds (nameArea().reduced (2, 5));

    juce::Component::SafePointer<PresetBar> safe (this);
    nameEditor->onReturnKey = [safe] { if (safe != nullptr) safe->finishNaming (true); };
    nameEditor->onEscapeKey = [safe] { if (safe != nullptr) safe->finishNaming (false); };
    nameEditor->onFocusLost = [safe] { if (safe != nullptr) safe->finishNaming (false); };
    nameEditor->grabKeyboardFocus();
    repaint();
}

void PresetBar::finishNaming (bool save)
{
    if (nameEditor == nullptr)
        return;

    const auto typed = nameEditor->getText();
    auto dying = std::shared_ptr<juce::TextEditor> (nameEditor.release());
    dying->onReturnKey = nullptr;
    dying->onEscapeKey = nullptr;
    dying->onFocusLost = nullptr;
    dying->setVisible (false);
    juce::MessageManager::callAsync ([dying]() mutable { dying.reset(); });

    if (save)
        saveAs (typed);
    repaint();
}

void PresetBar::saveAs (const juce::String& name)
{
    if (PresetManager::cleanName (name).isEmpty())
    {
        flash ("Name is empty");
        return;
    }

    if (presets.savePreset (name))
    {
        refresh();
        flash ("Saved");
    }
    else
    {
        flash ("Could not save");
    }
}

void PresetBar::flash (const juce::String& message)
{
    flashText = message;
    startTimer (1300);
    repaint();
}

void PresetBar::timerCallback()
{
    stopTimer();
    flashText.clear();
    repaint();
}
