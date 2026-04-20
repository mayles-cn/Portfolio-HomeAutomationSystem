#include "model_opengl_widget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QtMath>
#include <QTextStream>
#include <QVector2D>
#include <QVector>
#include <QWheelEvent>

namespace
{
constexpr int kModelStrideFloats = 6;
constexpr int kModelStrideBytes = kModelStrideFloats * static_cast<int>(sizeof(float));
constexpr int kLightQuadVertexCount = 6;
constexpr int kGridHalfLineCount = 1200;
constexpr float kOrbitSpeed = 0.22f;
constexpr float kPanScale = 0.0025f;
constexpr float kMinModelRadius = 1e-3f;

QVector3D safeNormal(const QVector3D &normal)
{
    if (normal.lengthSquared() <= 1e-8f)
    {
        return QVector3D(0.0f, 1.0f, 0.0f);
    }
    return normal.normalized();
}

QString resolveExistingModelPath(const QString &requestedPath)
{
    if (!requestedPath.trimmed().isEmpty())
    {
        QFileInfo requestedInfo(requestedPath.trimmed());
        const QString normalizedRequested = QDir::cleanPath(requestedInfo.isAbsolute()
                                                                 ? requestedInfo.absoluteFilePath()
                                                                 : QDir::current().filePath(requestedInfo.filePath()));
        if (QFileInfo::exists(normalizedRequested) && QFileInfo(normalizedRequested).isFile())
        {
            return normalizedRequested;
        }
    }

    const QString appDirectory = QCoreApplication::applicationDirPath();
    const QStringList fallbackCandidates = {
        QDir(appDirectory).filePath(QStringLiteral("models/home.obj")),
        QDir(appDirectory).filePath(QStringLiteral("../../../resources/models/home.obj")),
        QDir::current().filePath(QStringLiteral("resources/models/home.obj")),
    };

    for (const QString &candidate : fallbackCandidates)
    {
        const QString normalized = QDir::cleanPath(candidate);
        if (QFileInfo::exists(normalized) && QFileInfo(normalized).isFile())
        {
            return normalized;
        }
    }

    return QString();
}

struct FaceVertexRef
{
    int positionIndex = -1;
    int normalIndex = -1;
};

int resolveObjIndex(const int rawIndex, const int count)
{
    if (rawIndex > 0)
    {
        const int index = rawIndex - 1;
        return (index >= 0 && index < count) ? index : -1;
    }
    if (rawIndex < 0)
    {
        const int index = count + rawIndex;
        return (index >= 0 && index < count) ? index : -1;
    }
    return -1;
}

bool parseInt(const QString &text, int *value)
{
    bool ok = false;
    const int parsed = text.toInt(&ok);
    if (!ok)
    {
        return false;
    }
    if (value)
    {
        *value = parsed;
    }
    return true;
}

bool parseFaceVertexRef(const QString &token, const int positionCount, const int normalCount, FaceVertexRef *outRef)
{
    if (!outRef)
    {
        return false;
    }

    const QStringList pieces = token.split('/');
    if (pieces.isEmpty() || pieces.at(0).isEmpty())
    {
        return false;
    }

    int rawPositionIndex = 0;
    if (!parseInt(pieces.at(0), &rawPositionIndex))
    {
        return false;
    }

    const int resolvedPosition = resolveObjIndex(rawPositionIndex, positionCount);
    if (resolvedPosition < 0)
    {
        return false;
    }

    int resolvedNormal = -1;
    if (pieces.size() >= 3 && !pieces.at(2).isEmpty())
    {
        int rawNormalIndex = 0;
        if (parseInt(pieces.at(2), &rawNormalIndex))
        {
            resolvedNormal = resolveObjIndex(rawNormalIndex, normalCount);
        }
    }

    outRef->positionIndex = resolvedPosition;
    outRef->normalIndex = resolvedNormal;
    return true;
}

float cross2D(const QVector2D &a, const QVector2D &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

int dominantNormalAxis(const QVector3D &normal)
{
    const QVector3D absN(std::abs(normal.x()), std::abs(normal.y()), std::abs(normal.z()));
    if (absN.x() >= absN.y() && absN.x() >= absN.z())
    {
        return 0; // drop X, project YZ
    }
    if (absN.y() >= absN.z())
    {
        return 1; // drop Y, project XZ
    }
    return 2; // drop Z, project XY
}

QVector2D projectTo2D(const QVector3D &p, const int dropAxis)
{
    if (dropAxis == 0)
    {
        return QVector2D(p.y(), p.z());
    }
    if (dropAxis == 1)
    {
        return QVector2D(p.x(), p.z());
    }
    return QVector2D(p.x(), p.y());
}

QVector3D computeFaceNormalNewell(const QVector<FaceVertexRef> &faceRefs, const QVector<QVector3D> &positions)
{
    QVector3D normal(0.0f, 0.0f, 0.0f);
    if (faceRefs.size() < 3)
    {
        return normal;
    }

    for (int i = 0; i < faceRefs.size(); ++i)
    {
        const QVector3D &current = positions.at(faceRefs.at(i).positionIndex);
        const QVector3D &next = positions.at(faceRefs.at((i + 1) % faceRefs.size()).positionIndex);
        normal.setX(normal.x() + (current.y() - next.y()) * (current.z() + next.z()));
        normal.setY(normal.y() + (current.z() - next.z()) * (current.x() + next.x()));
        normal.setZ(normal.z() + (current.x() - next.x()) * (current.y() + next.y()));
    }
    return normal;
}

float signedArea2D(const QVector<QVector2D> &projectedPoints, const QVector<int> &polygon)
{
    float area2 = 0.0f;
    for (int i = 0; i < polygon.size(); ++i)
    {
        const QVector2D &a = projectedPoints.at(polygon.at(i));
        const QVector2D &b = projectedPoints.at(polygon.at((i + 1) % polygon.size()));
        area2 += cross2D(a, b);
    }
    return 0.5f * area2;
}

bool pointInTriangle2D(const QVector2D &p, const QVector2D &a, const QVector2D &b, const QVector2D &c)
{
    const float c1 = cross2D(b - a, p - a);
    const float c2 = cross2D(c - b, p - b);
    const float c3 = cross2D(a - c, p - c);

    const bool hasNeg = (c1 < 0.0f) || (c2 < 0.0f) || (c3 < 0.0f);
    const bool hasPos = (c1 > 0.0f) || (c2 > 0.0f) || (c3 > 0.0f);
    return !(hasNeg && hasPos);
}

QVector<std::array<int, 3>> triangulateFaceIndices(const QVector<FaceVertexRef> &faceRefs, const QVector<QVector3D> &positions)
{
    QVector<std::array<int, 3>> triangles;
    if (faceRefs.size() < 3)
    {
        return triangles;
    }

    QVector<int> polygon;
    polygon.reserve(faceRefs.size());
    for (int i = 0; i < faceRefs.size(); ++i)
    {
        if (!polygon.isEmpty())
        {
            const int prev = polygon.back();
            if (faceRefs.at(prev).positionIndex == faceRefs.at(i).positionIndex)
            {
                continue;
            }
        }
        polygon.push_back(i);
    }
    if (polygon.size() >= 2 &&
        faceRefs.at(polygon.front()).positionIndex == faceRefs.at(polygon.back()).positionIndex)
    {
        polygon.pop_back();
    }
    if (polygon.size() < 3)
    {
        return triangles;
    }

    const QVector3D rawNormal = computeFaceNormalNewell(faceRefs, positions);
    if (rawNormal.lengthSquared() <= 1e-10f)
    {
        for (int i = 1; i < polygon.size() - 1; ++i)
        {
            triangles.push_back({polygon.at(0), polygon.at(i), polygon.at(i + 1)});
        }
        return triangles;
    }

    const int dropAxis = dominantNormalAxis(rawNormal);
    QVector<QVector2D> projectedPoints;
    projectedPoints.resize(faceRefs.size());
    for (int i = 0; i < faceRefs.size(); ++i)
    {
        projectedPoints[i] = projectTo2D(positions.at(faceRefs.at(i).positionIndex), dropAxis);
    }

    const float area = signedArea2D(projectedPoints, polygon);
    if (std::abs(area) < 1e-8f)
    {
        for (int i = 1; i < polygon.size() - 1; ++i)
        {
            triangles.push_back({polygon.at(0), polygon.at(i), polygon.at(i + 1)});
        }
        return triangles;
    }

    const bool ccw = area > 0.0f;
    int guard = 0;
    const int maxGuard = polygon.size() * polygon.size() * 2;

    while (polygon.size() > 3 && guard < maxGuard)
    {
        ++guard;
        bool earFound = false;
        for (int i = 0; i < polygon.size(); ++i)
        {
            const int prev = polygon.at((i + polygon.size() - 1) % polygon.size());
            const int curr = polygon.at(i);
            const int next = polygon.at((i + 1) % polygon.size());

            const QVector2D &a = projectedPoints.at(prev);
            const QVector2D &b = projectedPoints.at(curr);
            const QVector2D &c = projectedPoints.at(next);

            const float orient = cross2D(b - a, c - b);
            if (ccw)
            {
                if (orient <= 1e-8f)
                {
                    continue;
                }
            }
            else
            {
                if (orient >= -1e-8f)
                {
                    continue;
                }
            }

            bool hasPointInside = false;
            for (int j = 0; j < polygon.size(); ++j)
            {
                const int candidate = polygon.at(j);
                if (candidate == prev || candidate == curr || candidate == next)
                {
                    continue;
                }
                if (pointInTriangle2D(projectedPoints.at(candidate), a, b, c))
                {
                    hasPointInside = true;
                    break;
                }
            }
            if (hasPointInside)
            {
                continue;
            }

            triangles.push_back({prev, curr, next});
            polygon.removeAt(i);
            earFound = true;
            break;
        }

        if (!earFound)
        {
            break;
        }
    }

    if (polygon.size() == 3)
    {
        triangles.push_back({polygon.at(0), polygon.at(1), polygon.at(2)});
    }
    else if (triangles.isEmpty())
    {
        for (int i = 1; i < polygon.size() - 1; ++i)
        {
            triangles.push_back({polygon.at(0), polygon.at(i), polygon.at(i + 1)});
        }
    }

    return triangles;
}

bool loadObjSafeInterleaved(
    const QString &objPath,
    QVector<float> *interleavedVertices,
    QVector3D *boundsMin,
    QVector3D *boundsMax,
    QString *error)
{
    if (!interleavedVertices || !boundsMin || !boundsMax)
    {
        if (error)
        {
            *error = QStringLiteral("invalid output buffers for OBJ parser");
        }
        return false;
    }

    QFile file(objPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
        {
            *error = QStringLiteral("cannot open OBJ file: %1").arg(objPath);
        }
        return false;
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    interleavedVertices->clear();
    *boundsMin = QVector3D(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    *boundsMax = QVector3D(
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest());

    QTextStream reader(&file);
    int lineNumber = 0;
    while (!reader.atEnd())
    {
        const QString rawLine = reader.readLine();
        ++lineNumber;
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }

        if (line.startsWith(QStringLiteral("v ")))
        {
            const QStringList values = line.mid(2).simplified().split(' ', Qt::SkipEmptyParts);
            if (values.size() < 3)
            {
                continue;
            }
            bool okX = false;
            bool okY = false;
            bool okZ = false;
            const float x = values.at(0).toFloat(&okX);
            const float y = values.at(1).toFloat(&okY);
            const float z = values.at(2).toFloat(&okZ);
            if (okX && okY && okZ)
            {
                positions.push_back(QVector3D(x, y, z));
            }
            continue;
        }

        if (line.startsWith(QStringLiteral("vn ")))
        {
            const QStringList values = line.mid(3).simplified().split(' ', Qt::SkipEmptyParts);
            if (values.size() < 3)
            {
                continue;
            }
            bool okX = false;
            bool okY = false;
            bool okZ = false;
            const float x = values.at(0).toFloat(&okX);
            const float y = values.at(1).toFloat(&okY);
            const float z = values.at(2).toFloat(&okZ);
            if (okX && okY && okZ)
            {
                normals.push_back(QVector3D(x, y, z));
            }
            continue;
        }

        if (!line.startsWith(QStringLiteral("f ")))
        {
            continue;
        }

        QString faceBody = line.mid(2).trimmed();
        const int commentIndex = faceBody.indexOf('#');
        if (commentIndex >= 0)
        {
            faceBody = faceBody.left(commentIndex).trimmed();
        }
        if (faceBody.isEmpty())
        {
            continue;
        }

        const QStringList tokenList = faceBody.simplified().split(' ', Qt::SkipEmptyParts);
        if (tokenList.size() < 3)
        {
            continue;
        }

        QVector<FaceVertexRef> faceRefs;
        faceRefs.reserve(tokenList.size());
        for (const QString &token : tokenList)
        {
            FaceVertexRef ref;
            if (parseFaceVertexRef(token, positions.size(), normals.size(), &ref))
            {
                faceRefs.push_back(ref);
            }
        }
        if (faceRefs.size() < 3)
        {
            continue;
        }

        const QVector3D p0 = positions.at(faceRefs.at(0).positionIndex);
        const QVector3D p1 = positions.at(faceRefs.at(1).positionIndex);
        const QVector3D p2 = positions.at(faceRefs.at(2).positionIndex);
        QVector3D faceNormal = QVector3D::crossProduct(p1 - p0, p2 - p0);
        if (faceNormal.lengthSquared() <= 1e-8f)
        {
            faceNormal = QVector3D(0.0f, 1.0f, 0.0f);
        }
        else
        {
            faceNormal.normalize();
        }

        const auto appendVertex = [&](const FaceVertexRef &ref) {
            const QVector3D position = positions.at(ref.positionIndex);
            QVector3D normal = faceNormal;
            if (ref.normalIndex >= 0 && ref.normalIndex < normals.size())
            {
                normal = safeNormal(normals.at(ref.normalIndex));
            }

            boundsMin->setX(std::min(boundsMin->x(), position.x()));
            boundsMin->setY(std::min(boundsMin->y(), position.y()));
            boundsMin->setZ(std::min(boundsMin->z(), position.z()));
            boundsMax->setX(std::max(boundsMax->x(), position.x()));
            boundsMax->setY(std::max(boundsMax->y(), position.y()));
            boundsMax->setZ(std::max(boundsMax->z(), position.z()));

            interleavedVertices->push_back(position.x());
            interleavedVertices->push_back(position.y());
            interleavedVertices->push_back(position.z());
            interleavedVertices->push_back(normal.x());
            interleavedVertices->push_back(normal.y());
            interleavedVertices->push_back(normal.z());
        };

        const QVector<std::array<int, 3>> triangles = triangulateFaceIndices(faceRefs, positions);
        for (const std::array<int, 3> &tri : triangles)
        {
            appendVertex(faceRefs.at(tri[0]));
            appendVertex(faceRefs.at(tri[1]));
            appendVertex(faceRefs.at(tri[2]));
        }
    }

    if (interleavedVertices->isEmpty())
    {
        if (error)
        {
            *error = QStringLiteral("no triangles parsed from OBJ: %1").arg(objPath);
        }
        return false;
    }

    return true;
}
} // namespace

ModelOpenGLWidget::ModelOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent),
      m_vertexBuffer(QOpenGLBuffer::VertexBuffer),
      m_lightQuadVBO(QOpenGLBuffer::VertexBuffer),
      m_gridVBO(QOpenGLBuffer::VertexBuffer)
{
    setObjectName(QStringLiteral("ModelViewport"));
    setMinimumSize(640, 420);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_animationTimer.setInterval(16);
    connect(&m_animationTimer, &QTimer::timeout, this, &ModelOpenGLWidget::tickAnimation);

    applySunToDirectionalLight();
    m_frameClock.start();
}

ModelOpenGLWidget::~ModelOpenGLWidget()
{
    m_animationTimer.stop();

    if (!context())
    {
        return;
    }

    makeCurrent();
    if (m_lightQuadVAO.isCreated())
    {
        m_lightQuadVAO.destroy();
    }
    if (m_lightQuadVBO.isCreated())
    {
        m_lightQuadVBO.destroy();
    }
    if (m_gridVAO.isCreated())
    {
        m_gridVAO.destroy();
    }
    if (m_gridVBO.isCreated())
    {
        m_gridVBO.destroy();
    }
    if (m_vertexArray.isCreated())
    {
        m_vertexArray.destroy();
    }
    if (m_vertexBuffer.isCreated())
    {
        m_vertexBuffer.destroy();
    }
    m_lightQuadShader.removeAllShaders();
    m_gridShader.removeAllShaders();
    m_shaderProgram.removeAllShaders();
    doneCurrent();
}

bool ModelOpenGLWidget::loadModel(const QString &objPath)
{
    const QString resolvedPath = resolveExistingModelPath(objPath);
    if (resolvedPath.isEmpty())
    {
        qWarning() << "ModelOpenGLWidget: unable to resolve model path from" << objPath;
        return false;
    }

    setProperty("pendingModelPath", resolvedPath);

    if (!context())
    {
        return true;
    }

    makeCurrent();
    const bool loaded = uploadModelData(resolvedPath);
    doneCurrent();

    if (loaded)
    {
        emit modelLoaded(m_vertexCount);
        update();
    }
    return loaded;
}

void ModelOpenGLWidget::saveCameraSnapshot(const QString &name)
{
    QString snapshotName = name.trimmed();
    if (snapshotName.isEmpty())
    {
        snapshotName = QStringLiteral("机位-%1").arg(m_camera.snapshotCount() + 1);
    }

    const int index = m_camera.saveSnapshot(snapshotName);
    emit cameraSnapshotSaved(snapshotName, index);
}

bool ModelOpenGLWidget::loadCameraSnapshot(int index)
{
    m_cameraTransitionActive = false;
    m_animationTimer.stop();
    const bool ok = m_camera.loadSnapshot(index);
    if (ok)
    {
        update();
    }
    return ok;
}

bool ModelOpenGLWidget::loadCameraSnapshot(const QString &name)
{
    m_cameraTransitionActive = false;
    m_animationTimer.stop();
    const bool ok = m_camera.loadSnapshot(name);
    if (ok)
    {
        update();
    }
    return ok;
}

void ModelOpenGLWidget::setAreaLightEnabled(bool on)
{
    m_lighting.setAreaLightEnabled(on);
    update();
}

void ModelOpenGLWidget::toggleAreaLight()
{
    m_lighting.toggleAreaLight();
    update();
}

void ModelOpenGLWidget::setSunHeight(const float heightDeg)
{
    m_sunHeightDeg = std::clamp(heightDeg, -5.0f, 85.0f);
    applySunToDirectionalLight();
    update();
}

void ModelOpenGLWidget::setSunAngle(const float angleDeg)
{
    float normalized = std::fmod(angleDeg, 360.0f);
    if (normalized < 0.0f)
    {
        normalized += 360.0f;
    }
    m_sunAngleDeg = normalized;
    applySunToDirectionalLight();
    update();
}

void ModelOpenGLWidget::setSunBrightness(const float brightness)
{
    m_sunBrightness = std::clamp(brightness, 0.0f, 3.0f);
    applySunToDirectionalLight();
    update();
}

void ModelOpenGLWidget::setModelGrayLevel(const float grayLevel)
{
    m_modelGrayLevel = std::clamp(grayLevel, 0.1f, 1.0f);
    update();
}

void ModelOpenGLWidget::setModelOpacity(const float opacity)
{
    m_modelOpacity = std::clamp(opacity, 0.1f, 1.0f);
    update();
}

void ModelOpenGLWidget::setGridSize(const float size)
{
    m_gridSize = std::clamp(size, 1.0f, 80.0f);
    updateGridGeometry();
    update();
}

void ModelOpenGLWidget::setGroundHeight(const float height)
{
    m_groundHeight = std::clamp(height, -3.0f, 3.0f);
    updateGridGeometry();
    update();
}

void ModelOpenGLWidget::applySunToDirectionalLight()
{
    DirectionalLight &sun = m_lighting.globalLight();

    const float heightRad = qDegreesToRadians(m_sunHeightDeg);
    const float angleRad = qDegreesToRadians(m_sunAngleDeg);
    const QVector3D sunDirection(
        std::cos(heightRad) * std::cos(angleRad),
        std::sin(heightRad),
        std::cos(heightRad) * std::sin(angleRad));

    sun.direction = -safeNormal(sunDirection);
    sun.ambientIntensity = 0.12f + 0.18f * m_sunBrightness;
    sun.diffuseIntensity = 0.35f + 0.90f * m_sunBrightness;
    sun.specularIntensity = 0.15f + 0.55f * m_sunBrightness;
}

void ModelOpenGLWidget::updateGridGeometry()
{
    if (!context() || !m_gridVBO.isCreated() || !m_gridVAO.isCreated() || !m_gridShader.isLinked())
    {
        return;
    }

    const float spacing = std::max(1.0f, m_gridSize);
    const float halfExtent = spacing * static_cast<float>(kGridHalfLineCount);

    QVector<float> vertices;
    vertices.reserve((kGridHalfLineCount * 2 + 1) * 4 * 3);

    for (int i = -kGridHalfLineCount; i <= kGridHalfLineCount; ++i)
    {
        const float p = static_cast<float>(i) * spacing;

        // Lines parallel to X axis.
        vertices << -halfExtent << 0.0f << p;
        vertices << halfExtent << 0.0f << p;

        // Lines parallel to Z axis.
        vertices << p << 0.0f << -halfExtent;
        vertices << p << 0.0f << halfExtent;
    }

    m_gridVertexCount = vertices.size() / 3;

    m_gridVAO.bind();
    m_gridVBO.bind();
    m_gridVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_gridVBO.allocate(vertices.constData(), vertices.size() * static_cast<int>(sizeof(float)));

    m_gridShader.bind();
    const int gridAttr = m_gridShader.attributeLocation("a_position");
    if (gridAttr >= 0)
    {
        m_gridShader.enableAttributeArray(gridAttr);
        m_gridShader.setAttributeBuffer(gridAttr, GL_FLOAT, 0, 3, 3 * static_cast<int>(sizeof(float)));
    }
    m_gridShader.release();

    m_gridVBO.release();
    m_gridVAO.release();
}

void ModelOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_shadersReady = buildShaders();
    if (!m_shadersReady)
    {
        return;
    }

