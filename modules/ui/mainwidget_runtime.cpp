#include "mainwidget.h"
#include "mainwidget_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QTimer>

#include "gesture_pipeline.h"
#include "mediapipe_stream_client.h"
#include "widgets/model_opengl_widget.h"

namespace
{
constexpr std::array<std::pair<int, int>, 21> kHandConnections = {{
    {0, 1}, {1, 2}, {2, 3}, {3, 4}, {0, 5}, {5, 6}, {6, 7},
    {7, 8}, {5, 9}, {9, 10}, {10, 11}, {11, 12}, {9, 13}, {13, 14},
    {14, 15}, {15, 16}, {13, 17}, {17, 18}, {18, 19}, {19, 20}, {0, 17},
}};
} // namespace

void MainWidget::drawSkeletonOverlay(QImage *image, const GestureFrameObservation &observation) const
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    if (!image || image->isNull())
    {
        return;
    }
    QPainter painter(image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const auto drawHand = [&](const int baseOffset, const QColor &color) {
        std::array<QPointF, 21> keypoints = {};
        std::array<bool, 21> valid = {};
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        bool normalizedInImage = true;
        int validCount = 0;

        for (int i = 0; i < 21; ++i)
        {
            const int xOffset = baseOffset + i * 3;
            const int yOffset = xOffset + 1;
            if (xOffset < 0 || yOffset < 0 ||
                xOffset >= static_cast<int>(observation.keypoints.size()) ||
                yOffset >= static_cast<int>(observation.keypoints.size()))
            {
                continue;
            }

            const float x = observation.keypoints[static_cast<size_t>(xOffset)];
            const float y = observation.keypoints[static_cast<size_t>(yOffset)];
            if (!std::isfinite(x) || !std::isfinite(y))
            {
                continue;
            }

            valid[i] = true;
            keypoints[i] = QPointF(x, y);
            ++validCount;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            if (x < -0.05f || x > 1.05f || y < -0.05f || y > 1.05f)
            {
                normalizedInImage = false;
            }
        }

        if (validCount < 5)
        {
            return;
        }
        const float spread = (maxX - minX) + (maxY - minY);
        if (spread < 0.02f)
        {
            return;
        }

        const float xRange = std::max(1e-5f, maxX - minX);
        const float yRange = std::max(1e-5f, maxY - minY);
        const float margin = 0.10f;
        const float widthScale = static_cast<float>(image->width() - 1);
        const float heightScale = static_cast<float>(image->height() - 1);

        const auto toPixelPoint = [&](const QPointF &point) {
            if (normalizedInImage)
            {
                return QPointF(point.x() * widthScale, point.y() * heightScale);
            }
            const float xNorm = static_cast<float>((point.x() - minX) / xRange);
            const float yNorm = static_cast<float>((point.y() - minY) / yRange);
            const float x = (margin + (1.0f - 2.0f * margin) * xNorm) * widthScale;
            const float y = (margin + (1.0f - 2.0f * margin) * yNorm) * heightScale;
            return QPointF(x, y);
        };

        QPen linePen(
            color,
            config.skeletonLineWidth,
            Qt::SolidLine,
            Qt::SquareCap,
            Qt::MiterJoin);
        painter.setPen(linePen);
        painter.setBrush(Qt::NoBrush);
        for (const auto &connection : kHandConnections)
        {
            const int a = connection.first;
            const int b = connection.second;
            if (a < 0 || b < 0 || a >= 21 || b >= 21 || !valid[a] || !valid[b])
            {
                continue;
            }
            painter.drawLine(toPixelPoint(keypoints[a]), toPixelPoint(keypoints[b]));
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        for (int i = 0; i < 21; ++i)
        {
            if (!valid[i])
            {
                continue;
            }
            const QPointF point = toPixelPoint(keypoints[i]);
            const qreal pointHalf = static_cast<qreal>(config.skeletonPointSize * 0.5f);
            painter.drawRect(
                QRectF(
                    point.x() - pointHalf,
                    point.y() - pointHalf,
                    config.skeletonPointSize,
                    config.skeletonPointSize));
        }
    };

    drawHand(0, QColor(config.skeletonLeftColor));
    drawHand(63, QColor(config.skeletonRightColor));
}
void MainWidget::initializeGestureRuntime()
{
    m_gestureModelPath = resolveGestureModelPath();
    if (m_gestureModelPath.isEmpty())
    {
        m_runtimeStatusLabel->setText(QStringLiteral("模型：未找到 cpp_model.json"));
        appendRuntimeLog(QStringLiteral("runtime_error"), QStringLiteral("Gesture model file not found."));
        return;
    }

    QString errorMessage;
    if (!m_gesturePipeline->loadFromJsonFile(m_gestureModelPath, &errorMessage))
    {
        m_runtimeStatusLabel->setText(QStringLiteral("模型加载失败"));
        if (m_trackerStatusLabel)
        {
            m_trackerStatusLabel->setText(errorMessage);
        }
        appendRuntimeLog(
            QStringLiteral("runtime_error"),
            QStringLiteral("Gesture model load failed: %1").arg(errorMessage));
        return;
    }

    m_runtimeStatusLabel->setText(QStringLiteral("模型：加载成功"));
    appendRuntimeLog(
        QStringLiteral("runtime_info"),
        QStringLiteral("Gesture model loaded: %1").arg(QDir::toNativeSeparators(m_gestureModelPath)));

    if (m_realtimeButton)
    {
        m_realtimeButton->setEnabled(true);
    }

    populateGestureMaskControls(m_gesturePipeline->classLabels());
}

void MainWidget::runSmokeInference()
{
    if (!m_gesturePipeline || !m_gesturePipeline->isReady() || !m_predictionStatusLabel)
    {
        return;
    }

    QVector<float> featureVector(m_gesturePipeline->featureDimension(), 0.0f);
    GesturePrediction prediction;
    std::optional<GestureEvent> gestureEvent;
    QString errorMessage;
    if (!m_gesturePipeline->predictFromFeatureVector(
            featureVector,
            &prediction,
            &gestureEvent,
            true,
            QDateTime::currentMSecsSinceEpoch(),
            0.0f,
            0.0f,
            &errorMessage))
    {
        m_predictionStatusLabel->setText(QStringLiteral("推理失败：%1").arg(errorMessage));
        return;
    }

    m_predictionStatusLabel->setText(
        QStringLiteral("当前识别：%1").arg(prediction.displayName));
}

void MainWidget::toggleRealtimeInference()
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    if (!m_gesturePipeline || !m_gesturePipeline->isReady())
    {
        m_trackerStatusLabel->setText(QStringLiteral("跟踪：模型尚未加载"));
        return;
    }

    if (m_realtimeRunning)
    {
        stopRealtimeInference();
        if (m_cameraStatusLabel)
        {
            m_cameraStatusLabel->setText(config.textCameraStopped);
        }
        return;
    }

    const QString bridgePath = resolveMediapipeBridgePath();
    if (bridgePath.isEmpty())
    {
        m_trackerStatusLabel->setText(
            QStringLiteral("未找到桥接程序 hand_landmarker_stream.exe，请先构建。"));
        appendRuntimeLog(QStringLiteral("runtime_error"), QStringLiteral("Bridge executable not found."));
        return;
    }

    const QString taskModelPath = resolveHandLandmarkerTaskPath();
    if (taskModelPath.isEmpty())
    {
        m_trackerStatusLabel->setText(QStringLiteral("未找到 hand_landmarker.task"));
        appendRuntimeLog(
            QStringLiteral("runtime_error"),
            QStringLiteral("HandLandmarker task model not found."));
        return;
    }

    const QVector<int> cameraCandidates = resolveCameraCandidates();
    if (cameraCandidates.isEmpty())
    {
        m_trackerStatusLabel->setText(QStringLiteral("实时识别启动失败：无可用摄像头"));
        appendRuntimeLog(
            QStringLiteral("runtime_error"),
            QStringLiteral("Realtime start failed: no camera candidates."));
        return;
    }

    if (m_cameraPreviewLabel)
    {
        m_cameraPreviewLabel->setText(QStringLiteral("骨架画面：等待关键点帧..."));
    }
    if (m_cameraStatusLabel)
    {
        m_cameraStatusLabel->setText(QStringLiteral("骨架画面：实时识别中"));
    }

    QString errorMessage;
    int selectedCameraIndex = -1;
    for (const int cameraIndex : cameraCandidates)
    {
        if (!m_mediapipeClient->start(
                bridgePath,
                taskModelPath,
                cameraIndex,
                true,
                false,
                &errorMessage))
        {
            continue;
        }

        QThread::msleep(static_cast<unsigned long>(config.cameraProbeWaitMs));
        QCoreApplication::processEvents();
        if (m_mediapipeClient->isRunning())
        {
            selectedCameraIndex = cameraIndex;
            break;
        }

        if (!m_mediapipeClient->lastError().isEmpty())
        {
            errorMessage = m_mediapipeClient->lastError();
        }
    }

    if (selectedCameraIndex < 0)
    {
        QStringList triedIndexes;
        for (const int index : cameraCandidates)
        {
            triedIndexes << QString::number(index);
        }
        m_trackerStatusLabel->setText(
            QStringLiteral(
                "实时识别启动失败：摄像头不可用（%1）。错误：%2")
                .arg(triedIndexes.join(QStringLiteral(", ")))
                .arg(errorMessage));
        appendRuntimeLog(
            QStringLiteral("runtime_error"),
            QStringLiteral("Realtime start failed: %1").arg(errorMessage));
        return;
    }

    m_selectedCameraIndex = selectedCameraIndex;
    if (m_cameraIndexSpinBox)
    {
        m_cameraIndexSpinBox->setValue(selectedCameraIndex);
    }

    m_gesturePipeline->resetSequence();
    m_realtimeFrameCount = 0;
    m_lastUiHeartbeatMs = 0;
    m_realtimeRunning = true;
    m_realtimeTimer->start();
    m_realtimeButton->setText(config.textRealtimeStop);
    m_trackerStatusLabel->setText(
        QStringLiteral("跟踪：已启动 camera=%1").arg(selectedCameraIndex));
    appendRuntimeLog(
        QStringLiteral("runtime_info"),
        QStringLiteral("Realtime started. camera=%1 bridge=%2 task=%3")
            .arg(selectedCameraIndex)
            .arg(QDir::toNativeSeparators(bridgePath))
            .arg(QDir::toNativeSeparators(taskModelPath)));

}
void MainWidget::onRealtimeInferenceTick()
{
    if (!m_realtimeRunning || !m_mediapipeClient)
    {
        return;
    }

    if (!m_mediapipeClient->isRunning())
    {
        m_trackerStatusLabel->setText(
            QStringLiteral("实时流已停止：%1").arg(m_mediapipeClient->lastError()));
        appendRuntimeLog(
            QStringLiteral("runtime_error"),
            QStringLiteral("Bridge stopped: %1").arg(m_mediapipeClient->lastError()));
        stopRealtimeInference();
        return;
    }

    GestureFrameObservation observation;
    if (!m_mediapipeClient->takeLatestObservation(&observation))
    {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - m_lastUiHeartbeatMs >= mainWidgetUiConfig().statusHeartbeatIntervalMs)
        {
            m_predictionStatusLabel->setText(mainWidgetUiConfig().textPredictionWaitingAnimated);
            m_lastUiHeartbeatMs = nowMs;
        }
        return;
    }
    ++m_realtimeFrameCount;
    m_latestObservation = observation;
    m_hasLatestObservation = true;
    if (m_debugPanel &&
        m_debugPanel->isVisible() &&
        m_cameraPreviewLabel)
    {
        QImage skeletonCanvas(
            std::max(1, m_cameraPreviewLabel->width()),
            std::max(1, m_cameraPreviewLabel->height()),
            QImage::Format_RGB888);
        skeletonCanvas.fill(QColor(mainWidgetUiConfig().skeletonBackgroundColor));
        if (m_drawSkeletonCheckBox && m_drawSkeletonCheckBox->isChecked())
        {
            drawSkeletonOverlay(&skeletonCanvas, observation);
        }
        const QPixmap pixmap = QPixmap::fromImage(skeletonCanvas).scaled(
            m_cameraPreviewLabel->size(),
            Qt::KeepAspectRatio,
            Qt::FastTransformation);
        m_cameraPreviewLabel->setPixmap(pixmap);
    }

    GesturePrediction prediction;
    std::optional<GestureEvent> gestureEvent;
    QString errorMessage;
    if (!m_gesturePipeline->pushObservation(observation, &prediction, &gestureEvent, &errorMessage))
    {
        if (!errorMessage.isEmpty())
        {
            m_predictionStatusLabel->setText(QStringLiteral("当前识别：推理失败"));
            if (m_trackerStatusLabel)
            {
                m_trackerStatusLabel->setText(QStringLiteral("跟踪错误：%1").arg(errorMessage));
            }
            appendPredictionLog(
                QStringLiteral("prediction_error"),
                &observation,
                nullptr,
                nullptr,
                errorMessage);
            return;
        }

        const int requiredFrames = std::max(1, m_gesturePipeline->requiredSequenceFrames());
        const int currentFrames = std::min(m_realtimeFrameCount, requiredFrames);
        m_predictionStatusLabel->setText(QStringLiteral("当前识别：准备中 %1/%2").arg(currentFrames).arg(requiredFrames));
        appendPredictionLog(
            QStringLiteral("warming_up"),
            &observation,
            nullptr,
            nullptr,
            QStringLiteral("collecting_window"));
        return;
    }

    QString eventText = QStringLiteral("事件：未触发");
    bool blocked = false;
    std::optional<GestureEvent> blockedEvent;
    if (gestureEvent.has_value() && m_maskedGestureLabels.contains(gestureEvent->label))
    {
        blocked = true;
        blockedEvent = gestureEvent;
        eventText = QStringLiteral("事件：%1 (%2) 已屏蔽")
                        .arg(gestureEvent->displayName)
                        .arg(gestureEvent->label);
        appendPredictionLog(
            QStringLiteral("event_blocked"),
            &observation,
            &prediction,
            &blockedEvent,
            QStringLiteral("blocked_by_debug_panel"));
        gestureEvent.reset();
    }
    else if (gestureEvent.has_value())
    {
        eventText = QStringLiteral("事件：%1 (%2)")
                        .arg(gestureEvent->displayName)
                        .arg(gestureEvent->label);
    }

    m_predictionStatusLabel->setText(QStringLiteral("当前识别：%1").arg(prediction.displayName));
    if (m_trackerStatusLabel)
    {
        m_trackerStatusLabel->setText(
            QStringLiteral("跟踪：L%1 R%2 | c=%3 m=%4 | %5")
                .arg(static_cast<int>(observation.leftStatus))
                .arg(static_cast<int>(observation.rightStatus))
                .arg(prediction.confidence, 0, 'f', 2)
                .arg(prediction.margin, 0, 'f', 2)
                .arg(eventText));
    }
    appendPredictionLog(
        QStringLiteral("prediction"),
        &observation,
        &prediction,
        &gestureEvent,
        blocked ? QStringLiteral("event_blocked") : QString());

    if (blocked && blockedEvent.has_value())
    {
        const QDateTime blockTime = QDateTime::fromMSecsSinceEpoch(blockedEvent->timestampMs);
        m_lastDecisionStatusLabel->setText(
            QStringLiteral("最近一次触发判定：%1\n事件：%2 (%3) 已被调试面板屏蔽")
                .arg(blockTime.toString(QStringLiteral("HH:mm:ss.zzz")))
                .arg(blockedEvent->displayName)
                .arg(blockedEvent->label));
    }

    if (gestureEvent.has_value())
    {
        const QDateTime triggerTime = QDateTime::fromMSecsSinceEpoch(gestureEvent->timestampMs);
        const float swipeRightProb =
            prediction.probabilityByLabel.value(QStringLiteral("swipe_right"), 0.0f);
        const float cheeseProb =
            prediction.probabilityByLabel.value(QStringLiteral("cheese"), 0.0f);
        m_lastDecisionStatusLabel->setText(
            QStringLiteral(
                "最近一次触发判定：%1\n"
                "触发事件：%2 (%3) | 当前预测：%4 (%5)\n"
                "c=%6 | margin=%7 | p(right)=%8 | p(cheese)=%9 | 左手=%10 右手=%11")
                .arg(triggerTime.toString(QStringLiteral("HH:mm:ss.zzz")))
                .arg(gestureEvent->displayName)
                .arg(gestureEvent->label)
                .arg(prediction.displayName)
                .arg(prediction.label)
                .arg(prediction.confidence, 0, 'f', 4)
                .arg(prediction.margin, 0, 'f', 4)
                .arg(swipeRightProb, 0, 'f', 4)
                .arg(cheeseProb, 0, 'f', 4)
                .arg(static_cast<int>(observation.leftStatus))
                .arg(static_cast<int>(observation.rightStatus)));
        appendPredictionLog(
            QStringLiteral("event"),
            &observation,
            &prediction,
            &gestureEvent,
            QStringLiteral("gesture_event_triggered"));

        handleGestureDrivenInteraction(gestureEvent.value());
    }
}

