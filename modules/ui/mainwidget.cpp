#include "mainwidget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLayoutItem>
#include <QListWidget>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QShortcut>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QScreen>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "gesture_pipeline.h"
#include "mediapipe_stream_client.h"
#include "widgets/frame_sequence_widget.h"
#include "widgets/model_opengl_widget.h"

#include "mainwidget_support.h"

MainWidget::MainWidget(QWidget *parent)
    : QWidget(parent),
      m_dragArea(0, 0, mainWidgetUiConfig().windowWidth, mainWidgetUiConfig().dragAreaHeight),
      m_gesturePipeline(std::make_unique<GesturePipeline>()),
      m_mediapipeClient(std::make_unique<MediapipeStreamClient>()),
      m_realtimeTimer(new QTimer(this))
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    setObjectName(QStringLiteral("MainWidgetRoot"));
    setWindowFlags(Qt::Window);
    setMinimumSize(960, 640);
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);

    setupMainUi();
    setupDebugPanelUi();
    applyUnifiedStyle();
    m_runtimeLogPath = resolveRuntimeLogPath();

    m_realtimeTimer->setInterval(config.realtimeTimerIntervalMs);
    connect(m_realtimeTimer, &QTimer::timeout, this, &MainWidget::onRealtimeInferenceTick);

    const QKeySequence configuredShortcut(config.toggleDebugHotkey);
    auto *toggleDebugShortcut = new QShortcut(
        configuredShortcut.isEmpty() ? QKeySequence(Qt::Key_F9) : configuredShortcut,
        this);
    toggleDebugShortcut->setContext(Qt::ApplicationShortcut);
    connect(toggleDebugShortcut, &QShortcut::activated, this, &MainWidget::toggleDebugPanel);

    auto *exitShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    exitShortcut->setContext(Qt::ApplicationShortcut);
    connect(exitShortcut, &QShortcut::activated, this, &MainWidget::requestAppQuit);

    initializeGestureRuntime();
    runSmokeInference();

    // 启动后自动开始手势识别
    QTimer::singleShot(500, this, [this]() {
        if (m_gesturePipeline && m_gesturePipeline->isReady() && !m_realtimeRunning)
        {
            toggleRealtimeInference();
        }
    });

    QTimer::singleShot(0, this, [this]() {
        setWindowState(windowState() | Qt::WindowMaximized);
    });
}

MainWidget::~MainWidget()
{
    m_isClosing = true;
    saveCameraSnapshotsToDisk();
    stopRealtimeInference();
    if (m_animationOverlay)
    {
        m_animationOverlay->close();
        delete m_animationOverlay;
        m_animationOverlay = nullptr;
    }
    if (m_debugPanel)
    {
        m_debugPanel->close();
        delete m_debugPanel;
        m_debugPanel = nullptr;
    }
}

void MainWidget::setupMainUi()
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(
        config.mainLayoutMargin,
        config.mainLayoutMargin,
        config.mainLayoutMargin,
        config.mainLayoutMargin);
    rootLayout->setSpacing(0);

    m_modelWidget = new ModelOpenGLWidget(this);
    m_modelWidget->setObjectName(QStringLiteral("ModelViewport"));
    m_modelWidget->setMinimumSize(config.modelViewportMinWidth, config.modelViewportMinHeight);
    rootLayout->addWidget(m_modelWidget, 1);

    setupAnimationOverlayUi();

    m_sequenceDirectories.clear();
    m_currentSequenceDirectoryIndex = -1;
    const QString rootPath = resolveImageSequenceRootPath();
    if (!rootPath.isEmpty())
    {
        QDir rootDir(rootPath);
        QStringList sequenceCandidates;
        const auto pushCandidate = [&sequenceCandidates](const QString &candidatePath) {
            const QString normalizedPath = QDir::cleanPath(candidatePath.trimmed());
            if (normalizedPath.isEmpty() || sequenceCandidates.contains(normalizedPath))
            {
                return;
            }
            sequenceCandidates.push_back(normalizedPath);
        };

        if (!config.imageSequenceFolderOrder.isEmpty())
        {
            for (const QString &folderName : config.imageSequenceFolderOrder)
            {
                pushCandidate(rootDir.filePath(folderName));
            }
        }
        else if (!config.defaultImageSequenceFolder.isEmpty())
        {
            pushCandidate(rootDir.filePath(config.defaultImageSequenceFolder));
        }

        const QFileInfoList sequenceDirs = rootDir.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QFileInfo &info : sequenceDirs)
        {
            pushCandidate(info.absoluteFilePath());
        }

        for (const QString &candidate : sequenceCandidates)
        {
            const QFileInfo candidateInfo(candidate);
            if (!candidateInfo.exists() || !candidateInfo.isDir())
            {
                continue;
            }
            const QString normalizedCandidate = QDir::cleanPath(candidateInfo.absoluteFilePath());
            if (!m_sequenceDirectories.contains(normalizedCandidate))
            {
                m_sequenceDirectories.push_back(normalizedCandidate);
            }
        }
    }

    bool hasSequence = false;
    for (int index = 0; index < m_sequenceDirectories.size(); ++index)
    {
        if (loadSequenceAtIndex(index))
        {
            hasSequence = true;
            break;
        }
    }
    if (!hasSequence && m_frameSequenceWidget)
    {
        m_frameSequenceWidget->clearFrames();
    }

    m_homeObjPath = resolveHomeObjPath();
    if (!m_homeObjPath.isEmpty() && m_modelWidget)
    {
        m_modelWidget->loadModel(m_homeObjPath);
    }

    auto *previousFrameShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
    previousFrameShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previousFrameShortcut, &QShortcut::activated, this, [this]() {
        switchSequenceByOffset(-1);
    });

    auto *nextFrameShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
    nextFrameShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextFrameShortcut, &QShortcut::activated, this, [this]() {
        switchSequenceByOffset(1);
    });

    m_cameraSnapshotStorePath = resolveCameraSnapshotStorePath();
    loadCameraSnapshotsFromDisk();

    updateSceneControlState();
    updateDeviceInfoDisplay();
}

