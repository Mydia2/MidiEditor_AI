#include "HelpDatabase.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace {

QStringList tokenize(const QString &text) {
    static const QRegularExpression re(QStringLiteral("[a-z0-9]{3,}"));
    QStringList tokens;
    auto it = re.globalMatch(text.toLower());
    while (it.hasNext()) {
        tokens.append(it.next().captured());
    }
    tokens.removeDuplicates();
    return tokens;
}

} // namespace

HelpDatabase &HelpDatabase::instance() {
    static HelpDatabase db;
    return db;
}

HelpDatabase::HelpDatabase() {
    QFile f(QStringLiteral(":/run_environment/help_db.json"));
    if (f.open(QIODevice::ReadOnly)) {
        loadFromJson(f.readAll());
    }
}

bool HelpDatabase::loadFromFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    return loadFromJson(f.readAll());
}

bool HelpDatabase::loadFromJson(const QByteArray &json) {
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return false;
    }
    QList<Section> sections;
    const QJsonArray arr = doc.object().value(QStringLiteral("sections")).toArray();
    sections.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Section s;
        s.page = o.value(QStringLiteral("page")).toString();
        s.anchor = o.value(QStringLiteral("anchor")).toString();
        s.title = o.value(QStringLiteral("title")).toString();
        s.pageTitle = o.value(QStringLiteral("pageTitle")).toString();
        s.text = o.value(QStringLiteral("text")).toString();
        for (const QJsonValue &kw : o.value(QStringLiteral("keywords")).toArray()) {
            s.keywords.append(kw.toString());
        }
        if (!s.page.isEmpty() && !s.title.isEmpty() && !s.text.isEmpty()) {
            sections.append(s);
        }
    }
    if (sections.isEmpty()) {
        return false;
    }
    _sections = sections;
    return true;
}

QList<HelpDatabase::Hit> HelpDatabase::search(const QString &query, int topN) const {
    const QStringList tokens = tokenize(query);
    if (tokens.isEmpty()) {
        return {};
    }
    QList<Hit> hits;
    for (const Section &s : _sections) {
        int score = 0;
        const QString titleLower = s.title.toLower();
        const QString textLower = s.text.toLower();
        for (const QString &t : tokens) {
            // Poor-man's stemming: "drums" must find the page indexed under
            // "drum". Score the best VARIANT, not the sum, so a token never
            // counts twice.
            QStringList variants{t};
            if (t.size() > 3 && t.endsWith(QLatin1Char('s'))) {
                variants.append(t.chopped(1));
            }
            int best = 0;
            for (const QString &v : variants) {
                int sc = 0;
                if (titleLower.contains(v)) sc += 3;
                if (s.keywords.contains(v)) sc += 2;
                // Body hits capped per token so one wordy page cannot drown
                // a precise title match elsewhere.
                sc += qMin(static_cast<int>(textLower.count(v)), 5);
                best = qMax(best, sc);
            }
            score += best;
        }
        if (score > 0) {
            hits.append({&s, score});
        }
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit &a, const Hit &b) { return a.score > b.score; });
    if (hits.size() > topN) {
        hits = hits.mid(0, topN);
    }
    return hits;
}

const HelpDatabase::Section *HelpDatabase::section(const QString &page,
                                                   const QString &anchor) const {
    for (const Section &s : _sections) {
        if (s.page == page && s.anchor == anchor) {
            return &s;
        }
    }
    return nullptr;
}