    if (!m_lightQuadVAO.create() || !m_lightQuadVBO.create())
    {
        qWarning() << "ModelOpenGLWidget: failed to create area-light buffers";
        return;
    }

    static const std::array<float, kLightQuadVertexCount * 3> quadVertices = {
        -0.5f, 0.0f, -0.5f,
         0.5f, 0.0f, -0.5f,
         0.5f, 0.0f,  0.5f,
        -0.5f, 0.0f, -0.5f,
         0.5f, 0.0f,  0.5f,
        -0.5f, 0.0f,  0.5f,
    };

    m_lightQuadVAO.bind();
    m_lightQuadVBO.bind();
    m_lightQuadVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_lightQuadVBO.allocate(quadVertices.data(), static_cast<int>(quadVertices.size() * sizeof(float)));

    m_lightQuadShader.bind();
    const int lightAttr = m_lightQuadShader.attributeLocation("a_position");
    if (lightAttr >= 0)
    {
        m_lightQuadShader.enableAttributeArray(lightAttr);
        m_lightQuadShader.setAttributeBuffer(lightAttr, GL_FLOAT, 0, 3, 3 * static_cast<int>(sizeof(float)));
    }
    m_lightQuadShader.release();

    m_lightQuadVBO.release();
    m_lightQuadVAO.release();

