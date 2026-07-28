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

namespace AppPaths {

/** Base data directory (ensured to exist). Windows: the exe directory. */
QString dataDir();

/** <dataDir()>/soundfonts, created on demand. */
QString soundFontsDir();

/** A file directly in dataDir(), e.g. dataFilePath("midieditor_ai.log")
 *  or dataFilePath("system_prompts.json"). Does not create the file. */
QString dataFilePath(const QString &fileName);

} // namespace AppPaths

#endif // APPPATHS_H_
