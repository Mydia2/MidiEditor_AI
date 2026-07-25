#include "FfxivDrumKitEditorDialog.h"
#include "Appearance.h"
#include "FfxivDrumKitStore.h"

#ifdef FLUIDSYNTH_SUPPORT
#  include "FfxivSoundFontHelper.h"
#  include "../midi/FfxivEqualizerService.h"
#  include "../midi/FluidSynthEngine.h"
#  include "../midi/MidiPlayer.h"
#endif

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// GM percussion key map (channel 10), notes 35-81. No table for this existed
// in the codebase - DrumKitPreset only groups notes - and a bare "38" tells
// the user nothing about which drum they are remapping.
const char *gmDrumName(int note) {
    switch (note) {
    case 35: return "Acoustic Bass Drum";
    case 36: return "Bass Drum 1";
    case 37: return "Side Stick";
    case 38: return "Acoustic Snare";
    case 39: return "Hand Clap";
    case 40: return "Electric Snare";
    case 41: return "Low Floor Tom";
    case 42: return "Closed Hi-Hat";
    case 43: return "High Floor Tom";
    case 44: return "Pedal Hi-Hat";
    case 45: return "Low Tom";
    case 46: return "Open Hi-Hat";
    case 47: return "Low-Mid Tom";
    case 48: return "Hi-Mid Tom";
    case 49: return "Crash Cymbal 1";
    case 50: return "High Tom";
    case 51: return "Ride Cymbal 1";
    case 52: return "Chinese Cymbal";
    case 53: return "Ride Bell";
    case 54: return "Tambourine";
    case 55: return "Splash Cymbal";
    case 56: return "Cowbell";
    case 57: return "Crash Cymbal 2";
    case 58: return "Vibraslap";
    case 59: return "Ride Cymbal 2";
    case 60: return "Hi Bongo";
    case 61: return "Low Bongo";
    case 62: return "Mute Hi Conga";
    case 63: return "Open Hi Conga";
    case 64: return "Low Conga";
    case 65: return "High Timbale";
    case 66: return "Low Timbale";
    case 67: return "High Agogo";
    case 68: return "Low Agogo";
    case 69: return "Cabasa";
    case 70: return "Maracas";
    case 71: return "Short Whistle";
    case 72: return "Long Whistle";
    case 73: return "Short Guiro";
    case 74: return "Long Guiro";
    case 75: return "Claves";
    case 76: return "Hi Wood Block";
    case 77: return "Low Wood Block";
    case 78: return "Mute Cuica";
    case 79: return "Open Cuica";
    case 80: return "Mute Triangle";
    case 81: return "Open Triangle";
    default: return "";
    }
}

QString pitchName(int note) {
    static const char *names[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};
    if (note < 0 || note > 127) return QStringLiteral("?");
    return QStringLiteral("%1%2").arg(names[note % 12]).arg((note / 12) - 1);
}

} // namespace

