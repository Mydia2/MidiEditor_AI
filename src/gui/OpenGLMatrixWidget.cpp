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

#include "OpenGLMatrixWidget.h"
#include "../protocol/Protocol.h"
#include "../midi/MidiFile.h"
#include <QDebug>
#include <QApplication>
#include <QContextMenuEvent>

OpenGLMatrixWidget::OpenGLMatrixWidget(QSettings *settings, QWidget *parent)
    : OpenGLPaintWidget(settings, parent) {
    // Create internal MatrixWidget instance
    _matrixWidget = new MatrixWidget(settings, this);

    // Hide the internal widget since we'll render its content through OpenGL
    _matrixWidget->hide();

    // Connect signals to forward them
    connect(_matrixWidget, &MatrixWidget::objectListChanged,
            this, &OpenGLMatrixWidget::onObjectListChanged);
    connect(_matrixWidget, &MatrixWidget::sizeChanged,
            this, &OpenGLMatrixWidget::onSizeChanged);

    qDebug() << "OpenGLMatrixWidget: Created with hardware acceleration";
}

OpenGLMatrixWidget::~OpenGLMatrixWidget() {
    // MatrixWidget will be deleted automatically as a child widget
}

void OpenGLMatrixWidget::paintContent(QPainter *painter) {
    // CRITICAL: Ensure the internal MatrixWidget has the same size as us
    QSize logicalSize = size();
    if (_matrixWidget->size() != logicalSize) {
        _matrixWidget->resize(logicalSize);
    }

    // OPTIMIZED DIRECT GPU RENDERING:
    // Use Qt's render() method which provides direct GPU acceleration
    // when used with an OpenGL-backed QPainter. This eliminates any
    // CPU->GPU->CPU->GPU conversions and provides true hardware acceleration.
    // Note: A default empty MIDI file is always loaded, so no null check needed
    _matrixWidget->render(painter, QPoint(0, 0), QRegion(),
                          QWidget::DrawWindowBackground | QWidget::DrawChildren);
}

// === Event Forwarding ===
//
// Every forwarded event goes through OpenGLPaintWidget::forwardToHosted(), which
// delivers it to the hidden MatrixWidget exactly once and then accepts it. Both
// halves matter: accepting stops Qt from propagating an un-accepted event from
// the hidden child back up into this wrapper, and the re-entry latch inside
// forwardToHosted() refuses the second forward even if some future handler
// starts ignoring events again. That combination is what GLCTX-001 (the
// 1.8.1.1 right-click stack overflow) needed, and it was still missing for the
// wheel.

void OpenGLMatrixWidget::mousePressEvent(QMouseEvent *event) {
    // Call parent to handle OpenGL mouse state
    OpenGLPaintWidget::mousePressEvent(event);

    // Forward to internal MatrixWidget for business logic
    if (forwardToHosted(event)) {
        // Use asynchronous update for consistent behavior with software rendering
        // This prevents GPU pipeline stalls during interactive operations
        update();
    }
}

void OpenGLMatrixWidget::mouseReleaseEvent(QMouseEvent *event) {
    // Call parent to handle OpenGL mouse state
    OpenGLPaintWidget::mouseReleaseEvent(event);

    // Forward to internal MatrixWidget for business logic
    if (forwardToHosted(event)) {
        // Use asynchronous update for consistent behavior with software rendering
        // This prevents GPU pipeline stalls during interactive operations
        update();
    }
}

void OpenGLMatrixWidget::mouseMoveEvent(QMouseEvent *event) {
    // Call parent to handle OpenGL mouse state
    OpenGLPaintWidget::mouseMoveEvent(event);

    // Forward to internal MatrixWidget for business logic
    if (forwardToHosted(event)) {
        // Use asynchronous update for smooth drag operations
        // This prevents GPU pipeline stalls and eliminates flickering during note selection/dragging
        update();
    }
}

void OpenGLMatrixWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    // Forward to internal MatrixWidget for timeline cursor positioning
    if (forwardToHosted(event)) {
        // Use asynchronous update for consistent behavior with software rendering
        update();
    }
}

void OpenGLMatrixWidget::resizeEvent(QResizeEvent *event) {
    // Resize internal MatrixWidget to match
    if (_matrixWidget) {
        _matrixWidget->resize(event->size());
    }

    // Let parent handle OpenGL resize
    QOpenGLWidget::resizeEvent(event);
}

