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
#include <QStringList>
#include <memory>

class QSettings;

namespace AppPaths {

/** Directory the running executable lives in.
 *
 *  Resolved from the OS, not from argv[0] (v2.2 review): Qt's WinMain shim
 *  rebuilds argv with WideCharToMultiByte(CP_ACP, ...) and this build ships
 *  no UTF-8 activeCodePage manifest, so an install path with characters
 *  outside the system ANSI codepage arrives as '?'; and argv[0] carries no
 *  directory at all when the program is started by bare name from PATH or by
 *  a wrapper (which used to make the CWD the data directory). Works before
 *  QApplication exists - initSettings() runs that early. */
QString exeDir();

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

/** Arguments EVERY self-relaunch must re-pass (theme-change restart,
 *  auto-updater). Returns {"--portable"} in portable mode, empty otherwise.
 *
 *  Why (v2.2 review): the switch and the marker file are documented as
 *  equivalent, so a copy started with `--portable` alone has no marker. A
 *  relaunch that drops the switch comes back on the system settings store and
 *  silently reads a different configuration than the one it just wrote.
 *  Re-passing the switch is harmless when the marker is present. */
QStringList relaunchArgs();

/** Call FIRST THING in main(), BEFORE QApplication and before the first
 *  QSettings use anywhere (Appearance::loadEarlySettings reads rendering
 *  flags pre-QApplication - the review's PORTABLE-SPLIT-001). Resolves the
 *  exe directory from argv[0], detects portable mode, and in portable mode
 *  redirects settings to an INI under <exeDir>/config with a one-time
 *  migration from the native scope. Needs no QApplication - QSettings
 *  path/format redirection is process-global static state. */
void initSettings(int argc, char *argv[]);

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

/** Test seam: pin exeDir() to `dir` (pass QString() to clear) and forget the
 *  portable decision, so a test can drive initSettings() with a synthetic
 *  argv against a QTemporaryDir instead of the test binary's own folder. */
void setExeDirForTests(const QString &dir);

/** The argv[0]-based exe-dir resolution - the LAST RESORT inside
 *  initSettings() once the OS query failed, exposed so it can be tested
 *  directly. Returns an empty string when argv[0] yields nothing usable
 *  (never the CWD). */
QString exeDirFromArgv(int argc, char *argv[]);

} // namespace AppPaths

#endif // APPPATHS_H_