QString MainWidget::resolveHomeObjPath() const
{
    const QString fromEnv = qEnvironmentVariable("HOME_AUTOMATION_HOME_OBJ").trimmed();
    const QString appDirectory = QCoreApplication::applicationDirPath();
    const MainWidgetUiConfig &config = mainWidgetUiConfig();

    QStringList candidates;
    candidates.push_back(fromEnv);
    for (const QString &candidate : config.homeObjCandidates)
    {
        QFileInfo info(candidate);
        if (info.isAbsolute())
        {
            candidates.push_back(QDir::cleanPath(candidate));
        }
        else
        {
            candidates.push_back(QDir::cleanPath(QDir(appDirectory).filePath(candidate)));
        }
    }

    for (const QString &candidate : candidates)
    {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate) && QFileInfo(candidate).isFile())
        {
            return QDir::cleanPath(candidate);
        }
    }

    return QString();
}

QString MainWidget::resolveCameraSnapshotStorePath() const
{
    const QString fromEnv = qEnvironmentVariable("HOME_AUTOMATION_CAMERA_SNAPSHOT_STORE").trimmed();
    const QString appDirectory = QCoreApplication::applicationDirPath();
    const MainWidgetUiConfig &config = mainWidgetUiConfig();

    const QString configured = fromEnv.isEmpty() ? config.cameraSnapshotStore : fromEnv;
    QFileInfo info(configured);
    if (info.isAbsolute())
    {
        return QDir::cleanPath(configured);
    }
    return QDir::cleanPath(QDir(appDirectory).filePath(configured));
}

