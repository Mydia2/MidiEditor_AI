/*
 * test_opengl_paint_guard - GLBLANK-001 regression cover (hotfix 2.1.1)
 *
 * "Enable GPU acceleration for MIDI events" swaps the piano roll and the
 * velocity lane for QOpenGLWidget-based wrappers (OpenGLMatrixWidget /
 * OpenGLMiscWidget) that draw a HIDDEN child widget through
 * QWidget::render(). That construction has now broken twice - a context-menu
 * stack overflow in 1.8.1.1, and a permanently blank editor area in 2.1.1 -
 * and had no automated cover at all. This file is that cover.
 *
 * What broke in 2.1.1: Qt's prepareToRender() reacts to a hidden widget by
 * walking the ancestor chain, clearing WA_WState_Hidden on every hidden
 * ancestor, invalidating their layouts, and setting the flag back afterwards.
 * When Qt paints the wrapper once during the window's show sequence - before
 * it is visible - its OWN ancestors are still flagged hidden and get
 * re-hidden for good. The editor group then keeps ZERO WIDTH for the rest of
 * the session, so the piano roll AND the group's tab strip stay invisible
 * while the docks around them render normally. The same layout pass also
 * resizes the wrapper mid-paint, re-entering paintGL() where the second
 * QPainter::begin() on the one QOpenGLPaintDevice fails.
 *
 * Three layers of cover, deliberately:
 *   1. the decision rule (OpenGLPaintWidget::canPaintNow) - pure, no OpenGL
 *      context needed, so it runs everywhere including CI;
 *   2. a source guard, so the rule cannot quietly be dropped out of
 *      paintGL() again the way the whole feature went uncovered before;
 *   3. an end-to-end paint through a real OpenGLPaintWidget subclass that
 *      renders a hidden child inside an editor-group-shaped layout. This is
 *      the one that reproduces the field failure; it needs a working OpenGL
 *      context and skips where there is none.
 *
 * GLGUARD_REPO_ROOT is injected by CMake for the source scan.
 */

#include "../src/gui/OpenGLPaintWidget.h"

#include <QtTest/QtTest>
#include <QFile>
#include <QLabel>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QSettings>
#include <QVBoxLayout>
#include <QWidget>

namespace {

/** True when this machine can actually give us an OpenGL context. */
bool openGlAvailable()
{
    QOpenGLContext ctx;
    if (!ctx.create()) {
        return false;
    }
    QOffscreenSurface surface;
    surface.setFormat(ctx.format());
    surface.create();
    if (!surface.isValid()) {
        return false;
    }
    const bool ok = ctx.makeCurrent(&surface);
    if (ok) {
        ctx.doneCurrent();
    }
    return ok;
}

} // namespace

/**
 * A minimal stand-in for OpenGLMatrixWidget: it does the one thing that
 * matters here - draw a HIDDEN child widget through QWidget::render() from
 * inside paintContent(). Everything else about the real widget (MidiFile,
 * tools, event forwarding) is irrelevant to this failure mode.
 */
class ProbeGlWidget : public OpenGLPaintWidget {
    Q_OBJECT

public:
    ProbeGlWidget(QSettings *settings, QWidget *parent)
        : OpenGLPaintWidget(settings, parent)
    {
        _child = new QLabel(QStringLiteral("content"), this);
        _child->hide(); // exactly what the real wrappers do
    }

    int paintContentCalls = 0;
    QSize lastPaintSize;

protected:
    void paintContent(QPainter *painter) override
    {
        ++paintContentCalls;
        lastPaintSize = size();
        if (_child->size() != size()) {
            _child->resize(size());
        }
        _child->render(painter, QPoint(0, 0), QRegion(),
                       QWidget::DrawWindowBackground | QWidget::DrawChildren);
    }

private:
    QLabel *_child;
};

class TestOpenGlPaintGuard : public QObject {
    Q_OBJECT

private slots:

    // --- 1. the decision rule ---------------------------------------------

    void canPaintNow_allowsTheOrdinaryFrame()
    {
        QVERIFY(OpenGLPaintWidget::canPaintNow(/*alreadyPainting*/ false,
                                               /*widgetVisible*/ true));
    }

