#include "gesture_model.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace
{
float safeScale(float value)
{
    return std::fabs(value) < 1e-6f ? 1.0f : value;
}

float softmaxExp(float value)
{
    return static_cast<float>(std::exp(static_cast<double>(value)));
}
}

bool GestureModel::loadFromJsonFile(const QString &path, QString *errorMessage)
{
    m_isLoaded = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(errorMessage, QStringLiteral("Failed to open model file: %1").arg(path));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        setError(
            errorMessage,
            QStringLiteral("Invalid model JSON: %1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject rootObject = document.object();
    if (!parseModelSection(rootObject, errorMessage))
    {
        return false;
    }
    if (!parseParametersSection(rootObject, errorMessage))
    {
        return false;
    }

    parseDisplayNames(rootObject);
    parseGating(rootObject);

    m_isLoaded = true;
    return true;
}

bool GestureModel::isLoaded() const
{
    return m_isLoaded;
}

int GestureModel::featureDimension() const
{
    return m_featureDimension;
}

int GestureModel::classCount() const
{
    return m_classCount;
}

int GestureModel::sequenceFrames() const
{
    return m_sequenceFrames;
}

int GestureModel::frameKeypointDimension() const
{
    return m_frameKeypointDimension;
}

bool GestureModel::includeStatus() const
{
    return m_includeStatus;
}

const QStringList &GestureModel::classLabels() const
{
    return m_classLabels;
}

const GestureGateConfig &GestureModel::gateConfig() const
{
    return m_gateConfig;
}

bool GestureModel::predict(
    const QVector<float> &featureVector,
    GesturePrediction *prediction,
    QString *errorMessage) const
{
    if (!m_isLoaded)
    {
        setError(errorMessage, QStringLiteral("Model is not loaded."));
        return false;
    }
    if (!prediction)
    {
        setError(errorMessage, QStringLiteral("Prediction output pointer is null."));
        return false;
    }
    if (featureVector.size() != m_featureDimension)
    {
        setError(
            errorMessage,
            QStringLiteral("Feature dimension mismatch. Expected %1, got %2.")
                .arg(m_featureDimension)
                .arg(featureVector.size()));
        return false;
    }

    QVector<float> normalized(m_featureDimension, 0.0f);
    for (int featureIndex = 0; featureIndex < m_featureDimension; ++featureIndex)
    {
        normalized[featureIndex] =
            (featureVector[featureIndex] - m_scalerMean[featureIndex]) /
            safeScale(m_scalerScale[featureIndex]);
    }

    QVector<float> logits(m_classCount, 0.0f);
    float maxLogit = -std::numeric_limits<float>::infinity();
    for (int classIndex = 0; classIndex < m_classCount; ++classIndex)
    {
        float logit = m_classifierIntercept[classIndex];
        const int rowOffset = classIndex * m_featureDimension;
        for (int featureIndex = 0; featureIndex < m_featureDimension; ++featureIndex)
        {
            logit += m_classifierCoef[rowOffset + featureIndex] * normalized[featureIndex];
        }

        logits[classIndex] = logit;
        maxLogit = std::max(maxLogit, logit);
    }

    QVector<float> probabilities(m_classCount, 0.0f);
    double denominator = 0.0;
    for (int classIndex = 0; classIndex < m_classCount; ++classIndex)
    {
        const float expValue = softmaxExp(logits[classIndex] - maxLogit);
        probabilities[classIndex] = expValue;
        denominator += expValue;
    }

    if (denominator <= 0.0)
    {
        setError(errorMessage, QStringLiteral("Softmax denominator is not positive."));
        return false;
    }

    for (float &probability : probabilities)
    {
        probability = static_cast<float>(probability / denominator);
    }

    int bestIndex = -1;
    int secondBestIndex = -1;
    for (int classIndex = 0; classIndex < m_classCount; ++classIndex)
    {
        if (bestIndex < 0 ||
            probabilities[classIndex] > probabilities[bestIndex])
        {
            secondBestIndex = bestIndex;
            bestIndex = classIndex;
            continue;
        }

        if (secondBestIndex < 0 ||
            probabilities[classIndex] > probabilities[secondBestIndex])
        {
            secondBestIndex = classIndex;
        }
    }

    prediction->classId = bestIndex;
    prediction->label = m_classLabels[bestIndex];
    prediction->displayName =
        m_displayNames.contains(prediction->label)
            ? m_displayNames.value(prediction->label)
            : prediction->label;
    prediction->confidence = probabilities[bestIndex];
    prediction->margin =
        secondBestIndex >= 0
            ? (probabilities[bestIndex] - probabilities[secondBestIndex])
            : probabilities[bestIndex];
    prediction->probabilities = probabilities;
    prediction->probabilityByLabel.clear();
    for (int classIndex = 0; classIndex < m_classCount; ++classIndex)
    {
        prediction->probabilityByLabel.insert(
            m_classLabels[classIndex],
            probabilities[classIndex]);
    }

    return true;
}

void GestureModel::setError(QString *errorMessage, const QString &message) const
{
    if (errorMessage)
    {
        *errorMessage = message;
    }
}

bool GestureModel::parseModelSection(const QJsonObject &rootObject, QString *errorMessage)
{
    const QJsonObject modelObject = rootObject.value(QStringLiteral("model")).toObject();
    if (modelObject.isEmpty())
    {
        setError(errorMessage, QStringLiteral("Missing 'model' section."));
        return false;
    }

    const int nFeatures = modelObject.value(QStringLiteral("n_features")).toInt(-1);
    const int nClasses = modelObject.value(QStringLiteral("n_classes")).toInt(-1);
    if (nFeatures <= 0 || nClasses <= 0)
    {
        setError(errorMessage, QStringLiteral("Invalid n_features or n_classes in model section."));
        return false;
    }

    const QJsonArray classLabelsArray = modelObject.value(QStringLiteral("class_labels")).toArray();
    if (classLabelsArray.size() != nClasses)
    {
        setError(
            errorMessage,
            QStringLiteral("class_labels size mismatch. Expected %1, got %2.")
                .arg(nClasses)
                .arg(classLabelsArray.size()));
        return false;
    }

    QStringList labels;
    labels.reserve(classLabelsArray.size());
    for (const QJsonValue &labelValue : classLabelsArray)
    {
        labels.push_back(labelValue.toString());
    }

    const QJsonObject featureLayout = modelObject.value(QStringLiteral("feature_layout")).toObject();
    if (!featureLayout.isEmpty())
    {
        m_sequenceFrames =
            featureLayout.value(QStringLiteral("sequence_frames")).toInt(m_sequenceFrames);
        m_frameKeypointDimension =
            featureLayout.value(QStringLiteral("frame_keypoint_dim")).toInt(m_frameKeypointDimension);
        m_includeStatus =
            featureLayout.value(QStringLiteral("include_status")).toBool(m_includeStatus);
    }

    m_featureDimension = nFeatures;
    m_classCount = nClasses;
    m_classLabels = labels;
    return true;
}

bool GestureModel::parseParametersSection(const QJsonObject &rootObject, QString *errorMessage)
{
    const QJsonObject parametersObject = rootObject.value(QStringLiteral("parameters")).toObject();
    if (parametersObject.isEmpty())
    {
        setError(errorMessage, QStringLiteral("Missing 'parameters' section."));
        return false;
    }

    const QJsonObject scalerObject = parametersObject.value(QStringLiteral("scaler")).toObject();
    const QJsonObject classifierObject =
        parametersObject.value(QStringLiteral("classifier")).toObject();
    if (scalerObject.isEmpty() || classifierObject.isEmpty())
    {
        setError(errorMessage, QStringLiteral("Missing scaler or classifier parameters."));
        return false;
    }

    if (!parseFloatArray(
            scalerObject.value(QStringLiteral("mean")).toArray(),
            m_featureDimension,
            &m_scalerMean,
            errorMessage,
            QStringLiteral("parameters.scaler.mean")))
    {
        return false;
    }

    if (!parseFloatArray(
            scalerObject.value(QStringLiteral("scale")).toArray(),
            m_featureDimension,
            &m_scalerScale,
            errorMessage,
            QStringLiteral("parameters.scaler.scale")))
    {
        return false;
    }

    const QJsonArray coefRows = classifierObject.value(QStringLiteral("coef")).toArray();
    if (coefRows.size() != m_classCount)
    {
        setError(
            errorMessage,
            QStringLiteral("Classifier coef row count mismatch. Expected %1, got %2.")
                .arg(m_classCount)
                .arg(coefRows.size()));
        return false;
    }

    m_classifierCoef.clear();
    m_classifierCoef.reserve(m_classCount * m_featureDimension);
    for (int classIndex = 0; classIndex < coefRows.size(); ++classIndex)
    {
        QVector<float> row;
        if (!parseFloatArray(
                coefRows[classIndex].toArray(),
                m_featureDimension,
                &row,
                errorMessage,
                QStringLiteral("parameters.classifier.coef[%1]").arg(classIndex)))
        {
            return false;
        }
        m_classifierCoef += row;
    }

    if (!parseFloatArray(
            classifierObject.value(QStringLiteral("intercept")).toArray(),
            m_classCount,
            &m_classifierIntercept,
            errorMessage,
            QStringLiteral("parameters.classifier.intercept")))
    {
        return false;
    }

    return true;
}

void GestureModel::parseDisplayNames(const QJsonObject &rootObject)
{
    m_displayNames.clear();

    const QJsonObject displayNamesObject =
        rootObject.value(QStringLiteral("display_names")).toObject();
    for (auto iter = displayNamesObject.constBegin(); iter != displayNamesObject.constEnd(); ++iter)
    {
        m_displayNames.insert(iter.key(), iter.value().toString(iter.key()));
    }
}

void GestureModel::parseGating(const QJsonObject &rootObject)
{
    m_gateConfig = GestureGateConfig();

    const QJsonObject gatingObject = rootObject.value(QStringLiteral("gating")).toObject();
    if (gatingObject.isEmpty())
    {
        m_gateConfig.neutralLabels = QStringList{QStringLiteral("idle")};
        return;
    }

    auto readFloat = [&gatingObject](const QString &key, float defaultValue) -> float {
        return static_cast<float>(gatingObject.value(key).toDouble(defaultValue));
    };

    m_gateConfig.confidenceThreshold =
        readFloat(QStringLiteral("confidence_threshold"), m_gateConfig.confidenceThreshold);
    m_gateConfig.marginThreshold =
        readFloat(QStringLiteral("margin_threshold"), m_gateConfig.marginThreshold);
    m_gateConfig.consecutiveFrames =
        gatingObject.value(QStringLiteral("consecutive_frames"))
            .toInt(m_gateConfig.consecutiveFrames);
    m_gateConfig.cooldownMs =
        gatingObject.value(QStringLiteral("cooldown_ms")).toInt(m_gateConfig.cooldownMs);
    m_gateConfig.hideNeutralPredictions =
        gatingObject.value(QStringLiteral("hide_neutral_predictions"))
            .toBool(m_gateConfig.hideNeutralPredictions);
    m_gateConfig.oneShotPerAppearance =
        gatingObject.value(QStringLiteral("one_shot_per_appearance"))
            .toBool(m_gateConfig.oneShotPerAppearance);
    m_gateConfig.handDisappearResetFrames =
        gatingObject.value(QStringLiteral("hand_disappear_reset_frames"))
            .toInt(m_gateConfig.handDisappearResetFrames);
    m_gateConfig.requireNeutralReset =
        gatingObject.value(QStringLiteral("require_neutral_reset"))
            .toBool(m_gateConfig.requireNeutralReset);
    m_gateConfig.neutralResetFrames =
        gatingObject.value(QStringLiteral("neutral_reset_frames"))
            .toInt(m_gateConfig.neutralResetFrames);
    m_gateConfig.enableSwipeDirectionGuard =
        gatingObject.value(QStringLiteral("enable_swipe_direction_guard"))
            .toBool(m_gateConfig.enableSwipeDirectionGuard);
    m_gateConfig.swipeDirectionMargin =
        readFloat(QStringLiteral("swipe_direction_margin"), m_gateConfig.swipeDirectionMargin);
    m_gateConfig.swipeCommitOnHandDisappear =
        gatingObject.value(QStringLiteral("swipe_commit_on_hand_disappear"))
            .toBool(m_gateConfig.swipeCommitOnHandDisappear);
    m_gateConfig.swipeLeftLabel =
        gatingObject.value(QStringLiteral("swipe_left_label"))
            .toString(m_gateConfig.swipeLeftLabel);
    m_gateConfig.swipeRightLabel =
        gatingObject.value(QStringLiteral("swipe_right_label"))
            .toString(m_gateConfig.swipeRightLabel);
    m_gateConfig.swipeConsecutiveFrames =
        gatingObject.value(QStringLiteral("swipe_consecutive_frames"))
            .toInt(m_gateConfig.swipeConsecutiveFrames);
    m_gateConfig.swipePendingMaxAgeMs =
        gatingObject.value(QStringLiteral("swipe_pending_max_age_ms"))
            .toInt(m_gateConfig.swipePendingMaxAgeMs);
    m_gateConfig.swipePairMinConfidence =
        readFloat(QStringLiteral("swipe_pair_min_confidence"),
                  m_gateConfig.swipePairMinConfidence);
    m_gateConfig.rightHandOnly =
        gatingObject.value(QStringLiteral("right_hand_only"))
            .toBool(m_gateConfig.rightHandOnly);
    m_gateConfig.rightHandMinStatus =
        gatingObject.value(QStringLiteral("right_hand_min_status"))
            .toInt(m_gateConfig.rightHandMinStatus);
    m_gateConfig.leftHandMaxStatus =
        gatingObject.value(QStringLiteral("left_hand_max_status"))
            .toInt(m_gateConfig.leftHandMaxStatus);
    m_gateConfig.rightHandAllowMirrored =
        gatingObject.value(QStringLiteral("right_hand_allow_mirrored"))
            .toBool(m_gateConfig.rightHandAllowMirrored);

    m_gateConfig.labelConfidenceThresholds.clear();
    const QJsonObject labelConfidenceThresholds =
        gatingObject.value(QStringLiteral("label_confidence_thresholds")).toObject();
    for (auto iter = labelConfidenceThresholds.constBegin();
         iter != labelConfidenceThresholds.constEnd();
         ++iter)
    {
        m_gateConfig.labelConfidenceThresholds.insert(
            iter.key(),
            static_cast<float>(iter.value().toDouble()));
    }

    m_gateConfig.labelMarginThresholds.clear();
    const QJsonObject labelMarginThresholds =
        gatingObject.value(QStringLiteral("label_margin_thresholds")).toObject();
    for (auto iter = labelMarginThresholds.constBegin();
         iter != labelMarginThresholds.constEnd();
         ++iter)
    {
        m_gateConfig.labelMarginThresholds.insert(
            iter.key(),
            static_cast<float>(iter.value().toDouble()));
    }

    m_gateConfig.neutralLabels.clear();
    const QJsonArray neutralLabels = gatingObject.value(QStringLiteral("neutral_labels")).toArray();
    for (const QJsonValue &neutralLabel : neutralLabels)
    {
        m_gateConfig.neutralLabels.push_back(neutralLabel.toString());
    }
    if (m_gateConfig.neutralLabels.isEmpty())
    {
        m_gateConfig.neutralLabels = QStringList{QStringLiteral("idle")};
    }
}

bool GestureModel::parseFloatArray(
    const QJsonArray &array,
    int expectedSize,
    QVector<float> *output,
    QString *errorMessage,
    const QString &fieldName)
{
    if (!output)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Internal error: null output buffer for %1").arg(fieldName);
        }
        return false;
    }

    if (array.size() != expectedSize)
    {
        if (errorMessage)
        {
            *errorMessage =
                QStringLiteral("%1 size mismatch. Expected %2, got %3.")
                    .arg(fieldName)
                    .arg(expectedSize)
                    .arg(array.size());
        }
        return false;
    }

    output->clear();
    output->reserve(expectedSize);
    for (const QJsonValue &value : array)
    {
        output->push_back(static_cast<float>(value.toDouble()));
    }

    return true;
}
