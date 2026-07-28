/*
 * MidiEditor AI
 *
 * AppPaths — Phase 45 (issue #13 + Portable Mode groundwork)
 *
 * The ONE place that decides where the app's data lives. Historically every
 * data file sat next to the executable - which on Windows is a FEATURE
 * (copy the folder, take everything with you) but is impossible inside a
 * signed macOS .app bundle and blocks any distributed macOS build
 * (issue #13).
 *
 * Contract:
 *   - Windows: byte-identical to the historical behaviour - everything is
 *     exe-relative. Guarded by test_app_paths; do not "improve" this.
 *   - macOS/Linux: QStandardPaths::AppDataLocation, created on demand.
 *
 * Deliberately NOT routed through here:
 *   - AutoUpdater's exe paths (it replaces the executable - that is
 *     exe-relative by definition, and Windows-only).
 *   - MainWindow's restart working directory (process mechanics, not data).
 *
 * A source-grep test pins that no other file uses applicationDirPath()
 * for data paths.
 */

#ifndef APPPATHS_H_
#define APPPATHS_H_

#include <QString>
#include <memory>

class QSettings;

namespace AppPaths {

/** Base data directory (ensured to exist). Windows: the exe directory. */
QString dataDir();

/** <dataDir()>/soundfonts, created on demand. */
QString soundFontsDir();

/** A file directly in dataDir(), e.g. dataFilePath("midieditor_ai.log")
 *  or dataFilePath("system_prompts.json"). Does not create the file. */
QString dataFilePath(const QString &fileName);

/** Portable Mode (Phase 45 step 4): true when `portable.txt` sits next to
 *  the executable or the app was started with `--portable`. Decided once. */
bool isPortable();

/** Call ONCE early in main(), before the first QSettings use. In portable
 *  mode this redirects default-constructed QSettings to an INI under
 *  <dataDir()>/config and migrates the native scope (registry) into the
 *  INI on first use - a USB-stick copy finally carries its settings. */
void initSettings();

/** THE settings accessor - the one place that knows where settings live.
 *  Portable: the INI scope initSettings() set up; tests: the scope
 *  installed via setSettingsScopeForTests(); otherwise the historical
 *  native scope QSettings("MidiEditor","NONE"). This replaces the
 *  per-service setSettingsScopeForTests seams the v2.1.0 round-2 review
 *  forced into FfxivEqualizerService and FfxivDrumKitStore. */
std::unique_ptr<QSettings> settings();

/** Central test seam: redirect settings() to a throwaway scope so tests
 *  can never wipe the developer's real configuration again. */
void setSettingsScopeForTests(const QString &organization,
                              const QString &application);

} // namespace AppPaths

#endif // APPPATHS_H_
