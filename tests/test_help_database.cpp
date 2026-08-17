/*
 * test_help_database
 *
 * Phase 44 ("Manual Bot"): the generated help_db.json is the bridge between
 * the manual and the search_help / get_help_section AI tools. These tests
 * pin the contract on the REAL generated file in the repo:
 *
 *   1. The DB loads and has a sane size (a broken generator that emits 3
 *      sections must fail loudly, not ship an empty manual bot).
 *   2. Every (page, anchor) resolves against manual/: the page file exists
 *      and contains id="anchor" - regenerating after manual edits is a
 *      release-checklist step, and this test catches drift.
 *   3. Retrieval pins: known questions land on the right page. These are
 *      the queries the roadmap names as acceptance criteria.
 *   4. section() round-trips every search hit.
 *
 * HELPDB_REPO_ROOT is injected by CMake so the test finds the repo files
 * regardless of the ctest working directory.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QFile>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "../src/ai/HelpDatabase.h"

class TestHelpDatabase : public QObject {
    Q_OBJECT

private:
    HelpDatabase _db;
    QString _root;

    QString firstPageFor(const QString &query) {
        const QList<HelpDatabase::Hit> hits = _db.search(query, 3);
        return hits.isEmpty() ? QString() : hits.first().section->page;
    }

private slots:

    void initTestCase() {
        _root = QStringLiteral(HELPDB_REPO_ROOT);
        QVERIFY2(_db.loadFromFile(_root + QStringLiteral("/run_environment/help_db.json")),
                 "help_db.json missing or unparsable - run scripts/build_help_db.py");
    }

    // --- 1. sane size ------------------------------------------------------
    void databaseHasSaneSize() {
        QVERIFY2(_db.sectionCount() >= 150,
                 qPrintable(QStringLiteral("only %1 sections - generator broken?")
                                .arg(_db.sectionCount())));
    }

    // --- 2. every page#anchor resolves against manual/ ---------------------
    void everyAnchorResolvesAgainstManual() {
        // Walk the DB via search on a term-free basis: section() has no
        // iterator, so probe through the loaded file directly.
        QFile f(_root + QStringLiteral("/run_environment/help_db.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonArray sections = QJsonDocument::fromJson(f.readAll())
                                        .object().value(QStringLiteral("sections")).toArray();
        QHash<QString, QString> pageCache;
        int checked = 0;
        for (const QJsonValue &v : sections) {
            const QJsonObject o = v.toObject();
            const QString page = o.value(QStringLiteral("page")).toString();
            const QString anchor = o.value(QStringLiteral("anchor")).toString();
            if (!pageCache.contains(page)) {
                QFile pf(_root + QStringLiteral("/manual/") + page);
                QVERIFY2(pf.open(QIODevice::ReadOnly),
                         qPrintable(QStringLiteral("manual page missing: %1").arg(page)));
                pageCache.insert(page, QString::fromUtf8(pf.readAll()));
            }
            if (!anchor.isEmpty()) {
                QVERIFY2(pageCache.value(page).contains(
                             QStringLiteral("id=\"%1\"").arg(anchor)),
                         qPrintable(QStringLiteral("stale anchor %1#%2 - rerun "
                                                   "scripts/build_help_db.py")
                                        .arg(page, anchor)));
            }
            ++checked;
        }
        QVERIFY(checked >= 150);
    }

    // --- 3. retrieval pins -------------------------------------------------
    void knownQueriesLandOnTheRightPage() {
        QCOMPARE(firstPageFor(QStringLiteral("how do I split drums")),
                 QStringLiteral("ffxiv-drum-split.html"));
        QCOMPARE(firstPageFor(QStringLiteral("thin notes density auto fit")),
                 QStringLiteral("auto-fit-voice-load.html"));
        // The playability check has its own page since the manual split it out
        // of the channel-fixer page; the fixer keeps only a forwarding stub.
        // Either is a correct landing - the stub points straight at the page.
        const QString play = firstPageFor(QStringLiteral("playability check collisions"));
        QVERIFY2(play == QStringLiteral("ffxiv-playability.html")
                     || play == QStringLiteral("ffxiv-channel-fixer.html"),
                 qPrintable(QStringLiteral("playability query landed on %1").arg(play)));
        // The MCP server is documented on its own page and, since midipilot.html
        // was split, in the tools reference (the overview only forwards there).
        const QString mcp = firstPageFor(QStringLiteral("connect mcp server client"));
        QVERIFY2(mcp == QStringLiteral("mcp-server.html")
                     || mcp == QStringLiteral("midipilot-tools.html"),
                 qPrintable(QStringLiteral("mcp query landed on %1").arg(mcp)));
    }

    // --- 4. section() round-trips search hits ------------------------------
    void sectionRoundTripsSearchHits() {
        const QList<HelpDatabase::Hit> hits =
            _db.search(QStringLiteral("tempo curve smooth transition"), 5);
        QVERIFY(!hits.isEmpty());
        for (const HelpDatabase::Hit &hit : hits) {
            const HelpDatabase::Section *s =
                _db.section(hit.section->page, hit.section->anchor);
            QVERIFY(s != nullptr);
            QCOMPARE(s->title, hit.section->title);
            QVERIFY(!s->text.isEmpty());
        }
    }

    // Junk queries must not crash or return nonsense scores.
    void junkQueryReturnsEmpty() {
        QVERIFY(_db.search(QStringLiteral("!!! ??? ..")).isEmpty());
        QVERIFY(_db.search(QString()).isEmpty());
        QVERIFY(_db.section(QStringLiteral("nope.html"), QString()) == nullptr);
    }
};

QTEST_APPLESS_MAIN(TestHelpDatabase)
#include "test_help_database.moc"
