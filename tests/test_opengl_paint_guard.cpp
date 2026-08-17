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
 * The 2026-08 hardware-acceleration bug run added three more failure modes to
 * this file, all in the same "hidden child hosted by a visible GL wrapper"
 * family:
 *   - G2:  a wheel notch over the velocity lane was forwarded to a child that
 *          has no wheel handler, so QWidget::wheelEvent ignored it, Qt
 *          propagated it back up to the wrapper, and the wrapper forwarded it
 *          again - EXCEPTION_STACK_OVERFLOW on every notch. Covered by the
 *          re-entry tests below plus a source guard that both wrappers route
 *          their forwarding through the one guarded helper.
 *   - G7:  the paint device was pinned to the LOGICAL size with device pixel
 *          ratio 1.0, so a monitor / scaling change mid-session left the editor
 *          drawing into a corner of the framebuffer.
 *   - G10: the wrappers kept QWidget's default Qt::NoFocus, so the editor area
 *          could never take keyboard focus and piano emulation was dead.
 *
 * GLGUARD_REPO_ROOT is injected by CMake for the source scan.
 */

#include "../src/gui/OpenGLPaintWidget.h"

#include <QtTest/QtTest>
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QSettings>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <functional>

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

/** Reads a repo file; an empty result means "unreadable" and the callers
 *  assert on it. */
QString readRepoFile(const QString &relativePath)
{
    QFile f(QStringLiteral(GLGUARD_REPO_ROOT "/") + relativePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}

} // namespace

/**
 * Stands in for the hosted MatrixWidget / MiscWidget. Two jobs: be a HIDDEN
 * child that gets rendered, and reproduce the one behaviour that made the
 * wrappers recurse - a handler that leaves the event un-accepted, which is what
 * Qt's own QWidget base handlers do (their whole body is event->ignore()) and
 * which makes Qt hand the event to the PARENT, i.e. back into the wrapper.
 */
class ProbeChild : public QLabel {
public:
    explicit ProbeChild(QWidget *parent)
        : QLabel(QStringLiteral("content"), parent)
    {
    }

    int wheelCalls = 0;

    /** Bounded so a lost re-entry guard fails an assertion instead of blowing
     *  the stack and taking the whole test binary with it. */
    int bounceBudget = 20;

    /** How this child hands the un-accepted event back up. Set by the tests. */
    std::function<void(QWheelEvent *)> bounce;

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        ++wheelCalls;
        event->ignore(); // == QWidget::wheelEvent
        if (bounce && bounceBudget-- > 0) {
            bounce(event);
        }
    }
};

/**
 * A minimal stand-in for OpenGLMatrixWidget: it does the two things that
 * matter here - draw a HIDDEN child widget through QWidget::render() from
 * inside paintContent(), and forward input to it through the base class's
 * guarded forwarder. Everything else about the real widget (MidiFile, tools)
 * is irrelevant to these failure modes.
 */
class ProbeGlWidget : public OpenGLPaintWidget {
    Q_OBJECT

public:
    ProbeGlWidget(QSettings *settings, QWidget *parent)
        : OpenGLPaintWidget(settings, parent)
    {
        _child = new ProbeChild(this);
        _child->hide(); // exactly what the real wrappers do
    }

    int paintContentCalls = 0;
    QSize lastPaintSize;
    int wheelCalls = 0;

    ProbeChild *child() const { return _child; }

    /** The protected forwarder, reachable from the tests. */
    bool forwardForTest(QEvent *event) { return forwardToHosted(event); }

    /** The protected paint device, so the tests can check its geometry. */
    QOpenGLPaintDevice *paintDeviceForTest() const { return _paintDevice; }

protected:
    QWidget *hostedWidget() const override { return _child; }

    void wheelEvent(QWheelEvent *event) override
    {
        ++wheelCalls;
        forwardToHosted(event);
    }

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
    ProbeChild *_child;
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

    // --- 2b. G2: forwarding an event to the hidden child must not recurse ---

