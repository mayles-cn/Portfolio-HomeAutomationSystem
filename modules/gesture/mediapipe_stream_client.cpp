#include "mediapipe_stream_client.h"

#include <algorithm>
#include <array>

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QStringList>

namespace
{
constexpr int kExpectedKeypointDimension = 126;

QString processErrorToText(const QProcess::ProcessError error)
{
    switch (error)
    {
    case QProcess::FailedToStart:
        return QStringLiteral("进程启动失败");
    case QProcess::Crashed:
        return QStringLiteral("进程崩溃");
    case QProcess::Timedout:
        return QStringLiteral("进程超时");
    case QProcess::WriteError:
        return QStringLiteral("进程写入错误");
    case QProcess::ReadError:
        return QStringLiteral("进程读取错误");
    case QProcess::UnknownError:
    default:
        return QStringLiteral("进程未知错误");
    }
}
}

MediapipeStreamClient::MediapipeStreamClient(QObject *parent)
    : QObject(parent),
      m_process(new QProcess(this))
{
    connect(
        m_process,
        &QProcess::readyReadStandardOutput,
        this,
        &MediapipeStreamClient::onReadyReadStandardOutput);
    connect(
        m_process,
        &QProcess::readyReadStandardError,
        this,
        &MediapipeStreamClient::onReadyReadStandardError);
    connect(
        m_process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        &MediapipeStreamClient::onProcessFinished);
    connect(
        m_process,
        &QProcess::errorOccurred,
        this,
        &MediapipeStreamClient::onProcessErrorOccurred);
}

MediapipeStreamClient::~MediapipeStreamClient()
{
    stop();
}

bool MediapipeStreamClient::start(
    const QString &bridgeExePath,
    const QString &taskModelPath,
    const int cameraIndex,
    const bool mirror,
    const bool showPreview,
    QString *errorMessage)
{
    stop();
    m_lastError.clear();
    m_lastStdErrLine.clear();
    m_stdoutBuffer.clear();
    m_latestObservation.reset();

    if (!QFileInfo::exists(bridgeExePath))
    {
        const QString message =
            QStringLiteral("未找到桥接程序：%1").arg(bridgeExePath);
        setError(message);
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return false;
    }
    if (!QFileInfo::exists(taskModelPath))
    {
        const QString message =
            QStringLiteral("未找到 hand_landmarker.task：%1").arg(taskModelPath);
        setError(message);
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return false;
    }

    QStringList args;
    args << QStringLiteral("--model_path=%1").arg(taskModelPath)
         << QStringLiteral("--camera_id=%1").arg(cameraIndex)
         << QStringLiteral("--mirror=%1").arg(mirror ? QStringLiteral("true")
                                                     : QStringLiteral("false"))
         << QStringLiteral("--num_hands=2")
         << QStringLiteral("--min_detection_conf=0.65")
         << QStringLiteral("--min_presence_conf=0.65")
         << QStringLiteral("--min_tracking_conf=0.65")
         << QStringLiteral("--hold_missing_frames=3")
         << QStringLiteral("--enable_ema=true")
         << QStringLiteral("--ema_alpha=0.65")
         << QStringLiteral("--show_preview=%1")
                .arg(showPreview ? QStringLiteral("true")
                                 : QStringLiteral("false"))
         << QStringLiteral("--print_header=true");

    m_process->setProgram(bridgeExePath);
    m_process->setArguments(args);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start();

    if (!m_process->waitForStarted(5000))
    {
        const QString message =
            QStringLiteral("桥接程序启动失败：%1").arg(m_process->errorString());
        setError(message);
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return false;
    }

    return true;
}

void MediapipeStreamClient::stop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
    {
        return;
    }

    m_process->terminate();
    if (!m_process->waitForFinished(400))
    {
        m_process->kill();
        m_process->waitForFinished(400);
    }
}

bool MediapipeStreamClient::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool MediapipeStreamClient::takeLatestObservation(GestureFrameObservation *observation)
{
    if (!observation || !m_latestObservation.has_value())
    {
        return false;
    }

    *observation = *m_latestObservation;
    m_latestObservation.reset();
    return true;
}

QString MediapipeStreamClient::lastError() const
{
    return m_lastError;
}

QString MediapipeStreamClient::lastStdErrLine() const
{
    return m_lastStdErrLine;
}

