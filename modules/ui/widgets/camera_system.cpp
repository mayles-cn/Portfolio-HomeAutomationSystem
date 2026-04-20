#include "camera_system.h"

#include <QtMath>
#include <QJsonObject>

CameraSystem::CameraSystem()
{
    setDefaultView();
}

void CameraSystem::setPosition(const QVector3D &pos) { m_position = pos; }
void CameraSystem::setTarget(const QVector3D &target) { m_target = target; }
void CameraSystem::setUp(const QVector3D &up) { m_up = up; }
void CameraSystem::setFov(float fov) { m_fov = qBound(5.0f, fov, 120.0f); }
void CameraSystem::setAspectRatio(float aspect) { m_aspectRatio = aspect; }

QMatrix4x4 CameraSystem::viewMatrix() const
{
    QMatrix4x4 view;
    view.lookAt(m_position, m_target, m_up);
    return view;
}

QMatrix4x4 CameraSystem::projectionMatrix() const
{
    QMatrix4x4 proj;
    proj.perspective(m_fov, m_aspectRatio, m_nearPlane, m_farPlane);
    return proj;
}

QMatrix3x3 CameraSystem::normalMatrix(const QMatrix4x4 &modelMatrix) const
{
    return (viewMatrix() * modelMatrix).normalMatrix();
}

void CameraSystem::orbit(float deltaYaw, float deltaPitch)
{
    QVector3D offset = m_position - m_target;
    float radius = offset.length();
    if (radius < 0.001f)
        return;

    float theta = qAtan2(offset.x(), offset.z());
    float phi = qAsin(qBound(-1.0f, offset.y() / radius, 1.0f));

    theta += qDegreesToRadians(deltaYaw);
    phi += qDegreesToRadians(deltaPitch);
    phi = qBound(-1.5f, phi, 1.5f);

    m_position.setX(m_target.x() + radius * qCos(phi) * qSin(theta));
    m_position.setY(m_target.y() + radius * qSin(phi));
    m_position.setZ(m_target.z() + radius * qCos(phi) * qCos(theta));
}

void CameraSystem::zoom(float delta)
{
    QVector3D direction = (m_target - m_position).normalized();
    float distance = (m_target - m_position).length();
    float newDistance = qMax(0.5f, distance - delta);
    m_position = m_target - direction * newDistance;
}

void CameraSystem::pan(float deltaX, float deltaY)
{
    QVector3D forward = (m_target - m_position).normalized();
    QVector3D right = QVector3D::crossProduct(forward, m_up).normalized();
    QVector3D actualUp = QVector3D::crossProduct(right, forward).normalized();

    QVector3D offset = right * deltaX + actualUp * deltaY;
    m_position += offset;
    m_target += offset;
}

int CameraSystem::saveSnapshot(const QString &name)
{
    CameraSnapshot snap;
    snap.name = name;
    snap.position = m_position;
    snap.target = m_target;
    snap.up = m_up;
    snap.fov = m_fov;
    m_snapshots.push_back(snap);
    return static_cast<int>(m_snapshots.size()) - 1;
}

bool CameraSystem::loadSnapshot(int index)
{
    if (index < 0 || index >= static_cast<int>(m_snapshots.size()))
        return false;
    const CameraSnapshot &snap = m_snapshots[static_cast<size_t>(index)];
    m_position = snap.position;
    m_target = snap.target;
    m_up = snap.up;
    m_fov = snap.fov;
    return true;
}

bool CameraSystem::loadSnapshot(const QString &name)
{
    for (size_t i = 0; i < m_snapshots.size(); ++i)
    {
        if (m_snapshots[i].name == name)
            return loadSnapshot(static_cast<int>(i));
    }
    return false;
}

bool CameraSystem::removeSnapshot(int index)
{
    if (index < 0 || index >= static_cast<int>(m_snapshots.size()))
        return false;
    m_snapshots.erase(m_snapshots.begin() + index);
    return true;
}

QJsonArray CameraSystem::snapshotsToJson() const
{
    QJsonArray array;
    for (const auto &snap : m_snapshots)
    {
        QJsonObject obj;
        obj["name"] = snap.name;
        obj["px"] = static_cast<double>(snap.position.x());
        obj["py"] = static_cast<double>(snap.position.y());
        obj["pz"] = static_cast<double>(snap.position.z());
        obj["tx"] = static_cast<double>(snap.target.x());
        obj["ty"] = static_cast<double>(snap.target.y());
        obj["tz"] = static_cast<double>(snap.target.z());
        obj["ux"] = static_cast<double>(snap.up.x());
        obj["uy"] = static_cast<double>(snap.up.y());
        obj["uz"] = static_cast<double>(snap.up.z());
        obj["fov"] = static_cast<double>(snap.fov);
        array.append(obj);
    }
    return array;
}

void CameraSystem::snapshotsFromJson(const QJsonArray &array)
{
    m_snapshots.clear();
    for (const auto &val : array)
    {
        QJsonObject obj = val.toObject();
        CameraSnapshot snap;
        snap.name = obj["name"].toString();
        snap.position = QVector3D(
            static_cast<float>(obj["px"].toDouble()),
            static_cast<float>(obj["py"].toDouble()),
            static_cast<float>(obj["pz"].toDouble()));
        snap.target = QVector3D(
            static_cast<float>(obj["tx"].toDouble()),
            static_cast<float>(obj["ty"].toDouble()),
            static_cast<float>(obj["tz"].toDouble()));
        snap.up = QVector3D(
            static_cast<float>(obj["ux"].toDouble()),
            static_cast<float>(obj["uy"].toDouble()),
            static_cast<float>(obj["uz"].toDouble()));
        snap.fov = static_cast<float>(obj["fov"].toDouble(45.0));
        m_snapshots.push_back(snap);
    }
}

void CameraSystem::setDefaultView()
{
    m_position = QVector3D(0.0f, 8.0f, 15.0f);
    m_target = QVector3D(0.0f, 0.0f, 0.0f);
    m_up = QVector3D(0.0f, 1.0f, 0.0f);
    m_fov = 45.0f;
}

CameraSnapshot CameraSystem::currentSnapshot(const QString &name) const
{
    CameraSnapshot snap;
    snap.name = name;
    snap.position = m_position;
    snap.target = m_target;
    snap.up = m_up;
    snap.fov = m_fov;
    return snap;
}

void CameraSystem::lerp(const CameraSnapshot &a, const CameraSnapshot &b, float t)
{
    t = qBound(0.0f, t, 1.0f);
    m_position = a.position * (1.0f - t) + b.position * t;
    m_target = a.target * (1.0f - t) + b.target * t;
    m_up = (a.up * (1.0f - t) + b.up * t).normalized();
    m_fov = a.fov * (1.0f - t) + b.fov * t;
}
