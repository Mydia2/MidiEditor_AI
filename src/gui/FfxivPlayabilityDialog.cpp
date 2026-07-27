#include "FfxivPlayabilityDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

// Child items carry their issue's index in the report so a click can look
// up the events without duplicating the list into the item.
constexpr int kIssueIndexRole = Qt::UserRole;

QString groupTitle(FfxivPlayabilityIssue::Type t, int count) {
    switch (t) {
    case FfxivPlayabilityIssue::Type::Overlap:
        return FfxivPlayabilityDialog::tr("Simultaneous notes (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::DuplicateNote:
        return FfxivPlayabilityDialog::tr("Stacked duplicates (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::OutOfRange:
        return FfxivPlayabilityDialog::tr("Notes outside C3-C6 (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::TrackName:
        return FfxivPlayabilityDialog::tr("Track names (%1)").arg(count);
    }
    return QString();
}

} // namespace

FfxivPlayabilityDialog::FfxivPlayabilityDialog(
    const FfxivPlayabilityReport &report, QWidget *parent)
    : QDialog(parent), _report(report) {
    setWindowTitle(tr("FFXIV Playability Check"));
    setMinimumSize(560, 420);

    auto *layout = new QVBoxLayout(this);

    _summaryLabel = new QLabel(this);
    _summaryLabel->setWordWrap(true);
    layout->addWidget(_summaryLabel);

    _tree = new QTreeWidget(this);
    _tree->setColumnCount(1);
    _tree->setHeaderHidden(true);
    _tree->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(_tree, &QTreeWidget::itemClicked,
            this, &FfxivPlayabilityDialog::onItemClicked);
    layout->addWidget(_tree, 1);

    auto *hint = new QLabel(
        tr("Click an issue to select its notes in the editor and move the "
           "cursor there. Notes starting on the same tick collide in game - "
           "fix them by editing, or with Delete Overlaps on the selection."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *buttons = new QHBoxLayout();
    _selectAllButton = new QPushButton(tr("Select all offending notes"), this);
    connect(_selectAllButton, &QPushButton::clicked,
            this, &FfxivPlayabilityDialog::onSelectAllClicked);
    buttons->addWidget(_selectAllButton);
    buttons->addStretch();
    auto *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setDefault(true);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    rebuildTree();
}

void FfxivPlayabilityDialog::refresh(const FfxivPlayabilityReport &report) {
    _report = report;
    rebuildTree();
}

void FfxivPlayabilityDialog::rebuildTree() {
    _tree->clear();

    if (_report.valid()) {
        _summaryLabel->setText(
            tr("<b>File is FFXIV-playable.</b> %1 note(s) on %2 track(s) "
               "checked - no overlaps, duplicates, range or instrument "
               "problems found.")
                .arg(_report.checkedNotes)
                .arg(_report.checkedTracks));
    } else {
        _summaryLabel->setText(
            tr("<b>%1 issue(s) found</b> in %2 note(s) on %3 track(s). "
               "Simultaneous and duplicate note starts will not play "
               "correctly in game - a performer plays one note at a time.")
                .arg(_report.issues.size())
                .arg(_report.checkedNotes)
                .arg(_report.checkedTracks));
    }

    // Fixed presentation order: what breaks playback first.
    const FfxivPlayabilityIssue::Type order[] = {
        FfxivPlayabilityIssue::Type::Overlap,
        FfxivPlayabilityIssue::Type::DuplicateNote,
        FfxivPlayabilityIssue::Type::OutOfRange,
        FfxivPlayabilityIssue::Type::TrackName,
    };
    for (FfxivPlayabilityIssue::Type type : order) {
        const int count = _report.countOf(type);
        if (count == 0) continue;
        auto *group = new QTreeWidgetItem(_tree);
        group->setText(0, groupTitle(type, count));
        group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
        for (int idx = 0; idx < _report.issues.size(); ++idx) {
            const FfxivPlayabilityIssue &issue = _report.issues.at(idx);
            if (issue.type != type) continue;
            auto *item = new QTreeWidgetItem(group);
            item->setText(0, tr("Track %1: %2").arg(issue.track)
                                               .arg(issue.details));
            item->setData(0, kIssueIndexRole, idx);
        }
        group->setExpanded(count <= 20);
    }

    _selectAllButton->setEnabled(!_report.offendingNotes().isEmpty());
}

void FfxivPlayabilityDialog::onItemClicked(QTreeWidgetItem *item, int) {
    if (!item) return;
    const QVariant idxVar = item->data(0, kIssueIndexRole);
    if (!idxVar.isValid()) return;
    const int idx = idxVar.toInt();
    if (idx < 0 || idx >= _report.issues.size()) return;
    const FfxivPlayabilityIssue &issue = _report.issues.at(idx);
    if (!issue.events.isEmpty())
        emit selectEventsRequested(issue.events);
    emit jumpToTickRequested(issue.tick);
}

void FfxivPlayabilityDialog::onSelectAllClicked() {
    const QList<MidiEvent *> events = _report.offendingNotes();
    if (!events.isEmpty())
        emit selectEventsRequested(events);
}