FfxivDrumKitEditorDialog::FfxivDrumKitEditorDialog(QWidget *parent)
    : QDialog(parent),
      _kitList(nullptr),
      _groupTabs(nullptr),
      _duplicateButton(nullptr),
      _deleteButton(nullptr),
      _saveButton(nullptr),
      _addRowButton(nullptr),
      _removeRowButton(nullptr),
      _headerLabel(nullptr),
      _statusLabel(nullptr) {

    setWindowTitle(tr("Edit FFXIV Drum Kits"));
    setMinimumSize(720, 520);
    setWindowIcon(Appearance::adjustIconForDarkMode(
        ":/run_environment/graphics/icon.png"));

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *intro = new QLabel(
        tr("A kit maps each GM drum onto a pitch of an FFXIV percussion "
           "instrument. The shipped kits cannot be changed - pick one and "
           "press Duplicate to start your own from it."), this);
    intro->setWordWrap(true);
    mainLayout->addWidget(intro);

    QHBoxLayout *columns = new QHBoxLayout();
    mainLayout->addLayout(columns, 1);

    // --- Left: the kit list -----------------------------------------------
    QVBoxLayout *leftColumn = new QVBoxLayout();
    leftColumn->addWidget(new QLabel(tr("Kits"), this));
    _kitList = new QListWidget(this);
    _kitList->setMinimumWidth(220);
    leftColumn->addWidget(_kitList, 1);
    QHBoxLayout *kitButtons = new QHBoxLayout();
    _duplicateButton = new QPushButton(tr("Duplicate..."), this);
    _deleteButton = new QPushButton(tr("Delete"), this);
    kitButtons->addWidget(_duplicateButton);
    kitButtons->addWidget(_deleteButton);
    leftColumn->addLayout(kitButtons);
    columns->addLayout(leftColumn, 2);

    // --- Right: one mapping table per FFXIV percussion group --------------
    QVBoxLayout *rightColumn = new QVBoxLayout();
    _headerLabel = new QLabel(this);
    _headerLabel->setStyleSheet("QLabel { font-weight: bold; }");
    rightColumn->addWidget(_headerLabel);
    _groupTabs = new QTabWidget(this);
    rightColumn->addWidget(_groupTabs, 1);

    QHBoxLayout *rowButtons = new QHBoxLayout();
    _addRowButton = new QPushButton(tr("Add mapping"), this);
    _removeRowButton = new QPushButton(tr("Remove selected"), this);
    rowButtons->addWidget(_addRowButton);
    rowButtons->addWidget(_removeRowButton);
    rowButtons->addStretch();
#ifdef FLUIDSYNTH_SUPPORT
    _groupPreviewButton = new QPushButton(tr("Preview group"), this);
    _groupPreviewButton->setToolTip(
        tr("Plays this instrument's mapped FFXIV pitches in drum order - "
           "hear whether the mapping makes musical sense as a run."));
    rowButtons->addWidget(_groupPreviewButton);
    connect(_groupPreviewButton, &QPushButton::clicked,
            this, [this]() { previewGroup(); });
#endif
    rightColumn->addLayout(rowButtons);
#ifdef FLUIDSYNTH_SUPPORT
    // Preview status line: what a click will play, or the visible reason it
    // cannot (a greyed control whose reason hides in a hover tooltip reads
    // as a bug - the buttons stay clickable and this label explains).
    _previewHintLabel = new QLabel(this);
    _previewHintLabel->setWordWrap(true);
    _previewHintLabel->setStyleSheet("QLabel { color: gray; font-size: 10px; }");
    rightColumn->addWidget(_previewHintLabel);
#endif
    columns->addLayout(rightColumn, 5);

    _statusLabel = new QLabel(this);
    _statusLabel->setWordWrap(true);
    mainLayout->addWidget(_statusLabel);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    _saveButton = buttons->addButton(tr("Save kit"), QDialogButtonBox::ApplyRole);
    QPushButton *closeButton = buttons->addButton(QDialogButtonBox::Close);
    mainLayout->addWidget(buttons);

    // One table per group, built once from the shipped skeleton: the group set
    // is fixed, only the rows inside change per kit. With FluidSynth support a
    // third column carries the per-mapping A/B audition button; collectKit()
    // and setEditable() only ever touch columns 0 and 1, so the extra column
    // cannot affect saving, and the audition stays available on the
    // read-only shipped kits (the primary comparison case).
#ifdef FLUIDSYNTH_SUPPORT
    const int tableColumns = 3;
    const QStringList tableHeaders = {tr("GM drum (source)"),
                                      tr("FFXIV pitch (target)"), tr("A/B")};
#else
    const int tableColumns = 2;
    const QStringList tableHeaders = {tr("GM drum (source)"),
                                      tr("FFXIV pitch (target)")};
#endif
    const QList<FfxivDrumMapPreset> builtins = FfxivDrumMapPreset::presets();
    if (!builtins.isEmpty()) {
        for (const FfxivDrumMapGroup &g : builtins.first().groups) {
            QTableWidget *table = new QTableWidget(0, tableColumns, this);
            table->setHorizontalHeaderLabels(tableHeaders);
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
            if (tableColumns > 2) {
                // Fixed width for the button column: ResizeToContents measures
                // items through the delegate and these cells hold only a cell
                // widget, so it would collapse to the header text.
                table->horizontalHeader()->setSectionResizeMode(
                    2, QHeaderView::Fixed);
                table->setColumnWidth(2, 52);
            }
            table->verticalHeader()->setVisible(false);
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            _groupTabs->addTab(table, tr("%1 (program %2)")
                                          .arg(g.trackName)
                                          .arg(g.programNumber));
            _groupTables.append(table);
            _groupNames.append(g.trackName);
            _groupPrograms.append(g.programNumber);
        }
    }

    connect(_kitList, &QListWidget::currentRowChanged,
            this, &FfxivDrumKitEditorDialog::onKitSelected);
    connect(_duplicateButton, &QPushButton::clicked,
            this, &FfxivDrumKitEditorDialog::onDuplicate);
    connect(_deleteButton, &QPushButton::clicked,
            this, &FfxivDrumKitEditorDialog::onDelete);
    connect(_saveButton, &QPushButton::clicked,
            this, &FfxivDrumKitEditorDialog::onSave);
    connect(_addRowButton, &QPushButton::clicked,
            this, &FfxivDrumKitEditorDialog::onAddRow);
    connect(_removeRowButton, &QPushButton::clicked,
            this, &FfxivDrumKitEditorDialog::onRemoveRow);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    reloadKitList();

#ifdef FLUIDSYNTH_SUPPORT
    // Show the preview situation BEFORE the first click - especially whether
    // the GM half of the A/B is available, which depends on a GM SoundFont
    // being loaded next to the FFXIV one.
    const QString blocker = previewHardBlocker();
    if (!blocker.isEmpty()) {
        setPreviewHint(blocker, true);
    } else if (FluidSynthEngine::instance()->sfontIdWithPreset(128, 0) < 0) {
        setPreviewHint(tr("Preview ready (FFXIV side only): no General MIDI "
                          "drum kit is loaded, so the GM half of the A/B "
                          "stays silent. Enable a GM SoundFont (e.g. "
                          "GeneralUser GS via Settings > Midi I/O > Download "
                          "Default...) alongside the FFXIV font to hear "
                          "both."),
                       true);
    } else {
        setPreviewHint(tr("Speaker button: plays the GM drum, then the FFXIV "
                          "target. \"Preview group\" plays a tab's mapped "
                          "pitches as a run."),
                       false);
    }
#endif
}

