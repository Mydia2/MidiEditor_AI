/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.+
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MIDITRACK_H_
#define MIDITRACK_H_

// Project includes
#include "../protocol/ProtocolEntry.h"

// Qt includes
#include <QObject>
#include <QString>

// Forward declarations
class TextEvent;
class MidiFile;
class QColor;

/**
 * \class MidiTrack
 *
 * \brief Represents a single MIDI track within a MIDI file.
 *
 * MidiTrack manages a collection of MIDI events that belong to a specific
 * track in the MIDI file. Each track can contain events for multiple MIDI
 * channels and provides organization and management capabilities:
 *
 * - **Event organization**: Groups related MIDI events together
 * - **Track naming**: User-friendly track names and identification
 * - **Channel assignment**: Default channel assignment for new events
 * - **Visibility control**: Show/hide tracks in the editor
 * - **Mute control**: Enable/disable track playback
 * - **Color coding**: Visual identification in the editor
 *
 * Key features:
 * - Protocol integration for undo/redo support
 * - Track name management with TextEvent integration
 * - Channel assignment for streamlined event creation
 * - Visibility and mute state management
 * - Color customization for visual organization
 * - Integration with the parent MidiFile
 *
 * Tracks provide a logical grouping mechanism that helps organize complex
 * MIDI compositions with multiple instruments or parts.
 */
class MidiTrack : public QObject, public ProtocolEntry {
    Q_OBJECT

public:
    /**
     * \brief Creates a new MidiTrack.
     * \param file The parent MidiFile this track belongs to
     */
    MidiTrack(MidiFile *file);

    /**
     * \brief Creates a new MidiTrack copying another instance.
     * \param other The MidiTrack instance to copy
     */
    MidiTrack(MidiTrack &other);

    /**
     * \brief Destroys the MidiTrack and cleans up resources.
     */
    virtual ~MidiTrack();

    // === Track Identification ===

    /**
     * \brief Gets the track name.
     * \return The user-friendly name of this track
     */
    QString name();

    /**
     * \brief Sets the track name.
     * \param name The new name for this track
     */
    void setName(QString name);

    /**
     * \brief Gets the track number.
     * \return The numeric identifier of this track
     */
    int number();

    /**
     * \brief Sets the track number.
     * \param number The new numeric identifier for this track
     */
    void setNumber(int number);

    /**
     * \brief Sets the TextEvent that contains the track name.
     * \param nameEvent The TextEvent containing the track name
     */
    void setNameEvent(TextEvent *nameEvent);

    /**
     * \brief Gets the TextEvent that contains the track name.
     * \return The TextEvent containing the track name, or nullptr if none
     */
    TextEvent *nameEvent();

    // === File Association ===

    /**
     * \brief Gets the parent MIDI file.
     * \return Pointer to the MidiFile containing this track
     */
    MidiFile *file();

    // === Channel Management ===

    /**
     * \brief Assigns a default MIDI channel to this track.
     * \param ch The MIDI channel (0-15) to assign
     */
    void assignChannel(int ch);

    /**
     * \brief Gets the assigned MIDI channel.
     * \return The default MIDI channel for this track (0-15)
     */
    int assignedChannel();

    // === Visibility and State ===

    /**
     * \brief Sets the hidden state of the track (DOCUMENT state, undoable).
     * \param hidden True to hide the track, false to show it
     *
     * FOCUS-DEADEYE-001 (v2.2 review): a deliberate user visibility action
     * also ENDS the focus overlay for this track (see setFocusHidden), so the
     * Tracks-panel eye, the Track menu's visibility entries and "Show all
     * tracks" can always bring a focus-hidden track back. Without that the
     * overlay would survive a setHidden(false) and those controls would look
     * dead while still pushing undo steps.
     */
    void setHidden(bool hidden);

