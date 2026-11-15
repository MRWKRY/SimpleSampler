#include "PresetManager.h"

PresetManager::PresetManager (SimpleSamplerProcessor& p, juce::File f)
    : processor (p), folder (std::move (f))
{
}

juce::File PresetManager::getDefaultFolder()
{
    return SimpleSamplerProcessor::getUserFolder().getChildFile ("Presets");
}

juce::String PresetManager::cleanName (const juce::String& name)
{
    auto n = juce::File::createLegalFileName (name.trim()).trim();
    if (n.endsWithIgnoreCase (extension))
        n = n.dropLastCharacters ((int) juce::String (extension).length()).trim();
    return n.substring (0, 80);
}

juce::File PresetManager::fileForName (const juce::String& name) const
{
    return folder.getChildFile (cleanName (name) + extension);
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    if (folder.isDirectory())
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
            names.add (f.getFileNameWithoutExtension());

    names.sort (true);
    return names;
}

bool PresetManager::presetExists (const juce::String& name) const
{
    return fileForName (name).existsAsFile();
}

bool PresetManager::savePreset (const juce::String& name)
{
    const auto clean = cleanName (name);
    if (clean.isEmpty())
        return false;

    if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
        return false;

    processor.setPresetName (clean);

    const auto xml = processor.createStateTree().createXml();
    if (xml == nullptr)
        return false;

    // write to a temporary file first so a failed save never destroys an existing preset
    const auto target = fileForName (clean);
    juce::TemporaryFile temp (target);
    if (! xml->writeTo (temp.getFile()))
        return false;

    if (! temp.overwriteTargetFileWithTemporary())
        return false;

    ++processor.stateVersion;   // lets the editor refresh the shown name
    return true;
}

bool PresetManager::loadPresetFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    const auto xml = juce::parseXML (file);
    if (xml == nullptr)
        return false;

    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! processor.restoreStateTree (tree, file.getParentDirectory()))
        return false;

    processor.setPresetName (file.getFileNameWithoutExtension());
    ++processor.stateVersion;
    return true;
}

bool PresetManager::loadPreset (const juce::String& name)
{
    return loadPresetFile (fileForName (name));
}

bool PresetManager::deletePreset (const juce::String& name)
{
    const auto file = fileForName (name);
    return file.existsAsFile() && file.deleteFile();
}

bool PresetManager::step (int direction)
{
    const auto names = getPresetNames();
    if (names.isEmpty())
        return false;

    int index = names.indexOf (getCurrentName(), true);
    if (index < 0)
        index = direction > 0 ? 0 : names.size() - 1;
    else
        index = (index + direction + names.size()) % names.size();

    return loadPreset (names[index]);
}