void FfxivDrumKitEditorDialog::reloadKitList(const QString &select) {
    const QList<FfxivDrumMapPreset> kits = FfxivDrumKitStore::instance()->allKits();
    _loading = true;
    _kitList->clear();
    int selectRow = 0;
    for (int i = 0; i < kits.size(); ++i) {
        const bool builtin = FfxivDrumKitStore::isBuiltinName(kits[i].name);
        QListWidgetItem *item = new QListWidgetItem(
            builtin ? tr("%1 (shipped)").arg(kits[i].name) : kits[i].name,
            _kitList);
        // The display text carries a suffix, so keep the real name in data.
        item->setData(Qt::UserRole, kits[i].name);
        if (kits[i].name == select) selectRow = i;
    }
    _loading = false;
    if (_kitList->count() > 0) {
        _kitList->setCurrentRow(selectRow);
    } else {
        showKit(FfxivDrumMapPreset());
    }
}

void FfxivDrumKitEditorDialog::onKitSelected() {
    if (_loading) return;
    QListWidgetItem *item = _kitList->currentItem();
    if (!item) return;
    const QString name = item->data(Qt::UserRole).toString();
    if (name == _editedName) return;
    if (_dirty && !confirmDiscardChanges()) {
        // Put the selection back without re-triggering this handler.
        _loading = true;
        for (int i = 0; i < _kitList->count(); ++i) {
            if (_kitList->item(i)->data(Qt::UserRole).toString() == _editedName) {
                _kitList->setCurrentRow(i);
                break;
            }
        }
        _loading = false;
        return;
    }
    showKit(FfxivDrumKitStore::instance()->kitByName(name));
}