void MainWidget::loadCameraSnapshotsFromDisk()
{
    if (!m_modelWidget)
    {
        return;
    }

    const QString path = m_cameraSnapshotStorePath.isEmpty()
                             ? resolveCameraSnapshotStorePath()
                             : m_cameraSnapshotStorePath;
    if (path.isEmpty())
    {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        updateSceneControlState();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        updateSceneControlState();
        return;
    }

    const QJsonObject root = document.object();
    const QJsonArray snapshots = root.value(QStringLiteral("snapshots")).toArray();
    m_modelWidget->cameraSystem().snapshotsFromJson(snapshots);
    bool hasStoredIndex = false;
    const int storedIndex = root.value(QStringLiteral("current_index")).toVariant().toInt(&hasStoredIndex);

    QJsonObject renderControls = root.value(QStringLiteral("render_controls")).toObject();
    if (renderControls.isEmpty())
    {
        renderControls = root;
    }
    m_sunHeightDeg = static_cast<float>(
        renderControls.value(QStringLiteral("sun_height_deg")).toDouble(m_sunHeightDeg));
    m_sunAngleDeg = static_cast<float>(
        renderControls.value(QStringLiteral("sun_angle_deg")).toDouble(m_sunAngleDeg));
    m_sunBrightness = static_cast<float>(
        renderControls.value(QStringLiteral("sun_brightness")).toDouble(m_sunBrightness));
    m_modelGrayLevel = static_cast<float>(
        renderControls.value(QStringLiteral("model_gray_level")).toDouble(m_modelGrayLevel));
    m_modelOpacity = static_cast<float>(
        renderControls.value(QStringLiteral("model_opacity")).toDouble(m_modelOpacity));
    m_gridSize = static_cast<float>(
        renderControls.value(QStringLiteral("grid_size")).toDouble(m_gridSize));
    m_groundHeight = static_cast<float>(
        renderControls.value(QStringLiteral("ground_height")).toDouble(m_groundHeight));
    applyRenderControlStateToModel();

    const int snapshotCount = m_modelWidget->cameraSystem().snapshotCount();
    if (snapshotCount > 0)
    {
        // 启动时总是加载机位1（索引0）
        m_currentCameraSnapshotIndex = 0;
        m_modelWidget->loadCameraSnapshot(0);
    }
    else
    {
        m_currentCameraSnapshotIndex = -1;
    }

    updateSceneControlState();
}

