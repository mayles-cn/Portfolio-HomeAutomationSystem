#ifndef HOME_AUTOMATION_GESTURE_GATE_H
#define HOME_AUTOMATION_GESTURE_GATE_H

#include <cstdint>
#include <optional>

#include "gesture_types.h"

class GestureGate
{
public:
    GestureGate();
    explicit GestureGate(const GestureGateConfig &config);

    void setConfig(const GestureGateConfig &config);
    void reset();

    std::optional<GestureEvent> process(
        const GesturePrediction &prediction,
        bool hasHand,
        std::int64_t timestampMs,
        float leftStatus = 0.0f,
        float rightStatus = 0.0f);

private:
    bool isLeftSwipeLabel(const QString &label) const;
    bool isRightSwipeLabel(const QString &label) const;
    bool isNeutralLabel(const QString &label) const;
    bool passesThresholds(const GesturePrediction &prediction) const;
    bool passesSwipeDirectionGuard(const GesturePrediction &prediction) const;
    bool passesSwipePairConfidenceGuard(const GesturePrediction &prediction) const;
    void clearPendingSwipe();
    std::optional<GestureEvent> tryEmitEvent(
        const GesturePrediction &prediction,
        std::int64_t timestampMs);
    std::optional<GestureEvent> tryEmitPendingSwipe(std::int64_t timestampMs);

    GestureGateConfig m_config;
    QString m_candidateLabel;
    int m_candidateFrames = 0;
    QString m_swipeCandidateLabel;
    int m_swipeCandidateFrames = 0;
    int m_bothHandsMissingHits = 0;
    int m_neutralFrames = 0;
    bool m_waitingForHandDisappearReset = false;
    bool m_readyForNextEvent = true;
    std::int64_t m_cooldownUntilMs = 0;

    QString m_pendingSwipeLabel;
    float m_pendingSwipeConfidence = 0.0f;
    float m_pendingSwipeMargin = 0.0f;
    int m_pendingSwipeClassId = -1;
    QString m_pendingSwipeDisplayName;
    std::int64_t m_pendingSwipeTimestampMs = 0;
};

#endif
