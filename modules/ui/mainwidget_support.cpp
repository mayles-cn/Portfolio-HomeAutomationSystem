#include "mainwidget_support.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>

namespace
{


QJsonObject objectValue(const QJsonObject &parent, const QString &key)
{
    const QJsonValue value = parent.value(key);
    return value.isObject() ? value.toObject() : QJsonObject();
}

int intValue(
    const QJsonObject &parent,
    const QString &key,
    const int fallback,
    const int minValue,
    const int maxValue = std::numeric_limits<int>::max())
{
    bool ok = false;
    const int value = parent.value(key).toVariant().toInt(&ok);
    if (!ok)
    {
        return fallback;
    }
    return std::clamp(value, minValue, maxValue);
}

float floatValue(
    const QJsonObject &parent,
    const QString &key,
    const float fallback,
    const float minValue,
    const float maxValue)
{
    bool ok = false;
    const float value = parent.value(key).toVariant().toFloat(&ok);
    if (!ok)
    {
        return fallback;
    }
    return std::clamp(value, minValue, maxValue);
}

QString stringValue(const QJsonObject &parent, const QString &key, const QString &fallback)
{
    const QString value = parent.value(key).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

QStringList stringListValue(const QJsonObject &parent, const QString &key, const QStringList &fallback)
{
    const QJsonArray array = parent.value(key).toArray();
    QStringList result;
    for (const QJsonValue &item : array)
    {
        const QString value = item.toString().trimmed();
        if (!value.isEmpty())
        {
            result.push_back(value);
        }
    }
    return result.isEmpty() ? fallback : result;
}

QString resolveConfigPath()
{
    const QString fromEnv = qEnvironmentVariable("HOME_AUTOMATION_SETTING_JSON").trimmed();
    const QString appDirectory = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        fromEnv,
        QDir(appDirectory).filePath(QStringLiteral("config/setting.json")),
        QDir(appDirectory).filePath(QStringLiteral("../../../config/setting.json")),
        QDir(appDirectory).filePath(QStringLiteral("../../../resources/config/setting.json")),
    };

    for (const QString &candidate : candidates)
    {
        if (!candidate.isEmpty() &&
            QFileInfo::exists(candidate) &&
            QFileInfo(candidate).isFile())
        {
            return QDir::cleanPath(candidate);
        }
    }
    return QString();
}

QJsonObject loadSettingsRoot()
{
    const QString configPath = resolveConfigPath();
    if (configPath.isEmpty())
    {
        return QJsonObject();
    }

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return QJsonObject();
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return QJsonObject();
    }
    return document.object();
}

void parseDisplayNames(const QJsonObject &parent, QHash<QString, QString> *displayNameByLabel)
{
    if (!displayNameByLabel)
    {
        return;
    }
    const QJsonObject namesObject = objectValue(parent, QStringLiteral("display_names"));
    for (auto iter = namesObject.constBegin(); iter != namesObject.constEnd(); ++iter)
    {
        const QString key = iter.key().trimmed();
        const QString value = iter.value().toString().trimmed();
        if (!key.isEmpty() && !value.isEmpty())
        {
            displayNameByLabel->insert(key, value);
        }
    }
}