void MainWidget::saveCameraSnapshotsToDisk() const
{
    if (!m_modelWidget)
    {
        return;
    }

    const QString path = m_cameraSnapshotStorePath.isEmpty()
                             ? resolveCameraSnapshotStorePath()
                             : m_cameraSnapshotStorePath;
    if (path.isEmpty())
    {
        return;
    }

    QFileInfo target(path);
    QDir().mkpath(target.absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        return;
    }

    QJsonObject root;
    root.insert(
        QStringLiteral("saved_at"),
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    root.insert(QStringLiteral("current_index"), m_currentCameraSnapshotIndex);
    root.insert(QStringLiteral("snapshots"), m_modelWidget->cameraSystem().snapshotsToJson());

    QJsonObject renderControls;
    renderControls.insert(QStringLiteral("sun_height_deg"), m_sunHeightDeg);
    renderControls.insert(QStringLiteral("sun_angle_deg"), m_sunAngleDeg);
    renderControls.insert(QStringLiteral("sun_brightness"), m_sunBrightness);
    renderControls.insert(QStringLiteral("model_gray_level"), m_modelGrayLevel);
    renderControls.insert(QStringLiteral("model_opacity"), m_modelOpacity);
    renderControls.insert(QStringLiteral("grid_size"), m_gridSize);
    renderControls.insert(QStringLiteral("ground_height"), m_groundHeight);
    root.insert(QStringLiteral("render_controls"), renderControls);

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString MainWidget::resolveGestureModelPath() const
{
    const QString appDirectory = QCoreApplication::applicationDirPath();
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    QStringList candidates;
    for (const QString &candidate : config.gestureModelCandidates)
    {
        QFileInfo candidateInfo(candidate);
        if (candidateInfo.isAbsolute())
        {
            candidates.push_back(QDir::cleanPath(candidate));
        }
        else
        {
            candidates.push_back(QDir::cleanPath(QDir(appDirectory).filePath(candidate)));
        }
    }

    for (const QString &candidate : candidates)
    {
        if (QFileInfo::exists(candidate) && QFileInfo(candidate).isFile())
        {
            return QDir::cleanPath(candidate);
        }
    }

    return QString();
}

QString MainWidget::resolveMediapipeBridgePath() const
{
    const QString fromEnv = qEnvironmentVariable("HOME_AUTOMATION_MEDIAPIPE_BRIDGE");
    const QString appDirectory = QCoreApplication::applicationDirPath();
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    QStringList candidates;
    candidates.push_back(fromEnv);
    for (const QString &candidate : config.mediapipeBridgeCandidates)
    {
        QFileInfo candidateInfo(candidate);
        if (candidateInfo.isAbsolute())
        {
            candidates.push_back(QDir::cleanPath(candidate));
        }
        else
        {
            candidates.push_back(QDir::cleanPath(QDir(appDirectory).filePath(candidate)));
        }
    }

    for (const QString &candidate : candidates)
    {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate) && QFileInfo(candidate).isFile())
        {
            return QDir::cleanPath(candidate);
        }
    }
    return QString();
}