void OpenGLMatrixWidget::enterEvent(QEnterEvent *event) {
    // Forward to internal MatrixWidget for piano key hover effects
    if (forwardToHosted(event)) {
        // Hidden widget's update() doesn't trigger OpenGL repaint
        update();
    }
}

void OpenGLMatrixWidget::leaveEvent(QEvent *event) {
    // Forward to internal MatrixWidget for piano key hover effects
    if (forwardToHosted(event)) {
        // Hidden widget's update() doesn't trigger OpenGL repaint
        update();
    }
}

void OpenGLMatrixWidget::wheelEvent(QWheelEvent *event) {
    // Forward wheel events to internal MatrixWidget for mouse scrolling.
    //
    // The matrix used to survive this only by accident: MatrixWidget::wheelEvent
    // never calls the QWidget base handler, so the event stayed accepted. One
    // early-out that reached QWidget::wheelEvent (whose whole body is
    // event->ignore()) would have turned piano-roll scrolling into the same
    // unbounded parent/child ping-pong that killed the velocity lane.
    if (forwardToHosted(event)) {
        // Hidden widget's zoom/scroll->update() doesn't trigger OpenGL repaint
        update();
    }
}

void OpenGLMatrixWidget::contextMenuEvent(QContextMenuEvent *event) {
    // Forward context menu events to internal MatrixWidget.
    //
    // The internal MatrixWidget is a *hidden child* of this widget. If its
    // contextMenuEvent leaves the event un-accepted (e.g. the right-click
    // happened with no events selected, as with the Measure tool), Qt
    // propagates the ignored QContextMenuEvent up the parent chain — straight
    // back to this wrapper. That re-enters contextMenuEvent → sendEvent →
    // ignored → propagate → ... an unbounded loop that blows the stack
    // (0xc00000fd EXCEPTION_STACK_OVERFLOW). Accepting the event here breaks
    // the cycle: we have handled it by delegating to the internal widget, so it
    // must not bubble back up to us. forwardToHosted() does both, and its
    // re-entry latch is a second line of defence if the child ever ignores it
    // again.
    forwardToHosted(event);
}

void OpenGLMatrixWidget::keyPressEvent(QKeyEvent *event) {
    // Forward to internal MatrixWidget. takeKeyPressEvent() is a direct call
    // rather than a posted event, so there is no propagation to re-enter - but
    // the event still has to be accepted: since G10 gave this wrapper
    // Qt::ClickFocus it can hold keyboard focus, and an un-accepted key would
    // travel on to MainWindow::keyPressEvent, whose fallback hands it to the
    // very same takeKeyPressEvent() a second time (double piano notes). The
    // software path behaves identically - MatrixWidget::keyPressEvent never
    // ignores either.
    if (_matrixWidget) {
        _matrixWidget->takeKeyPressEvent(event);
        // Hidden widget's conditional update() doesn't trigger OpenGL repaint
        // Update unconditionally since we can't check the tool's return value
        update();
    }
    event->accept();
}

void OpenGLMatrixWidget::keyReleaseEvent(QKeyEvent *event) {
    // Forward to internal MatrixWidget (see keyPressEvent for the accept()).
    if (_matrixWidget) {
        _matrixWidget->takeKeyReleaseEvent(event);
        // Hidden widget's conditional update() doesn't trigger OpenGL repaint
        // Update unconditionally since we can't check the tool's return value
        update();
    }
    event->accept();
}

void OpenGLMatrixWidget::setFile(MidiFile *file) {
    // Delegate to internal MatrixWidget
    if (_matrixWidget) {
        // Get the old file to disconnect from its protocol
        MidiFile *oldFile = _matrixWidget->midiFile();
        if (oldFile && oldFile->protocol()) {
            disconnect(oldFile->protocol(), &Protocol::actionFinished, this, &OpenGLMatrixWidget::registerRelayout);
            disconnect(oldFile->protocol(), &Protocol::actionFinished, this, QOverload<>::of(&QWidget::update));
        }

        _matrixWidget->setFile(file);

        // CRITICAL: Connect to protocol actionFinished signal for OpenGL updates
        // The internal MatrixWidget is hidden, so its calls don't trigger OpenGL updates
        // Set up both connections that MatrixWidget has: registerRelayout() and update()
        if (file && file->protocol()) {
            connect(file->protocol(), &Protocol::actionFinished, this, &OpenGLMatrixWidget::registerRelayout);
            connect(file->protocol(), &Protocol::actionFinished, this, QOverload<>::of(&QWidget::update));
        }
    }
}
