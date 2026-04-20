#include "gesture_gate.h"

#include <algorithm>

#include <QDateTime>

GestureGate::GestureGate()
{
    reset();
}

GestureGate::GestureGate(const GestureGateConfig &config)
    : m_config(config)
{
    reset();
}

void GestureGate::setConfig(const GestureGateConfig &config)
{
    m_config = config;
    reset();
}

void GestureGate::reset()
{
    m_candidateLabel.clear();
    m_candidateFrames = 0;
    m_swipeCandidateLabel.clear();
    m_swipeCandidateFrames = 0;
    m_bothHandsMissingHits = 0;
    m_neutralFrames = 0;
    m_waitingForHandDisappearReset = false;
    m_readyForNextEvent = true;
    m_cooldownUntilMs = 0;

    clearPendingSwipe();
}

bool GestureGate::isLeftSwipeLabel(const QString &label) const
{
    if (label == QStringLiteral("swipe_left"))
    {
        return true;
    }
    return !m_config.swipeLeftLabel.isEmpty() &&
           label == m_config.swipeLeftLabel;
}

bool GestureGate::isRightSwipeLabel(const QString &label) const
{
    if (!m_config.swipeRightLabel.isEmpty())
    {
        return label == m_config.swipeRightLabel;
    }
    return label == QStringLiteral("swipe_right");
}