QString MainWidget::resolveHandLandmarkerTaskPath() const
{
    const QString fromEnv = qEnvironmentVariable("HOME_AUTOMATION_HAND_TASK");
    const QString appDirectory = QCoreApplication::applicationDirPath();
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    QStringList candidates;
    candidates.push_back(fromEnv);
    for (const QString &candidate : config.handLandmarkerTaskCandidates)
    {
        QFileInfo candidateInfo(candidate);
        if (candidateInfo.isAbsolute())
        {
            candidates.push_back(QDir::cleanPath(candidate));
        }
        else
        {
            candidates.push_back(QDir::cleanPath(QDir(appDirectory).filePath(candidate)));
        }
    }

    for (const QString &candidate : candidates)
    {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate) && QFileInfo(candidate).isFile())
        {
            return QDir::cleanPath(candidate);
        }
    }
    return QString();
}

QString MainWidget::resolveImageSequenceRootPath() const
{
    const QString fromEnv = qEnvironmentVariable("HOME_AUTOMATION_IMAGE_SEQUENCE_ROOT").trimmed();
    const QString appDirectory = QCoreApplication::applicationDirPath();
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    QStringList candidates;
    candidates.push_back(fromEnv);
    for (const QString &candidate : config.imageSequenceRootCandidates)
    {
        QFileInfo candidateInfo(candidate);
        if (candidateInfo.isAbsolute())
        {
            candidates.push_back(QDir::cleanPath(candidate));
        }
        else
        {
            candidates.push_back(QDir::cleanPath(QDir(appDirectory).filePath(candidate)));
        }
    }

    for (const QString &candidate : candidates)
    {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate) && QFileInfo(candidate).isDir())
        {
            return QDir::cleanPath(candidate);
        }
    }

    return QString();
}