void MainWidget::setupAnimationOverlayUi()
{
    if (m_animationOverlay)
    {
        return;
    }

    m_animationOverlay = new QWidget(
        nullptr,
        Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_animationOverlay->setObjectName(QStringLiteral("AnimationOverlayRoot"));
    m_animationOverlay->setAttribute(Qt::WA_StyledBackground, true);
    m_animationOverlay->setAttribute(Qt::WA_TranslucentBackground, true);
    m_animationOverlay->setFixedSize(200, 260);

    auto *overlayLayout = new QVBoxLayout(m_animationOverlay);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setSpacing(2);

    m_deviceNameLabel = new QLabel(QStringLiteral("首页"), m_animationOverlay);
    m_deviceNameLabel->setObjectName(QStringLiteral("DeviceNameLabel"));
    m_deviceNameLabel->setAlignment(Qt::AlignCenter);
    m_deviceNameLabel->setStyleSheet(QStringLiteral(
        "QLabel#DeviceNameLabel {"
        "  color: #2F3A45;"
        "  font-size: 15px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "  border: none;"
        "  padding: 4px 8px 2px 8px;"
        "}"));
    overlayLayout->addWidget(m_deviceNameLabel);

    m_deviceStatusLabel = new QLabel(QStringLiteral("就绪"), m_animationOverlay);
    m_deviceStatusLabel->setObjectName(QStringLiteral("DeviceStatusLabel"));
    m_deviceStatusLabel->setAlignment(Qt::AlignCenter);
    m_deviceStatusLabel->setStyleSheet(QStringLiteral(
        "QLabel#DeviceStatusLabel {"
        "  color: #677281;"
        "  font-size: 12px;"
        "  font-weight: 400;"
        "  background: transparent;"
        "  border: none;"
        "  padding: 2px 8px 4px 8px;"
        "}"));
    overlayLayout->addWidget(m_deviceStatusLabel);

    m_frameSequenceWidget = new FrameSequenceWidget(m_animationOverlay);
    m_frameSequenceWidget->setObjectName(QStringLiteral("AnimationSequenceWidget"));
    m_frameSequenceWidget->setMinimumSize(200, 200);
    m_frameSequenceWidget->setMaximumSize(200, 200);
    overlayLayout->addWidget(m_frameSequenceWidget);

    m_animationOverlay->setStyleSheet(
        m_animationOverlay->styleSheet() +
        QStringLiteral(
            "QWidget#AnimationOverlayRoot {"
            "  background: transparent;"
            "  border: none;"
            "}"
            "QWidget#AnimationSequenceWidget {"
            "  background: transparent;"
            "  border: none;"
            "}"
            "QLabel#SequenceFramePreview {"
            "  background: transparent;"
            "  border: none;"
            "  color: transparent;"
            "}"));

    positionAnimationOverlay();
    m_animationOverlay->show();
    m_animationOverlay->raise();
}

void MainWidget::setupDebugPanelUi()
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    m_debugPanel = new QWidget(nullptr, Qt::Tool | Qt::WindowStaysOnTopHint);
    m_debugPanel->setObjectName(QStringLiteral("DebugPanelRoot"));
    m_debugPanel->setWindowTitle(config.textDebugPanelTitle);
    m_debugPanel->setFixedSize(config.debugPanelWidth, height());
    m_debugPanel->setAttribute(Qt::WA_QuitOnClose, false);
    m_debugPanel->setAttribute(Qt::WA_StyledBackground, true);

    auto *panelRootLayout = new QVBoxLayout(m_debugPanel);
    panelRootLayout->setContentsMargins(
        config.debugPanelMargin,
        config.debugPanelMargin,
        config.debugPanelMargin,
        config.debugPanelMargin);
    panelRootLayout->setSpacing(0);

    auto *panelScrollArea = new QScrollArea(m_debugPanel);
    panelScrollArea->setWidgetResizable(true);
    panelScrollArea->setFrameShape(QFrame::NoFrame);
    panelRootLayout->addWidget(panelScrollArea, 1);

    auto *panelContent = new QWidget(panelScrollArea);
    panelScrollArea->setWidget(panelContent);
    auto *panelContentLayout = new QVBoxLayout(panelContent);
    panelContentLayout->setContentsMargins(0, 0, 0, 0);
    panelContentLayout->setSpacing(std::max(config.debugPanelSpacing, 10));

    auto *sceneCard = new QFrame(panelContent);
    sceneCard->setObjectName(QStringLiteral("DebugCard"));
    auto *sceneCardLayout = new QVBoxLayout(sceneCard);
    sceneCardLayout->setContentsMargins(14, 14, 14, 14);
    sceneCardLayout->setSpacing(std::max(config.debugPanelSpacing, 10));
    panelContentLayout->addWidget(sceneCard);

    auto *sceneTitle = new QLabel(QStringLiteral("场景与动画"), sceneCard);
    sceneTitle->setObjectName(QStringLiteral("SectionTitle"));
    sceneCardLayout->addWidget(sceneTitle);

    m_modelStatusLabel = new QLabel(config.textModelLoading, sceneCard);
    m_modelStatusLabel->setWordWrap(true);
    m_modelStatusLabel->setObjectName(QStringLiteral("SubtleLabel"));
    sceneCardLayout->addWidget(m_modelStatusLabel);

    m_predictionStatusLabel = new QLabel(config.textPredictionWaiting, sceneCard);
    m_predictionStatusLabel->setObjectName(QStringLiteral("ResultLabel"));
    sceneCardLayout->addWidget(m_predictionStatusLabel);

    m_areaLightLabel = new QLabel(config.textAreaLightOff, sceneCard);
    m_areaLightLabel->setWordWrap(true);
    m_areaLightLabel->setObjectName(QStringLiteral("SubtleLabel"));
    sceneCardLayout->addWidget(m_areaLightLabel);

    m_cameraSnapshotLabel = new QLabel(config.textSnapshotEmpty, sceneCard);
    m_cameraSnapshotLabel->setWordWrap(true);
    m_cameraSnapshotLabel->setObjectName(QStringLiteral("SubtleLabel"));
    sceneCardLayout->addWidget(m_cameraSnapshotLabel);

    auto *sequenceControlTitle = new QLabel(QStringLiteral("动画序列"), sceneCard);
    sequenceControlTitle->setObjectName(QStringLiteral("SectionTitle"));
    sceneCardLayout->addWidget(sequenceControlTitle);

    auto *sequenceControlLayout = new QHBoxLayout();
    sequenceControlLayout->setContentsMargins(0, 0, 0, 0);
    sequenceControlLayout->setSpacing(config.sequenceControlSpacing);

    m_prevSequenceButton = new QPushButton(QStringLiteral("上一段"), sceneCard);
    sequenceControlLayout->addWidget(m_prevSequenceButton);

    m_frameSequenceSlider = new QSlider(Qt::Horizontal, sceneCard);
    m_frameSequenceSlider->setRange(0, 100);
    m_frameSequenceSlider->setSingleStep(1);
    m_frameSequenceSlider->setPageStep(5);
    m_frameSequenceSlider->setTracking(true);
    sequenceControlLayout->addWidget(m_frameSequenceSlider, 1);

    m_nextSequenceButton = new QPushButton(QStringLiteral("下一段"), sceneCard);
    sequenceControlLayout->addWidget(m_nextSequenceButton);
    sceneCardLayout->addLayout(sequenceControlLayout);

    auto *cameraControlTitle = new QLabel(QStringLiteral("机位与灯光"), sceneCard);
    cameraControlTitle->setObjectName(QStringLiteral("SectionTitle"));
    sceneCardLayout->addWidget(cameraControlTitle);

    auto *cameraControlLayout = new QHBoxLayout();
    cameraControlLayout->setContentsMargins(0, 0, 0, 0);
    cameraControlLayout->setSpacing(std::max(8, config.sequenceControlSpacing - 2));
    m_saveCameraSnapshotButton = new QPushButton(QStringLiteral("添加机位"), sceneCard);
    m_deleteCameraSnapshotButton = new QPushButton(QStringLiteral("删除机位"), sceneCard);
    cameraControlLayout->addWidget(m_saveCameraSnapshotButton);
    cameraControlLayout->addWidget(m_deleteCameraSnapshotButton);
    sceneCardLayout->addLayout(cameraControlLayout);

    auto *cameraListLabel = new QLabel(QStringLiteral("机位列表"), sceneCard);
    cameraListLabel->setObjectName(QStringLiteral("SubtleLabel"));
    sceneCardLayout->addWidget(cameraListLabel);

    m_cameraSnapshotList = new QListWidget(sceneCard);
    m_cameraSnapshotList->setObjectName(QStringLiteral("CameraSnapshotList"));
    m_cameraSnapshotList->setMinimumHeight(132);
    sceneCardLayout->addWidget(m_cameraSnapshotList);

    m_toggleAreaLightButton = new QPushButton(QStringLiteral("切换顶灯"), sceneCard);
    sceneCardLayout->addWidget(m_toggleAreaLightButton);

    auto *renderCard = new QFrame(panelContent);
    renderCard->setObjectName(QStringLiteral("DebugCard"));
    auto *renderCardLayout = new QVBoxLayout(renderCard);
    renderCardLayout->setContentsMargins(14, 14, 14, 14);
    renderCardLayout->setSpacing(std::max(config.debugPanelSpacing, 10));
    panelContentLayout->addWidget(renderCard);

    auto *renderTitle = new QLabel(QStringLiteral("渲染参数"), renderCard);
    renderTitle->setObjectName(QStringLiteral("SectionTitle"));
    renderCardLayout->addWidget(renderTitle);

    struct SliderRow
    {
        QSlider *slider = nullptr;
        QLabel *valueLabel = nullptr;
    };

    const auto createSliderRow =
        [&](const QString &title, const int min, const int max, const int initialValue) -> SliderRow {
        auto *row = new QWidget(renderCard);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto *nameLabel = new QLabel(title, row);
        nameLabel->setMinimumWidth(86);
        rowLayout->addWidget(nameLabel);

        auto *slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(min, max);
        slider->setValue(initialValue);
        rowLayout->addWidget(slider, 1);

        auto *valueLabel = new QLabel(QString::number(initialValue), row);
        valueLabel->setMinimumWidth(56);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(valueLabel);

        renderCardLayout->addWidget(row);
        return SliderRow{slider, valueLabel};
    };

    const SliderRow sunHeightRow = createSliderRow(
        QStringLiteral("太阳高度"),
        -5,
        85,
        static_cast<int>(std::lround(m_sunHeightDeg)));
    m_sunHeightSlider = sunHeightRow.slider;
    sunHeightRow.valueLabel->setText(QStringLiteral("%1°").arg(m_sunHeightSlider->value()));

    const SliderRow sunAngleRow = createSliderRow(
        QStringLiteral("太阳角度"),
        0,
        360,
        static_cast<int>(std::lround(m_sunAngleDeg)));
    m_sunAngleSlider = sunAngleRow.slider;
    sunAngleRow.valueLabel->setText(QStringLiteral("%1°").arg(m_sunAngleSlider->value()));

    const SliderRow sunBrightnessRow = createSliderRow(
        QStringLiteral("太阳亮度"),
        0,
        300,
        static_cast<int>(std::lround(m_sunBrightness * 100.0f)));
    m_sunBrightnessSlider = sunBrightnessRow.slider;
    sunBrightnessRow.valueLabel->setText(
        QStringLiteral("%1.%2")
            .arg(m_sunBrightnessSlider->value() / 100)
            .arg(m_sunBrightnessSlider->value() % 100, 2, 10, QLatin1Char('0')));

    const SliderRow modelGrayRow = createSliderRow(
        QStringLiteral("模型灰度"),
        10,
        100,
        static_cast<int>(std::lround(m_modelGrayLevel * 100.0f)));
    m_modelGraySlider = modelGrayRow.slider;
    modelGrayRow.valueLabel->setText(QStringLiteral("%1%").arg(m_modelGraySlider->value()));

    const SliderRow modelOpacityRow = createSliderRow(
        QStringLiteral("模型透明"),
        10,
        100,
        static_cast<int>(std::lround(m_modelOpacity * 100.0f)));
    m_modelOpacitySlider = modelOpacityRow.slider;
    modelOpacityRow.valueLabel->setText(QStringLiteral("%1%").arg(m_modelOpacitySlider->value()));

    const SliderRow gridSizeRow = createSliderRow(
        QStringLiteral("网格大小"),
        1,
        80,
        static_cast<int>(std::lround(m_gridSize)));
    m_gridSizeSlider = gridSizeRow.slider;
    gridSizeRow.valueLabel->setText(QStringLiteral("%1").arg(m_gridSizeSlider->value()));

    const SliderRow groundHeightRow = createSliderRow(
        QStringLiteral("地面高度"),
        -300,
        300,
        static_cast<int>(std::lround(m_groundHeight * 100.0f)));
    m_groundHeightSlider = groundHeightRow.slider;
    groundHeightRow.valueLabel->setText(
        QStringLiteral("%1.%2")
            .arg(m_groundHeightSlider->value() / 100)
            .arg(std::abs(m_groundHeightSlider->value() % 100), 2, 10, QLatin1Char('0')));

    auto *debugCard = new QFrame(panelContent);
    debugCard->setObjectName(QStringLiteral("DebugCard"));
    auto *debugCardLayout = new QVBoxLayout(debugCard);
    debugCardLayout->setContentsMargins(14, 14, 14, 14);
    debugCardLayout->setSpacing(std::max(config.debugPanelSpacing, 10));
    panelContentLayout->addWidget(debugCard);

    auto *debugTitle = new QLabel(QStringLiteral("实时识别调试"), debugCard);
    debugTitle->setObjectName(QStringLiteral("SectionTitle"));
    debugCardLayout->addWidget(debugTitle);

    m_realtimeButton = new QPushButton(config.textRealtimeStart, debugCard);
    m_realtimeButton->setEnabled(false);
    connect(m_realtimeButton, &QPushButton::clicked, this, &MainWidget::toggleRealtimeInference);
    debugCardLayout->addWidget(m_realtimeButton);

    m_runtimeStatusLabel = new QLabel(config.textModelUninitialized, debugCard);
    m_runtimeStatusLabel->setWordWrap(true);
    m_runtimeStatusLabel->setObjectName(QStringLiteral("SubtleLabel"));
    debugCardLayout->addWidget(m_runtimeStatusLabel);

    m_trackerStatusLabel = new QLabel(config.textTrackerNotStarted, debugCard);
    m_trackerStatusLabel->setWordWrap(true);
    m_trackerStatusLabel->setObjectName(QStringLiteral("SubtleLabel"));
    debugCardLayout->addWidget(m_trackerStatusLabel);

    m_cameraPreviewLabel = new QLabel(config.textCameraPreviewHint, debugCard);
    m_cameraPreviewLabel->setObjectName(QStringLiteral("CameraPreview"));
    m_cameraPreviewLabel->setAlignment(Qt::AlignCenter);
    m_cameraPreviewLabel->setMinimumHeight(config.cameraPreviewMinHeight);
    debugCardLayout->addWidget(m_cameraPreviewLabel);

    m_drawSkeletonCheckBox = new QCheckBox(config.textDrawSkeleton, debugCard);
    m_drawSkeletonCheckBox->setChecked(true);
    debugCardLayout->addWidget(m_drawSkeletonCheckBox);

    auto *cameraControlRow = new QHBoxLayout();
    cameraControlRow->setSpacing(config.cameraControlSpacing);
    m_cameraIndexSpinBox = new QSpinBox(debugCard);
    m_cameraIndexSpinBox->setRange(config.cameraIndexSpinMin, config.cameraIndexSpinMax);
    m_cameraIndexSpinBox->setValue(resolvePreferredCameraIndex());
    m_applyCameraIndexButton = new QPushButton(config.textApplyCameraIndex, debugCard);
    connect(m_applyCameraIndexButton, &QPushButton::clicked, this, &MainWidget::applyCameraIndex);
    cameraControlRow->addWidget(m_cameraIndexSpinBox);
    cameraControlRow->addWidget(m_applyCameraIndexButton);
    debugCardLayout->addLayout(cameraControlRow);

    m_cameraStatusLabel = new QLabel(config.textCameraWaiting, debugCard);
    m_cameraStatusLabel->setWordWrap(true);
    m_cameraStatusLabel->setObjectName(QStringLiteral("SubtleLabel"));
    debugCardLayout->addWidget(m_cameraStatusLabel);

    auto *maskTitle = new QLabel(config.textMaskTitle, debugCard);
    maskTitle->setObjectName(QStringLiteral("SectionTitle"));
    debugCardLayout->addWidget(maskTitle);

    m_gestureMaskContainer = new QWidget(debugCard);
    m_gestureMaskLayout = new QGridLayout(m_gestureMaskContainer);
    m_gestureMaskLayout->setContentsMargins(0, 0, 0, 0);
    m_gestureMaskLayout->setHorizontalSpacing(0);
    m_gestureMaskLayout->setVerticalSpacing(config.maskLayoutVerticalSpacing);
    debugCardLayout->addWidget(m_gestureMaskContainer);

    m_lastDecisionStatusLabel = new QLabel(config.textLastDecisionNone, debugCard);
    m_lastDecisionStatusLabel->setWordWrap(true);
    m_lastDecisionStatusLabel->setObjectName(QStringLiteral("SubtleLabel"));
    debugCardLayout->addWidget(m_lastDecisionStatusLabel);

    debugCardLayout->addStretch();
    panelContentLayout->addStretch();

    populateGestureMaskControls(config.defaultMaskLabels);

    if (m_frameSequenceSlider && m_frameSequenceWidget)
    {
        m_frameSequenceSlider->setValue(m_frameSequenceWidget->normalizedProgress());
    }
    const bool hasSequence = m_currentSequenceDirectoryIndex >= 0;
    if (m_frameSequenceSlider)
    {
        m_frameSequenceSlider->setEnabled(hasSequence);
    }
    if (m_prevSequenceButton)
    {
        m_prevSequenceButton->setEnabled(hasSequence && m_sequenceDirectories.size() > 1);
    }
    if (m_nextSequenceButton)
    {
        m_nextSequenceButton->setEnabled(hasSequence && m_sequenceDirectories.size() > 1);
    }

    connect(m_frameSequenceSlider, &QSlider::valueChanged, this, [this](const int progress) {
        if (m_frameSequenceWidget)
        {
            m_frameSequenceWidget->setNormalizedProgress(progress);
        }
    });
    connect(m_prevSequenceButton, &QPushButton::clicked, this, [this]() {
        switchSequenceByOffset(-1);
    });
    connect(m_nextSequenceButton, &QPushButton::clicked, this, [this]() {
        switchSequenceByOffset(1);
    });
    connect(m_frameSequenceWidget, &FrameSequenceWidget::normalizedProgressChanged, this, [this](const int progress) {
        if (!m_frameSequenceSlider)
        {
            return;
        }
        const QSignalBlocker blocker(m_frameSequenceSlider);
        m_frameSequenceSlider->setValue(progress);
    });

    connect(m_modelWidget, &ModelOpenGLWidget::modelLoaded, this, [this](const int vertexCount) {
        if (!m_modelStatusLabel)
        {
            return;
        }
        m_modelStatusLabel->setText(
            QStringLiteral("%1：顶点 %2").arg(mainWidgetUiConfig().textModelReady).arg(vertexCount));
    });
    connect(m_modelWidget, &ModelOpenGLWidget::cameraSnapshotSaved, this, [this](const QString &, const int index) {
        m_currentCameraSnapshotIndex = index;
        updateSceneControlState();
        saveCameraSnapshotsToDisk();
    });
    connect(m_saveCameraSnapshotButton, &QPushButton::clicked, this, &MainWidget::saveCurrentCameraSnapshot);
    connect(m_deleteCameraSnapshotButton, &QPushButton::clicked, this, &MainWidget::deleteCurrentCameraSnapshot);
    connect(m_cameraSnapshotList, &QListWidget::currentRowChanged, this, [this](const int index) {
        if (index >= 0)
        {
            applyCameraSnapshotSelection(index);
        }
    });
    connect(m_toggleAreaLightButton, &QPushButton::clicked, this, &MainWidget::toggleAreaLightFromUi);

    connect(m_sunHeightSlider, &QSlider::valueChanged, this, [this, valueLabel = sunHeightRow.valueLabel](const int value) {
        m_sunHeightDeg = static_cast<float>(value);
        valueLabel->setText(QStringLiteral("%1°").arg(value));
        applyRenderControlStateToModel();
    });
    connect(m_sunAngleSlider, &QSlider::valueChanged, this, [this, valueLabel = sunAngleRow.valueLabel](const int value) {
        m_sunAngleDeg = static_cast<float>(value);
        valueLabel->setText(QStringLiteral("%1°").arg(value));
        applyRenderControlStateToModel();
    });
    connect(m_sunBrightnessSlider, &QSlider::valueChanged, this, [this, valueLabel = sunBrightnessRow.valueLabel](const int value) {
        m_sunBrightness = static_cast<float>(value) / 100.0f;
        valueLabel->setText(QStringLiteral("%1.%2").arg(value / 100).arg(value % 100, 2, 10, QLatin1Char('0')));
        applyRenderControlStateToModel();
    });
    connect(m_modelGraySlider, &QSlider::valueChanged, this, [this, valueLabel = modelGrayRow.valueLabel](const int value) {
        m_modelGrayLevel = static_cast<float>(value) / 100.0f;
        valueLabel->setText(QStringLiteral("%1%").arg(value));
        applyRenderControlStateToModel();
    });
    connect(m_modelOpacitySlider, &QSlider::valueChanged, this, [this, valueLabel = modelOpacityRow.valueLabel](const int value) {
        m_modelOpacity = static_cast<float>(value) / 100.0f;
        valueLabel->setText(QStringLiteral("%1%").arg(value));
        applyRenderControlStateToModel();
    });
    connect(m_gridSizeSlider, &QSlider::valueChanged, this, [this, valueLabel = gridSizeRow.valueLabel](const int value) {
        m_gridSize = static_cast<float>(value);
        valueLabel->setText(QStringLiteral("%1").arg(value));
        applyRenderControlStateToModel();
    });
    connect(m_groundHeightSlider, &QSlider::valueChanged, this, [this, valueLabel = groundHeightRow.valueLabel](const int value) {
        m_groundHeight = static_cast<float>(value) / 100.0f;
        valueLabel->setText(QStringLiteral("%1.%2").arg(value / 100).arg(std::abs(value % 100), 2, 10, QLatin1Char('0')));
        applyRenderControlStateToModel();
    });

    connect(m_sunHeightSlider, &QSlider::sliderReleased, this, [this]() { saveCameraSnapshotsToDisk(); });
    connect(m_sunAngleSlider, &QSlider::sliderReleased, this, [this]() { saveCameraSnapshotsToDisk(); });
    connect(m_sunBrightnessSlider, &QSlider::sliderReleased, this, [this]() { saveCameraSnapshotsToDisk(); });
    connect(m_modelGraySlider, &QSlider::sliderReleased, this, [this]() { saveCameraSnapshotsToDisk(); });
    connect(m_modelOpacitySlider, &QSlider::sliderReleased, this, [this]() { saveCameraSnapshotsToDisk(); });
    connect(m_gridSizeSlider, &QSlider::sliderReleased, this, [this]() { saveCameraSnapshotsToDisk(); });
    connect(m_groundHeightSlider, &QSlider::sliderReleased, this, [this]() { saveCameraSnapshotsToDisk(); });

    if (m_modelWidget && m_modelWidget->isModelLoaded())
    {
        const QString name = m_homeObjPath.isEmpty() ? QStringLiteral("home.obj") : QFileInfo(m_homeObjPath).fileName();
        m_modelStatusLabel->setText(QStringLiteral("%1：%2").arg(config.textModelReady, name));
    }
    else
    {
        m_modelStatusLabel->setText(config.textModelMissing);
    }

    applyRenderControlStateToModel();
    updateSceneControlState();
    m_debugPanel->hide();
}