    // The exact shape of the crash: the child leaves the event un-accepted, Qt
    // hands it to the parent (this wrapper), and the wrapper forwards it to the
    // child a second time. Before the fix that loop had no bound at all; the
    // latch inside forwardToHosted() refuses the re-entered forward, so the
    // child sees the event exactly once and the event ends ACCEPTED, which is
    // what stops Qt's propagation walk.
    void forwardingRefusesReEntryAndEndsAccepted()
    {
        QSettings settings(QStringLiteral("MidiEditorTest"),
                           QStringLiteral("OpenGlPaintGuardTest"));
        QWidget host;
        ProbeGlWidget view(&settings, &host);

        view.child()->bounce = [&view](QWheelEvent *e) {
            // Stand in for Qt's parent-chain propagation of an ignored event.
            view.forwardForTest(e);
        };

        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(0, 0),
                          QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);

        QVERIFY(view.forwardForTest(&wheel));

        QCOMPARE(view.child()->wheelCalls, 1);
        QVERIFY2(view.child()->bounceBudget == 19,
                 "the child bounced more than once - the re-entry guard did not "
                 "hold, which is an unbounded recursion in the real widget");
        QVERIFY2(wheel.isAccepted(),
                 "an un-accepted forwarded event is exactly what Qt propagates "
                 "back into this wrapper (GLCTX-001 / G2)");
    }

    // The same thing through the real handler and Qt's real delivery path: a
    // wheel event sent to the wrapper must reach wheelEvent() once, be handed
    // to the child once, and come back accepted.
    void wheelEventOverTheWrapperDoesNotRecurse()
    {
        QSettings settings(QStringLiteral("MidiEditorTest"),
                           QStringLiteral("OpenGlPaintGuardTest"));
        QWidget host;
        ProbeGlWidget view(&settings, &host);

        view.child()->bounce = [&view](QWheelEvent *e) {
            // Deliver the ignored event the way Qt would - to the parent widget.
            QApplication::sendEvent(&view, e);
        };

        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(0, 0),
                          QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(&view, &wheel);

        QVERIFY2(view.wheelCalls > 0,
                 "the wrapper's wheelEvent was never reached - test setup issue");
        QVERIFY2(view.wheelCalls <= 2,
                 qPrintable(QStringLiteral("wheelEvent re-entered %1 times - in "
                                           "the real widget this is an "
                                           "unbounded recursion")
                                .arg(view.wheelCalls)));
        QCOMPARE(view.child()->wheelCalls, 1);
        QVERIFY(wheel.isAccepted());
    }

    // Neither wrapper may hand-roll its forwarding any more: the accept() and
    // the re-entry latch live in exactly one place, and a new handler that
    // calls QApplication::sendEvent() directly would silently opt out of both.
    void bothWrappersForwardThroughTheGuardedHelper()
    {
        const QStringList wrappers = {
            QStringLiteral("src/gui/OpenGLMatrixWidget.cpp"),
            QStringLiteral("src/gui/OpenGLMiscWidget.cpp"),
        };

        for (const QString &relativePath : wrappers) {
            const QString src = readRepoFile(relativePath);
            QVERIFY2(!src.isEmpty(), qPrintable(relativePath));

            QVERIFY2(src.contains(QStringLiteral("forwardToHosted(")),
                     qPrintable(relativePath + QStringLiteral(
                                    " no longer forwards through "
                                    "OpenGLPaintWidget::forwardToHosted()")));
            QVERIFY2(!src.contains(QStringLiteral("QApplication::sendEvent(")),
                     qPrintable(relativePath + QStringLiteral(
                                    " forwards an event by hand again - it would "
                                    "skip the accept() and the re-entry guard "
                                    "(GLCTX-001 / G2)")));
        }
    }

    // --- 2c. G7: the paint target must follow the device pixel ratio --------

    void glPaintDeviceSizeIsTheLogicalSizeInDevicePixels()
    {
        QCOMPARE(OpenGLPaintWidget::glPaintDeviceSize(QSize(800, 600), 1.0),
                 QSize(800, 600));
        QCOMPARE(OpenGLPaintWidget::glPaintDeviceSize(QSize(800, 600), 1.5),
                 QSize(1200, 900));
        QCOMPARE(OpenGLPaintWidget::glPaintDeviceSize(QSize(801, 601), 1.25),
                 QSize(1001, 751));
        QCOMPARE(OpenGLPaintWidget::glPaintDeviceSize(QSize(800, 600), 2.0),
                 QSize(1600, 1200));
        // Defensive: a nonsensical ratio must not collapse the paint target.
        QCOMPARE(OpenGLPaintWidget::glPaintDeviceSize(QSize(800, 600), 0.0),
                 QSize(800, 600));
    }

    // Source guard: the old code pinned the device to the logical size with
    // ratio 1.0 and set glViewport from the logical size, which is why moving
    // the window onto a scaled monitor left the editor in a corner of the
    // framebuffer. Nothing else in the tree re-evaluates the ratio, so this must
    // not come back.
    void paintDeviceGeometryFollowsTheDevicePixelRatio()
    {
        const QString src = readRepoFile(
            QStringLiteral("src/gui/OpenGLPaintWidget.cpp"));
        QVERIFY(!src.isEmpty());

        QVERIFY2(!src.contains(QStringLiteral("setDevicePixelRatio(1.0)")),
                 "the paint device is pinned to device pixel ratio 1.0 again - "
                 "the editor will draw into a corner of the surface on any "
                 "scaled display (G7)");
        QVERIFY2(src.contains(QStringLiteral("devicePixelRatioF()")),
                 "the paint geometry must be derived from the widget's actual "
                 "device pixel ratio");
        QVERIFY2(!src.contains(QStringLiteral("glViewport(0, 0, w, h)")),
                 "glViewport takes DEVICE pixels, not the logical widget size");
    }

    // --- 2d. G18: the anti-aliasing preference must be honoured ------------

    void paintGlHonoursTheAntialiasingSetting()
    {
        const QString src = readRepoFile(
            QStringLiteral("src/gui/OpenGLPaintWidget.cpp"));
        QVERIFY(!src.isEmpty());

        QVERIFY2(src.contains(QStringLiteral("rendering/antialiasing")),
                 "the hardware path ignores the user's anti-aliasing setting "
                 "again (G18); MSAA does not cover the rasterised grid pixmap");
        QVERIFY2(!src.contains(
                     QStringLiteral("setRenderHint(QPainter::Antialiasing, true)")),
                 "anti-aliasing is hard-coded on again instead of reading the "
                 "setting");
    }

    // --- 2e. G10: the editor area must be able to take keyboard focus -------

    void wrapperTakesClickFocusLikeTheWidgetItHosts()
    {
        QSettings settings(QStringLiteral("MidiEditorTest"),
                           QStringLiteral("OpenGlPaintGuardTest"));
        QWidget host;
        ProbeGlWidget view(&settings, &host);

        QCOMPARE(view.focusPolicy(), Qt::ClickFocus);
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

        // G7: the paint device must describe the framebuffer QOpenGLWidget
        // actually gave us - size() in DEVICE pixels plus the real ratio - so
        // QPainter's logical coordinates stay the hosted widget's own. On a
        // 100%-scaled machine this is a no-op; the source guard above is what
        // covers the scaled case, which needs a second monitor to reproduce.
        QOpenGLPaintDevice *device = view->paintDeviceForTest();
        QVERIFY2(device, "no OpenGL paint device was ever created");
        const qreal dpr = view->devicePixelRatioF();
        QCOMPARE(device->devicePixelRatio(), dpr);
        QCOMPARE(device->size(),
                 OpenGLPaintWidget::glPaintDeviceSize(view->size(), dpr));

        settings.clear();
    }
};

QTEST_MAIN(TestOpenGlPaintGuard)
#include "test_opengl_paint_guard.moc"
