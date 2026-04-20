#ifndef HOME_AUTOMATION_FRAME_SEQUENCE_WIDGET_H
#define HOME_AUTOMATION_FRAME_SEQUENCE_WIDGET_H

#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QResizeEvent;
class QTimer;

class FrameSequenceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FrameSequenceWidget(QWidget *parent = nullptr);

    bool loadFromDirectory(const QString &sequenceDirectory);
    void clearFrames();

    int frameCount() const;
    int normalizedProgress() const;
    QString sequenceDirectory() const;

    void setNormalizedProgress(int progress);
    void setPlaybackIntervalMs(int intervalMs);
    void setLoopPlayback(bool enabled);
    void startPlayback();
    void stopPlayback();
    bool isPlaying() const;

signals:
    void normalizedProgressChanged(int progress);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    int normalizedToFrameIndex(int normalizedValue) const;
    int frameIndexToNormalized(int frameIndex) const;
    void advanceFrame();
    void updateFramePixmap();

    QLabel *m_frameLabel = nullptr;
    QTimer *m_playbackTimer = nullptr;

    QString m_sequenceDirectory;
    QStringList m_framePaths;

    int m_currentFrameIndex = 0;
    int m_normalizedProgress = 0;
    bool m_loopPlayback = true;
};

#endif
