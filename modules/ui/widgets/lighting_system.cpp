#include "lighting_system.h"

LightingSystem::LightingSystem() = default;

void LightingSystem::setAreaLightEnabled(bool on)
{
    m_areaLight.enabled = on;
}

void LightingSystem::toggleAreaLight()
{
    m_areaLight.enabled = !m_areaLight.enabled;
}

void LightingSystem::setAreaLightPosition(const QVector3D &pos)
{
    m_areaLight.position = pos;
}

void LightingSystem::setAreaLightSize(float w, float h)
{
    m_areaLight.width = w;
    m_areaLight.height = h;
}
