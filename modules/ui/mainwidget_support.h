#ifndef HOME_AUTOMATION_MAINWIDGET_SUPPORT_H
#define HOME_AUTOMATION_MAINWIDGET_SUPPORT_H

#include <QHash>
#include <QPoint>
#include <QString>
#include <QStringList>

class QMouseEvent;

struct MainWidgetUiConfig
{
    int windowWidth = 1000;
    int windowHeight = 560;
    int debugPanelWidth = 320;
    int debugPanelGap = 14;
    int dragAreaHeight = 58;
    int realtimeTimerIntervalMs = 16;
    int statusHeartbeatIntervalMs = 400;

    int cameraProbeMinIndex = 0;
    int cameraProbeMaxIndex = 5;
    int cameraProbeWaitMs = 180;
    int cameraIndexSpinMin = 0;
    int cameraIndexSpinMax = 10;

    int mainLayoutMargin = 28;
    int mainLayoutSpacing = 20;
    int debugPanelMargin = 16;
    int debugPanelSpacing = 12;
    int cameraPreviewMinHeight = 176;
    int cameraControlSpacing = 10;
    int maskLayoutVerticalSpacing = 8;
    int sequenceControlSpacing = 12;
    int sequenceStepButtonWidth = 40;
    int sequenceStepButtonHeight = 40;
    float sequencePlayerWidthRatio = 0.5f;
    float sequencePlayerHeightRatio = 0.35f;
    int sceneControlSpacing = 22;
    int modelViewportMinWidth = 700;
    int modelViewportMinHeight = 420;
    int snapshotTransitionMs = 800;

    QString toggleDebugHotkey = QStringLiteral("F9");

    QString textPredictionWaiting = QStringLiteral("当前识别：等待中");
    QString textPredictionWaitingAnimated = QStringLiteral("当前识别：等待中...");
    QString textPredictionFailed = QStringLiteral("当前识别：推理失败");
    QString textModelUninitialized = QStringLiteral("模型：未初始化");
    QString textTrackerStopped = QStringLiteral("跟踪：已停止");
    QString textTrackerNotStarted = QStringLiteral("跟踪：未启动");
    QString textCameraPreviewHint = QStringLiteral("F9 打开调试面板");
    QString textDrawSkeleton = QStringLiteral("绘制骨架");
    QString textApplyCameraIndex = QStringLiteral("应用索引");
    QString textCameraWaiting = QStringLiteral("骨架画面：等待实时识别");
    QString textCameraStopped = QStringLiteral("骨架画面：已停止实时识别");
    QString textMaskTitle = QStringLiteral("屏蔽手势输出");
    QString textLastDecisionNone = QStringLiteral("最近触发：无");
    QString textRealtimeStart = QStringLiteral("启动实时识别");
    QString textRealtimeStop = QStringLiteral("停止实时识别");
    QString textDebugPanelTitle = QStringLiteral("Gesture Debug Panel");
    QString textModelLoading = QStringLiteral("模型状态：加载中");
    QString textModelReady = QStringLiteral("模型状态：已加载");
    QString textModelMissing = QStringLiteral("模型状态：未找到 home.obj");
    QString textSnapshotEmpty = QStringLiteral("机位：无");
    QString textAreaLightOn = QStringLiteral("顶灯：开启");
    QString textAreaLightOff = QStringLiteral("顶灯：关闭");

    QString fontFamily = QStringLiteral("Microsoft YaHei");
    int baseFontSize = 14;
    int resultFontSize = 30;
    int borderRadiusPx = 14;
    int checkboxIndicatorSize = 12;
    int checkboxSpacing = 4;
    QString buttonPadding = QStringLiteral("8px 16px");
    QString spinBoxPadding = QStringLiteral("4px 10px");

    QString colorWindowBackground = QStringLiteral("#F5F6F8");
    QString colorWindowText = QStringLiteral("#2B3138");
    QString colorResultText = QStringLiteral("#2C6BED");
    QString colorCameraPreviewBackground = QStringLiteral("#F8FAFD");
    QString colorCameraPreviewBorder = QStringLiteral("#DFE5ED");
    QString colorButtonBackground = QStringLiteral("#FFFFFF");
    QString colorButtonBorder = QStringLiteral("#D5DEE8");
    QString colorButtonText = QStringLiteral("#2F3A45");
    QString colorButtonHoverBackground = QStringLiteral("#F1F5FB");
    QString colorButtonDisabledBackground = QStringLiteral("#F2F4F7");
    QString colorButtonDisabledBorder = QStringLiteral("#E4E9F0");
    QString colorButtonDisabledText = QStringLiteral("#9AA4B2");
    QString colorCheckboxIndicatorBorder = QStringLiteral("#C5D0DC");
    QString colorCheckboxIndicatorBackground = QStringLiteral("#FFFFFF");
    QString colorCheckboxCheckedBackground = QStringLiteral("#2C6BED");
    QString colorCheckboxCheckedBorder = QStringLiteral("#2C6BED");
    QString colorSpinBoxBackground = QStringLiteral("#FFFFFF");
    QString colorSpinBoxBorder = QStringLiteral("#D5DEE8");
    QString colorSpinBoxText = QStringLiteral("#2F3A45");

