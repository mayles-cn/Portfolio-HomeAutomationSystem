#ifndef HOME_AUTOMATION_MAINWIDGET_H
#define HOME_AUTOMATION_MAINWIDGET_H

#include <memory>
#include <optional>

#include <QHash>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "gesture_types.h"

class GesturePipeline;
class MediapipeStreamClient;
class FrameSequenceWidget;
class ModelOpenGLWidget;
class QCheckBox;
class QCloseEvent;
class QGridLayout;
class QLabel;
class QListWidget;
class QMoveEvent;
class QPushButton;
class QResizeEvent;
class QSlider;
class QSpinBox;
class QTimer;
class QWidget;
class QImage;

class MainWidget : public QWidget
{
public:
    explicit MainWidget(QWidget *parent = nullptr);
    ~MainWidget() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void setupMainUi();
    void setupAnimationOverlayUi();
    void setupDebugPanelUi();
    void applyUnifiedStyle();

    void initializeGestureRuntime();
    void runSmokeInference();
    void toggleRealtimeInference();
    void onRealtimeInferenceTick();
    void stopRealtimeInference();

    void toggleDebugPanel();
    void positionDebugPanel();
    void positionAnimationOverlay();
    void applyCameraIndex();
    void handleGestureDrivenInteraction(const GestureEvent &gestureEvent);
    void saveCurrentCameraSnapshot();
    void deleteCurrentCameraSnapshot();
    void cycleCameraSnapshot(int offset);
    void cycleScene(int offset);
    void applyCameraSnapshotSelection(int index);
    void toggleAreaLightFromUi();
    void updateSceneControlState();
    void updateDeviceInfoDisplay();
    QString deviceNameForSnapshotIndex(int snapshotIndex) const;
    int sequenceIndexForSnapshotIndex(int snapshotIndex) const;
    void applyRenderControlStateToModel();
    QString resolveHomeObjPath() const;
    QString resolveCameraSnapshotStorePath() const;
    void loadCameraSnapshotsFromDisk();
    void saveCameraSnapshotsToDisk() const;

    void populateGestureMaskControls(const QStringList &labels);
    void refreshMaskedGestureSet();
    QString displayNameForLabel(const QString &label) const;
    int resolvePreferredCameraIndex() const;
    QVector<int> resolveCameraCandidates() const;

    void drawSkeletonOverlay(
        QImage *image,
        const GestureFrameObservation &observation) const;

    void appendRuntimeLog(const QString &type, const QString &message);
    void appendPredictionLog(
        const QString &type,
        const GestureFrameObservation *observation,
        const GesturePrediction *prediction,
        const std::optional<GestureEvent> *gestureEvent,
        const QString &message);
    bool loadSequenceAtIndex(int index);
    void switchSequenceByOffset(int offset);
    void requestAppQuit();
    QString resolveGestureModelPath() const;
    QString resolveMediapipeBridgePath() const;
    QString resolveHandLandmarkerTaskPath() const;
    QString resolveImageSequenceRootPath() const;
    QString resolveRuntimeLogPath() const;
    bool isInDragArea(const QPoint &localPos) const;

    ModelOpenGLWidget *m_modelWidget = nullptr;
    FrameSequenceWidget *m_frameSequenceWidget = nullptr;
    QSlider *m_frameSequenceSlider = nullptr;
    QPushButton *m_prevSequenceButton = nullptr;
    QPushButton *m_nextSequenceButton = nullptr;
    QPushButton *m_saveCameraSnapshotButton = nullptr;
    QPushButton *m_deleteCameraSnapshotButton = nullptr;
    QPushButton *m_toggleAreaLightButton = nullptr;
    QPushButton *m_realtimeButton = nullptr;
    QLabel *m_runtimeStatusLabel = nullptr;
    QLabel *m_trackerStatusLabel = nullptr;
    QLabel *m_predictionStatusLabel = nullptr;
    QLabel *m_lastDecisionStatusLabel = nullptr;
    QLabel *m_modelStatusLabel = nullptr;
    QLabel *m_cameraSnapshotLabel = nullptr;
    QLabel *m_areaLightLabel = nullptr;
    QLabel *m_deviceNameLabel = nullptr;
    QLabel *m_deviceStatusLabel = nullptr;

    QWidget *m_debugPanel = nullptr;
    QWidget *m_animationOverlay = nullptr;
    QLabel *m_cameraPreviewLabel = nullptr;
    QLabel *m_cameraStatusLabel = nullptr;
    QCheckBox *m_drawSkeletonCheckBox = nullptr;
    QSpinBox *m_cameraIndexSpinBox = nullptr;
    QPushButton *m_applyCameraIndexButton = nullptr;
    QListWidget *m_cameraSnapshotList = nullptr;
    QSlider *m_sunHeightSlider = nullptr;
    QSlider *m_sunAngleSlider = nullptr;
    QSlider *m_sunBrightnessSlider = nullptr;
    QSlider *m_modelGraySlider = nullptr;
    QSlider *m_modelOpacitySlider = nullptr;
    QSlider *m_gridSizeSlider = nullptr;
    QSlider *m_groundHeightSlider = nullptr;
    QWidget *m_gestureMaskContainer = nullptr;
    QGridLayout *m_gestureMaskLayout = nullptr;
    QHash<QString, QCheckBox *> m_gestureMaskCheckBoxes;
    QSet<QString> m_maskedGestureLabels;

    bool m_isDragging = false;
    QPoint m_dragOffset;
    QRect m_dragArea;

    std::unique_ptr<GesturePipeline> m_gesturePipeline;
    std::unique_ptr<MediapipeStreamClient> m_mediapipeClient;
    QTimer *m_realtimeTimer = nullptr;

    bool m_realtimeRunning = false;
    bool m_isClosing = false;
    int m_selectedCameraIndex = -1;
    int m_realtimeFrameCount = 0;
    int m_currentCameraSnapshotIndex = -1;
    qint64 m_lastUiHeartbeatMs = 0;
    qint64 m_lastGestureActionMs = 0;
    static constexpr int kGestureActionCooldownMs = 1200;

    GestureFrameObservation m_latestObservation;
    bool m_hasLatestObservation = false;
    QStringList m_sequenceDirectories;
    int m_currentSequenceDirectoryIndex = -1;

    QString m_gestureModelPath;
    QString m_runtimeLogPath;
    QString m_homeObjPath;
    QString m_cameraSnapshotStorePath;

    float m_sunHeightDeg = 45.0f;
    float m_sunAngleDeg = 45.0f;
    float m_sunBrightness = 1.0f;
    float m_modelGrayLevel = 0.95f;
    float m_modelOpacity = 1.0f;
    float m_gridSize = 10.0f;
    float m_groundHeight = -1.02f;
};

#endif