QString MainWidget::resolveRuntimeLogPath() const
{
    const QString fromEnv = qEnvironmentVariable("HOME_AUTOMATION_RUNTIME_LOG").trimmed();
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    const QString appDirectory = QCoreApplication::applicationDirPath();
    if (!fromEnv.isEmpty())
    {
        QFileInfo configured(fromEnv);
        if (configured.isAbsolute())
        {
            return QDir::cleanPath(fromEnv);
        }
        return QDir::cleanPath(QDir(appDirectory).filePath(fromEnv));
    }

    QFileInfo configuredDefault(config.runtimeLogDefault);
    if (configuredDefault.isAbsolute())
    {
        return QDir::cleanPath(config.runtimeLogDefault);
    }
    return QDir::cleanPath(QDir(appDirectory).filePath(config.runtimeLogDefault));
}
void MainWidget::appendRuntimeLog(const QString &type, const QString &message)
{
    const QString path = m_runtimeLogPath.isEmpty() ? resolveRuntimeLogPath() : m_runtimeLogPath;
    QFileInfo target(path);
    QDir().mkpath(target.absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    object.insert(
        QStringLiteral("time"),
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    object.insert(QStringLiteral("timestamp_ms"), QDateTime::currentMSecsSinceEpoch());
    object.insert(QStringLiteral("message"), message);
    object.insert(QStringLiteral("realtime_running"), m_realtimeRunning);

    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
}

void MainWidget::appendPredictionLog(
    const QString &type,
    const GestureFrameObservation *observation,
    const GesturePrediction *prediction,
    const std::optional<GestureEvent> *gestureEvent,
    const QString &message)
{
    const QString path = m_runtimeLogPath.isEmpty() ? resolveRuntimeLogPath() : m_runtimeLogPath;
    QFileInfo target(path);
    QDir().mkpath(target.absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    object.insert(
        QStringLiteral("time"),
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    object.insert(QStringLiteral("timestamp_ms"), QDateTime::currentMSecsSinceEpoch());
    object.insert(QStringLiteral("message"), message);

    if (observation)
    {
        object.insert(QStringLiteral("obs_timestamp_ms"), static_cast<qint64>(observation->timestampMs));
        object.insert(QStringLiteral("left_status"), static_cast<int>(observation->leftStatus));
        object.insert(QStringLiteral("right_status"), static_cast<int>(observation->rightStatus));
        object.insert(QStringLiteral("has_hand"), observation->hasHand);
    }

    if (prediction)
    {
        object.insert(QStringLiteral("pred_label"), prediction->label);
        object.insert(QStringLiteral("pred_display_name"), prediction->displayName);
        object.insert(QStringLiteral("confidence"), prediction->confidence);
        object.insert(QStringLiteral("margin"), prediction->margin);
        object.insert(
            QStringLiteral("p_swipe_right"),
            prediction->probabilityByLabel.value(QStringLiteral("swipe_right"), 0.0f));
        object.insert(
            QStringLiteral("p_cheese"),
            prediction->probabilityByLabel.value(QStringLiteral("cheese"), 0.0f));
    }

    if (gestureEvent && gestureEvent->has_value())
    {
        const GestureEvent &event = gestureEvent->value();
        object.insert(QStringLiteral("event_label"), event.label);
        object.insert(QStringLiteral("event_display_name"), event.displayName);
        object.insert(QStringLiteral("event_confidence"), event.confidence);
        object.insert(QStringLiteral("event_margin"), event.margin);
        object.insert(QStringLiteral("event_timestamp_ms"), static_cast<qint64>(event.timestampMs));
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.write("\n");
}

void MainWidget::stopRealtimeInference()
{
    if (m_realtimeTimer)
    {
        m_realtimeTimer->stop();
    }
    if (m_mediapipeClient)
    {
        m_mediapipeClient->stop();
    }
    m_realtimeRunning = false;
    m_realtimeFrameCount = 0;
    m_hasLatestObservation = false;
    if (m_realtimeButton)
    {
        m_realtimeButton->setText(mainWidgetUiConfig().textRealtimeStart);
    }
    if (m_trackerStatusLabel && !m_trackerStatusLabel->text().contains(QStringLiteral("停止")))
    {
        m_trackerStatusLabel->setText(mainWidgetUiConfig().textTrackerStopped);
    }
    if (!m_isClosing)
    {
        appendRuntimeLog(QStringLiteral("runtime_info"), QStringLiteral("Realtime stopped."));
    }
}

