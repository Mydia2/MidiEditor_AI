/*
 * MidiEditor AI
 *
 * HelpDatabase — Phase 44 ("Manual Bot")
 *
 * Loads run_environment/help_db.json (generated from manual/*.html by
 * scripts/build_help_db.py, embedded via resources.qrc) and answers the
 * search_help / get_help_section AI tools. The manual is the ONLY truth -
 * the DB is regenerated in the release checklist so it cannot drift.
 *
 * Retrieval is plain token scoring over a few hundred sections: no
 * embeddings, no runtime model, fully offline. Title hits weigh 3,
 * keyword hits 2, body hits 1 (capped per token) - enough to send
 * "how do I split drums" to ffxiv-drum-split.html reliably.
 */

#ifndef HELPDATABASE_H_
#define HELPDATABASE_H_

#include <QList>
#include <QString>
#include <QStringList>

class HelpDatabase {
public:
    struct Section {
        QString page;      ///< manual file name, e.g. "ffxiv-drum-split.html"
        QString anchor;    ///< h2 id ("" = the page's h1 intro section)
        QString title;
        QString pageTitle; ///< the page's h1, for context in results
        QStringList keywords;
        QString text;      ///< plain text of the section
    };
    struct Hit {
        const Section *section;
        int score;
    };

    /** The app-wide instance, loaded from the embedded qrc copy. */
    static HelpDatabase &instance();

    /** Constructs empty-or-qrc-loaded; tests build their own instance and
     *  loadFromFile() the repo copy over it. */
    HelpDatabase();

    bool isLoaded() const { return !_sections.isEmpty(); }
    int sectionCount() const { return _sections.size(); }

    /** Top-N sections for a free-text query (empty when nothing scored). */
    QList<Hit> search(const QString &query, int topN = 5) const;

    /** The section for page#anchor, or nullptr. */
    const Section *section(const QString &page, const QString &anchor) const;

    /** Test seam / explicit source: load from a JSON file on disk instead
     *  of the qrc. Replaces the current content; false on parse failure. */
    bool loadFromFile(const QString &path);

private:
    bool loadFromJson(const QByteArray &json);

    QList<Section> _sections;
};

#endif // HELPDATABASE_H_
