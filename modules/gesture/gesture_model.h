#ifndef HOME_AUTOMATION_GESTURE_MODEL_H
#define HOME_AUTOMATION_GESTURE_MODEL_H

#include <QString>
#include <QStringList>
#include <QVector>

#include "gesture_types.h"

class QJsonArray;
class QJsonObject;

class GestureModel
{
public:
    bool loadFromJsonFile(const QString &path, QString *errorMessage = nullptr);

    bool isLoaded() const;
    int featureDimension() const;
    int classCount() const;
    int sequenceFrames() const;
    int frameKeypointDimension() const;
    bool includeStatus() const;
    const QStringList &classLabels() const;
    const GestureGateConfig &gateConfig() const;

    bool predict(
        const QVector<float> &featureVector,
        GesturePrediction *prediction,
        QString *errorMessage = nullptr) const;

private:
    void setError(QString *errorMessage, const QString &message) const;
    bool parseModelSection(const QJsonObject &rootObject, QString *errorMessage);
    bool parseParametersSection(const QJsonObject &rootObject, QString *errorMessage);
    void parseDisplayNames(const QJsonObject &rootObject);
    void parseGating(const QJsonObject &rootObject);
    static bool parseFloatArray(
        const QJsonArray &array,
        int expectedSize,
        QVector<float> *output,
        QString *errorMessage,
        const QString &fieldName);

    bool m_isLoaded = false;
    int m_featureDimension = 0;
    int m_classCount = 0;
    int m_sequenceFrames = 20;
    int m_frameKeypointDimension = 126;
    bool m_includeStatus = true;

    QStringList m_classLabels;
    QHash<QString, QString> m_displayNames;
    GestureGateConfig m_gateConfig;

    QVector<float> m_scalerMean;
    QVector<float> m_scalerScale;
    QVector<float> m_classifierCoef;
    QVector<float> m_classifierIntercept;
};

#endif