    if (!m_gridVAO.create() || !m_gridVBO.create())
    {
        qWarning() << "ModelOpenGLWidget: failed to create grid buffers";
        return;
    }
    updateGridGeometry();

    m_camera.setAspectRatio(height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f);

    const QString initialModelPath = property("pendingModelPath").toString();
    if (!initialModelPath.isEmpty())
    {
        if (!uploadModelData(initialModelPath))
        {
            qWarning() << "ModelOpenGLWidget: failed to upload pending model" << initialModelPath;
        }
    }
    else
    {
        const QString defaultPath = resolveExistingModelPath(QString());
        if (!defaultPath.isEmpty() && !uploadModelData(defaultPath))
        {
            qWarning() << "ModelOpenGLWidget: failed to upload default model" << defaultPath;
        }
    }

    if (m_isReady)
    {
        emit modelLoaded(m_vertexCount);
    }

    m_frameClock.restart();
}

void ModelOpenGLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_shadersReady)
    {
        return;
    }

    const QMatrix4x4 viewMatrix = m_camera.viewMatrix();
    const QMatrix4x4 projectionMatrix = m_camera.projectionMatrix();

    if (m_gridVertexCount > 0 && m_gridVAO.isCreated())
    {
        const QVector3D cameraPos = m_camera.position();
        const float spacing = std::max(1.0f, m_gridSize);
        const float anchorX = std::floor(cameraPos.x() / spacing) * spacing;
        const float anchorZ = std::floor(cameraPos.z() / spacing) * spacing;
        QMatrix4x4 gridModel;
        gridModel.setToIdentity();
        gridModel.translate(anchorX, m_groundHeight, anchorZ);

        glDisable(GL_CULL_FACE);
        m_gridShader.bind();
        m_gridShader.setUniformValue("u_model", gridModel);
        m_gridShader.setUniformValue("u_view", viewMatrix);
        m_gridShader.setUniformValue("u_projection", projectionMatrix);
        m_gridShader.setUniformValue("u_color", QVector3D(0.82f, 0.84f, 0.88f));
        m_gridShader.setUniformValue("u_alpha", 1.0f);
        m_gridVAO.bind();
        glDrawArrays(GL_LINES, 0, m_gridVertexCount);
        m_gridVAO.release();
        m_gridShader.release();
        glEnable(GL_CULL_FACE);
    }

    if (!m_isReady || m_vertexCount <= 0)
    {
        return;
    }

    QMatrix4x4 modelMatrix;
    modelMatrix.setToIdentity();
    const QMatrix3x3 normalMatrix = modelMatrix.normalMatrix();

    const DirectionalLight &globalLight = m_lighting.globalLight();
    const AreaLight &areaLight = m_lighting.areaLight();

    m_shaderProgram.bind();
    m_shaderProgram.setUniformValue("u_model", modelMatrix);
    m_shaderProgram.setUniformValue("u_view", viewMatrix);
    m_shaderProgram.setUniformValue("u_projection", projectionMatrix);
    m_shaderProgram.setUniformValue("u_normal_matrix", normalMatrix);
    m_shaderProgram.setUniformValue("u_camera_pos", m_camera.position());
    m_shaderProgram.setUniformValue(
        "u_base_color",
        QVector3D(m_modelGrayLevel, m_modelGrayLevel, m_modelGrayLevel));
    m_shaderProgram.setUniformValue("u_model_alpha", m_modelOpacity);

    m_shaderProgram.setUniformValue("u_global_light_dir", safeNormal(globalLight.direction));
    m_shaderProgram.setUniformValue("u_global_light_color", globalLight.color);
    m_shaderProgram.setUniformValue("u_global_ambient", globalLight.ambientIntensity);
    m_shaderProgram.setUniformValue("u_global_diffuse", globalLight.diffuseIntensity);
    m_shaderProgram.setUniformValue("u_global_specular", globalLight.specularIntensity);

    m_shaderProgram.setUniformValue("u_area_enabled", areaLight.enabled ? 1 : 0);
    m_shaderProgram.setUniformValue("u_area_pos", areaLight.position);
    m_shaderProgram.setUniformValue("u_area_color", areaLight.color);
    m_shaderProgram.setUniformValue("u_area_intensity", areaLight.intensity);
    m_shaderProgram.setUniformValue("u_area_radius", std::max(0.1f, areaLight.falloffRadius));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_vertexArray.bind();
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    m_vertexArray.release();
    m_shaderProgram.release();
    glDisable(GL_BLEND);

    if (areaLight.enabled && m_lightQuadVAO.isCreated())
    {
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        QMatrix4x4 lightModel;
        lightModel.setToIdentity();
        lightModel.translate(areaLight.position);
        lightModel.scale(std::max(0.05f, areaLight.width), 1.0f, std::max(0.05f, areaLight.height));

        const QMatrix4x4 lightMvp = projectionMatrix * viewMatrix * lightModel;
        const float alpha = std::clamp(0.18f + areaLight.intensity * 0.07f, 0.12f, 0.45f);

        m_lightQuadShader.bind();
        m_lightQuadShader.setUniformValue("u_mvp", lightMvp);
        m_lightQuadShader.setUniformValue(
            "u_light_color",
            QVector4D(areaLight.color.x(), areaLight.color.y(), areaLight.color.z(), alpha));

        m_lightQuadVAO.bind();
        glDrawArrays(GL_TRIANGLES, 0, kLightQuadVertexCount);
        m_lightQuadVAO.release();

        m_lightQuadShader.release();

        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
    }
}

void ModelOpenGLWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, width, height);
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    m_camera.setAspectRatio(aspect);
}

void ModelOpenGLWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePos = event->pos();
    if (event->button() == Qt::LeftButton)
    {
        m_isDragging = true;
    }
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
    {
        m_isPanning = true;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void ModelOpenGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    if (m_isDragging)
    {
        m_camera.orbit(-static_cast<float>(delta.x()) * kOrbitSpeed, -static_cast<float>(delta.y()) * kOrbitSpeed);
        update();
    }
    else if (m_isPanning)
    {
        const float panX = -static_cast<float>(delta.x()) * kPanScale;
        const float panY = static_cast<float>(delta.y()) * kPanScale;
        m_camera.pan(panX, panY);
        update();
    }

    QOpenGLWidget::mouseMoveEvent(event);
}

void ModelOpenGLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_isDragging = false;
    }
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
    {
        m_isPanning = false;
    }

    QOpenGLWidget::mouseReleaseEvent(event);
}

void ModelOpenGLWidget::wheelEvent(QWheelEvent *event)
{
    const qreal steps = static_cast<qreal>(event->angleDelta().y()) / 120.0;
    if (std::abs(steps) > 1e-4)
    {
        const float zoomDelta = static_cast<float>(steps) * 0.25f;
        m_camera.zoom(zoomDelta);
        update();
    }

    event->accept();
}

