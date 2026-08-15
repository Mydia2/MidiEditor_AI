#ifndef PROMPTPROFILE_H
#define PROMPTPROFILE_H

#include <QString>
#include <QStringList>

/**
 * \struct PromptProfile
 *
 * \brief A user- or built-in-defined system-prompt override bound to a set
 *        of \c "<provider>:<modelId>" patterns (with optional \c '*' suffix
 *        glob).
 *
 * Persisted by \ref PromptProfileStore under
 * \c "AI/prompt_profiles/<id>/...". When the active model matches any pattern
 * in \ref models and \ref enabled is true, the profile's \ref system text
 * either replaces or appends to the default system prompt depending on
 * \ref appendToDefault.
 */
struct PromptProfile {
    QString id;                 ///< Stable internal id (uuid-like).
    QString name;               ///< Human label shown in the dialog.
    QString system;             ///< The prompt body (replace or append).
    bool appendToDefault = true;///< true → append, false → replace.
    bool builtin = false;       ///< Read-only shipped profile.
    bool enabled = true;        ///< Resolution skips disabled profiles.
    /// Phase 47: when true, the `pitch_bend` branch is removed from the
    /// `events.anyOf` tool schema for every model matched by this profile.
    /// Agent mode only (Simple mode sends no tool schema, and the MCP server
    /// always serves the full schema). It exists because `pitch_bend` is the
    /// cheapest branch of the event schema - three required fields where a
    /// note needs six - so a weak model under schema pressure emits a lone
    /// placeholder bend instead of the notes it just planned. Taking the
    /// branch away is the only mitigation the model cannot ignore.
    bool disallowPitchBend = false;
    QStringList models;         ///< "<provider>:<modelId>" or "<provider>:<prefix>*".
};

#endif // PROMPTPROFILE_H
