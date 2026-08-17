/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EDITORTOOL_H_
#define EDITORTOOL_H_

// Qt includes
#include <QCursor>
#include <QPainter>

// Project includes
#include "Tool.h"

// Forward declarations
class MatrixWidget;
class MainWindow;

/**
 * \class EditorTool
 *
 * \brief Base class for interactive editing tools in the MIDI editor.
 *
 * EditorTool provides the foundation for all interactive editing tools that
 * operate on the matrix widget. These tools handle user input and provide
 * visual feedback for editing operations:
 *
 * - **Tool selection**: Only one EditorTool can be active at a time
 * - **Visual feedback**: Tools draw overlays and cursors on the matrix
 * - **Input handling**: Process mouse clicks, drags, and keyboard input
 * - **Widget integration**: Connect to MatrixWidget for editing
 * - **State management**: Maintain tool-specific state and settings
 *
 * Key features:
 * - Exclusive selection (selecting one deselects others)
 * - Custom drawing capabilities for visual feedback
 * - Mouse and keyboard event handling
 * - Integration with the matrix widget coordinate system
 * - Protocol integration for undo/redo support
 *
 * Common editor tools include SelectTool, NewNoteTool, EraserTool, etc.
 * Each tool provides specialized functionality for different editing tasks.
 */
class EditorTool : public Tool {
public:
    /**
     * \brief Creates a new EditorTool.
     */
    EditorTool();

    /**
     * \brief Creates a new EditorTool copying another instance.
     * \param other The EditorTool instance to copy
     */
    EditorTool(EditorTool &other);

    /**
     * \brief Draws the EditorTool's visual feedback to the painter.
     * \param painter The QPainter to draw with
     */
    virtual void draw(QPainter *painter);

    /**
     * \brief Called when the mouse is clicked above the widget.
     * \param leftClick True if left mouse button was pressed
     * \return True if the widget needs to be repainted after the tool's action
     */
    virtual bool press(bool leftClick);

    /**
     * \brief Called when a key is pressed while the widget is focused.
     * \param key The key code that was pressed
     * \return True if the widget needs to be repainted after the tool's action
     */
    virtual bool pressKey(int key);

    /**
     * \brief Called when a key is released while the widget is focused.
     * \param key The key code that was released
     * \return True if the widget needs to be repainted after the tool's action
     */
    virtual bool releaseKey(int key);

    /**
     * \brief Called when the mouse is released above the widget.
     * \return True if the widget needs to be repainted after the tool's action
     */
    virtual bool release();

    /**
     * \brief Called when the mouse is released without triggering the main action.
     * \return True if the widget needs to be repainted after the tool's action
     *
     * This method is called when the mouse is released but the main release
     * action should not be executed (e.g., when canceling an operation).
     */
    virtual bool releaseOnly();

    /**
     * \brief Called when the mouse is moved above the widget.
     * \param mouseX Current mouse X coordinate
     * \param mouseY Current mouse Y coordinate
     * \return True if the widget needs to be repainted after the tool's action
     */
    virtual bool move(int mouseX, int mouseY);

    /**
     * \brief Called when the mouse has exited the widget.
     */
    virtual void exit();

    /**
     * \brief Called when the mouse has entered the widget.
     */
    virtual void enter();

    /**
     * \brief Deselects this tool.
     *
     * Updates the tool's button appearance (if it exists) to show
     * the deselected state.
     */
    void deselect();

    /**
     * \brief Selects this tool.
     *
     * Updates the tool's button appearance (if it exists) to show
     * the selected state.
     */
    void select();

    /**
     * \brief Checks if this tool is currently selected.
     * \return True if the tool is selected
     */
    bool selected();

    /**
     * \brief Handles button click events for tool selection.
     */
    virtual void buttonClick();

    /**
     * \brief Creates a copy of this tool for the protocol system.
     * \return A new ProtocolEntry representing this tool's state
     */
    virtual ProtocolEntry *copy();

    /**
     * \brief Reloads the tool's state from a protocol entry.
     * \param entry The protocol entry to restore state from
     */
    virtual void reloadState(ProtocolEntry *entry);

    /**
     * \brief Sets the matrix widget for all editor tools.
     * \param w The MatrixWidget to associate with tools
     */
    static void setMatrixWidget(MatrixWidget *w);

    /**
     * \brief The matrix widget the tools currently act on - i.e. the FOCUSED
     * editor view in a multi-pane layout. Lets a view tell whether it is the
     * tool's target, so only the focused pane draws the tool overlay/selection.
     */
    static MatrixWidget *currentMatrixWidget();

    /**
     * \brief Sets the OpenGL container widget for cursor operations.
     * \param container The OpenGL container widget that should receive cursor changes
     *
     * \deprecated Kept for source compatibility with the MainWindow setup and
     * teardown calls. Cursor routing is resolved per view by cursorTarget()
     * instead: one process-wide container pointer could only ever be right for
     * one editor group, so with a second group open the cursor was applied to
     * the wrong pane.
     */
    static void setOpenGLContainer(QWidget *container);

    /**
     * \brief The widget that must receive cursor changes for the pane the tools
     *  are currently acting on.
     * \return The visible pane, or nullptr when there is no current view.
     *
     * Under hardware acceleration the visible pane is an OpenGLMatrixWidget and
     * the tools' `matrixWidget` is its HIDDEN child, whose cursor Qt never
     * applies - hence the indirection. Every other pane (the software primary
     * view, and the second editor group's compare view, which is always a plain
     * MatrixWidget) is the visible widget itself. Resolving this per call is
     * what keeps both cases right when they exist side by side.
     */
    static QWidget *cursorTarget();

    /**
     * \brief Applies \a cursor to cursorTarget(), if there is one.
     */
    static void setToolCursor(const QCursor &cursor);

    /**
     * \brief Sets the main window for all editor tools.
     * \param mw The MainWindow to associate with tools
     */
    static void setMainWindow(MainWindow *mw);

    /**
     * \brief Tests if a point is within a rectangle.
     * \param x Point X coordinate
     * \param y Point Y coordinate
     * \param x_start Rectangle start X coordinate
     * \param y_start Rectangle start Y coordinate
     * \param x_end Rectangle end X coordinate
     * \param y_end Rectangle end Y coordinate
     * \return True if the point is within the rectangle
     */
    bool pointInRect(int x, int y, int x_start, int y_start, int x_end, int y_end);

protected:
    /** \brief Flag indicating if this tool is selected */
    bool etool_selected;

    /** \brief Current mouse coordinates */
    int mouseX, mouseY;

    /** \brief Flag indicating if mouse is inside the widget */
    bool mouseIn;

    /** \brief Static reference to the matrix widget */
    static MatrixWidget *matrixWidget;

    /** \brief Static reference to the OpenGL container widget for cursor
     *  operations. Retained only so setOpenGLContainer() keeps working; the
     *  cursor routing itself no longer reads it - see cursorTarget(). */
    static QWidget *_openglContainer;

    /** \brief Static reference to the main window */
    static MainWindow *_mainWindow;
};

#endif // EDITORTOOL_H_