void MainWidget::applyUnifiedStyle()
{
    const QString style = buildMainStyleSheet(mainWidgetUiConfig());
    setStyleSheet(style);
    if (m_debugPanel)
    {
        m_debugPanel->setStyleSheet(style);
    }
}
bool MainWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != this)
    {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type())
    {
    case QEvent::MouseButtonPress:
    {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            const QPoint localPos = localMousePosition(mouseEvent);
            if (isInDragArea(localPos))
            {
                m_isDragging = true;
                m_dragOffset = globalMousePosition(mouseEvent) - frameGeometry().topLeft();
                return true;
            }
        }
        break;
    }
    case QEvent::MouseMove:
    {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_isDragging && (mouseEvent->buttons() & Qt::LeftButton))
        {
            move(globalMousePosition(mouseEvent) - m_dragOffset);
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease:
    {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            m_isDragging = false;
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void MainWidget::resizeEvent(QResizeEvent *event)
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    QWidget::resizeEvent(event);
    m_dragArea = QRect(0, 0, width(), config.dragAreaHeight);
    if (m_debugPanel)
    {
        m_debugPanel->setFixedSize(config.debugPanelWidth, height());
    }
    positionDebugPanel();
    positionAnimationOverlay();
}

void MainWidget::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    positionDebugPanel();
    positionAnimationOverlay();
}