    QString skeletonBackgroundColor = QStringLiteral("#F8FAFD");
    QString skeletonLeftColor = QStringLiteral("#2C6BED");
    QString skeletonRightColor = QStringLiteral("#EF9F32");
    float skeletonLineWidth = 2.0f;
    float skeletonPointSize = 5.0f;

    QHash<QString, QString> displayNameByLabel = {
        {QStringLiteral("idle"), QStringLiteral("无意义")},
        {QStringLiteral("open"), QStringLiteral("张开")},
        {QStringLiteral("close"), QStringLiteral("关闭")},
        {QStringLiteral("swipe_right"), QStringLiteral("右滑")},
        {QStringLiteral("point_left"), QStringLiteral("左指")},
        {QStringLiteral("point_right"), QStringLiteral("右指")},
        {QStringLiteral("cheese"), QStringLiteral("茄子")},
    };

    QStringList defaultMaskLabels = {
        QStringLiteral("open"),
        QStringLiteral("close"),
        QStringLiteral("swipe_right"),
        QStringLiteral("point_left"),
        QStringLiteral("point_right"),
        QStringLiteral("cheese"),
    };
    QString defaultImageSequenceFolder = QStringLiteral("AirConditioner");

    QStringList gestureModelCandidates = {
        QStringLiteral("models/cpp_model.json"),
        QStringLiteral("../../../resources/models/cpp_model.json"),
    };
    QStringList mediapipeBridgeCandidates = {
        QStringLiteral("tools/hand_landmarker_stream.exe"),
        QStringLiteral("../../../tools/hand_landmarker_stream.exe"),
        QStringLiteral("D:/SomeCppProjects/mediapipe/bazel-bin/mediapipe/examples/desktop/hand_tracking/hand_landmarker_stream.exe"),
    };
    QStringList handLandmarkerTaskCandidates = {
        QStringLiteral("models/hand_landmarker.task"),
        QStringLiteral("../../../resources/models/hand_landmarker.task"),
    };
    QHash<QString, QString> deviceDisplayNames = {
        {QStringLiteral("AirConditioner"), QStringLiteral("\u7A7A\u8C03")},
        {QStringLiteral("WashMachine"), QStringLiteral("\u6D17\u8863\u673A")},
        {QStringLiteral("KitchenHood"), QStringLiteral("\u6CB9\u70DF\u673A")},
        {QStringLiteral("ElectricFan"), QStringLiteral("\u98CE\u6247")},
        {QStringLiteral("Sounder"), QStringLiteral("\u97F3\u54CD")},
        {QStringLiteral("Curtain"), QStringLiteral("\u7A97\u5E18")},
        {QStringLiteral("CoffeeMarker"), QStringLiteral("\u5496\u5561\u673A")},
        {QStringLiteral("HomeKey"), QStringLiteral("\u667A\u80FD\u94A5\u5319")},
    };

    QStringList imageSequenceFolderOrder = {
        QStringLiteral("AirConditioner"),
        QStringLiteral("WashMachine"),
        QStringLiteral("KitchenHood"),
        QStringLiteral("ElectricFan"),
        QStringLiteral("Sounder"),
        QStringLiteral("Curtain"),
        QStringLiteral("CoffeeMarker"),
        QStringLiteral("HomeKey"),
    };
    QStringList imageSequenceRootCandidates = {
        QStringLiteral("images"),
        QStringLiteral("../../../resources/images"),
    };
    QString runtimeLogDefault = QStringLiteral("logs/realtime_prediction.jsonl");
    QStringList homeObjCandidates = {
        QStringLiteral("models/home.obj"),
        QStringLiteral("../../../resources/models/home.obj"),
    };
    QString cameraSnapshotStore = QStringLiteral("config/camera_snapshots.json");
};

const MainWidgetUiConfig &mainWidgetUiConfig();
QPoint localMousePosition(const QMouseEvent *event);
QPoint globalMousePosition(const QMouseEvent *event);
bool parseCameraIndexFromEnv(int *cameraIndex);
QString buildMainStyleSheet(const MainWidgetUiConfig &config);

#endif