void FfxivDrumKitEditorDialog::showKit(const FfxivDrumMapPreset &kit) {
    _loading = true;
    _editedName = kit.name;
    _editable = !kit.name.isEmpty() && !FfxivDrumKitStore::isBuiltinName(kit.name);

    for (QTableWidget *table : _groupTables) table->setRowCount(0);
    for (int gi = 0; gi < kit.groups.size() && gi < _groupTables.size(); ++gi) {
        for (const FfxivDrumNoteMap &m : kit.groups[gi].mappings)
            addRow(gi, m.sourceNote, m.targetNote);
    }

    if (kit.name.isEmpty()) {
        _headerLabel->setText(tr("No kit selected"));
    } else if (_editable) {
        _headerLabel->setText(tr("Editing: %1").arg(kit.name));
    } else {
        _headerLabel->setText(tr("%1 - shipped kit, read-only").arg(kit.name));
    }
    setEditable(_editable);
    _dirty = false;
    _loading = false;
    setStatus(QString(), false);
}

void FfxivDrumKitEditorDialog::addRow(int groupIndex, int sourceNote,
                                      int targetNote) {
    if (groupIndex < 0 || groupIndex >= _groupTables.size()) return;
    QTableWidget *table = _groupTables[groupIndex];
    const int row = table->rowCount();
    table->insertRow(row);

    QSpinBox *source = new QSpinBox(table);
    source->setRange(FfxivDrumKitStore::kMinSource, FfxivDrumKitStore::kMaxSource);
    source->setValue(sourceNote);
    source->installEventFilter(this);
    // A dynamic suffix names the drum without needing a second column or a
    // custom item delegate.
    auto updateSourceSuffix = [source](int value) {
        const QString name = QString::fromLatin1(gmDrumName(value));
        source->setSuffix(name.isEmpty() ? QString()
                                         : QStringLiteral("  ") + name);
    };
    updateSourceSuffix(sourceNote);
    connect(source, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, updateSourceSuffix](int value) {
                updateSourceSuffix(value);
                markDirty();
            });
    table->setCellWidget(row, 0, source);

    QSpinBox *target = new QSpinBox(table);
    target->setRange(FfxivDrumKitStore::kMinTarget, FfxivDrumKitStore::kMaxTarget);
    target->setValue(targetNote);
    target->installEventFilter(this);
    auto updateTargetSuffix = [target](int value) {
        target->setSuffix(QStringLiteral("  ") + pitchName(value));
    };
    updateTargetSuffix(targetNote);
    connect(target, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, updateTargetSuffix](int value) {
                updateTargetSuffix(value);
                markDirty();
            });
    table->setCellWidget(row, 1, target);

