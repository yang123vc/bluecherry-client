#include "LiveFeedItem.h"
#include "MJpegFeedItem.h"
#include "PtzPresetsWindow.h"
#include "core/BluecherryApp.h"
#include "core/CameraPtzControl.h"
#include "LiveViewWindow.h"
#include "ui/MainWindow.h"
#include "utils/FileUtils.h"
#include "core/DVRServer.h"
#include <QMessageBox>
#include <QDesktopServices>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QDateTime>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QDataStream>
#include <QPixmapCache>
#include <QInputDialog>
#include <QSignalMapper>
#include <QApplication>
#include <QDesktopWidget>

LiveFeedItem::LiveFeedItem(QDeclarativeItem *parent)
    : QDeclarativeItem(parent), m_customCursor(DefaultCursor)
{
}

void LiveFeedItem::setCamera(const DVRCamera &camera)
{
    if (camera == m_camera)
        return;

    MJpegFeedItem *mjpeg = findChild<MJpegFeedItem*>(QLatin1String("mjpegFeed"));
    Q_ASSERT(mjpeg);
    if (!mjpeg)
        return;

    if (m_camera)
    {
        static_cast<QObject*>(m_camera)->disconnect(this);
        mjpeg->clear();
    }

    m_camera = camera;

    if (m_camera)
    {
        connect(m_camera, SIGNAL(dataUpdated()), SLOT(cameraDataUpdated()));
        connect(m_camera, SIGNAL(onlineChanged(bool)), SLOT(cameraDataUpdated()));
    }

    emit cameraChanged(camera);
    cameraDataUpdated();
}

void LiveFeedItem::cameraDataUpdated()
{
    emit cameraNameChanged(cameraName());

    MJpegFeedItem *mjpeg = findChild<MJpegFeedItem*>(QLatin1String("mjpegFeed"));

    if (!m_camera.isOnline())
    {
        if (m_camera.isDisabled())
        {
            mjpeg->clear();
            setStatusText(tr("<span style='color:#444444'>Disabled</span>"));
            return;
        }
        else if (!mjpeg->stream() || mjpeg->stream()->currentFrame().isNull())
        {
            mjpeg->clear();
            setStatusText(tr("<span style='color:#444444'>Offline</span>"));
            return;
        }
    }

    QSharedPointer<MJpegStream> nstream = m_camera.mjpegStream();
    if (nstream != mjpeg->stream())
        mjpeg->setStream(m_camera.mjpegStream());
}

void LiveFeedItem::setStatusText(const QString &text)
{
    m_statusText = text;
    emit statusTextChanged(m_statusText);
}

void LiveFeedItem::openNewWindow()
{
    LiveViewWindow::openWindow(bcApp->mainWindow, false, camera())->show();
}

void LiveFeedItem::openFullScreen()
{
    LiveViewWindow::openWindow(bcApp->mainWindow, true, camera())->showFullScreen();
}

void LiveFeedItem::close()
{
    bool closeFeedItem = parentItem()->metaObject()->invokeMethod(parentItem(), "removeItem", Q_ARG(QDeclarativeItem*, this));
    Q_ASSERT(closeFeedItem);
    Q_UNUSED(closeFeedItem);
}

void LiveFeedItem::saveSnapshot(const QString &ifile)
{
    if (!m_camera)
        return;

    /* Grab the current frame, so the user gets what they expect regardless of the time taken by the dialog */
    MJpegFrame frame = m_camera.mjpegStream()->currentFrame();
    if (frame.isNull())
        return;

    QWidget *window = scene()->views().value(0);

    QString file = ifile;

    if (file.isEmpty())
    {
        file = getSaveFileNameExt(window, tr("%1 - Save Snapshot").arg(m_camera.displayName()),
                           QDesktopServices::storageLocation(QDesktopServices::PicturesLocation),
                           QLatin1String("ui/snapshotSaveLocation"),
                           QString::fromLatin1("%1 - %2.jpg").arg(m_camera.displayName(),
                                                                  QDateTime::currentDateTime().toString(
                                                                  QLatin1String("yyyy-MM-dd hh-mm-ss"))),
                           tr("Image (*.jpg)"));

        if (file.isEmpty())
            return;
    }

    if (!frame.save(file, "jpeg"))
    {
        QMessageBox::critical(window, tr("Snapshot Error"), tr("An error occurred while saving the snapshot image."),
                              QMessageBox::Ok);
        return;
    }
}

void LiveFeedItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    event->accept();

    QMenu menu(event->widget());

    MJpegFeedItem *mjpeg = findChild<MJpegFeedItem*>(QLatin1String("mjpegFeed"));
    if (!mjpeg)
        return;

    menu.addAction(tr("Snapshot"), this, SLOT(saveSnapshot()))->setEnabled(mjpeg->stream() && !mjpeg->stream()->currentFrame().isNull());
    menu.addSeparator();

    QMenu *ptzmenu = 0;
    if (camera().hasPtz())
    {
        QAction *a = menu.addAction(tr("Pan / Tilt / Zoom"), this, SLOT(togglePtzEnabled()));
        a->setCheckable(true);
        a->setChecked(ptz());

        if (m_ptz)
        {
            ptzmenu = ptzMenu();
            ptzmenu->setTitle(tr("PTZ"));
            menu.addMenu(ptzmenu);
            menu.addSeparator();
        }
    }

    QAction *a = menu.addAction(mjpeg->isPaused() ? tr("Paused") : tr("Pause"), mjpeg, SLOT(togglePaused()));
    a->setCheckable(true);
    a->setChecked(mjpeg->isPaused());
    a->setEnabled(m_camera && (m_camera.mjpegStream() || a->isChecked()));

    menu.addSeparator();
    menu.addAction(tr("Open in window"), this, SLOT(openNewWindow()));
    menu.addAction(tr("Open as fullscreen"), this, SLOT(openFullScreen()));
    menu.addSeparator();

    QAction *actClose = menu.addAction(tr("Close camera"), this, SLOT(close()));
    actClose->setEnabled(m_camera);

    menu.exec(event->screenPos());
    delete ptzmenu;
}

void LiveFeedItem::saveState(QDataStream *stream)
{
    Q_ASSERT(stream);

    *stream << m_camera;
}

void LiveFeedItem::loadState(QDataStream *stream)
{
    Q_ASSERT(stream);

    DVRCamera c;
    *stream >> c;
    setCamera(c);
}

void LiveFeedItem::setCustomCursor(CustomCursor cursor)
{
    if (cursor == m_customCursor)
        return;

    m_customCursor = cursor;

    int rotate = 0;

    switch (m_customCursor)
    {
    case DefaultCursor:
        setCursor(QCursor());
        return;
    case MoveCursorE:
        break;
    case MoveCursorSE:
        rotate = 45;
        break;
    case MoveCursorS:
        rotate = 90;
        break;
    case MoveCursorSW:
        rotate = 135;
        break;
    case MoveCursorW:
        rotate = 180;
        break;
    case MoveCursorNW:
        rotate = 225;
        break;
    case MoveCursorN:
        rotate = 270;
        break;
    case MoveCursorNE:
        rotate = 315;
        break;
    }

    QString key = QString::fromLatin1("ptzcursor-%1").arg(rotate);
    QPixmap pm;
    if (!QPixmapCache::find(key, pm))
    {
        pm = QPixmap(QLatin1String(":/images/ptz-arrow.png")).transformed(QTransform().rotate(rotate), Qt::SmoothTransformation);
        QPixmapCache::insert(key, pm);
    }

    setCursor(QCursor(pm));
}

void LiveFeedItem::setPtzEnabled(bool ptzEnabled)
{
    if (ptzEnabled == !m_ptz.isNull())
        return;

    if (ptzEnabled)
        m_ptz = CameraPtzControl::sharedObjectFor(camera());
    else
        m_ptz.clear();

    emit ptzChanged(m_ptz.data());
}

