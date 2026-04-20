#ifndef HOME_AUTOMATION_GESTURE_PIPELINE_H
#define HOME_AUTOMATION_GESTURE_PIPELINE_H

#include <cstdint>
#include <optional>

#include <QStringList>
#include <QVector>

#include "gesture_gate.h"
#include "gesture_model.h"
#include "gesture_types.h"

class GesturePipeline
{
public:
    bool loadFromJsonFile(const QString &path, QString *errorMessage = nullptr);
    bool isReady() const;
    int featureDimension() const;
    int requiredSequenceFrames() const;
    const QStringList &classLabels() const;

    void resetSequence();

    bool predictFromFeatureVector(
        const QVector<float> &featureVector,
        GesturePrediction *prediction,
        std::optional<GestureEvent> *event = nullptr,
        bool hasHand = true,
        std::int64_t timestampMs = 0,
        float leftStatus = 0.0f,
        float rightStatus = 0.0f,
        QString *errorMessage = nullptr);

    bool pushObservation(
        const GestureFrameObservation &observation,
        GesturePrediction *prediction,
        std::optional<GestureEvent> *event = nullptr,
        QString *errorMessage = nullptr);

private:
    QVector<float> buildFeatureVector() const;

    GestureModel m_model;
    GestureGate m_gate;
    QVector<GestureFrameObservation> m_observations;
};

#endif