void ModelOpenGLWidget::tickAnimation()
{
    tickCameraTransition();
}

void ModelOpenGLWidget::tickCameraTransition()
{
    if (!m_cameraTransitionActive)
    {
        return;
    }

    if (!m_frameClock.isValid())
    {
        m_frameClock.start();
        return;
    }

    float dt = static_cast<float>(m_frameClock.restart()) / 1000.0f;
    dt = std::clamp(dt, 0.0f, 0.1f);

    m_transitionProgress += m_transitionSpeed * dt;
    if (m_transitionProgress >= 1.0f)
    {
        m_transitionProgress = 1.0f;
    }

    m_camera.lerp(m_transitionFrom, m_transitionTo, m_transitionProgress);
    update();

    if (m_transitionProgress >= 1.0f)
    {
        m_cameraTransitionActive = false;
        m_animationTimer.stop();
        emit cameraTransitionFinished();
    }
}

bool ModelOpenGLWidget::buildShaders()
{
    static const char *kModelVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat3 u_normal_matrix;

out vec3 v_world_pos;
out vec3 v_world_normal;

void main()
{
    vec4 world = u_model * vec4(a_position, 1.0);
    v_world_pos = world.xyz;
    v_world_normal = normalize(u_normal_matrix * a_normal);
    gl_Position = u_projection * u_view * world;
}
)";

    static const char *kModelFragmentShader = R"(