std::optional<GestureEvent> GestureGate::process(
    const GesturePrediction &prediction,
    bool hasHand,
    std::int64_t timestampMs,
    float leftStatus,
    float rightStatus)
{
    const std::int64_t nowMs =
        timestampMs > 0 ? timestampMs : QDateTime::currentMSecsSinceEpoch();

    bool effectiveHasHand = hasHand;
    if (m_config.rightHandOnly)
    {
        const bool leftAllowed =
            static_cast<int>(leftStatus) <= m_config.leftHandMaxStatus;
        bool rightReady =
            static_cast<int>(rightStatus) >= m_config.rightHandMinStatus;
        bool effectiveLeftAllowed = leftAllowed;

        // Some pipelines run with mirrored preview where handedness can look
        // swapped in status channels. In that case, allow left-only status as
        // a mirrored-right fallback so gate does not deadlock.
        if (!rightReady &&
            m_config.rightHandAllowMirrored &&
            static_cast<int>(leftStatus) >= m_config.rightHandMinStatus &&
            static_cast<int>(rightStatus) <= m_config.leftHandMaxStatus)
        {
            rightReady = true;
            effectiveLeftAllowed = true;
        }

        if (!effectiveLeftAllowed)
        {
            clearPendingSwipe();
            m_swipeCandidateLabel.clear();
            m_swipeCandidateFrames = 0;
        }

        effectiveHasHand = effectiveLeftAllowed && rightReady;
    }

    if (effectiveHasHand)
    {
        m_bothHandsMissingHits = 0;
    }
    else
    {
        m_bothHandsMissingHits = std::min(
            m_bothHandsMissingHits + 1,
            std::max(1, m_config.handDisappearResetFrames));
    }

    const bool handDisappearEdge =
        !effectiveHasHand &&
        m_bothHandsMissingHits >= std::max(1, m_config.handDisappearResetFrames);

    if (m_config.oneShotPerAppearance &&
        m_waitingForHandDisappearReset &&
        handDisappearEdge)
    {
        m_waitingForHandDisappearReset = false;
        m_readyForNextEvent = !m_config.requireNeutralReset;
        m_neutralFrames = 0;
    }

    if (m_config.swipeCommitOnHandDisappear &&
        handDisappearEdge &&
        !m_pendingSwipeLabel.isEmpty())
    {
        const auto committedSwipeEvent = tryEmitPendingSwipe(nowMs);
        clearPendingSwipe();
        if (committedSwipeEvent.has_value())
        {
            return committedSwipeEvent;
        }
    }

    if (m_config.swipeCommitOnHandDisappear &&
        !m_pendingSwipeLabel.isEmpty() &&
        m_config.swipePendingMaxAgeMs > 0 &&
        m_pendingSwipeTimestampMs > 0 &&
        nowMs - m_pendingSwipeTimestampMs > m_config.swipePendingMaxAgeMs)
    {
        clearPendingSwipe();
    }

    if (!effectiveHasHand)
    {
        m_candidateFrames = 0;
        m_candidateLabel.clear();
        m_swipeCandidateFrames = 0;
        m_swipeCandidateLabel.clear();
        return std::nullopt;
    }

    if (isLeftSwipeLabel(prediction.label))
    {
        // Left swipe is intentionally disabled: never let it enter event
        // candidates nor pending swipe commit path.
        m_candidateFrames = 0;
        m_candidateLabel.clear();
        m_swipeCandidateFrames = 0;
        m_swipeCandidateLabel.clear();
        clearPendingSwipe();
        return std::nullopt;
    }

    const bool isNeutral = isNeutralLabel(prediction.label);
    const bool passesThreshold = passesThresholds(prediction);
    const bool passesSwipeGuard = passesSwipeDirectionGuard(prediction);
    const bool passesSwipePairGuard = passesSwipePairConfidenceGuard(prediction);
    bool isValid = passesThreshold && passesSwipeGuard && passesSwipePairGuard;

    const bool swipeCandidate = isRightSwipeLabel(prediction.label);
    const bool nonSwipeActionCandidate =
        !swipeCandidate && !isNeutral;

    if (m_config.swipeCommitOnHandDisappear &&
        nonSwipeActionCandidate &&
        isValid &&
        !m_pendingSwipeLabel.isEmpty())
    {
        // If a strong non-swipe action is already visible (e.g. cheese),
        // suppress stale pending swipe commits on a later disappear edge.
        clearPendingSwipe();
    }

    if (m_config.swipeCommitOnHandDisappear &&
        swipeCandidate)
    {
        if (isValid)
        {
            if (prediction.label == m_swipeCandidateLabel)
            {
                ++m_swipeCandidateFrames;
            }
            else
            {
                m_swipeCandidateLabel = prediction.label;
                m_swipeCandidateFrames = 1;
            }

            if (m_swipeCandidateFrames >= std::max(1, m_config.swipeConsecutiveFrames))
            {
                m_pendingSwipeLabel = prediction.label;
                m_pendingSwipeConfidence = prediction.confidence;
                m_pendingSwipeMargin = prediction.margin;
                m_pendingSwipeClassId = prediction.classId;
                m_pendingSwipeDisplayName = prediction.displayName;
                m_pendingSwipeTimestampMs = nowMs;
            }
        }
        else
        {
            m_swipeCandidateLabel.clear();
            m_swipeCandidateFrames = 0;
        }

        m_candidateFrames = 0;
        m_candidateLabel.clear();
        return std::nullopt;
    }

    m_swipeCandidateLabel.clear();
    m_swipeCandidateFrames = 0;

    if (isValid && isNeutral)
    {
        ++m_neutralFrames;
    }
    else
    {
        m_neutralFrames = 0;
    }

    if (m_config.requireNeutralReset &&
        m_neutralFrames >= std::max(1, m_config.neutralResetFrames))
    {
        m_readyForNextEvent = true;
    }

    if (!isValid || isNeutral)
    {
        m_candidateFrames = 0;
        m_candidateLabel.clear();
        return std::nullopt;
    }

    if (prediction.label == m_candidateLabel)
    {
        ++m_candidateFrames;
    }
    else
    {
        m_candidateLabel = prediction.label;
        m_candidateFrames = 1;
    }

    int requiredFrames = std::max(1, m_config.consecutiveFrames);
    if (swipeCandidate)
    {
        requiredFrames = std::max(
            requiredFrames,
            std::max(1, m_config.swipeConsecutiveFrames));
    }

    if (m_candidateFrames < requiredFrames)
    {
        return std::nullopt;
    }

    return tryEmitEvent(prediction, nowMs);
}

bool GestureGate::isNeutralLabel(const QString &label) const
{
    return m_config.neutralLabels.contains(label);
}

bool GestureGate::passesThresholds(const GesturePrediction &prediction) const
{
    const float confidenceThreshold = m_config.labelConfidenceThresholds.contains(prediction.label)
                                          ? m_config.labelConfidenceThresholds.value(prediction.label)
                                          : m_config.confidenceThreshold;
    const float marginThreshold = m_config.labelMarginThresholds.contains(prediction.label)
                                      ? m_config.labelMarginThresholds.value(prediction.label)
                                      : m_config.marginThreshold;

    return prediction.confidence >= confidenceThreshold &&
           prediction.margin >= marginThreshold;
}

bool GestureGate::passesSwipeDirectionGuard(const GesturePrediction &prediction) const
{
    if (!m_config.enableSwipeDirectionGuard)
    {
        return true;
    }

    if (!isRightSwipeLabel(prediction.label))
    {
        return true;
    }

    const QString rightLabel = m_config.swipeRightLabel.isEmpty()
                                   ? QStringLiteral("swipe_right")
                                   : m_config.swipeRightLabel;
    const QString leftLabel = m_config.swipeLeftLabel.isEmpty()
                                  ? QStringLiteral("swipe_left")
                                  : m_config.swipeLeftLabel;
    const float leftProbability =
        prediction.probabilityByLabel.value(leftLabel, 0.0f);
    const float rightProbability =
        prediction.probabilityByLabel.value(rightLabel, 0.0f);
    const float directionGap = rightProbability - leftProbability;

    return directionGap >= m_config.swipeDirectionMargin;
}