    /**
     * \brief Phase 9.9f §15.2 (Show-Mode follow-the-host): flip the
     * hidden flag WITHOUT recording a Protocol step or emitting
     * trackChanged. Used on the viewer side to apply the presenter's
     * view state silently — viewers shouldn't have a hat-pass land
     * in their undo history. Caller must trigger a repaint manually
     * (e.g. MainWindow::updateAll) when applying a batch.
     */
    void setHiddenSilent(bool hidden) { _hidden = hidden; }

    /**
     * \brief Gets the EFFECTIVE hidden state of the track - what the editor
     *        actually shows.
     * \return True if the track is invisible, false if visible - INCLUDING
     *         the temporary focus overlay (see setFocusHidden).
     *
     * Use this for RENDERING and HIT-TESTING - and for a control that shows
     * the user what is on screen, such as the Tracks panel's eye. Anything
     * that reads the DOCUMENT's visibility - a save-time warning, the collab
     * broadcast, the AI's track list, a dialog whose checkbox writes the
     * document flag - must use hiddenByUser() instead, or it mistakes the view
     * overlay for the file's own state (FOCUS-DEADEYE-001).
     */
    bool hidden();

    /**
     * \brief The track's OWN (document) hidden flag, ignoring the focus
     *        overlay - this is the value the protocol/undo system stores.
     */
    bool hiddenByUser() const { return _hidden; }

    /**
     * \brief FOCUS-UNDO-001 (v2.2 review): temporary VIEW-state hiding for
     *  the playability workbench's focus mode. Unlike _hidden this flag is
     *  NOT copied by the copy ctor and NOT restored by reloadState - it can
     *  therefore never be baked into an undo snapshot while focus is active
     *  and later resurface on Ctrl+Z. hidden() ORs it in, so every renderer
     *  honours it without knowing about focus mode. Same pattern as
     *  MidiChannel's snapshot counters.
     *
     * Cleared again by setHidden() (a deliberate user visibility action wins
     * over the overlay) and by whoever turned it on - the workbench remembers
     * exactly which tracks it dimmed and clears those track objects when it
     * closes, which also covers a track removed while focus was active
     * (its object stays alive for undo).
     */
    void setFocusHidden(bool focusHidden) { _focusHidden = focusHidden; }
    bool focusHidden() const { return _focusHidden; }

    /**
     * \brief Sets the muted state of the track.
     * \param muted True to mute the track, false to unmute it
     */
    void setMuted(bool muted);

    /**
     * \brief Gets the muted state of the track.
     * \return True if the track is muted, false if audible
     */
    bool muted();

    // === Protocol System Integration ===

    /**
     * \brief Creates a copy of this track for the protocol system.
     * \return A new ProtocolEntry representing this track's state
     */
    virtual ProtocolEntry *copy();

    /**
     * \brief Reloads the track's state from a protocol entry.
     * \param entry The protocol entry to restore state from
     */
    virtual void reloadState(ProtocolEntry *entry);

    // === Visual and Utility Methods ===

    /**
     * \brief Gets the track's display color.
     * \return Pointer to the QColor used for visual representation
     */
    QColor *color();

    /**
     * \brief Creates a copy of this track in another MIDI file.
     * \param file The target MidiFile to copy the track to
     * \return Pointer to the newly created MidiTrack copy
     */
    MidiTrack *copyToFile(MidiFile *file);

signals:
    /**
     * \brief Emitted when the track's properties change.
     */
    void trackChanged();

private:
    /** \brief Track number identifier */
    int _number;

    /** \brief TextEvent containing the track name */
    TextEvent *_nameEvent;

    /** \brief Parent MIDI file */
    MidiFile *_file;

    /** \brief Track visibility and mute state */
    bool _hidden, _muted;

    /** \brief FOCUS-UNDO-001: focus-mode overlay - deliberately NOT copied
     *  by the copy ctor, NOT touched by reloadState. See setFocusHidden. */
    bool _focusHidden = false;

    /** \brief Default MIDI channel assignment */
    int _assignedChannel;
};

#endif // MIDITRACK_H_
