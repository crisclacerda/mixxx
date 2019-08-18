/**
* @file controllerpresetinfoenumerator.cpp
* @author Be be.0@gmx.com
* @date Sat Jul 18 2015
* @brief Enumerate list of available controller mapping presets
*/
#include "controllers/controllerpresetinfoenumerator.h"

#include <QDirIterator>

#include "controllers/defs_controllers.h"

namespace {
bool presetInfoNameComparator(const PresetInfo &a, const PresetInfo &b) {
    if (a.getDirPath() == b.getDirPath()) {
        // FIXME: Mixxx copies every loaded mapping into the user mapping folder
        // with a different file name. This is confusing, especially when developing
        // a mapping and working on it in the user mapping folder. Sorting
        // by file path here is a quick hack to keep the identically named mappings
        // in a consistent order.
        if (a.getName() == b.getName()) {
            return a.getPath() < b.getPath();
        } else {
            return a.getName() < b.getName();
        }
    } else {
        return a.getDirPath() < b.getDirPath();
    }
}
}

PresetInfoEnumerator::PresetInfoEnumerator(const QStringList& searchPaths)
        : m_controllerDirPaths(searchPaths) {
    loadSupportedPresets();
}

QList<PresetInfo> PresetInfoEnumerator::getPresetsByExtension(const QString& extension) {
    if (extension == MIDI_PRESET_EXTENSION) {
        return m_midiPresets;
    } else if (extension == HID_PRESET_EXTENSION) {
        return m_hidPresets;
    } else if (extension == BULK_PRESET_EXTENSION) {
        return m_bulkPresets;
    } else if (extension == KEYBOARD_PRESET_EXTENSION) {
        return m_kbdPresets;
    } else

    qDebug() << "Extension not registered to presetinfo" << extension;
    return QList<PresetInfo>();
}

void PresetInfoEnumerator::loadSupportedPresets() {
    for (const QString& dirPath : m_controllerDirPaths) {
        QDirIterator it(dirPath);
        while (it.hasNext()) {
            it.next();
            const QString path = it.filePath();

            if (path.endsWith(MIDI_PRESET_EXTENSION, Qt::CaseInsensitive)) {
                m_midiPresets.append(PresetInfo(path));
            } else if (path.endsWith(HID_PRESET_EXTENSION, Qt::CaseInsensitive)) {
                m_hidPresets.append(PresetInfo(path));
            } else if (path.endsWith(BULK_PRESET_EXTENSION, Qt::CaseInsensitive)) {
                m_bulkPresets.append(PresetInfo(path));
            } else if (path.endsWith(KEYBOARD_PRESET_EXTENSION, Qt::CaseInsensitive)) {
                m_kbdPresets.append(PresetInfo(path));
            }
        }
    }

    std::sort(m_midiPresets.begin(), m_midiPresets.end(), presetInfoNameComparator);
    std::sort(m_hidPresets.begin(), m_hidPresets.end(), presetInfoNameComparator);
    std::sort(m_bulkPresets.begin(), m_bulkPresets.end(), presetInfoNameComparator);

    qDebug() << "Extension" << MIDI_PRESET_EXTENSION << "total"
             << m_midiPresets.length() << "presets";
    qDebug() << "Extension" << HID_PRESET_EXTENSION << "total"
             << m_hidPresets.length() << "presets";
    qDebug() << "Extension" << BULK_PRESET_EXTENSION << "total"
             << m_bulkPresets.length() << "presets";
    qDebug() << "Extension" << KEYBOARD_PRESET_EXTENSION << "total"
             << m_kbdPresets.length() << "presets";
}
