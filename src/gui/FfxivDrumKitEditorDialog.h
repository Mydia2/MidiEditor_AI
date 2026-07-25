/*
 * MidiEditor AI
 *
 * FfxivDrumKitEditorDialog - v2.1.0 feature #2 (roadmap Phase 35).
 *
 * Editor for the FFXIV drum pitch-mapping kits, reached from the drum
 * split's "Edit kits..." button. The shipped kits are read-only; editing
 * always starts by duplicating an existing kit (copy-then-edit), so a kit
 * never starts as an empty table.
 *
 * Only the note mappings are editable. The group track names and their
 * FFXIV program numbers are a byte-contract with the playback/export path
 * (FluidSynthEngine::drumProgramForTrackName), so they are shown but fixed -
 * FfxivDrumKitStore::validate enforces that on save.
 */

#ifndef FFXIVDRUMKITEDITORDIALOG_H_
#define FFXIVDRUMKITEDITORDIALOG_H_

#include <QDialog>
#include <QList>
#include <QString>

#include "DrumKitPreset.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTabWidget;

class FfxivDrumKitEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit FfxivDrumKitEditorDialog(QWidget *parent = nullptr);

    /// True when a kit was saved or deleted, i.e. the caller must reload its
    /// kit list. Also carries the name to preselect (empty when deleted).
    bool kitsChanged() const { return _kitsChanged; }
    QString lastSavedKitName() const { return _lastSavedKitName; }

private slots:
    void onKitSelected();
    void onDuplicate();
    void onDelete();
    void onSave();
    void onAddRow();
    void onRemoveRow();

private:
    void reloadKitList(const QString &select = QString());
    void showKit(const FfxivDrumMapPreset &kit);
    /// Builds a preset from the current tables under the edited name.
    FfxivDrumMapPreset collectKit() const;
    /// Adds one mapping row to the table of `groupIndex`.
    void addRow(int groupIndex, int sourceNote, int targetNote);
    void setEditable(bool editable);
    void markDirty();
    /// Offers to save pending edits. Returns false when the user cancels.
    bool confirmDiscardChanges();
    void setStatus(const QString &text, bool isError);

    void closeEvent(QCloseEvent *event) override;
    /// Selects the row of the spin box gaining focus. Every cell is covered
    /// by a spin box and the vertical header is hidden, so a mouse click
    /// never reaches the table itself - without this, "Remove selected"
    /// only ever works right after "Add mapping".
    bool eventFilter(QObject *watched, QEvent *event) override;

    QListWidget *_kitList;
    QTabWidget *_groupTabs;
    QList<QTableWidget *> _groupTables; ///< parallel to the kit's groups
    QList<QString> _groupNames;
    QList<int> _groupPrograms;
    QPushButton *_duplicateButton;
    QPushButton *_deleteButton;
    QPushButton *_saveButton;
    QPushButton *_addRowButton;
    QPushButton *_removeRowButton;
    QLabel *_headerLabel;
    QLabel *_statusLabel;

    QString _editedName;   ///< the kit currently shown ("" = none)
    bool _editable = false; ///< false for shipped kits
    bool _dirty = false;
    bool _kitsChanged = false;
    bool _loading = false;  ///< suppresses dirty marking while populating
    QString _lastSavedKitName;
};

#endif // FFXIVDRUMKITEDITORDIALOG_H_