void MainWidget::closeEvent(QCloseEvent *event)
{
    m_isClosing = true;
    saveCameraSnapshotsToDisk();
    stopRealtimeInference();
    if (m_animationOverlay)
    {
        m_animationOverlay->hide();
    }
    if (m_debugPanel)
    {
        m_debugPanel->hide();
    }
    QWidget::closeEvent(event);
}

bool MainWidget::loadSequenceAtIndex(const int index)
{
    if (!m_frameSequenceWidget || m_sequenceDirectories.isEmpty())
    {
        return false;
    }
    if (index < 0 || index >= m_sequenceDirectories.size())
    {
        return false;
    }

    const QString sequenceDirectory = m_sequenceDirectories.at(index);
    if (!m_frameSequenceWidget->loadFromDirectory(sequenceDirectory))
    {
        return false;
    }

    m_currentSequenceDirectoryIndex = index;
    m_frameSequenceWidget->setToolTip(QFileInfo(sequenceDirectory).fileName());
    if (m_frameSequenceSlider)
    {
        const QSignalBlocker blocker(m_frameSequenceSlider);
        m_frameSequenceSlider->setValue(m_frameSequenceWidget->normalizedProgress());
    }
    return true;
}

void MainWidget::switchSequenceByOffset(const int offset)
{
    if (offset == 0 || m_sequenceDirectories.isEmpty())
    {
        return;
    }

    const int sequenceCount = m_sequenceDirectories.size();
    int startIndex = m_currentSequenceDirectoryIndex;
    if (startIndex < 0 || startIndex >= sequenceCount)
    {
        startIndex = 0;
    }

    int nextIndex = (startIndex + offset) % sequenceCount;
    if (nextIndex < 0)
    {
        nextIndex += sequenceCount;
    }

    if (loadSequenceAtIndex(nextIndex))
    {
        return;
    }

    // Keep navigation resilient if one candidate folder is invalid.
    for (int step = 1; step < sequenceCount; ++step)
    {
        int probeIndex = (nextIndex + step) % sequenceCount;
        if (loadSequenceAtIndex(probeIndex))
        {
            return;
        }
    }
}