#version 330 core

in vec3 v_world_pos;
in vec3 v_world_normal;

uniform vec3 u_camera_pos;
uniform vec3 u_base_color;
uniform float u_model_alpha;

uniform vec3 u_global_light_dir;
uniform vec3 u_global_light_color;
uniform float u_global_ambient;
uniform float u_global_diffuse;
uniform float u_global_specular;

uniform int u_area_enabled;
uniform vec3 u_area_pos;
uniform vec3 u_area_color;
uniform float u_area_intensity;
uniform float u_area_radius;

out vec4 frag_color;

void main()
{
    vec3 N = normalize(v_world_normal);
    vec3 V = normalize(u_camera_pos - v_world_pos);

    vec3 base = u_base_color;
    vec3 lit = base * max(u_global_ambient, 0.0);

    vec3 globalL = normalize(-u_global_light_dir);
    float globalDiff = max(dot(N, globalL), 0.0);
    vec3 globalHalf = normalize(globalL + V);
    float globalSpec = pow(max(dot(N, globalHalf), 0.0), 32.0);

    lit += base * u_global_light_color * (globalDiff * max(u_global_diffuse, 0.0));
    lit += u_global_light_color * (globalSpec * max(u_global_specular, 0.0));

    if (u_area_enabled == 1)
    {
        vec3 toLight = u_area_pos - v_world_pos;
        float distanceToLight = length(toLight);
        vec3 areaL = normalize(toLight);
        float areaDiff = max(dot(N, areaL), 0.0);
        vec3 areaHalf = normalize(areaL + V);
        float areaSpec = pow(max(dot(N, areaHalf), 0.0), 18.0);

        float radius = max(u_area_radius, 0.001);
        float attenuation = clamp(1.0 - distanceToLight / radius, 0.0, 1.0);
        attenuation = attenuation * attenuation;
        float intensity = max(u_area_intensity, 0.0) * attenuation;

        lit += base * u_area_color * areaDiff * intensity;
        lit += u_area_color * areaSpec * intensity * 0.25;
    }

    frag_color = vec4(clamp(lit, 0.0, 1.0), clamp(u_model_alpha, 0.0, 1.0));
}
)";

    static const char *kLightVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 a_position;
