#ifndef HOME_AUTOMATION_CAMERA_SYSTEM_H
#define HOME_AUTOMATION_CAMERA_SYSTEM_H

#include <QJsonArray>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>
#include <vector>

struct CameraSnapshot
{
    QString name;
    QVector3D position;
    QVector3D target;
    QVector3D up = QVector3D(0.0f, 1.0f, 0.0f);
    float fov = 45.0f;
};

class CameraSystem
{
public:
    CameraSystem();

    void setPosition(const QVector3D &pos);
    void setTarget(const QVector3D &target);
    void setUp(const QVector3D &up);
    void setFov(float fov);
    void setAspectRatio(float aspect);

    QVector3D position() const { return m_position; }
    QVector3D target() const { return m_target; }
    QVector3D up() const { return m_up; }
    float fov() const { return m_fov; }

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix() const;
    QMatrix3x3 normalMatrix(const QMatrix4x4 &modelMatrix) const;

    void orbit(float deltaYaw, float deltaPitch);
    void zoom(float delta);
    void pan(float deltaX, float deltaY);

    int saveSnapshot(const QString &name);
    bool loadSnapshot(int index);
    bool loadSnapshot(const QString &name);
    bool removeSnapshot(int index);
    int snapshotCount() const { return static_cast<int>(m_snapshots.size()); }
    const CameraSnapshot &snapshot(int index) const { return m_snapshots.at(static_cast<size_t>(index)); }
    const std::vector<CameraSnapshot> &snapshots() const { return m_snapshots; }

    QJsonArray snapshotsToJson() const;
    void snapshotsFromJson(const QJsonArray &array);

    void setDefaultView();
    CameraSnapshot currentSnapshot(const QString &name) const;

    void lerp(const CameraSnapshot &a, const CameraSnapshot &b, float t);

private:
    QVector3D m_position;
    QVector3D m_target;
    QVector3D m_up;
    float m_fov = 45.0f;
    float m_aspectRatio = 16.0f / 9.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;

    std::vector<CameraSnapshot> m_snapshots;
};

#endif