bool GestureGate::passesSwipePairConfidenceGuard(const GesturePrediction &prediction) const
{
    if (!isRightSwipeLabel(prediction.label))
    {
        return true;
    }

    const QString rightLabel = m_config.swipeRightLabel.isEmpty()
                                   ? QStringLiteral("swipe_right")
                                   : m_config.swipeRightLabel;
    const QString leftLabel = m_config.swipeLeftLabel.isEmpty()
                                  ? QStringLiteral("swipe_left")
                                  : m_config.swipeLeftLabel;
    const float leftProbability =
        prediction.probabilityByLabel.value(leftLabel, 0.0f);
    const float rightProbability =
        prediction.probabilityByLabel.value(rightLabel, 0.0f);
    const float pairProbability = leftProbability + rightProbability;
    return pairProbability >= m_config.swipePairMinConfidence;
}

void GestureGate::clearPendingSwipe()
{
    m_pendingSwipeLabel.clear();
    m_pendingSwipeConfidence = 0.0f;
    m_pendingSwipeMargin = 0.0f;
    m_pendingSwipeClassId = -1;
    m_pendingSwipeDisplayName.clear();
    m_pendingSwipeTimestampMs = 0;
}

std::optional<GestureEvent> GestureGate::tryEmitEvent(
    const GesturePrediction &prediction,
    std::int64_t timestampMs)
{
    if (isLeftSwipeLabel(prediction.label))
    {
        return std::nullopt;
    }

    if (m_config.cooldownMs > 0 && timestampMs < m_cooldownUntilMs)
    {
        m_candidateFrames = 0;
        m_candidateLabel.clear();
        return std::nullopt;
    }

    if (m_config.oneShotPerAppearance && m_waitingForHandDisappearReset)
    {
        m_candidateFrames = 0;
        m_candidateLabel.clear();
        return std::nullopt;
    }

    if (m_config.requireNeutralReset && !m_readyForNextEvent)
    {
        m_candidateFrames = 0;
        m_candidateLabel.clear();
        return std::nullopt;
    }

    GestureEvent event;
    event.classId = prediction.classId;
    event.label = prediction.label;
    event.displayName = prediction.displayName;
    event.confidence = prediction.confidence;
    event.margin = prediction.margin;
    event.timestampMs = timestampMs;

    m_cooldownUntilMs = timestampMs + std::max(0, m_config.cooldownMs);
    if (m_config.requireNeutralReset)
    {
        m_readyForNextEvent = false;
        m_neutralFrames = 0;
    }
    if (m_config.oneShotPerAppearance)
    {
        m_waitingForHandDisappearReset = true;
        m_bothHandsMissingHits = 0;
    }

    m_candidateFrames = 0;
    m_candidateLabel.clear();
    return event;
}

std::optional<GestureEvent> GestureGate::tryEmitPendingSwipe(std::int64_t timestampMs)
{
    if (m_pendingSwipeLabel.isEmpty())
    {
        return std::nullopt;
    }
    if (!isRightSwipeLabel(m_pendingSwipeLabel))
    {
        return std::nullopt;
    }
    if (m_config.cooldownMs > 0 && timestampMs < m_cooldownUntilMs)
    {
        return std::nullopt;
    }
    if (m_config.oneShotPerAppearance && m_waitingForHandDisappearReset)
    {
        return std::nullopt;
    }
    if (m_config.requireNeutralReset && !m_readyForNextEvent)
    {
        return std::nullopt;
    }

    GestureEvent event;
    event.classId = m_pendingSwipeClassId;
    event.label = m_pendingSwipeLabel;
    event.displayName = m_pendingSwipeDisplayName.isEmpty()
                            ? m_pendingSwipeLabel
                            : m_pendingSwipeDisplayName;
    event.confidence = m_pendingSwipeConfidence;
    event.margin = m_pendingSwipeMargin;
    event.timestampMs = timestampMs;

    m_cooldownUntilMs = timestampMs + std::max(0, m_config.cooldownMs);
    if (m_config.requireNeutralReset)
    {
        m_readyForNextEvent = false;
        m_neutralFrames = 0;
    }

    // Swipe event is committed on hand disappearance boundary itself.
    m_waitingForHandDisappearReset = false;
    m_candidateFrames = 0;
    m_candidateLabel.clear();
    return event;
}