void LiveFeedItem::wheelEvent(QGraphicsSceneWheelEvent *event)
{
    if (!m_ptz)
    {
        event->ignore();
        return;
    }

    event->accept();

    int steps = event->delta() / 120;
    if (!steps)
        return;

    m_ptz->move((steps < 0) ? CameraPtzControl::MoveWide : CameraPtzControl::MoveTele);
}

QMenu *LiveFeedItem::ptzMenu()
{
    QMenu *menu = new QMenu;

    QMenu *presetsMenu = menu->addMenu(tr("Presets"));
    QSignalMapper *mapper = new QSignalMapper(presetsMenu);
    connect(mapper, SIGNAL(mapped(int)), m_ptz.data(), SLOT(moveToPreset(int)));

    const QMap<int,QString> &presets = m_ptz->presets();
    for (QMap<int,QString>::ConstIterator it = presets.constBegin(); it != presets.constEnd(); ++it)
    {
        /* Necessary to avoid mnemonics (issue #730) */
        QString text = it.value();
        text.replace(QLatin1Char('&'), QLatin1String("&&"));
        QAction *a = presetsMenu->addAction(text, mapper, SLOT(map()));
        mapper->setMapping(a, it.key());
    }

    presetsMenu->addSeparator();
    presetsMenu->addAction(tr("Edit presets..."), this, SLOT(ptzPresetWindow()));

    menu->addAction(tr("Edit presets..."), this, SLOT(ptzPresetWindow()));
    menu->addAction(tr("Save preset..."), this, SLOT(ptzPresetSave()));

    menu->addSeparator();
    QAction *a = menu->addAction(tr("Cancel actions"), m_ptz.data(), SLOT(cancelAll()));
    a->setEnabled(m_ptz->hasPendingActions());
    connect(m_ptz.data(), SIGNAL(hasPendingActionsChanged(bool)), a, SLOT(setEnabled(bool)));
    menu->addSeparator();
    menu->addAction(tr("Disable PTZ"), this, SLOT(togglePtzEnabled()));

    return menu;
}

void LiveFeedItem::showPtzMenu(QDeclarativeItem *sourceItem)
{
    if (!m_ptz)
        return;

    QMenu *menu = ptzMenu();

    QPoint pos = QCursor::pos();
    if (sourceItem)
    {
        QGraphicsView *view = sourceItem->scene()->views().value(0);
        Q_ASSERT(view && sourceItem->scene()->views().size() == 1);
        if (view)
            pos = view->mapToGlobal(view->mapFromScene(sourceItem->mapToScene(0, sourceItem->height())));
    }

    menu->exec(pos);
    delete menu;
}

void LiveFeedItem::ptzPresetSave()
{
    if (!m_ptz)
        return;

    QGraphicsView *view = scene()->views().value(0);
    Q_ASSERT(view && scene()->views().size() == 1);

    QString re = QInputDialog::getText(view, tr("Save PTZ preset"), tr("Enter a name for the new PTZ preset:"));
    if (re.isEmpty())
        return;

    m_ptz->savePreset(-1, re);
}

void LiveFeedItem::ptzPresetWindow()
{
    if (!m_ptz)
        return;

    PtzPresetsWindow *window = new PtzPresetsWindow(m_ptz.data(), bcApp->mainWindow);
    window->setAttribute(Qt::WA_DeleteOnClose);

    QGraphicsView *view = scene()->views().value(0);
    Q_ASSERT(view && scene()->views().size() == 1);
    if (view)
    {
        QRect itemScreenRect = view->mapFromScene(mapToScene(QRectF(0, 0, width(), height()))).boundingRect();
        itemScreenRect.moveTopLeft(view->viewport()->mapToGlobal(itemScreenRect.topLeft()));

        window->move(itemScreenRect.right() - qRound(window->width() / 2.0),
                     itemScreenRect.top() + qMax(0, qRound((itemScreenRect.height() - window->height()) / 2.0)));
    }

    window->show();
}
