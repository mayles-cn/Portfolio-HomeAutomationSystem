#ifndef HOME_AUTOMATION_LIGHTING_SYSTEM_H
#define HOME_AUTOMATION_LIGHTING_SYSTEM_H

#include <QVector3D>
#include <QVector4D>

struct DirectionalLight
{
    QVector3D direction = QVector3D(0.3f, -0.7f, 0.5f).normalized();
    QVector3D color = QVector3D(1.0f, 1.0f, 1.0f);
    float ambientIntensity = 0.25f;
    float diffuseIntensity = 0.7f;
    float specularIntensity = 0.3f;
};

struct AreaLight
{
    QVector3D position = QVector3D(0.0f, 6.0f, 0.0f);
    QVector3D color = QVector3D(1.0f, 0.95f, 0.85f);
    float width = 4.0f;
    float height = 4.0f;
    float intensity = 1.5f;
    float falloffRadius = 15.0f;
    bool enabled = false;
};

class LightingSystem
{
public:
    LightingSystem();

    DirectionalLight &globalLight() { return m_globalLight; }
    const DirectionalLight &globalLight() const { return m_globalLight; }

    AreaLight &areaLight() { return m_areaLight; }
    const AreaLight &areaLight() const { return m_areaLight; }

    void setAreaLightEnabled(bool on);
    bool isAreaLightEnabled() const { return m_areaLight.enabled; }
    void toggleAreaLight();

    void setAreaLightPosition(const QVector3D &pos);
    void setAreaLightSize(float w, float h);

private:
    DirectionalLight m_globalLight;
    AreaLight m_areaLight;
};

#endif