void MainWidget::requestAppQuit()
{
    m_isClosing = true;
    saveCameraSnapshotsToDisk();
    stopRealtimeInference();
    if (m_animationOverlay)
    {
        m_animationOverlay->hide();
    }
    if (m_debugPanel)
    {
        m_debugPanel->hide();
    }
    qApp->quit();
}

void MainWidget::handleGestureDrivenInteraction(const GestureEvent &gestureEvent)
{
    if (!m_modelWidget)
    {
        return;
    }

    // 应用层冷却：防止同一手势或不同手势在短时间内连续触发
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - m_lastGestureActionMs < kGestureActionCooldownMs)
    {
        return;
    }

    const QString label = gestureEvent.label;

    // 右滑 → 循环切换到下一个场景相机与设备
    if (label == QStringLiteral("swipe_right"))
    {
        cycleScene(1);
        m_lastGestureActionMs = nowMs;
    }
    // 茄子 → 切换场景灯光
    else if (label == QStringLiteral("cheese"))
    {
        m_modelWidget->toggleAreaLight();
        updateSceneControlState();
        updateDeviceInfoDisplay();
        m_lastGestureActionMs = nowMs;
    }
    // 张开 → 启动当前设备动画播放
    else if (label == QStringLiteral("open"))
    {
        if (m_frameSequenceWidget && !m_frameSequenceWidget->isPlaying())
        {
            m_frameSequenceWidget->startPlayback();
        }
        updateDeviceInfoDisplay();
        m_lastGestureActionMs = nowMs;
    }
    // 握拳 → 停止当前设备动画播放
    else if (label == QStringLiteral("close"))
    {
        if (m_frameSequenceWidget && m_frameSequenceWidget->isPlaying())
        {
            m_frameSequenceWidget->stopPlayback();
        }
        updateDeviceInfoDisplay();
        m_lastGestureActionMs = nowMs;
    }
    // 左指 → 上一个场景
    else if (label == QStringLiteral("point_left"))
    {
        cycleScene(-1);
        m_lastGestureActionMs = nowMs;
    }
    // 右指 → 下一个场景
    else if (label == QStringLiteral("point_right"))
    {
        cycleScene(1);
        m_lastGestureActionMs = nowMs;
    }
}

