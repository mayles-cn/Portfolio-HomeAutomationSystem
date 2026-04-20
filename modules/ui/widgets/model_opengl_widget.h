#ifndef HOME_AUTOMATION_MODEL_OPENGL_WIDGET_H
#define HOME_AUTOMATION_MODEL_OPENGL_WIDGET_H

#include <QElapsedTimer>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QTimer>

#include "camera_system.h"
#include "lighting_system.h"

class QMouseEvent;
class QWheelEvent;

class ModelOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit ModelOpenGLWidget(QWidget *parent = nullptr);
    ~ModelOpenGLWidget() override;

    CameraSystem &cameraSystem() { return m_camera; }
    const CameraSystem &cameraSystem() const { return m_camera; }
    LightingSystem &lightingSystem() { return m_lighting; }
    const LightingSystem &lightingSystem() const { return m_lighting; }

    bool loadModel(const QString &objPath);
    bool isModelLoaded() const { return m_isReady; }

    void saveCameraSnapshot(const QString &name);
    bool loadCameraSnapshot(int index);
    bool loadCameraSnapshot(const QString &name);

    void setAreaLightEnabled(bool on);
    void toggleAreaLight();
    void setSunHeight(float heightDeg);
    void setSunAngle(float angleDeg);
    void setSunBrightness(float brightness);
    void setModelGrayLevel(float grayLevel);
    void setModelOpacity(float opacity);
    void setGridSize(float size);
    void setGroundHeight(float height);

    float sunHeight() const { return m_sunHeightDeg; }
    float sunAngle() const { return m_sunAngleDeg; }
    float sunBrightness() const { return m_sunBrightness; }
    float modelGrayLevel() const { return m_modelGrayLevel; }
    float modelOpacity() const { return m_modelOpacity; }
    float gridSize() const { return m_gridSize; }
    float groundHeight() const { return m_groundHeight; }

signals:
    void modelLoaded(int vertexCount);
    void cameraSnapshotSaved(const QString &name, int index);
    void cameraTransitionFinished();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void tickAnimation();
    void tickCameraTransition();
    void applySunToDirectionalLight();
    void updateGridGeometry();
    bool buildShaders();
    bool uploadModelData(const QString &objPath);

    QOpenGLShaderProgram m_shaderProgram;
    QOpenGLShaderProgram m_lightQuadShader;
    QOpenGLShaderProgram m_gridShader;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLVertexArrayObject m_vertexArray;
    QOpenGLBuffer m_lightQuadVBO;
    QOpenGLVertexArrayObject m_lightQuadVAO;
    QOpenGLBuffer m_gridVBO;
    QOpenGLVertexArrayObject m_gridVAO;

    QTimer m_animationTimer;
    QElapsedTimer m_frameClock;

    CameraSystem m_camera;
    LightingSystem m_lighting;

    int m_vertexCount = 0;
    bool m_isReady = false;
    bool m_shadersReady = false;

    QPoint m_lastMousePos;
    bool m_isDragging = false;
    bool m_isPanning = false;

    bool m_cameraTransitionActive = false;
    CameraSnapshot m_transitionFrom;
    CameraSnapshot m_transitionTo;
    float m_transitionProgress = 0.0f;
    float m_transitionSpeed = 0.0f;

    QVector3D m_modelCenter;
    float m_modelRadius = 1.0f;
    int m_gridVertexCount = 0;

    float m_sunHeightDeg = 45.0f;
    float m_sunAngleDeg = 45.0f;
    float m_sunBrightness = 1.0f;
    float m_modelGrayLevel = 0.95f;
    float m_modelOpacity = 1.0f;
    float m_gridSize = 10.0f;
    float m_groundHeight = -1.02f;
};

#endif