uniform mat4 u_mvp;

void main()
{
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

static const char *kLightFragmentShader = R"(
#version 330 core
uniform vec4 u_light_color;
out vec4 frag_color;

void main()
{
    frag_color = u_light_color;
}
)";

    static const char *kGridVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 a_position;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

void main()
{
    vec4 world = u_model * vec4(a_position, 1.0);
    gl_Position = u_projection * u_view * world;
}
)";

    static const char *kGridFragmentShader = R"(
#version 330 core
uniform vec3 u_color;
uniform float u_alpha;
out vec4 frag_color;

void main()
{
    frag_color = vec4(u_color, clamp(u_alpha, 0.0, 1.0));
}
)";

    if (!m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, kModelVertexShader))
    {
        qWarning() << "ModelOpenGLWidget vertex shader compile failed:" << m_shaderProgram.log();
        return false;
    }
    if (!m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, kModelFragmentShader))
    {
        qWarning() << "ModelOpenGLWidget fragment shader compile failed:" << m_shaderProgram.log();
        return false;
    }
    if (!m_shaderProgram.link())
    {
        qWarning() << "ModelOpenGLWidget shader link failed:" << m_shaderProgram.log();
        return false;
    }

    if (!m_lightQuadShader.addShaderFromSourceCode(QOpenGLShader::Vertex, kLightVertexShader))
    {
        qWarning() << "Area light vertex shader compile failed:" << m_lightQuadShader.log();
        return false;
    }
    if (!m_lightQuadShader.addShaderFromSourceCode(QOpenGLShader::Fragment, kLightFragmentShader))
    {
        qWarning() << "Area light fragment shader compile failed:" << m_lightQuadShader.log();
        return false;
    }
    if (!m_lightQuadShader.link())
    {
        qWarning() << "Area light shader link failed:" << m_lightQuadShader.log();
        return false;
    }

    if (!m_gridShader.addShaderFromSourceCode(QOpenGLShader::Vertex, kGridVertexShader))
    {
        qWarning() << "Grid vertex shader compile failed:" << m_gridShader.log();
        return false;
    }
    if (!m_gridShader.addShaderFromSourceCode(QOpenGLShader::Fragment, kGridFragmentShader))
    {
        qWarning() << "Grid fragment shader compile failed:" << m_gridShader.log();
        return false;
    }
    if (!m_gridShader.link())
    {
        qWarning() << "Grid shader link failed:" << m_gridShader.log();
        return false;
    }

    return true;
}

