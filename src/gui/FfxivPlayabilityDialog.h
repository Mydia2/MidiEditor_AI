/*
 * MidiEditor AI
 *
 * FfxivPlayabilityDialog — Phase 46
 *
 * GUI surface for FfxivPlayabilityValidator: the monophony/overlap/range
 * check used to exist only inside the `validate_ffxiv` AI tool, so a human
 * arranging by hand found unplayable spots by exporting and hearing notes
 * go missing in game. This dialog shows the full report grouped by issue
 * type; clicking an issue selects its notes in the editor and moves the
 * cursor there, and one button selects every offending note at once (fix
 * side: the existing Delete Overlaps / editing tools).
 *
 * Modal, but the report can go stale while it is open - exec() spins the
 * event loop, so an MCP/agent edit can still mutate the file. The caller
 * therefore re-validates on every finished protocol action and pushes the
 * fresh report in via refresh(); stale MidiEvent pointers are never
 * emitted because each refresh rebuilds the tree from the new report.
 */

#ifndef FFXIVPLAYABILITYDIALOG_H_
#define FFXIVPLAYABILITYDIALOG_H_

#include <QDialog>

#include "../ai/FfxivPlayabilityValidator.h"

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class MidiEvent;

class FfxivPlayabilityDialog : public QDialog {
    Q_OBJECT

public:
    explicit FfxivPlayabilityDialog(const FfxivPlayabilityReport &report,
                                    QWidget *parent = nullptr);

    /** Replaces the report and rebuilds the tree (called by MainWindow when
     *  the file changed underneath the open dialog). */
    void refresh(const FfxivPlayabilityReport &report);

signals:
    /** Select these notes in the editor (issue click / the select-all
     *  button). Never emitted with stale pointers - see class comment. */
    void selectEventsRequested(const QList<MidiEvent *> &events);

    /** Move the edit cursor to this tick (issue click). */
    void jumpToTickRequested(int tick);

private slots:
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onSelectAllClicked();

private:
    void rebuildTree();

    FfxivPlayabilityReport _report;
    QLabel *_summaryLabel = nullptr;
    QTreeWidget *_tree = nullptr;
    QPushButton *_selectAllButton = nullptr;
};

#endif // FFXIVPLAYABILITYDIALOG_H_