void MainWidget::saveCurrentCameraSnapshot()
{
    if (!m_modelWidget)
    {
        return;
    }

    const int nextIndex = m_modelWidget->cameraSystem().snapshotCount() + 1;
    const QString snapshotName = QStringLiteral("机位-%1").arg(nextIndex, 2, 10, QLatin1Char('0'));
    m_modelWidget->saveCameraSnapshot(snapshotName);
    m_currentCameraSnapshotIndex = m_modelWidget->cameraSystem().snapshotCount() - 1;
    saveCameraSnapshotsToDisk();
    updateSceneControlState();
}

void MainWidget::deleteCurrentCameraSnapshot()
{
    if (!m_modelWidget)
    {
        return;
    }

    const int count = m_modelWidget->cameraSystem().snapshotCount();
    if (count <= 0)
    {
        m_currentCameraSnapshotIndex = -1;
        updateSceneControlState();
        return;
    }

    int indexToRemove = m_currentCameraSnapshotIndex;
    if (m_cameraSnapshotList && m_cameraSnapshotList->currentRow() >= 0)
    {
        indexToRemove = m_cameraSnapshotList->currentRow();
    }
    indexToRemove = std::clamp(indexToRemove, 0, count - 1);

    if (!m_modelWidget->cameraSystem().removeSnapshot(indexToRemove))
    {
        return;
    }

    const int remainingCount = m_modelWidget->cameraSystem().snapshotCount();
    if (remainingCount <= 0)
    {
        m_currentCameraSnapshotIndex = -1;
    }
    else
    {
        m_currentCameraSnapshotIndex = std::clamp(indexToRemove, 0, remainingCount - 1);
        m_modelWidget->loadCameraSnapshot(m_currentCameraSnapshotIndex);
    }

    saveCameraSnapshotsToDisk();
    updateSceneControlState();
}

void MainWidget::cycleCameraSnapshot(const int offset)
{
    if (!m_modelWidget)
    {
        return;
    }

    const int count = m_modelWidget->cameraSystem().snapshotCount();
    if (count <= 0)
    {
        m_currentCameraSnapshotIndex = -1;
        updateSceneControlState();
        return;
    }

    if (m_currentCameraSnapshotIndex < 0 || m_currentCameraSnapshotIndex >= count)
    {
        m_currentCameraSnapshotIndex = 0;
    }
    else if (offset != 0)
    {
        m_currentCameraSnapshotIndex = (m_currentCameraSnapshotIndex + offset) % count;
        if (m_currentCameraSnapshotIndex < 0)
        {
            m_currentCameraSnapshotIndex += count;
        }
    }

    m_modelWidget->loadCameraSnapshot(m_currentCameraSnapshotIndex);
    saveCameraSnapshotsToDisk();
    updateSceneControlState();
}

void MainWidget::cycleScene(int offset)
{
    if (!m_modelWidget)
    {
        return;
    }

    const int count = m_modelWidget->cameraSystem().snapshotCount();
    if (count <= 0)
    {
        switchSequenceByOffset(offset);
        updateDeviceInfoDisplay();
        return;
    }

    if (m_currentCameraSnapshotIndex < 0 || m_currentCameraSnapshotIndex >= count)
    {
        m_currentCameraSnapshotIndex = 0;
    }
    else
    {
        m_currentCameraSnapshotIndex = (m_currentCameraSnapshotIndex + offset) % count;
        if (m_currentCameraSnapshotIndex < 0)
        {
            m_currentCameraSnapshotIndex += count;
        }
    }

    // 直接跳转，不使用过渡动画
    m_modelWidget->loadCameraSnapshot(m_currentCameraSnapshotIndex);

    const int seqIdx = sequenceIndexForSnapshotIndex(m_currentCameraSnapshotIndex);
    if (seqIdx >= 0)
    {
        loadSequenceAtIndex(seqIdx);
    }

    updateSceneControlState();
    updateDeviceInfoDisplay();
}

int MainWidget::sequenceIndexForSnapshotIndex(int snapshotIndex) const
{
    // 机位0=默认首页，无对应设备序列
    // 机位1=空调(AirConditioner)=序列0, 机位2=洗衣机=序列1, ...
    if (snapshotIndex <= 0)
    {
        return -1;
    }
    const int seqIdx = snapshotIndex - 1;
    if (seqIdx >= 0 && seqIdx < m_sequenceDirectories.size())
    {
        return seqIdx;
    }
    return -1;
}

QString MainWidget::deviceNameForSnapshotIndex(int snapshotIndex) const
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    if (snapshotIndex <= 0)
    {
        return QStringLiteral("首页");
    }
    // 机位1→序列目录0, 机位2→序列目录1, ...
    const int seqIdx = snapshotIndex - 1;
    if (seqIdx >= 0 && seqIdx < m_sequenceDirectories.size())
    {
        const QString folderName = QFileInfo(m_sequenceDirectories.at(seqIdx)).fileName();
        if (config.deviceDisplayNames.contains(folderName))
        {
            return config.deviceDisplayNames.value(folderName);
        }
        return folderName;
    }
    return QStringLiteral("未知设备");
}

void MainWidget::updateDeviceInfoDisplay()
{
    if (m_deviceNameLabel)
    {
        const QString name = deviceNameForSnapshotIndex(m_currentCameraSnapshotIndex);
        m_deviceNameLabel->setText(name);
    }

    if (m_deviceStatusLabel)
    {
        QStringList statusParts;

        // 灯光状态
        if (m_modelWidget)
        {
            const bool lightOn = m_modelWidget->lightingSystem().isAreaLightEnabled();
            statusParts.push_back(lightOn ? QStringLiteral("灯光: 开")
                                          : QStringLiteral("灯光: 关"));
        }

        // 播放状态
        if (m_frameSequenceWidget)
        {
            if (m_frameSequenceWidget->isPlaying())
            {
                statusParts.push_back(QStringLiteral("播放中"));
            }
            else if (m_frameSequenceWidget->frameCount() > 0)
            {
                statusParts.push_back(QStringLiteral("已停止"));
            }
        }

        m_deviceStatusLabel->setText(statusParts.isEmpty()
                                         ? QStringLiteral("就绪")
                                         : statusParts.join(QStringLiteral(" | ")));
    }
}

