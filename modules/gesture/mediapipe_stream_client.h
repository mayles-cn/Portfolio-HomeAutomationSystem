#ifndef HOME_AUTOMATION_MEDIAPIPE_STREAM_CLIENT_H
#define HOME_AUTOMATION_MEDIAPIPE_STREAM_CLIENT_H

#include <optional>

#include <QObject>
#include <QProcess>
#include <QString>

#include "gesture_types.h"

class MediapipeStreamClient : public QObject
{
    Q_OBJECT

public:
    explicit MediapipeStreamClient(QObject *parent = nullptr);
    ~MediapipeStreamClient() override;

    bool start(
        const QString &bridgeExePath,
        const QString &taskModelPath,
        int cameraIndex,
        bool mirror,
        bool showPreview,
        QString *errorMessage = nullptr);
    void stop();

    bool isRunning() const;
    bool takeLatestObservation(GestureFrameObservation *observation);
    QString lastError() const;
    QString lastStdErrLine() const;

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    static QString trimLine(const QByteArray &line);
    bool parseObservationLine(
        const QByteArray &line,
        GestureFrameObservation *observation,
        QString *errorMessage) const;
    void setError(const QString &message);

    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    std::optional<GestureFrameObservation> m_latestObservation;
    QString m_lastError;
    QString m_lastStdErrLine;
};

#endif
