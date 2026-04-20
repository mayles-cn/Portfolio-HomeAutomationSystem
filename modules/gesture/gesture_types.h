#ifndef HOME_AUTOMATION_GESTURE_TYPES_H
#define HOME_AUTOMATION_GESTURE_TYPES_H

#include <array>
#include <cstdint>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct GesturePrediction
{
    int classId = -1;
    QString label;
    QString displayName;
    float confidence = 0.0f;
    float margin = 0.0f;
    QVector<float> probabilities;
    QHash<QString, float> probabilityByLabel;
};

struct GestureGateConfig
{
    float confidenceThreshold = 0.6f;
    float marginThreshold = 0.2f;
    int consecutiveFrames = 1;
    int cooldownMs = 450;
    QHash<QString, float> labelConfidenceThresholds;
    QHash<QString, float> labelMarginThresholds;
    QStringList neutralLabels;
    bool hideNeutralPredictions = true;
    bool oneShotPerAppearance = false;
    int handDisappearResetFrames = 4;
    bool requireNeutralReset = false;
    int neutralResetFrames = 2;
    bool enableSwipeDirectionGuard = true;
    float swipeDirectionMargin = 0.18f;
    bool swipeCommitOnHandDisappear = true;
    // Left swipe logic is disabled in gate; keep this label only for
    // probability diagnostics and backward-compatible JSON parsing.
    QString swipeLeftLabel = QStringLiteral("swipe_left");
    QString swipeRightLabel = QStringLiteral("swipe_right");
    int swipeConsecutiveFrames = 2;
    int swipePendingMaxAgeMs = 450;
    float swipePairMinConfidence = 0.8f;
    bool rightHandOnly = true;
    int rightHandMinStatus = 1;
    int leftHandMaxStatus = 0;
    bool rightHandAllowMirrored = true;
};

struct GestureEvent
{
    int classId = -1;
    QString label;
    QString displayName;
    float confidence = 0.0f;
    float margin = 0.0f;
    std::int64_t timestampMs = 0;
};

struct GestureFrameObservation
{
    std::array<float, 126> keypoints = {};
    float leftStatus = 0.0f;
    float rightStatus = 0.0f;
    bool hasHand = false;
    std::int64_t timestampMs = 0;
};

#endif