void MainWidget::applyCameraSnapshotSelection(const int index)
{
    if (!m_modelWidget)
    {
        return;
    }

    const int count = m_modelWidget->cameraSystem().snapshotCount();
    if (count <= 0)
    {
        m_currentCameraSnapshotIndex = -1;
        updateSceneControlState();
        return;
    }

    const int clampedIndex = std::clamp(index, 0, count - 1);
    if (m_currentCameraSnapshotIndex == clampedIndex)
    {
        return;
    }

    m_currentCameraSnapshotIndex = clampedIndex;
    m_modelWidget->loadCameraSnapshot(m_currentCameraSnapshotIndex);

    const int seqIdx = sequenceIndexForSnapshotIndex(m_currentCameraSnapshotIndex);
    if (seqIdx >= 0)
    {
        loadSequenceAtIndex(seqIdx);
    }

    saveCameraSnapshotsToDisk();
    updateSceneControlState();
    updateDeviceInfoDisplay();
}

void MainWidget::toggleAreaLightFromUi()
{
    if (!m_modelWidget)
    {
        return;
    }
    m_modelWidget->toggleAreaLight();
    updateSceneControlState();
}

void MainWidget::applyRenderControlStateToModel()
{
    m_sunHeightDeg = std::clamp(m_sunHeightDeg, -5.0f, 85.0f);
    m_sunAngleDeg = std::fmod(m_sunAngleDeg, 360.0f);
    if (m_sunAngleDeg < 0.0f)
    {
        m_sunAngleDeg += 360.0f;
    }
    m_sunBrightness = std::clamp(m_sunBrightness, 0.0f, 3.0f);
    m_modelGrayLevel = std::clamp(m_modelGrayLevel, 0.1f, 1.0f);
    m_modelOpacity = std::clamp(m_modelOpacity, 0.1f, 1.0f);
    m_gridSize = std::clamp(m_gridSize, 1.0f, 80.0f);
    m_groundHeight = std::clamp(m_groundHeight, -3.0f, 3.0f);

    if (m_sunHeightSlider)
    {
        const QSignalBlocker blocker(m_sunHeightSlider);
        m_sunHeightSlider->setValue(static_cast<int>(std::lround(m_sunHeightDeg)));
    }
    if (m_sunAngleSlider)
    {
        const QSignalBlocker blocker(m_sunAngleSlider);
        m_sunAngleSlider->setValue(static_cast<int>(std::lround(m_sunAngleDeg)));
    }
    if (m_sunBrightnessSlider)
    {
        const QSignalBlocker blocker(m_sunBrightnessSlider);
        m_sunBrightnessSlider->setValue(static_cast<int>(std::lround(m_sunBrightness * 100.0f)));
    }
    if (m_modelGraySlider)
    {
        const QSignalBlocker blocker(m_modelGraySlider);
        m_modelGraySlider->setValue(static_cast<int>(std::lround(m_modelGrayLevel * 100.0f)));
    }
    if (m_modelOpacitySlider)
    {
        const QSignalBlocker blocker(m_modelOpacitySlider);
        m_modelOpacitySlider->setValue(static_cast<int>(std::lround(m_modelOpacity * 100.0f)));
    }
    if (m_gridSizeSlider)
    {
        const QSignalBlocker blocker(m_gridSizeSlider);
        m_gridSizeSlider->setValue(static_cast<int>(std::lround(m_gridSize)));
    }
    if (m_groundHeightSlider)
    {
        const QSignalBlocker blocker(m_groundHeightSlider);
        m_groundHeightSlider->setValue(static_cast<int>(std::lround(m_groundHeight * 100.0f)));
    }

    if (!m_modelWidget)
    {
        return;
    }

    m_modelWidget->setSunHeight(m_sunHeightDeg);
    m_modelWidget->setSunAngle(m_sunAngleDeg);
    m_modelWidget->setSunBrightness(m_sunBrightness);
    m_modelWidget->setModelGrayLevel(m_modelGrayLevel);
    m_modelWidget->setModelOpacity(m_modelOpacity);
    m_modelWidget->setGridSize(m_gridSize);
    m_modelWidget->setGroundHeight(m_groundHeight);
}

void MainWidget::updateSceneControlState()
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    if (!m_modelWidget)
    {
        return;
    }

    const bool areaLightOn = m_modelWidget->lightingSystem().isAreaLightEnabled();
    if (m_areaLightLabel)
    {
        m_areaLightLabel->setText(areaLightOn ? config.textAreaLightOn : config.textAreaLightOff);
    }
    if (m_toggleAreaLightButton)
    {
        m_toggleAreaLightButton->setText(areaLightOn ? QStringLiteral("关闭顶灯") : QStringLiteral("开启顶灯"));
    }

    const int snapshotCount = m_modelWidget->cameraSystem().snapshotCount();
    if (snapshotCount <= 0)
    {
        m_currentCameraSnapshotIndex = -1;
        if (m_cameraSnapshotLabel)
        {
            m_cameraSnapshotLabel->setText(config.textSnapshotEmpty);
        }
        if (m_cameraSnapshotList)
        {
            const QSignalBlocker blocker(m_cameraSnapshotList);
            m_cameraSnapshotList->clear();
            m_cameraSnapshotList->setEnabled(false);
        }
    }
    else
    {
        if (m_currentCameraSnapshotIndex < 0 || m_currentCameraSnapshotIndex >= snapshotCount)
        {
            m_currentCameraSnapshotIndex = 0;
        }
        const CameraSnapshot &snapshot = m_modelWidget->cameraSystem().snapshot(m_currentCameraSnapshotIndex);
        if (m_cameraSnapshotLabel)
        {
            m_cameraSnapshotLabel->setText(
                QStringLiteral("机位：%1/%2  %3")
                    .arg(m_currentCameraSnapshotIndex + 1)
                    .arg(snapshotCount)
                    .arg(snapshot.name));
        }
        if (m_cameraSnapshotList)
        {
            const QSignalBlocker blocker(m_cameraSnapshotList);
            m_cameraSnapshotList->clear();

            const auto &snapshots = m_modelWidget->cameraSystem().snapshots();
            for (int i = 0; i < snapshotCount && i < static_cast<int>(snapshots.size()); ++i)
            {
                const QString snapshotName = snapshots[static_cast<size_t>(i)].name.trimmed();
                const QString displayName = snapshotName.isEmpty()
                                                ? QStringLiteral("机位-%1").arg(i + 1, 2, 10, QLatin1Char('0'))
                                                : snapshotName;
                m_cameraSnapshotList->addItem(
                    QStringLiteral("%1. %2")
                        .arg(i + 1, 2, 10, QLatin1Char('0'))
                        .arg(displayName));
            }

            m_cameraSnapshotList->setCurrentRow(m_currentCameraSnapshotIndex);
            m_cameraSnapshotList->setEnabled(true);
        }
    }

    if (m_saveCameraSnapshotButton)
    {
        m_saveCameraSnapshotButton->setEnabled(true);
    }
    if (m_deleteCameraSnapshotButton)
    {
        m_deleteCameraSnapshotButton->setEnabled(snapshotCount > 0);
    }
}

