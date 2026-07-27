/*
 * MidiEditor AI
 *
 * FfxivPlayabilityDialog — Phase 46
 *
 * The FFXIV playability WORKBENCH (grown from a plain report on first QA
 * feedback): shows problems AND offers the repairs.
 *
 *   - A check row on top: every check category is a checkbox (monophony,
 *     range, track names, channel spread, empty tracks, voice load), so
 *     single aspects can be re-checked in isolation; "Run checks" re-runs.
 *   - The findings tree, grouped by type. Clicking an issue selects its
 *     notes in the editor and moves the cursor there.
 *   - A contextual fix row: only the tools matching the FOUND problems are
 *     offered - Delete Overlaps (selects the colliding notes first), the
 *     channel fixer, Auto-Fit Voice Load.
 *   - "Analyze with MidiPilot": submits the findings to the AI through the
 *     normal chat path and mirrors the answer into the dialog.
 *
 * Modeless (like the Auto-Fit dialog): the fix buttons hand work to tools
 * that need a usable editor - a modal dialog would block Auto-Fit's own
 * modeless window. Single instance via QPointer in MainWindow; closed on
 * tab switch; re-validated after every finished protocol action, so the
 * tree never holds stale note pointers and repairs show their effect live.
 *
 * The validator itself stays dependency-free; the voice-limit / note-rate
 * rows are synthesized HERE from an Auto-Fit dry run and the voice
 * analyzer (see MainWindow::runPlayabilityChecks).
 */

#ifndef FFXIVPLAYABILITYDIALOG_H_
#define FFXIVPLAYABILITYDIALOG_H_

#include <QDialog>

#include "../ai/FfxivPlayabilityValidator.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;
class MidiEvent;

class FfxivPlayabilityDialog : public QDialog {
    Q_OBJECT

public:
    explicit FfxivPlayabilityDialog(QWidget *parent = nullptr);

    /** The currently selected check categories (voice load is dialog-side,
     *  reported via voiceLoadEnabled()). */
    FfxivPlayabilityChecks selectedChecks() const;
    bool voiceLoadEnabled() const;

    /** Replaces the report and rebuilds the tree. Called by MainWindow with
     *  a fresh validation whenever the file changed underneath the open
     *  dialog, and after every "Run checks". */
    void refresh(const FfxivPlayabilityReport &report);

    /** Shows the AI's answer in the analysis pane (MainWindow forwards
     *  MidiPilotWidget::assistantReplied here after an analysis request). */
    void showAnalysis(const QString &text);

    /** Whether this dialog is waiting for a MidiPilot answer it requested -
     *  MainWindow only forwards replies while this is true, so unrelated
     *  chat traffic never lands in the pane. */
    bool awaitingAnalysis() const { return _awaitingAnalysis; }

    /** Enables/disables the MidiPilot button (provider configured?). */
    void setMidiPilotAvailable(bool available);

signals:
    /** Re-run the checks with the current selection. */
    void runChecksRequested();

    /** Select these notes in the editor. */
    void selectEventsRequested(const QList<MidiEvent *> &events);

    /** Move the edit cursor to this tick. */
    void jumpToTickRequested(int tick);

    /** Run a repair tool. actionId is a MainWindow action-map id:
     *  "delete_overlaps" (the colliding notes get selected first),
     *  "fix_ffxiv_channels", "auto_fit_voice_load". */
    void fixRequested(const QString &actionId);

    /** Submit this prompt to MidiPilot (normal chat path, answer mirrored
     *  back via showAnalysis). */
    void analyzeRequested(const QString &prompt);

private slots:
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onSelectAllClicked();
    void onAnalyzeClicked();

private:
    void rebuildTree();
    void rebuildFixRow();
    QString buildAnalysisPrompt() const;

    FfxivPlayabilityReport _report;

    QCheckBox *_checkMonophony = nullptr;
    QCheckBox *_checkRange = nullptr;
    QCheckBox *_checkNames = nullptr;
    QCheckBox *_checkChannels = nullptr;
    QCheckBox *_checkEmpty = nullptr;
    QCheckBox *_checkVoiceLoad = nullptr;

    QLabel *_summaryLabel = nullptr;
    QTreeWidget *_tree = nullptr;

    QPushButton *_selectAllButton = nullptr;
    QPushButton *_fixOverlapsButton = nullptr;
    QPushButton *_fixChannelsButton = nullptr;
    QPushButton *_fixVoiceButton = nullptr;
    QPushButton *_analyzeButton = nullptr;

    QTextBrowser *_analysisView = nullptr;
    bool _awaitingAnalysis = false;
    bool _midiPilotAvailable = false;
};

#endif // FFXIVPLAYABILITYDIALOG_H_
