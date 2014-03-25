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

#include "qwlsurface_p.h"

#include "qwaylandsurface.h"
#include "qwlcompositor_p.h"
#include "qwlinputdevice_p.h"
#include "qwlextendedsurface_p.h"
#include "qwlregion_p.h"
#include "qwlsubsurface_p.h"
#include "qwlsurfacebuffer_p.h"
#include "qwlshellsurface_p.h"

#include <QtCore/QDebug>
#include <QTouchEvent>

#include <wayland-server.h>

#ifdef QT_WAYLAND_WINDOWMANAGER_SUPPORT
#include "waylandwindowmanagerintegration.h"
#endif

QT_BEGIN_NAMESPACE

namespace QtWayland {

static bool QT_WAYLAND_PRINT_BUFFERING_WARNINGS = qEnvironmentVariableIsSet("QT_WAYLAND_PRINT_BUFFERING_WARNINGS");

class FrameCallback {
public:
    FrameCallback(Surface *surf, wl_resource *res)
        : surface(surf)
        , resource(res)
        , canSend(false)
    {
#if WAYLAND_VERSION_MAJOR < 1 || (WAYLAND_VERSION_MAJOR == 1 && WAYLAND_VERSION_MINOR <= 2)
        res->data = this;
        res->destroy = destroyCallback;
#else
        wl_resource_set_implementation(res, 0, this, destroyCallback);
#endif
    }
    ~FrameCallback()
    {
    }
    void destroy()
    {
        if (resource)
            wl_resource_destroy(resource);
        else
            delete this;
    }
    void send(uint time)
    {
        wl_callback_send_done(resource, time);
        wl_resource_destroy(resource);
    }
    static void destroyCallback(wl_resource *res)
    {
#if WAYLAND_VERSION_MAJOR < 1 || (WAYLAND_VERSION_MAJOR == 1 && WAYLAND_VERSION_MINOR <= 2)
        FrameCallback *_this = static_cast<FrameCallback *>(res->data);
#else
        FrameCallback *_this = static_cast<FrameCallback *>(wl_resource_get_user_data(res));
#endif
        _this->surface->removeFrameCallback(_this);
        delete _this;
    }
    Surface *surface;
    wl_resource *resource;
    bool canSend;
};

Surface::Surface(struct wl_client *client, uint32_t id, QWaylandCompositor *compositor, QWaylandSurface *surface)
    : QtWaylandServer::wl_surface(client, id)
    , m_compositor(compositor->handle())
    , m_waylandSurface(surface)
    , m_buffer(0)
    , m_surfaceMapped(false)
    , m_attacher(0)
    , m_extendedSurface(0)
    , m_subSurface(0)
    , m_shellSurface(0)
    , m_inputPanelSurface(0)
    , m_transientInactive(false)
    , m_isCursorSurface(false)
{
    m_pending.buffer = 0;
    m_pending.newlyAttached = false;
}

Surface::~Surface()
{
    delete m_subSurface;

    m_bufferRef = QWaylandBufferRef();

    for (int i = 0; i < m_bufferPool.size(); i++)
        delete m_bufferPool[i];

    foreach (FrameCallback *c, m_pendingFrameCallbacks)
        c->destroy();
    foreach (FrameCallback *c, m_frameCallbacks)
        c->destroy();
}

void Surface::releaseSurfaces()
{

}

Surface *Surface::fromResource(struct ::wl_resource *resource)
{
    return static_cast<Surface *>(Resource::fromResource(resource)->surface);
}

QWaylandSurface::Type Surface::type() const
{
    if (m_buffer && m_buffer->waylandBufferHandle()) {
        if (m_buffer->isShmBuffer()) {
            return QWaylandSurface::Shm;
        } else {
            return QWaylandSurface::Texture;
        }
    }
    return QWaylandSurface::Invalid;
}

bool Surface::isYInverted() const
{
    if (m_buffer)
        return m_buffer->isYInverted();
    return false;
}

bool Surface::visible() const
{
    return m_buffer ? bool(m_buffer->waylandBufferHandle()) : false;
}

QPointF Surface::pos() const
{
    return m_shellSurface ? m_shellSurface->adjustedPosToTransientParent() : m_position;
}

QPointF Surface::nonAdjustedPos() const
{
    return m_position;
}

void Surface::setPos(const QPointF &pos)
{
    bool emitChange = pos != m_position;
    m_position = pos;
    if (emitChange)
        m_waylandSurface->posChanged();
}

QSize Surface::size() const
{
    return m_size;
}

void Surface::setSize(const QSize &size)
{
    if (size != m_size) {
        m_opaqueRegion = QRegion();
        m_inputRegion = QRegion(QRect(QPoint(), size));
        m_size = size;
        if (m_shellSurface) {
            m_shellSurface->adjustPosInResize();
        }
        m_waylandSurface->sizeChanged();
    }
}

QRegion Surface::inputRegion() const
{
    return m_inputRegion;
}

QRegion Surface::opaqueRegion() const
{
    return m_opaqueRegion;
}

void Surface::sendFrameCallback()
{
    uint time = m_compositor->currentTimeMsecs();
    foreach (FrameCallback *callback, m_frameCallbacks) {
        if (callback->canSend) {
            callback->send(time);
            m_frameCallbacks.removeOne(callback);
        }
    }
}

void Surface::removeFrameCallback(FrameCallback *callback)
{
    m_pendingFrameCallbacks.removeOne(callback);
    m_frameCallbacks.removeOne(callback);
}

QWaylandSurface * Surface::waylandSurface() const
{
    return m_waylandSurface;
}

QPoint Surface::lastMousePos() const
{
    return m_lastLocalMousePos;
}

void Surface::setExtendedSurface(ExtendedSurface *extendedSurface)
{
    m_extendedSurface = extendedSurface;
    if (m_extendedSurface)
        emit m_waylandSurface->extendedSurfaceReady();
}

ExtendedSurface *Surface::extendedSurface() const
{
    return m_extendedSurface;
}

void Surface::setSubSurface(SubSurface *subSurface)
{
    m_subSurface = subSurface;
}

SubSurface *Surface::subSurface() const
{
    return m_subSurface;
}

void Surface::setShellSurface(ShellSurface *shellSurface)
{
    m_shellSurface = shellSurface;
}

ShellSurface *Surface::shellSurface() const
{
    return m_shellSurface;
}

void Surface::setInputPanelSurface(InputPanelSurface *inputPanelSurface)
{
    m_inputPanelSurface = inputPanelSurface;
}

InputPanelSurface *Surface::inputPanelSurface() const
{
    return m_inputPanelSurface;
}

Compositor *Surface::compositor() const
{
    return m_compositor;
}

/*!
 * Sets the backbuffer for this surface. The back buffer is not yet on
 * screen and will become live during the next swapBuffers().
 *
 * The backbuffer represents the current state of the surface for the
 * purpose of GUI-thread accessible properties such as size and visibility.
 */
void Surface::setBackBuffer(SurfaceBuffer *buffer)
{
    m_buffer = buffer;

    if (m_buffer) {
        bool valid = m_buffer->waylandBufferHandle() != 0;
        setSize(valid ? m_buffer->size() : QSize());

        if ((!m_subSurface || !m_subSurface->parent()) && !m_surfaceMapped) {
             m_surfaceMapped = true;
             emit m_waylandSurface->mapped();
        } else if (!valid && m_surfaceMapped) {
             m_surfaceMapped = false;
             emit m_waylandSurface->unmapped();
        }

        m_damage = m_damage.intersected(QRect(QPoint(), m_size));
        emit m_waylandSurface->damaged(m_damage);
    } else {
        InputDevice *inputDevice = m_compositor->defaultInputDevice();
        if (inputDevice->keyboardFocus() == this)
            inputDevice->setKeyboardFocus(0);
        if (inputDevice->mouseFocus() == this)
            inputDevice->setMouseFocus(0, QPointF(), QPointF());
    }
    m_damage = QRegion();
}

SurfaceBuffer *Surface::createSurfaceBuffer(struct ::wl_resource *buffer)
{
    SurfaceBuffer *newBuffer = 0;
    for (int i = 0; i < m_bufferPool.size(); i++) {
        if (!m_bufferPool[i]->isRegisteredWithBuffer()) {
            newBuffer = m_bufferPool[i];
            newBuffer->initialize(buffer);
            break;
        }
    }

    if (!newBuffer) {
        newBuffer = new SurfaceBuffer(this);
        newBuffer->initialize(buffer);
        m_bufferPool.append(newBuffer);
        if (m_bufferPool.size() > 3)
            qWarning() << "Increased buffer pool size to" << m_bufferPool.size() << "for surface with title:" << title() << "className:" << className();
    }

    return newBuffer;
}

void Surface::surface_destroy_resource(Resource *)
{
    m_destroyed = true;
    m_waylandSurface->destroy();
    emit m_waylandSurface->surfaceDestroyed();
}

void Surface::surface_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void Surface::surface_attach(Resource *, struct wl_resource *buffer, int x, int y)
{
    if (m_pending.buffer)
        m_pending.buffer->disown();
    m_pending.buffer = createSurfaceBuffer(buffer);
    m_pending.offset = QPoint(x, y);
    m_pending.newlyAttached = true;
}

void Surface::surface_damage(Resource *, int32_t x, int32_t y, int32_t width, int32_t height)
{
    m_pending.damage = m_pending.damage.united(QRect(x, y, width, height));
}

void Surface::surface_frame(Resource *resource, uint32_t callback)
{
    struct wl_resource *frame_callback = wl_resource_create(resource->client(), &wl_callback_interface, wl_callback_interface.version, callback);
    m_pendingFrameCallbacks << new FrameCallback(this, frame_callback);
}

void Surface::surface_set_opaque_region(Resource *, struct wl_resource *region)
{
    m_opaqueRegion = region ? Region::fromResource(region)->region() : QRegion();
}

void Surface::surface_set_input_region(Resource *, struct wl_resource *region)
{
    m_inputRegion = region ? Region::fromResource(region)->region() : QRegion(QRect(QPoint(), size()));
}

void Surface::surface_commit(Resource *)
{
    m_damage = m_pending.damage;

    if (m_pending.buffer || m_pending.newlyAttached) {
        setBackBuffer(m_pending.buffer);
        if (!m_buffer && m_surfaceMapped) {
            m_surfaceMapped = false;
            emit m_waylandSurface->unmapped();
        }

        m_bufferRef = QWaylandBufferRef(m_buffer);

        if (m_attacher)
            m_attacher->attach(m_bufferRef);
        emit m_waylandSurface->configure();
    }

    m_pending.buffer = 0;
    m_pending.offset = QPoint();
    m_pending.newlyAttached = false;
    m_pending.damage = QRegion();

    if (m_buffer)
        m_buffer->setCommitted();

    m_frameCallbacks << m_pendingFrameCallbacks;
    m_pendingFrameCallbacks.clear();

    emit m_waylandSurface->redraw();
}

void Surface::frameStarted()
{
    foreach (FrameCallback *c, m_frameCallbacks)
        c->canSend = true;
}

void Surface::setClassName(const QString &className)
{
    if (m_className != className) {
        m_className = className;
        emit waylandSurface()->classNameChanged();
    }
}

void Surface::setTitle(const QString &title)
{
    if (m_title != title) {
        m_title = title;
        emit waylandSurface()->titleChanged();
    }
}

} // namespace Wayland

QT_END_NAMESPACE