bool MainWidget::isInDragArea(const QPoint &localPos) const
{
    return m_dragArea.contains(localPos);
}

void MainWidget::toggleDebugPanel()
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    if (!m_debugPanel)
    {
        return;
    }

    if (m_debugPanel->isVisible())
    {
        m_debugPanel->hide();
        return;
    }

    positionDebugPanel();
    m_debugPanel->show();
    m_debugPanel->raise();
    m_debugPanel->activateWindow();
    if (m_cameraStatusLabel && !m_realtimeRunning)
    {
        m_cameraStatusLabel->setText(config.textCameraWaiting);
    }
}

void MainWidget::positionDebugPanel()
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    if (!m_debugPanel)
    {
        return;
    }

    QPoint anchor = mapToGlobal(QPoint(width() + config.debugPanelGap, 0));
    QScreen *screen = QGuiApplication::screenAt(mapToGlobal(QPoint(width() / 2, height() / 2)));
    if (!screen && !QGuiApplication::screens().isEmpty())
    {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen)
    {
        m_debugPanel->move(anchor);
        return;
    }

    const QRect available = screen->availableGeometry();
    if (anchor.x() + m_debugPanel->width() > available.right())
    {
        anchor.setX(mapToGlobal(QPoint(-m_debugPanel->width(), 0)).x());
    }
    if (anchor.x() < available.left())
    {
        anchor.setX(available.left());
    }
    if (anchor.y() < available.top())
    {
        anchor.setY(available.top());
    }
    if (anchor.y() + m_debugPanel->height() > available.bottom())
    {
        anchor.setY(available.bottom() - m_debugPanel->height());
    }
    m_debugPanel->move(anchor);
    if (m_debugPanel->isVisible())
    {
        m_debugPanel->raise();
    }
}

void MainWidget::positionAnimationOverlay()
{
    if (!m_animationOverlay)
    {
        return;
    }

    const int margin = 14;
    QPoint anchor = mapToGlobal(QPoint(
        width() - m_animationOverlay->width() - margin,
        margin));

    QScreen *screen = QGuiApplication::screenAt(mapToGlobal(QPoint(width() / 2, height() / 2)));
    if (!screen && !QGuiApplication::screens().isEmpty())
    {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen)
    {
        m_animationOverlay->move(anchor);
        return;
    }

    const QRect available = screen->availableGeometry();
    const int clampedX = std::clamp(
        anchor.x(),
        available.left(),
        available.right() - m_animationOverlay->width());
    const int clampedY = std::clamp(
        anchor.y(),
        available.top(),
        available.bottom() - m_animationOverlay->height());

    m_animationOverlay->move(QPoint(clampedX, clampedY));
}

void MainWidget::applyCameraIndex()
{
    m_selectedCameraIndex = m_cameraIndexSpinBox ? m_cameraIndexSpinBox->value() : -1;
    if (m_cameraStatusLabel)
    {
        if (m_realtimeRunning)
        {
            m_cameraStatusLabel->setText(
                QStringLiteral("摄像头索引已更新为 %1（下次启动生效）").arg(m_selectedCameraIndex));
        }
        else
        {
            m_cameraStatusLabel->setText(
                QStringLiteral("摄像头索引已设置为 %1").arg(m_selectedCameraIndex));
        }
    }
}
void MainWidget::populateGestureMaskControls(const QStringList &labels)
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    if (!m_gestureMaskLayout || !m_gestureMaskContainer)
    {
        return;
    }

    while (QLayoutItem *item = m_gestureMaskLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->deleteLater();
        }
        delete item;
    }
    m_gestureMaskCheckBoxes.clear();

    QStringList filteredLabels;
    for (const QString &rawLabel : labels)
    {
        const QString label = rawLabel.trimmed();
        if (label.isEmpty() || label == QStringLiteral("idle") || label == QStringLiteral("swipe_left"))
        {
            continue;
        }
        filteredLabels.push_back(label);
    }
    if (filteredLabels.isEmpty())
    {
        filteredLabels = config.defaultMaskLabels;
    }

    int index = 0;
    for (const QString &label : filteredLabels)
    {
        const QString displayName = displayNameForLabel(label);
        const QString checkboxText = displayName == label ? label : displayName;
        auto *box = new QCheckBox(checkboxText, m_gestureMaskContainer);
        box->setChecked(m_maskedGestureLabels.contains(label));
        connect(box, &QCheckBox::toggled, this, &MainWidget::refreshMaskedGestureSet);
        m_gestureMaskLayout->addWidget(box, index, 0);
        m_gestureMaskCheckBoxes.insert(label, box);
        ++index;
    }

    refreshMaskedGestureSet();
}

void MainWidget::refreshMaskedGestureSet()
{
    m_maskedGestureLabels.clear();
    for (auto iter = m_gestureMaskCheckBoxes.constBegin(); iter != m_gestureMaskCheckBoxes.constEnd(); ++iter)
    {
        if (iter.value() && iter.value()->isChecked())
        {
            m_maskedGestureLabels.insert(iter.key());
        }
    }
}

QString MainWidget::displayNameForLabel(const QString &label) const
{
    return mainWidgetUiConfig().displayNameByLabel.value(label, label);
}

int MainWidget::resolvePreferredCameraIndex() const
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    if (m_cameraIndexSpinBox)
    {
        return m_cameraIndexSpinBox->value();
    }
    if (m_selectedCameraIndex >= 0)
    {
        return m_selectedCameraIndex;
    }

    int envIndex = 0;
    if (parseCameraIndexFromEnv(&envIndex))
    {
        return envIndex;
    }
    return config.cameraIndexSpinMin;
}

QVector<int> MainWidget::resolveCameraCandidates() const
{
    const MainWidgetUiConfig &config = mainWidgetUiConfig();
    QVector<int> candidates;
    const auto pushUnique = [&candidates](const int value) {
        if (value >= 0 && !candidates.contains(value))
        {
            candidates.push_back(value);
        }
    };

    pushUnique(resolvePreferredCameraIndex());

    int envIndex = 0;
    if (parseCameraIndexFromEnv(&envIndex))
    {
        pushUnique(envIndex);
    }

    for (int index = config.cameraProbeMinIndex; index <= config.cameraProbeMaxIndex; ++index)
    {
        pushUnique(index);
    }

    return candidates;
}

 