bool ModelOpenGLWidget::uploadModelData(const QString &objPath)
{
    const QString resolvedPath = resolveExistingModelPath(objPath);
    if (resolvedPath.isEmpty())
    {
        qWarning() << "ModelOpenGLWidget: OBJ file not found for path" << objPath;
        return false;
    }

    QVector<float> interleaved;
    QVector3D boundsMin(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    QVector3D boundsMax(
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest());
    QString parseError;
    if (!loadObjSafeInterleaved(resolvedPath, &interleaved, &boundsMin, &boundsMax, &parseError))
    {
        qWarning() << "ModelOpenGLWidget:" << parseError;
        return false;
    }

    if (interleaved.isEmpty())
    {
        qWarning() << "ModelOpenGLWidget: OBJ contains no renderable vertices" << resolvedPath;
        return false;
    }

    m_modelCenter = (boundsMin + boundsMax) * 0.5f;
    m_modelRadius = (boundsMax - boundsMin).length() * 0.5f;
    if (m_modelRadius < kMinModelRadius)
    {
        m_modelRadius = 1.0f;
    }

    const float invRadius = 1.0f / m_modelRadius;
    for (int i = 0; i < interleaved.size(); i += kModelStrideFloats)
    {
        QVector3D pos(interleaved[i], interleaved[i + 1], interleaved[i + 2]);
        pos = (pos - m_modelCenter) * invRadius;
        interleaved[i] = pos.x();
        interleaved[i + 1] = pos.y();
        interleaved[i + 2] = pos.z();
    }

    m_modelCenter = QVector3D(0.0f, 0.0f, 0.0f);
    m_modelRadius = 1.0f;

    if (!m_vertexArray.isCreated() && !m_vertexArray.create())
    {
        qWarning() << "ModelOpenGLWidget: failed to create model VAO";
        return false;
    }
    if (!m_vertexBuffer.isCreated() && !m_vertexBuffer.create())
    {
        qWarning() << "ModelOpenGLWidget: failed to create model VBO";
        return false;
    }

    m_vertexArray.bind();
    m_vertexBuffer.bind();
    m_vertexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_vertexBuffer.allocate(interleaved.constData(), interleaved.size() * static_cast<int>(sizeof(float)));

    m_shaderProgram.bind();
    const int posAttr = m_shaderProgram.attributeLocation("a_position");
    const int normalAttr = m_shaderProgram.attributeLocation("a_normal");
    if (posAttr >= 0)
    {
        m_shaderProgram.enableAttributeArray(posAttr);
        m_shaderProgram.setAttributeBuffer(posAttr, GL_FLOAT, 0, 3, kModelStrideBytes);
    }
    if (normalAttr >= 0)
    {
        m_shaderProgram.enableAttributeArray(normalAttr);
        m_shaderProgram.setAttributeBuffer(normalAttr, GL_FLOAT, 3 * static_cast<int>(sizeof(float)), 3, kModelStrideBytes);
    }
    m_shaderProgram.release();

    m_vertexBuffer.release();
    m_vertexArray.release();

    m_vertexCount = interleaved.size() / kModelStrideFloats;
    m_isReady = m_vertexCount > 0;

    if (m_isReady)
    {
        m_camera.setTarget(QVector3D(0.0f, 0.0f, 0.0f));
        m_camera.setPosition(QVector3D(0.0f, 1.35f, 3.1f));
        m_camera.setUp(QVector3D(0.0f, 1.0f, 0.0f));
        m_camera.setFov(45.0f);

        m_lighting.setAreaLightPosition(QVector3D(0.0f, 1.9f, 0.0f));
        m_lighting.setAreaLightSize(1.8f, 1.8f);
    }

    return m_isReady;
}