MainWidgetUiConfig loadMainWidgetUiConfig()
{
    MainWidgetUiConfig config;
    const QJsonObject rootObject = loadSettingsRoot();
    if (rootObject.isEmpty())
    {
        return config;
    }

    const QJsonObject uiObject = objectValue(rootObject, QStringLiteral("ui"));
    const QJsonObject mainWidgetObject = objectValue(uiObject, QStringLiteral("main_widget"));
    const QJsonObject styleObject = objectValue(mainWidgetObject, QStringLiteral("style"));
    const QJsonObject runtimeObject = objectValue(rootObject, QStringLiteral("runtime"));
    const QJsonObject pathsObject = objectValue(runtimeObject, QStringLiteral("paths"));

    config.windowWidth = intValue(mainWidgetObject, QStringLiteral("window_width"), config.windowWidth, 480, 3840);
    config.windowHeight = intValue(mainWidgetObject, QStringLiteral("window_height"), config.windowHeight, 320, 2160);
    config.debugPanelWidth =
        intValue(mainWidgetObject, QStringLiteral("debug_panel_width"), config.debugPanelWidth, 140, 1200);
    config.debugPanelGap = intValue(mainWidgetObject, QStringLiteral("debug_panel_gap"), config.debugPanelGap, 0, 500);
    config.dragAreaHeight =
        intValue(mainWidgetObject, QStringLiteral("drag_area_height"), config.dragAreaHeight, 0, 300);
    config.realtimeTimerIntervalMs = intValue(
        mainWidgetObject,
        QStringLiteral("realtime_timer_interval_ms"),
        config.realtimeTimerIntervalMs,
        1,
        1000);
    config.statusHeartbeatIntervalMs = intValue(
        mainWidgetObject,
        QStringLiteral("status_heartbeat_interval_ms"),
        config.statusHeartbeatIntervalMs,
        100,
        5000);

    config.cameraProbeMinIndex = intValue(
        mainWidgetObject,
        QStringLiteral("camera_probe_min_index"),
        config.cameraProbeMinIndex,
        0,
        32);
    config.cameraProbeMaxIndex = intValue(
        mainWidgetObject,
        QStringLiteral("camera_probe_max_index"),
        config.cameraProbeMaxIndex,
        config.cameraProbeMinIndex,
        64);
    config.cameraProbeWaitMs = intValue(
        mainWidgetObject,
        QStringLiteral("camera_probe_wait_ms"),
        config.cameraProbeWaitMs,
        10,
        5000);
    config.cameraIndexSpinMin = intValue(
        mainWidgetObject,
        QStringLiteral("camera_index_spin_min"),
        config.cameraIndexSpinMin,
        0,
        64);
    config.cameraIndexSpinMax = intValue(
        mainWidgetObject,
        QStringLiteral("camera_index_spin_max"),
        config.cameraIndexSpinMax,
        config.cameraIndexSpinMin,
        128);

    config.mainLayoutMargin = intValue(
        mainWidgetObject,
        QStringLiteral("main_layout_margin"),
        config.mainLayoutMargin,
        0,
        100);
    config.mainLayoutSpacing = intValue(
        mainWidgetObject,
        QStringLiteral("main_layout_spacing"),
        config.mainLayoutSpacing,
        0,
        100);
    config.debugPanelMargin = intValue(
        mainWidgetObject,
        QStringLiteral("debug_panel_margin"),
        config.debugPanelMargin,
        0,
        100);
    config.debugPanelSpacing = intValue(
        mainWidgetObject,
        QStringLiteral("debug_panel_spacing"),
        config.debugPanelSpacing,
        0,
        100);
    config.cameraPreviewMinHeight = intValue(
        mainWidgetObject,
        QStringLiteral("camera_preview_min_height"),
        config.cameraPreviewMinHeight,
        40,
        1000);
    config.cameraControlSpacing = intValue(
        mainWidgetObject,
        QStringLiteral("camera_control_spacing"),
        config.cameraControlSpacing,
        0,
        40);
    config.maskLayoutVerticalSpacing = intValue(
        mainWidgetObject,
        QStringLiteral("mask_layout_vertical_spacing"),
        config.maskLayoutVerticalSpacing,
        0,
        40);
    config.sequenceControlSpacing = intValue(
        mainWidgetObject,
        QStringLiteral("sequence_control_spacing"),
        config.sequenceControlSpacing,
        0,
        64);
    config.sequenceStepButtonWidth = intValue(
        mainWidgetObject,
        QStringLiteral("sequence_step_button_width"),
        config.sequenceStepButtonWidth,
        16,
        120);
    config.sequenceStepButtonHeight = intValue(
        mainWidgetObject,
        QStringLiteral("sequence_step_button_height"),
        config.sequenceStepButtonHeight,
        16,
        120);
    config.sequencePlayerWidthRatio = floatValue(
        mainWidgetObject,
        QStringLiteral("sequence_player_width_ratio"),
        config.sequencePlayerWidthRatio,
        0.2f,
        1.0f);
    config.sequencePlayerHeightRatio = floatValue(
        mainWidgetObject,
        QStringLiteral("sequence_player_height_ratio"),
        config.sequencePlayerHeightRatio,
        0.2f,
        1.0f);
    config.sceneControlSpacing = intValue(
        mainWidgetObject,
        QStringLiteral("scene_control_spacing"),
        config.sceneControlSpacing,
        0,
        64);
    config.modelViewportMinWidth = intValue(
        mainWidgetObject,
        QStringLiteral("model_viewport_min_width"),
        config.modelViewportMinWidth,
        240,
        2000);
    config.modelViewportMinHeight = intValue(
        mainWidgetObject,
        QStringLiteral("model_viewport_min_height"),
        config.modelViewportMinHeight,
        180,
        1600);
    config.snapshotTransitionMs = intValue(
        mainWidgetObject,
        QStringLiteral("snapshot_transition_ms"),
        config.snapshotTransitionMs,
        0,
        5000);

    config.toggleDebugHotkey =
        stringValue(mainWidgetObject, QStringLiteral("toggle_debug_hotkey"), config.toggleDebugHotkey);
    config.textPredictionWaiting =
        stringValue(mainWidgetObject, QStringLiteral("text_prediction_waiting"), config.textPredictionWaiting);
    config.textPredictionWaitingAnimated = stringValue(
        mainWidgetObject,
        QStringLiteral("text_prediction_waiting_animated"),
        config.textPredictionWaitingAnimated);
    config.textPredictionFailed =
        stringValue(mainWidgetObject, QStringLiteral("text_prediction_failed"), config.textPredictionFailed);
    config.textModelUninitialized =
        stringValue(mainWidgetObject, QStringLiteral("text_model_uninitialized"), config.textModelUninitialized);
    config.textTrackerStopped =
        stringValue(mainWidgetObject, QStringLiteral("text_tracker_stopped"), config.textTrackerStopped);
    config.textTrackerNotStarted =
        stringValue(mainWidgetObject, QStringLiteral("text_tracker_not_started"), config.textTrackerNotStarted);
    config.textCameraPreviewHint =
        stringValue(mainWidgetObject, QStringLiteral("text_camera_preview_hint"), config.textCameraPreviewHint);
    config.textDrawSkeleton =
        stringValue(mainWidgetObject, QStringLiteral("text_draw_skeleton"), config.textDrawSkeleton);
    config.textApplyCameraIndex =
        stringValue(mainWidgetObject, QStringLiteral("text_apply_camera_index"), config.textApplyCameraIndex);
    config.textCameraWaiting =
        stringValue(mainWidgetObject, QStringLiteral("text_camera_waiting"), config.textCameraWaiting);
    config.textCameraStopped =
        stringValue(mainWidgetObject, QStringLiteral("text_camera_stopped"), config.textCameraStopped);
    config.textMaskTitle = stringValue(mainWidgetObject, QStringLiteral("text_mask_title"), config.textMaskTitle);
    config.textLastDecisionNone =
        stringValue(mainWidgetObject, QStringLiteral("text_last_decision_none"), config.textLastDecisionNone);
    config.textRealtimeStart =
        stringValue(mainWidgetObject, QStringLiteral("text_realtime_start"), config.textRealtimeStart);
    config.textRealtimeStop =
        stringValue(mainWidgetObject, QStringLiteral("text_realtime_stop"), config.textRealtimeStop);
    config.textDebugPanelTitle =
        stringValue(mainWidgetObject, QStringLiteral("text_debug_panel_title"), config.textDebugPanelTitle);
    config.textModelLoading =
        stringValue(mainWidgetObject, QStringLiteral("text_model_loading"), config.textModelLoading);
    config.textModelReady =
        stringValue(mainWidgetObject, QStringLiteral("text_model_ready"), config.textModelReady);
    config.textModelMissing =
        stringValue(mainWidgetObject, QStringLiteral("text_model_missing"), config.textModelMissing);
    config.textSnapshotEmpty =
        stringValue(mainWidgetObject, QStringLiteral("text_snapshot_empty"), config.textSnapshotEmpty);
    config.textAreaLightOn =
        stringValue(mainWidgetObject, QStringLiteral("text_area_light_on"), config.textAreaLightOn);
    config.textAreaLightOff =
        stringValue(mainWidgetObject, QStringLiteral("text_area_light_off"), config.textAreaLightOff);

    config.fontFamily = stringValue(styleObject, QStringLiteral("font_family"), config.fontFamily);
    config.baseFontSize = intValue(styleObject, QStringLiteral("font_size"), config.baseFontSize, 8, 48);
    config.resultFontSize = intValue(styleObject, QStringLiteral("result_font_size"), config.resultFontSize, 12, 96);
    config.borderRadiusPx = intValue(styleObject, QStringLiteral("border_radius_px"), config.borderRadiusPx, 0, 24);
    config.checkboxIndicatorSize = intValue(
        styleObject,
        QStringLiteral("checkbox_indicator_size"),
        config.checkboxIndicatorSize,
        8,
        64);
    config.checkboxSpacing = intValue(styleObject, QStringLiteral("checkbox_spacing"), config.checkboxSpacing, 0, 32);
    config.buttonPadding = stringValue(styleObject, QStringLiteral("button_padding"), config.buttonPadding);
    config.spinBoxPadding = stringValue(styleObject, QStringLiteral("spinbox_padding"), config.spinBoxPadding);

    config.colorWindowBackground =
        stringValue(styleObject, QStringLiteral("color_window_background"), config.colorWindowBackground);
    config.colorWindowText = stringValue(styleObject, QStringLiteral("color_window_text"), config.colorWindowText);
    config.colorResultText = stringValue(styleObject, QStringLiteral("color_result_text"), config.colorResultText);
    config.colorCameraPreviewBackground = stringValue(
        styleObject,
        QStringLiteral("color_camera_preview_background"),
        config.colorCameraPreviewBackground);
    config.colorCameraPreviewBorder =
        stringValue(styleObject, QStringLiteral("color_camera_preview_border"), config.colorCameraPreviewBorder);
    config.colorButtonBackground =
        stringValue(styleObject, QStringLiteral("color_button_background"), config.colorButtonBackground);
    config.colorButtonBorder = stringValue(styleObject, QStringLiteral("color_button_border"), config.colorButtonBorder);
    config.colorButtonText = stringValue(styleObject, QStringLiteral("color_button_text"), config.colorButtonText);
    config.colorButtonHoverBackground = stringValue(
        styleObject,
        QStringLiteral("color_button_hover_background"),
        config.colorButtonHoverBackground);
    config.colorButtonDisabledBackground = stringValue(
        styleObject,
        QStringLiteral("color_button_disabled_background"),
        config.colorButtonDisabledBackground);
    config.colorButtonDisabledBorder = stringValue(
        styleObject,
        QStringLiteral("color_button_disabled_border"),
        config.colorButtonDisabledBorder);
    config.colorButtonDisabledText = stringValue(
        styleObject,
        QStringLiteral("color_button_disabled_text"),
        config.colorButtonDisabledText);
    config.colorCheckboxIndicatorBorder = stringValue(
        styleObject,
        QStringLiteral("color_checkbox_indicator_border"),
        config.colorCheckboxIndicatorBorder);
    config.colorCheckboxIndicatorBackground = stringValue(
        styleObject,
        QStringLiteral("color_checkbox_indicator_background"),
        config.colorCheckboxIndicatorBackground);
    config.colorCheckboxCheckedBackground = stringValue(
        styleObject,
        QStringLiteral("color_checkbox_checked_background"),
        config.colorCheckboxCheckedBackground);
    config.colorCheckboxCheckedBorder = stringValue(
        styleObject,
        QStringLiteral("color_checkbox_checked_border"),
        config.colorCheckboxCheckedBorder);
    config.colorSpinBoxBackground =
        stringValue(styleObject, QStringLiteral("color_spinbox_background"), config.colorSpinBoxBackground);
    config.colorSpinBoxBorder =
        stringValue(styleObject, QStringLiteral("color_spinbox_border"), config.colorSpinBoxBorder);
    config.colorSpinBoxText = stringValue(styleObject, QStringLiteral("color_spinbox_text"), config.colorSpinBoxText);

    config.skeletonBackgroundColor =
        stringValue(styleObject, QStringLiteral("skeleton_background_color"), config.skeletonBackgroundColor);
    config.skeletonLeftColor =
        stringValue(styleObject, QStringLiteral("skeleton_left_color"), config.skeletonLeftColor);
    config.skeletonRightColor =
        stringValue(styleObject, QStringLiteral("skeleton_right_color"), config.skeletonRightColor);
    config.skeletonLineWidth = floatValue(
        styleObject,
        QStringLiteral("skeleton_line_width"),
        config.skeletonLineWidth,
        0.5f,
        12.0f);
    config.skeletonPointSize = floatValue(
        styleObject,
        QStringLiteral("skeleton_point_size"),
        config.skeletonPointSize,
        1.0f,
        20.0f);

    config.defaultMaskLabels =
        stringListValue(mainWidgetObject, QStringLiteral("mask_default_labels"), config.defaultMaskLabels);
    config.defaultImageSequenceFolder = stringValue(
        pathsObject,
        QStringLiteral("default_image_sequence_folder"),
        config.defaultImageSequenceFolder);
    parseDisplayNames(mainWidgetObject, &config.displayNameByLabel);

    const QJsonObject deviceNamesObject = objectValue(mainWidgetObject, QStringLiteral("device_display_names"));
    for (auto iter = deviceNamesObject.constBegin(); iter != deviceNamesObject.constEnd(); ++iter)
    {
        const QString key = iter.key().trimmed();
        const QString value = iter.value().toString().trimmed();
        if (!key.isEmpty() && !value.isEmpty())
        {
            config.deviceDisplayNames.insert(key, value);
        }
    }

    config.gestureModelCandidates = stringListValue(
        pathsObject,
        QStringLiteral("model_candidates"),
        config.gestureModelCandidates);
    config.mediapipeBridgeCandidates = stringListValue(
        pathsObject,
        QStringLiteral("bridge_candidates"),
        config.mediapipeBridgeCandidates);
    config.handLandmarkerTaskCandidates = stringListValue(
        pathsObject,
        QStringLiteral("hand_task_candidates"),
        config.handLandmarkerTaskCandidates);
    config.imageSequenceFolderOrder = stringListValue(
        pathsObject,
        QStringLiteral("image_sequence_folder_order"),
        config.imageSequenceFolderOrder);
    config.imageSequenceRootCandidates = stringListValue(
        pathsObject,
        QStringLiteral("image_sequence_root_candidates"),
        config.imageSequenceRootCandidates);
    config.homeObjCandidates = stringListValue(
        pathsObject,
        QStringLiteral("home_obj_candidates"),
        config.homeObjCandidates);
    config.runtimeLogDefault =
        stringValue(pathsObject, QStringLiteral("runtime_log_default"), config.runtimeLogDefault);
    config.cameraSnapshotStore =
        stringValue(pathsObject, QStringLiteral("camera_snapshot_store"), config.cameraSnapshotStore);

    return config;
}