void MediapipeStreamClient::onReadyReadStandardOutput()
{
    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    int lineBreak = m_stdoutBuffer.indexOf('\n');
    while (lineBreak >= 0)
    {
        const QByteArray rawLine = m_stdoutBuffer.left(lineBreak);
        m_stdoutBuffer.remove(0, lineBreak + 1);

        GestureFrameObservation observation;
        QString parseError;
        if (parseObservationLine(rawLine, &observation, &parseError))
        {
            m_latestObservation = observation;
        }
        else if (!parseError.isEmpty())
        {
            setError(parseError);
        }

        lineBreak = m_stdoutBuffer.indexOf('\n');
    }
}

void MediapipeStreamClient::onReadyReadStandardError()
{
    const QByteArray stderrData = m_process->readAllStandardError();
    const QList<QByteArray> lines = stderrData.split('\n');
    for (const QByteArray &line : lines)
    {
        const QString text = trimLine(line);
        if (!text.isEmpty())
        {
            m_lastStdErrLine = text;
        }
    }
}

void MediapipeStreamClient::onProcessFinished(
    const int exitCode,
    const QProcess::ExitStatus exitStatus)
{
    const QString statusText =
        exitStatus == QProcess::NormalExit
            ? QStringLiteral("正常退出")
            : QStringLiteral("异常退出");
    QString message = QStringLiteral("桥接程序已结束：%1, exitCode=%2")
                          .arg(statusText)
                          .arg(exitCode);
    if (!m_lastStdErrLine.isEmpty())
    {
        message += QStringLiteral(" | stderr: %1").arg(m_lastStdErrLine);
    }
    setError(message);
}

void MediapipeStreamClient::onProcessErrorOccurred(const QProcess::ProcessError error)
{
    QString message = QStringLiteral("桥接程序错误：%1")
                          .arg(processErrorToText(error));
    if (!m_process->errorString().isEmpty())
    {
        message += QStringLiteral(" (%1)").arg(m_process->errorString());
    }
    setError(message);
}

QString MediapipeStreamClient::trimLine(const QByteArray &line)
{
    return QString::fromUtf8(line).trimmed();
}

bool MediapipeStreamClient::parseObservationLine(
    const QByteArray &line,
    GestureFrameObservation *observation,
    QString *errorMessage) const
{
    const QString text = trimLine(line);
    if (text.isEmpty())
    {
        return false;
    }
    if (!text.startsWith(QLatin1Char('{')))
    {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("桥接输出 JSON 解析失败：%1")
                                .arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("type")).toString() ==
        QStringLiteral("hand_landmarker_stream"))
    {
        return false;
    }

    const QJsonArray keypointsArray = object.value(QStringLiteral("keypoints")).toArray();
    if (keypointsArray.size() != kExpectedKeypointDimension)
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral(
                "桥接输出 keypoints 维度错误，期望 %1，实际 %2")
                                .arg(kExpectedKeypointDimension)
                                .arg(keypointsArray.size());
        }
        return false;
    }

    std::array<float, kExpectedKeypointDimension> keypoints = {};
    for (int i = 0; i < keypointsArray.size(); ++i)
    {
        keypoints[static_cast<size_t>(i)] =
            static_cast<float>(keypointsArray[i].toDouble(0.0));
    }

    observation->keypoints = keypoints;
    observation->leftStatus =
        static_cast<float>(object.value(QStringLiteral("left_status")).toInt(0));
    observation->rightStatus =
        static_cast<float>(object.value(QStringLiteral("right_status")).toInt(0));
    observation->hasHand = object.value(QStringLiteral("has_hand"))
                               .toBool((observation->leftStatus != 0.0f ||
                                        observation->rightStatus != 0.0f));
    const auto fallbackTs = QDateTime::currentMSecsSinceEpoch();
    observation->timestampMs =
        static_cast<std::int64_t>(object.value(QStringLiteral("timestamp_ms"))
                                      .toVariant()
                                      .toLongLong());
    if (observation->timestampMs <= 0)
    {
        observation->timestampMs = fallbackTs;
    }
    return true;
}

void MediapipeStreamClient::setError(const QString &message)
{
    if (!message.isEmpty())
    {
        m_lastError = message;
    }
}