#ifdef FLUIDSYNTH_SUPPORT
    // Per-mapping A/B audition: the GM drum first, its FFXIV target right
    // after - the back-to-back sequencing is what makes the comparison
    // possible at all (two separate buttons would put a mouse-move between
    // the sounds). Reads the CURRENT spin values via QPointer at click time,
    // so it follows edits and cannot dangle after row removal.
    QToolButton *abButton = new QToolButton(table);
    abButton->setAutoRaise(true);
    abButton->setIcon(Appearance::adjustIconForDarkMode(
        ":/run_environment/graphics/channelwidget/loud.png"));
    abButton->setToolTip(tr("Play the GM drum, then the FFXIV target"));
    const int program = (groupIndex < _groupPrograms.size())
                            ? _groupPrograms[groupIndex] : 0;
    QPointer<QSpinBox> sourceRef(source);
    QPointer<QSpinBox> targetRef(target);
    connect(abButton, &QToolButton::clicked, this,
            [this, sourceRef, targetRef, program, table, abButton]() {
                if (!sourceRef || !targetRef) return;
                const QModelIndex idx = table->indexAt(abButton->pos());
                if (idx.isValid()) table->selectRow(idx.row());
                previewMapping(sourceRef->value(), targetRef->value(), program);
            });
    table->setCellWidget(row, 2, abButton);
#endif
}

void FfxivDrumKitEditorDialog::setEditable(bool editable) {
    _addRowButton->setEnabled(editable);
    _removeRowButton->setEnabled(editable);
    _saveButton->setEnabled(editable);
    _deleteButton->setEnabled(editable);
    for (QTableWidget *table : _groupTables) {
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int col = 0; col < 2; ++col) {
                if (QWidget *w = table->cellWidget(row, col))
                    w->setEnabled(editable);
            }
        }
    }
}

void FfxivDrumKitEditorDialog::markDirty() {
    if (_loading || !_editable) return;
    _dirty = true;
    _headerLabel->setText(tr("Editing: %1 *").arg(_editedName));
}

FfxivDrumMapPreset FfxivDrumKitEditorDialog::collectKit() const {
    FfxivDrumMapPreset kit;
    kit.name = _editedName;
    for (int gi = 0; gi < _groupTables.size(); ++gi) {
        FfxivDrumMapGroup group;
        group.trackName = _groupNames[gi];
        group.programNumber = _groupPrograms[gi];
        QTableWidget *table = _groupTables[gi];
        for (int row = 0; row < table->rowCount(); ++row) {
            QSpinBox *source = qobject_cast<QSpinBox *>(table->cellWidget(row, 0));
            QSpinBox *target = qobject_cast<QSpinBox *>(table->cellWidget(row, 1));
            if (!source || !target) continue;
            group.mappings.append({source->value(), target->value()});
        }
        kit.groups.append(group);
    }
    return kit;
}

void FfxivDrumKitEditorDialog::onAddRow() {
    const int gi = _groupTabs->currentIndex();
    if (gi < 0) return;
    // Seed with a source that is not used yet, so a fresh row is valid as-is
    // and the user only has to pick the target pitch.
    QSet<int> used;
    const FfxivDrumMapPreset current = collectKit();
    for (const FfxivDrumMapGroup &g : current.groups) {
        for (const FfxivDrumNoteMap &m : g.mappings) used.insert(m.sourceNote);
    }
    int source = FfxivDrumKitStore::kMinSource;
    while (source < FfxivDrumKitStore::kMaxSource && used.contains(source))
        ++source;
    addRow(gi, source, 60);
    markDirty();
    QTableWidget *table = _groupTables[gi];
    table->selectRow(table->rowCount() - 1);
}

void FfxivDrumKitEditorDialog::onRemoveRow() {
    const int gi = _groupTabs->currentIndex();
    if (gi < 0) return;
    QTableWidget *table = _groupTables[gi];
    const int row = table->currentRow();
    if (row < 0) {
        setStatus(tr("Select a mapping row first."), true);
        return;
    }
    table->removeRow(row);
    markDirty();
}

