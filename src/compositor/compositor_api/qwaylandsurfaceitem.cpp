/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Compositor.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Digia Plc and its Subsidiary(-ies) nor the names
**     of its contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwaylandsurfaceitem.h"
#include "qwaylandquicksurface.h"
#include <QtCompositor/qwaylandcompositor.h>
#include <QtCompositor/qwaylandinput.h>

#include <QtGui/QKeyEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QQuickWindow>

#include <QtCore/QMutexLocker>
#include <QtCore/QMutex>

QT_BEGIN_NAMESPACE

QMutex *QWaylandSurfaceItem::mutex = 0;

class TextureProvider : public QSGTextureProvider
{
public:
    TextureProvider()
        : tex(0)
    {
    }

    QSGTexture *texture() const Q_DECL_OVERRIDE
    {
        if (tex)
            tex->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
        return tex;
    }

    QSGTexture *tex;
    bool smooth;
};

QWaylandSurfaceItem::QWaylandSurfaceItem(QQuickItem *parent)
    : QQuickItem(parent)
    , m_surface(0)
    , m_texProvider(0)
    , m_paintEnabled(true)
    , m_mapped(false)
    , m_touchEventsEnabled(false)
    , m_resizeSurfaceToItem(false)
    , m_newTexture(false)
{
    if (!mutex)
        mutex = new QMutex;

        setFlag(ItemHasContents);
}

QWaylandSurfaceItem::QWaylandSurfaceItem(QWaylandQuickSurface *surface, QQuickItem *parent)
    : QQuickItem(parent)
    , m_surface(0)
    , m_texProvider(0)
    , m_paintEnabled(true)
    , m_mapped(false)
    , m_touchEventsEnabled(false)
    , m_resizeSurfaceToItem(false)
{
    init(surface);
}

void QWaylandSurfaceItem::init(QWaylandQuickSurface *surface)
{
    if (m_surface)
        m_surface->destroy();

    if (!surface)
        return;

    m_surface = surface;
    surface->ref();
    m_mapped = true;
    update();

    if (m_resizeSurfaceToItem) {
        updateSurfaceSize();
    } else {
        setWidth(surface->size().width());
        setHeight(surface->size().height());
    }

    updatePosition();

    setSmooth(true);

    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton |
        Qt::ExtraButton1 | Qt::ExtraButton2 | Qt::ExtraButton3 | Qt::ExtraButton4 |
        Qt::ExtraButton5 | Qt::ExtraButton6 | Qt::ExtraButton7 | Qt::ExtraButton8 |
        Qt::ExtraButton9 | Qt::ExtraButton10 | Qt::ExtraButton11 |
        Qt::ExtraButton12 | Qt::ExtraButton13);
    setAcceptHoverEvents(true);
    connect(surface, &QWaylandSurface::mapped, this, &QWaylandSurfaceItem::surfaceMapped);
    connect(surface, &QWaylandSurface::unmapped, this, &QWaylandSurfaceItem::surfaceUnmapped);
    connect(surface, &QWaylandSurface::surfaceDestroyed, this, &QWaylandSurfaceItem::surfaceDestroyed);
    connect(surface, &QWaylandSurface::parentChanged, this, &QWaylandSurfaceItem::parentChanged);
    connect(surface, &QWaylandSurface::sizeChanged, this, &QWaylandSurfaceItem::updateSize);
    connect(surface, &QWaylandSurface::posChanged, this, &QWaylandSurfaceItem::updatePosition);
    connect(surface, &QWaylandSurface::configure, this, &QWaylandSurfaceItem::updateBuffer);
    connect(surface, &QWaylandSurface::redraw, this, &QQuickItem::update);
    connect(this, &QWaylandSurfaceItem::widthChanged, this, &QWaylandSurfaceItem::updateSurfaceSize);
    connect(this, &QWaylandSurfaceItem::heightChanged, this, &QWaylandSurfaceItem::updateSurfaceSize);

    m_yInverted = surface ? surface->isYInverted() : true;
    emit yInvertedChanged();
}

QWaylandSurfaceItem::~QWaylandSurfaceItem()
{
    QMutexLocker locker(mutex);
    m_texProvider->deleteLater();
    if (m_surface)
        m_surface->destroy();
}

void QWaylandSurfaceItem::setSurface(QWaylandQuickSurface *surface)
{
    if (surface == m_surface)
        return;

    init(surface);
    emit surfaceChanged();
}

bool QWaylandSurfaceItem::isYInverted() const
{
    return m_yInverted;
}

QSGTextureProvider *QWaylandSurfaceItem::textureProvider() const
{
    if (!m_texProvider)
        m_texProvider = new TextureProvider();
    return m_texProvider;
}

void QWaylandSurfaceItem::mousePressEvent(QMouseEvent *event)
{
    if (m_surface) {
        QWaylandInputDevice *inputDevice = m_surface->compositor()->defaultInputDevice();
        if (inputDevice->mouseFocus() != m_surface)
            inputDevice->setMouseFocus(m_surface, event->localPos(), event->windowPos());
        inputDevice->sendMousePressEvent(event->button(), event->localPos(), event->windowPos());
    }
}

void QWaylandSurfaceItem::mouseMoveEvent(QMouseEvent *event)
{
    if (m_surface){
        QWaylandInputDevice *inputDevice = m_surface->compositor()->defaultInputDevice();
        inputDevice->sendMouseMoveEvent(m_surface, event->localPos(), event->windowPos());
    }
}

void QWaylandSurfaceItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_surface){
        QWaylandInputDevice *inputDevice = m_surface->compositor()->defaultInputDevice();
        inputDevice->sendMouseReleaseEvent(event->button(), event->localPos(), event->windowPos());
    }
}

void QWaylandSurfaceItem::wheelEvent(QWheelEvent *event)
{
    if (m_surface) {
        QWaylandInputDevice *inputDevice = m_surface->compositor()->defaultInputDevice();
        inputDevice->sendMouseWheelEvent(event->orientation(), event->delta());
    }
}

void QWaylandSurfaceItem::keyPressEvent(QKeyEvent *event)
{
    if (m_surface && hasFocus()) {
        QWaylandInputDevice *inputDevice = m_surface->compositor()->defaultInputDevice();
        inputDevice->sendFullKeyEvent(event);
    }
}

void QWaylandSurfaceItem::keyReleaseEvent(QKeyEvent *event)
{
    if (m_surface && hasFocus()) {
        QWaylandInputDevice *inputDevice = m_surface->compositor()->defaultInputDevice();
        inputDevice->sendFullKeyEvent(event);
    }
}

void QWaylandSurfaceItem::touchEvent(QTouchEvent *event)
{
    if (m_touchEventsEnabled && m_surface) {
        QWaylandInputDevice *inputDevice = m_surface->compositor()->defaultInputDevice();
        event->accept();
        if (inputDevice->mouseFocus() != m_surface) {
            QPoint pointPos;
            QList<QTouchEvent::TouchPoint> points = event->touchPoints();
            if (!points.isEmpty())
                pointPos = points.at(0).pos().toPoint();
            inputDevice->setMouseFocus(m_surface, pointPos, pointPos);
        }
        inputDevice->sendFullTouchEvent(event);
    } else {
        event->ignore();
    }
}

void QWaylandSurfaceItem::takeFocus()
{
    setFocus(true);

    if (m_surface) {
        QWaylandInputDevice *inputDevice = m_surface->compositor()->defaultInputDevice();
        inputDevice->setKeyboardFocus(m_surface);
    }
}

void QWaylandSurfaceItem::surfaceMapped()
{
    m_mapped = true;
    update();
}

void QWaylandSurfaceItem::surfaceUnmapped()
{
    m_mapped = false;
    update();
}

void QWaylandSurfaceItem::parentChanged(QWaylandSurface *newParent, QWaylandSurface *oldParent)
{
    Q_UNUSED(oldParent);

    if (newParent) {
        setPaintEnabled(true);
        setVisible(true);
        setOpacity(1);
        setEnabled(true);
    }
}

void QWaylandSurfaceItem::updateSize()
{
    setSize(m_surface->size());
}

void QWaylandSurfaceItem::updateSurfaceSize()
{
    if (m_resizeSurfaceToItem) {
        m_surface->requestSize(QSize(width(), height()));
    }
}

void QWaylandSurfaceItem::updatePosition()
{
    setPosition(m_surface->pos());
}

/*!
    \qmlproperty bool QtWayland::QWaylandSurfaceItem::paintEnabled

    If this property is true, the \l item is hidden, though the texture
    will still be updated. As opposed to hiding the \l item by
    setting \l{Item::visible}{visible} to false, setting this property to true
    will not prevent mouse or keyboard input from reaching \l item.
*/
bool QWaylandSurfaceItem::paintEnabled() const
{
    return m_paintEnabled;
}

void QWaylandSurfaceItem::setPaintEnabled(bool enabled)
{
    m_paintEnabled = enabled;
    update();
}

void QWaylandSurfaceItem::updateBuffer()
{
    bool inv = m_yInverted;
    m_yInverted = surface()->isYInverted();
    if (inv != m_yInverted)
        emit yInvertedChanged();

    m_newTexture = true;
}

QSGNode *QWaylandSurfaceItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_surface) {
        delete oldNode;
        return 0;
    }

    if (!m_texProvider)
        m_texProvider = new TextureProvider();

    // Order here is important, as the state of visible is that of the pending
    // buffer but will be replaced after we advance the buffer queue.
    bool visible = m_surface->visible();
    if (visible)
        m_texProvider->tex = surface()->texture();
    m_texProvider->smooth = smooth();
    if (m_newTexture)
        emit m_texProvider->textureChanged();
    m_newTexture = false;

    if (!visible || !m_texProvider->tex || !m_paintEnabled || !m_mapped) {
        delete oldNode;
        return 0;
    }

    QSGSimpleTextureNode *node = static_cast<QSGSimpleTextureNode *>(oldNode);

    if (!node)
        node = new QSGSimpleTextureNode();
    node->setTexture(m_texProvider->tex);
    if (surface()->isYInverted()) {
        node->setRect(0, height(), width(), -height());
    } else {
        node->setRect(0, 0, width(), height());
    }

    return node;
}

void QWaylandSurfaceItem::setTouchEventsEnabled(bool enabled)
{
    if (m_touchEventsEnabled != enabled) {
        m_touchEventsEnabled = enabled;
        emit touchEventsEnabledChanged();
    }
}

void QWaylandSurfaceItem::setResizeSurfaceToItem(bool enabled)
{
    if (m_resizeSurfaceToItem != enabled) {
        m_resizeSurfaceToItem = enabled;
        emit resizeSurfaceToItemChanged();
    }
}

QT_END_NAMESPACE