const MainWidgetUiConfig &mainWidgetUiConfigInternal()
{
    static const MainWidgetUiConfig config = loadMainWidgetUiConfig();
    return config;
}

QPoint localMousePositionInternal(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

QPoint globalMousePositionInternal(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->globalPosition().toPoint();
#else
    return event->globalPos();
#endif
}

bool parseCameraIndexFromEnvInternal(int *cameraIndex)
{
    if (!cameraIndex)
    {
        return false;
    }

    const QString envValue = qEnvironmentVariable("HOME_AUTOMATION_CAMERA_INDEX").trimmed();
    if (envValue.isEmpty())
    {
        return false;
    }

    bool ok = false;
    const int value = envValue.toInt(&ok);
    if (!ok || value < 0)
    {
        return false;
    }

    *cameraIndex = value;
    return true;
}

QString buildMainStyleSheetInternal(const MainWidgetUiConfig &config)
{
    const int cardRadius = 0;
    const int buttonRadius = 0;
    const int indicatorRadius = 0;

    QString style;
    style += QStringLiteral(
                 "QWidget {"
                 "  color: %1;"
                 "  font-family: '%2';"
                 "  font-size: %3px;"
                 "}"
                 "QWidget#MainWidgetRoot,"
                 "QWidget#DebugPanelRoot {"
                 "  background-color: %4;"
                 "}")
                 .arg(config.colorWindowText, config.fontFamily)
                 .arg(config.baseFontSize)
                 .arg(config.colorWindowBackground);
    style += QStringLiteral(
                 "QFrame#PrimaryCard,"
                 "QFrame#ControlCard,"
                 "QFrame#DebugCard {"
                 "  background-color: #FFFFFF;"
                 "  border: 1px solid %1;"
                 "  border-radius: %2px;"
                 "}")
                 .arg(config.colorCameraPreviewBorder)
                 .arg(cardRadius);
    style += QStringLiteral(
                 "QLabel#PageTitle {"
                 "  font-size: %1px;"
                 "  font-weight: 700;"
                 "  color: #141A22;"
                 "}"
                 "QLabel#SectionTitle {"
                 "  font-size: %2px;"
                 "  font-weight: 600;"
                 "  color: #222B36;"
                 "}"
                 "QLabel#SubtleLabel {"
                 "  color: #677281;"
                 "  font-size: %3px;"
                 "}")
                 .arg(std::max(18, config.baseFontSize + 7))
                 .arg(std::max(14, config.baseFontSize + 2))
                 .arg(std::max(11, config.baseFontSize - 1));
    style += QStringLiteral(
                 "QLabel#ResultLabel {"
                 "  font-size: %1px;"
                 "  font-weight: 700;"
                 "  color: %2;"
                  "  border: none;"
                 "  padding-top: 4px;"
                 "}")
                 .arg(std::max(18, config.resultFontSize))
                 .arg(config.colorResultText);
    style += QStringLiteral(
                 "QLabel#CameraPreview {"
                 "  background-color: %1;"
                 "  border: 1px solid %2;"
                 "  border-radius: %3px;"
                 "  color: #7C8696;"
                 "}")
                 .arg(config.colorCameraPreviewBackground, config.colorCameraPreviewBorder)
                 .arg(cardRadius);
    style += QStringLiteral(
                 "QLabel#SequenceFramePreview {"
                 "  background-color: #FBFCFE;"
                 "  border: 1px solid %1;"
                 "  border-radius: %2px;"
                 "  color: #7C8696;"
                 "}")
                 .arg(config.colorCameraPreviewBorder)
                 .arg(cardRadius);
    style += QStringLiteral(
                 "QOpenGLWidget#ModelViewport {"
                 "  border: 1px solid %1;"
                 "  border-radius: %2px;"
                 "  background-color: #FFFFFF;"
                 "}")
                 .arg(config.colorCameraPreviewBorder)
                 .arg(cardRadius);
    style += QStringLiteral(
                 "QPushButton {"
                 "  background-color: %1;"
                 "  border: 1px solid %2;"
                 "  color: %3;"
                  "  padding: %4;"
                  "  border-radius: %5px;"
                 "  min-height: 34px;"
                 "}")
                 .arg(
                     config.colorButtonBackground,
                     config.colorButtonBorder,
                     config.colorButtonText,
                     config.buttonPadding)
                 .arg(buttonRadius);
    style += QStringLiteral(
                 "QPushButton:hover {"
                 "  background-color: %1;"
                 "  border-color: #C5D1DF;"
                 "}")
                 .arg(config.colorButtonHoverBackground);
    style += QStringLiteral(
                 "QPushButton:pressed {"
                 "  background-color: #E8EEF8;"
                 "  border-color: #B7C7DA;"
                 "}");
    style += QStringLiteral(
                 "QPushButton:disabled {"
                 "  background-color: %1;"
                 "  border-color: %2;"
                 "  color: %3;"
                 "}")
                 .arg(
                     config.colorButtonDisabledBackground,
                     config.colorButtonDisabledBorder,
                     config.colorButtonDisabledText);
    style += QStringLiteral(
                 "QPushButton#SequenceStepButton {"
                 "  background-color: #F8FAFD;"
                 "  border: 1px solid %1;"
                 "  color: %1;"
                 "  font-size: %2px;"
                 "  font-weight: 600;"
                 "  padding: 0;"
                 "  min-width: %3px;"
                 "  min-height: %4px;"
                 "  border-radius: %5px;"
                 "}")
                 .arg(config.colorButtonBorder)
                 .arg(std::max(12, config.baseFontSize + 3))
                 .arg(config.sequenceStepButtonWidth)
                 .arg(config.sequenceStepButtonHeight)
                 .arg(buttonRadius);
    style += QStringLiteral(
                 "QPushButton#SequenceStepButton:hover {"
                 "  background-color: #ECF3FF;"
                 "  border-color: %1;"
                 "  color: %2;"
                 "}")
                 .arg(config.colorResultText, config.colorResultText);
    style += QStringLiteral(
                 "QCheckBox {"
                 "  spacing: %1px;"
                  "  border: none;"
                 "  color: #394451;"
                 "}")
                 .arg(config.checkboxSpacing);
    style += QStringLiteral(
                 "QCheckBox::indicator {"
                 "  width: %1px;"
                 "  height: %1px;"
                 "  border: 1px solid %2;"
                 "  background-color: %3;"
                 "  border-radius: %4px;"
                 "}")
                 .arg(config.checkboxIndicatorSize)
                 .arg(config.colorCheckboxIndicatorBorder, config.colorCheckboxIndicatorBackground)
                 .arg(indicatorRadius);
    style += QStringLiteral(
                 "QCheckBox::indicator:hover {"
                 "  border-color: #9EADBE;"
                 "}");
    style += QStringLiteral(
                 "QCheckBox::indicator:checked {"
                 "  background-color: %1;"
                 "  border: 1px solid %2;"
                 "}")
                 .arg(config.colorCheckboxCheckedBackground, config.colorCheckboxCheckedBorder);
    style += QStringLiteral(
                 "QSpinBox {"
                 "  background-color: %1;"
                 "  border: 1px solid %2;"
                 "  color: %3;"
                 "  padding: %4;"
                 "  border-radius: %5px;"
                 "}")
                 .arg(
                     config.colorSpinBoxBackground,
                     config.colorSpinBoxBorder,
                     config.colorSpinBoxText,
                     config.spinBoxPadding)
                 .arg(buttonRadius);
    style += QStringLiteral(
                 "QSpinBox:focus {"
                 "  border-color: %1;"
                 "}")
                 .arg(config.colorResultText);
    style += QStringLiteral(
                 "QSlider::groove:horizontal {"
                 "  height: 6px;"
                 "  background: #E4EAF2;"
                 "  border-radius: 0px;"
                 "}"
                 "QSlider::sub-page:horizontal {"
                 "  background: %1;"
                 "  border-radius: 0px;"
                 "}"
                 "QSlider::add-page:horizontal {"
                 "  background: #E4EAF2;"
                 "  border-radius: 0px;"
                 "  border: none;"
                 "}")
                 .arg(config.colorResultText);
    style += QStringLiteral(
                 "QSlider::handle:horizontal {"
                 "  width: 16px;"
                 "  margin: -6px 0;"
                 "  background: #FFFFFF;"
                 "  border: 1px solid #C7D2DF;"
                 "  border-radius: 0px;"
                 "}"
                 "QSlider::handle:horizontal:hover {"
                 "  border-color: %1;"
                 "}")
                 .arg(config.colorResultText);
    style += QStringLiteral(
                 "QToolTip {"
                 "  background: #FFFFFF;"
                 "  color: #2F3A45;"
                 "  border: 1px solid %1;"
                 "  padding: 4px 6px;"
                 "}")
                 .arg(config.colorCameraPreviewBorder);
    return style;
}

} // namespace

const MainWidgetUiConfig &mainWidgetUiConfig()
{
    return mainWidgetUiConfigInternal();
}

QPoint localMousePosition(const QMouseEvent *event)
{
    return localMousePositionInternal(event);
}

QPoint globalMousePosition(const QMouseEvent *event)
{
    return globalMousePositionInternal(event);
}

bool parseCameraIndexFromEnv(int *cameraIndex)
{
    return parseCameraIndexFromEnvInternal(cameraIndex);
}

QString buildMainStyleSheet(const MainWidgetUiConfig &config)
{
    return buildMainStyleSheetInternal(config);
}