    // The re-entrant case: a layout pass inside paintContent() resizes the
    // widget, Qt paints it again, and the nested QPainter::begin() on the
    // same QOpenGLPaintDevice cannot succeed.
    void canPaintNow_refusesWhileAnOuterPaintOwnsTheDevice()
    {
        QVERIFY(!OpenGLPaintWidget::canPaintNow(true, true));
    }

    // The root cause of GLBLANK-001: painting before the widget is visible
    // lets Qt's prepareToRender() re-hide the still-appearing ancestors.
    void canPaintNow_refusesBeforeTheWidgetIsVisible()
    {
        QVERIFY(!OpenGLPaintWidget::canPaintNow(false, false));
    }

    void canPaintNow_refusesWhenBothConditionsFail()
    {
        QVERIFY(!OpenGLPaintWidget::canPaintNow(true, false));
    }

    // --- 2. the source guard ----------------------------------------------

    // A refused frame must be DEFERRED, never dropped: the frame that
    // paintGL() turned away was the only one the triggering resize/show ever
    // scheduled, which is why the widget stayed blank for a whole session
    // instead of recovering on the next repaint.
    void paintGlConsultsTheGuardAndDefersInsteadOfDropping()
    {
        const QString path =
            QStringLiteral(GLGUARD_REPO_ROOT "/src/gui/OpenGLPaintWidget.cpp");
        QFile f(path);
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(path));
        const QString src = QString::fromUtf8(f.readAll());

        const int guardAt = src.indexOf(QStringLiteral("canPaintNow("));
        QVERIFY2(guardAt >= 0,
                 "paintGL() no longer consults canPaintNow() - GLBLANK-001 "
                 "(blank editor area with GPU acceleration on) can come back");

        const int paintGlAt =
            src.indexOf(QStringLiteral("void OpenGLPaintWidget::paintGL()"));
        QVERIFY(paintGlAt >= 0);
        QVERIFY2(guardAt > paintGlAt,
                 "the guard must sit INSIDE paintGL(), before any painting");

        QVERIFY2(src.contains(QStringLiteral("requestDeferredRepaint()")),
                 "a refused frame must be re-requested, not dropped");
    }

    // --- 3. end to end, on a real OpenGL context --------------------------

    // The regression itself: an editor-group-shaped layout whose view is an
    // OpenGLPaintWidget rendering a hidden child. Before the fix the group
    // collapsed to zero width and its sibling strip never appeared.
    void editorGroupKeepsItsWidthAndPaints()
    {
        if (!openGlAvailable()) {
            QSKIP("no usable OpenGL context on this machine");
        }

        QSettings settings(QStringLiteral("MidiEditorTest"),
                           QStringLiteral("OpenGlPaintGuardTest"));

        // [ window [ group [ tab strip | view ] ] ] - the same nesting the
        // editor uses, because the collapse happened to the GROUP, not just
        // to the view.
        QWidget top;
        QVBoxLayout *topLayout = new QVBoxLayout(&top);
        topLayout->setContentsMargins(0, 0, 0, 0);

        QWidget *group = new QWidget(&top);
        topLayout->addWidget(group);

        QVBoxLayout *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        QLabel *tabStrip = new QLabel(QStringLiteral("tabs"), group);
        groupLayout->addWidget(tabStrip, 0);
        ProbeGlWidget *view = new ProbeGlWidget(&settings, group);
        groupLayout->addWidget(view, 1);

        top.resize(800, 600);
        top.show();
        QVERIFY(QTest::qWaitForWindowExposed(&top));
        // Deferred frames arrive through the event loop, so give them a turn.
        QTest::qWait(300);

        QVERIFY2(view->width() > 100,
                 qPrintable(QStringLiteral("view collapsed to width %1 - the "
                                           "GLBLANK-001 failure")
                                .arg(view->width())));
        QVERIFY2(group->width() > 100,
                 "the editor group collapsed - its tab strip would be invisible");
        QVERIFY2(tabStrip->isVisible(),
                 "the sibling strip was left hidden by prepareToRender()");
        QVERIFY2(view->paintContentCalls > 0,
                 "the view never painted at all - a deferred frame was dropped");
        QVERIFY2(view->lastPaintSize.width() > 100,
                 "the view only ever painted at its pre-layout size");

        settings.clear();
    }
};

QTEST_MAIN(TestOpenGlPaintGuard)
#include "test_opengl_paint_guard.moc"