void FfxivDrumKitEditorDialog::onDuplicate() {
    QListWidgetItem *item = _kitList->currentItem();
    if (!item) return;
    const QString sourceName = item->data(Qt::UserRole).toString();
    // Duplicate what is ON SCREEN when the shown kit is editable, so pending
    // edits are carried into the copy instead of being silently dropped.
    FfxivDrumMapPreset source =
        (_editable && sourceName == _editedName)
            ? collectKit()
            : FfxivDrumKitStore::instance()->kitByName(sourceName);

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Duplicate kit"), tr("Name for the copy:"), QLineEdit::Normal,
        tr("%1 (copy)").arg(sourceName), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    // saveKit is insert-or-overwrite by contract, and the suggested default
    // name is the same on every duplicate of a kit - accepting it twice
    // would silently replace the user's edited first copy with a pristine
    // one. Overwriting must be a conscious choice, like Delete is.
    const QString existing =
        FfxivDrumKitStore::instance()->existingUserKitName(name);
    if (!existing.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Duplicate kit"),
            tr("A kit named \"%1\" already exists. Replace it with a copy of "
               "\"%2\"?").arg(existing).arg(sourceName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        // Reuse the stored spelling so a case-variant cannot fork the index.
        source.name = existing;
    } else {
        source.name = name;
    }

    QString error;
    if (!FfxivDrumKitStore::instance()->saveKit(source, &error)) {
        setStatus(error, true);
        return;
    }
    _dirty = false;
    _kitsChanged = true;
    _lastSavedKitName = source.name;
    reloadKitList(source.name);
    setStatus(existing.isEmpty()
                  ? tr("Created \"%1\" - edit its mappings and press Save kit.")
                        .arg(source.name)
                  : tr("Replaced \"%1\" with a copy of \"%2\".")
                        .arg(source.name)
                        .arg(sourceName),
              false);
}

void FfxivDrumKitEditorDialog::onDelete() {
    if (_editedName.isEmpty() || !_editable) return;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Delete kit"),
        tr("Delete the kit \"%1\"?").arg(_editedName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QString error;
    FfxivDrumKitStore::instance()->deleteKit(_editedName, &error);
    if (!error.isEmpty()) {
        setStatus(error, true);
        return;
    }
    const QString deleted = _editedName;
    _dirty = false;
    _editedName.clear();
    _kitsChanged = true;
    _lastSavedKitName.clear();
    reloadKitList();
    setStatus(tr("Deleted \"%1\".").arg(deleted), false);
}

void FfxivDrumKitEditorDialog::onSave() {
    if (!_editable) return;
    const FfxivDrumMapPreset kit = collectKit();
    QString error;
    if (!FfxivDrumKitStore::instance()->saveKit(kit, &error)) {
        setStatus(error, true);
        return;
    }
    _dirty = false;
    _kitsChanged = true;
    _lastSavedKitName = kit.name;
    _headerLabel->setText(tr("Editing: %1").arg(_editedName));
    int mappings = 0;
    for (const FfxivDrumMapGroup &g : kit.groups) mappings += g.mappings.size();
    setStatus(tr("Saved \"%1\" with %2 mapping(s).").arg(kit.name).arg(mappings),
              false);
}

bool FfxivDrumKitEditorDialog::confirmDiscardChanges() {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("Save the changes to \"%1\" first?").arg(_editedName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) {
        onSave();
        // A rejected save (validation) must not silently discard the edits.
        if (_dirty) return false;
    }
    return true;
}

void FfxivDrumKitEditorDialog::setStatus(const QString &text, bool isError) {
    _statusLabel->setText(text);
    _statusLabel->setStyleSheet(isError ? "QLabel { color: #f85149; }"
                                        : "QLabel { color: gray; }");
}

#ifdef FLUIDSYNTH_SUPPORT
// ---------------------------------------------------------------------------
// Audio preview (v2.1.0 #2). Everything below talks to FluidSynth directly
// through the engine's preview API - deliberately NOT through the MIDI byte
// path, which in FFXIV SoundFont Mode forces every channel to bank 0 while
// the GM half of an A/B pair lives in bank 128 (GeneralUser GS or another
// GM font loaded alongside the FFXIV one).
// ---------------------------------------------------------------------------

QString FfxivDrumKitEditorDialog::previewHardBlocker() const {
    if (!FluidSynthEngine::instance()->isInitialized()) {
        return tr("The built-in synthesizer is not running - select "
                  "\"FluidSynth (Built-in Synthesizer)\" as the MIDI output "
                  "to hear previews.");
    }
    if (MidiPlayer::isPlaying()) {
        return tr("Stop playback to hear previews.");
    }
    return QString();
}

int FfxivDrumKitEditorDialog::ffxivSfontId() const {
    // Pin the target side to the FFXIV font explicitly: GeneralUser GS also
    // carries bank-0 programs 116-119 (Taiko/Melodic Tom/...), so relying on
    // stack priority would let a reordered font list hijack the audition.
    // loadedSoundFonts() is oldest-loaded-first and the stack loads the UI
    // list in REVERSE, so the highest-priority font is the LAST element -
    // iterate backwards so that with two FFXIV-named fonts enabled the
    // audition uses the same one playback resolves.
    const QList<QPair<int, QString>> fonts =
        FluidSynthEngine::instance()->loadedSoundFonts();
    for (int i = fonts.size() - 1; i >= 0; --i) {
        if (FfxivSoundFontHelper::isFfxivSoundFont(fonts[i].second))
            return fonts[i].first;
    }
    return -1;
}

void FfxivDrumKitEditorDialog::setPreviewHint(const QString &text,
                                              bool isWarning) {
    if (!_previewHintLabel) return;
    _previewHintLabel->setText(text);
    _previewHintLabel->setStyleSheet(
        isWarning ? "QLabel { color: #f0883e; font-size: 10px; }"
                  : "QLabel { color: gray; font-size: 10px; }");
}

void FfxivDrumKitEditorDialog::previewMapping(int sourceNote, int targetNote,
                                              int program) {
    const QString blocker = previewHardBlocker();
    if (!blocker.isEmpty()) {
        setPreviewHint(blocker, true);
        return;
    }
    FfxivEqualizerService *eq = FfxivEqualizerService::instance();
    if (eq->masterGain() <= 0.0f || eq->gainFor(program, false) <= 0.0f) {
        // The engine would swallow the NoteOn (a muted preset must be truly
        // silent); without this hint that reads as a broken button.
        setPreviewHint(tr("This instrument is muted in the FFXIV SoundFont "
                          "Equalizer - unmute it to hear the preview."),
                       true);
        return;
    }

    FluidSynthEngine *engine = FluidSynthEngine::instance();
    const int gmId = engine->sfontIdWithPreset(128, 0);
    const int fxId = ffxivSfontId();
    const QString groupName =
        _groupNames.value(_groupPrograms.indexOf(program), tr("FFXIV"));

    QList<FluidSynthEngine::PreviewNote> seq;
    int targetAt = 0;
    if (gmId >= 0) {
        FluidSynthEngine::PreviewNote gm;
        gm.sfontId = gmId;
        gm.bank = 128;   // the GM percussion kit
        gm.program = 0;  // Standard Kit
        gm.key = sourceNote;
        gm.atMs = 0;
        gm.holdMs = 320;
        seq.append(gm);
        targetAt = 430;  // back to back, with a beat of air between A and B
    }
    FluidSynthEngine::PreviewNote fx;
    fx.sfontId = fxId;
    fx.bank = 0;
    fx.program = program;
    fx.key = targetNote;
    fx.atMs = targetAt;
    fx.holdMs = 320;
    seq.append(fx);
    engine->playPreviewNotes(seq);

    if (gmId < 0) {
        setPreviewHint(tr("FFXIV target only - no General MIDI drum kit is "
                          "loaded, so the GM half of the comparison stays "
                          "silent. Enable a GM SoundFont (e.g. GeneralUser "
                          "GS via Settings > Midi I/O > Download Default...) "
                          "alongside the FFXIV font to hear both."),
                       true);
    } else if (fxId < 0) {
        setPreviewHint(tr("Playing GM %1 (%2), then %3 at %4 - note: no FFXIV "
                          "SoundFont is loaded, the target may not use the "
                          "in-game sound.")
                           .arg(sourceNote)
                           .arg(QString::fromLatin1(gmDrumName(sourceNote)))
                           .arg(groupName)
                           .arg(pitchName(targetNote)),
                       true);
    } else {
        setPreviewHint(tr("Playing GM %1 (%2), then %3 at %4.")
                           .arg(sourceNote)
                           .arg(QString::fromLatin1(gmDrumName(sourceNote)))
                           .arg(groupName)
                           .arg(pitchName(targetNote)),
                       false);
    }
}

void FfxivDrumKitEditorDialog::previewGroup() {
    const QString blocker = previewHardBlocker();
    if (!blocker.isEmpty()) {
        setPreviewHint(blocker, true);
        return;
    }
    const int gi = _groupTabs->currentIndex();
    if (gi < 0 || gi >= _groupTables.size()) return;
    const int program = _groupPrograms[gi];

    FfxivEqualizerService *eq = FfxivEqualizerService::instance();
    if (eq->masterGain() <= 0.0f || eq->gainFor(program, false) <= 0.0f) {
        setPreviewHint(tr("This instrument is muted in the FFXIV SoundFont "
                          "Equalizer - unmute it to hear the preview."),
                       true);
        return;
    }

    // The judgment a mapping needs is not one pitch in isolation but the
    // RELATIONSHIP between the targets (does the tom run still run?), so
    // play them as a run, ordered by their source drums.
    QTableWidget *table = _groupTables[gi];
    QList<QPair<int, int>> pairs; // (source, target)
    for (int row = 0; row < table->rowCount(); ++row) {
        QSpinBox *source = qobject_cast<QSpinBox *>(table->cellWidget(row, 0));
        QSpinBox *target = qobject_cast<QSpinBox *>(table->cellWidget(row, 1));
        if (!source || !target) continue;
        pairs.append(qMakePair(source->value(), target->value()));
    }
    if (pairs.isEmpty()) {
        setPreviewHint(tr("This instrument has no mappings to play."), true);
        return;
    }
    std::sort(pairs.begin(), pairs.end());

    const int fxId = ffxivSfontId();
    QList<FluidSynthEngine::PreviewNote> seq;
    int at = 0;
    for (const QPair<int, int> &p : pairs) {
        FluidSynthEngine::PreviewNote n;
        n.sfontId = fxId;
        n.bank = 0;
        n.program = program;
        n.key = p.second;
        n.atMs = at;
        n.holdMs = 220;
        seq.append(n);
        at += 250;
    }
    FluidSynthEngine::instance()->playPreviewNotes(seq);
    setPreviewHint(tr("Playing %1 mapped %2 pitch(es) in drum order.")
                       .arg(pairs.size())
                       .arg(_groupNames.value(gi)),
                   false);
}
#endif // FLUIDSYNTH_SUPPORT

bool FfxivDrumKitEditorDialog::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::FocusIn) {
        for (QTableWidget *table : _groupTables) {
            for (int row = 0; row < table->rowCount(); ++row) {
                if (table->cellWidget(row, 0) == watched
                    || table->cellWidget(row, 1) == watched) {
                    table->selectRow(row);
                    return QDialog::eventFilter(watched, event);
                }
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void FfxivDrumKitEditorDialog::closeEvent(QCloseEvent *event) {
    if (_dirty && !confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void FfxivDrumKitEditorDialog::reject() {
    // Esc lands here, NOT in closeEvent: QDialog::reject() closes through a
    // private path that deliberately swallows the close event (Qt 6's
    // CloseEventEater), so without this override Esc silently discarded
    // unsaved edits while the Close button and the window X prompted.
    if (_dirty && !confirmDiscardChanges()) {
        return;
    }
    QDialog::reject();
}
