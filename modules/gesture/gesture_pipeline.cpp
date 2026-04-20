#include "gesture_pipeline.h"

#include <algorithm>
#include <cstddef>

#include <QDateTime>

bool GesturePipeline::loadFromJsonFile(const QString &path, QString *errorMessage)
{
    m_observations.clear();
    if (!m_model.loadFromJsonFile(path, errorMessage))
    {
        return false;
    }

    m_gate.setConfig(m_model.gateConfig());
    return true;
}

bool GesturePipeline::isReady() const
{
    return m_model.isLoaded();
}

int GesturePipeline::featureDimension() const
{
    return m_model.featureDimension();
}

int GesturePipeline::requiredSequenceFrames() const
{
    return m_model.sequenceFrames();
}

const QStringList &GesturePipeline::classLabels() const
{
    return m_model.classLabels();
}

void GesturePipeline::resetSequence()
{
    m_observations.clear();
    m_gate.reset();
}

bool GesturePipeline::predictFromFeatureVector(
    const QVector<float> &featureVector,
    GesturePrediction *prediction,
    std::optional<GestureEvent> *event,
    bool hasHand,
    std::int64_t timestampMs,
    float leftStatus,
    float rightStatus,
    QString *errorMessage)
{
    if (!m_model.predict(featureVector, prediction, errorMessage))
    {
        return false;
    }

    if (!event)
    {
        return true;
    }

    const std::int64_t resolvedTimestampMs =
        timestampMs > 0 ? timestampMs : QDateTime::currentMSecsSinceEpoch();
    *event = m_gate.process(
        *prediction,
        hasHand,
        resolvedTimestampMs,
        leftStatus,
        rightStatus);
    return true;
}

bool GesturePipeline::pushObservation(
    const GestureFrameObservation &observation,
    GesturePrediction *prediction,
    std::optional<GestureEvent> *event,
    QString *errorMessage)
{
    if (!m_model.isLoaded())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Gesture model is not loaded.");
        }
        return false;
    }

    m_observations.push_back(observation);

    const int requiredFrames = std::max(1, m_model.sequenceFrames());
    while (m_observations.size() > requiredFrames)
    {
        m_observations.removeFirst();
    }

    if (m_observations.size() < requiredFrames)
    {
        return false;
    }

    const QVector<float> featureVector = buildFeatureVector();
    // Align with Python validator gating semantics: gate timing is based on
    // local realtime clock, not tracker-emitted frame timestamps.
    const std::int64_t gateTimestampMs = QDateTime::currentMSecsSinceEpoch();
    return predictFromFeatureVector(
        featureVector,
        prediction,
        event,
        observation.hasHand,
        gateTimestampMs,
        observation.leftStatus,
        observation.rightStatus,
        errorMessage);
}

QVector<float> GesturePipeline::buildFeatureVector() const
{
    const int sequenceFrames = std::max(1, m_model.sequenceFrames());
    const int keypointDimension = std::max(0, m_model.frameKeypointDimension());
    const int clampedKeypointDimension = std::min(keypointDimension, 126);
    const bool includeStatus = m_model.includeStatus();

    const int expectedDimension = sequenceFrames * keypointDimension +
                                  (includeStatus ? sequenceFrames * 2 : 0);

    QVector<float> featureVector;
    featureVector.reserve(expectedDimension);

    for (const GestureFrameObservation &observation : m_observations)
    {
        for (int index = 0; index < clampedKeypointDimension; ++index)
        {
            featureVector.push_back(observation.keypoints[static_cast<std::size_t>(index)]);
        }
        for (int index = clampedKeypointDimension; index < keypointDimension; ++index)
        {
            featureVector.push_back(0.0f);
        }
    }

    if (includeStatus)
    {
        for (const GestureFrameObservation &observation : m_observations)
        {
            featureVector.push_back(observation.leftStatus);
        }
        for (const GestureFrameObservation &observation : m_observations)
        {
            featureVector.push_back(observation.rightStatus);
        }
    }

    return featureVector;
}
