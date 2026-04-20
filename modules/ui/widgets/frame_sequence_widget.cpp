#include "frame_sequence_widget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
QPixmap toGrayPreserveAlpha(const QPixmap &sourcePixmap)
{
    QImage image = sourcePixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y)
    {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const QRgb pixel = row[x];
            const int gray = qGray(pixel);
            row[x] = qRgba(gray, gray, gray, qAlpha(pixel));
        }
    }
    return QPixmap::fromImage(image);
}

QStringList sortedFramePaths(const QString &sequenceDirectory)
{
    static const QStringList kImageFilters = {
        QStringLiteral("*.png"),
        QStringLiteral("*.jpg"),
        QStringLiteral("*.jpeg"),
        QStringLiteral("*.bmp"),
        QStringLiteral("*.webp"),
    };

    QDir sequenceDir(sequenceDirectory);
    const QFileInfoList entries = sequenceDir.entryInfoList(
        kImageFilters,
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);

    std::vector<QFileInfo> sortedEntries;
    sortedEntries.reserve(static_cast<size_t>(entries.size()));
    for (const QFileInfo &entry : entries)
    {
        sortedEntries.push_back(entry);
    }

    std::sort(sortedEntries.begin(), sortedEntries.end(), [](const QFileInfo &lhs, const QFileInfo &rhs) {
        bool leftNumeric = false;
        bool rightNumeric = false;
        const int leftNumber = lhs.completeBaseName().toInt(&leftNumeric);
        const int rightNumber = rhs.completeBaseName().toInt(&rightNumeric);
        if (leftNumeric && rightNumeric && leftNumber != rightNumber)
        {
            return leftNumber < rightNumber;
        }
        if (leftNumeric != rightNumeric)
        {
            return leftNumeric;
        }
        return QString::compare(lhs.fileName(), rhs.fileName(), Qt::CaseInsensitive) < 0;
    });

    QStringList framePaths;
    for (const QFileInfo &entry : sortedEntries)
    {
        framePaths.push_back(entry.absoluteFilePath());
    }
    return framePaths;
}
} // namespace

FrameSequenceWidget::FrameSequenceWidget(QWidget *parent)
    : QWidget(parent),
      m_frameLabel(new QLabel(this)),
      m_playbackTimer(new QTimer(this))
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_frameLabel->setObjectName(QStringLiteral("SequenceFramePreview"));
    m_frameLabel->setAlignment(Qt::AlignCenter);
    m_frameLabel->setText(QStringLiteral("未加载序列帧"));
    rootLayout->addWidget(m_frameLabel);

    m_playbackTimer->setInterval(33);
    connect(m_playbackTimer, &QTimer::timeout, this, &FrameSequenceWidget::advanceFrame);
}

bool FrameSequenceWidget::loadFromDirectory(const QString &sequenceDirectory)
{
    const QString normalizedDirectory = QDir::cleanPath(sequenceDirectory.trimmed());
    if (normalizedDirectory.isEmpty())
    {
        clearFrames();
        return false;
    }

    const QStringList framePaths = sortedFramePaths(normalizedDirectory);
    if (framePaths.isEmpty())
    {
        clearFrames();
        return false;
    }

    m_sequenceDirectory = normalizedDirectory;
    m_framePaths = framePaths;
    m_currentFrameIndex = 0;
    m_normalizedProgress = 0;
    updateFramePixmap();
    emit normalizedProgressChanged(m_normalizedProgress);
    return true;
}

void FrameSequenceWidget::clearFrames()
{
    stopPlayback();
    m_sequenceDirectory.clear();
    m_framePaths.clear();
    m_currentFrameIndex = 0;
    m_normalizedProgress = 0;
    if (m_frameLabel)
    {
        m_frameLabel->clear();
        m_frameLabel->setText(QStringLiteral("未加载序列帧"));
    }
    emit normalizedProgressChanged(m_normalizedProgress);
}

int FrameSequenceWidget::frameCount() const
{
    return m_framePaths.size();
}

int FrameSequenceWidget::normalizedProgress() const
{
    return m_normalizedProgress;
}

QString FrameSequenceWidget::sequenceDirectory() const
{
    return m_sequenceDirectory;
}

void FrameSequenceWidget::setNormalizedProgress(int progress)
{
    if (m_framePaths.isEmpty())
    {
        return;
    }

    const int clampedProgress = std::clamp(progress, 0, 100);
    const int targetFrameIndex = normalizedToFrameIndex(clampedProgress);
    if (targetFrameIndex == m_currentFrameIndex && clampedProgress == m_normalizedProgress)
    {
        return;
    }

    m_currentFrameIndex = targetFrameIndex;
    m_normalizedProgress = clampedProgress;
    updateFramePixmap();
    emit normalizedProgressChanged(m_normalizedProgress);
}

void FrameSequenceWidget::setPlaybackIntervalMs(int intervalMs)
{
    if (!m_playbackTimer)
    {
        return;
    }
    m_playbackTimer->setInterval(std::clamp(intervalMs, 10, 2000));
}

void FrameSequenceWidget::setLoopPlayback(bool enabled)
{
    m_loopPlayback = enabled;
}

void FrameSequenceWidget::startPlayback()
{
    if (!m_playbackTimer || m_framePaths.size() <= 1)
    {
        return;
    }
    m_playbackTimer->start();
}

void FrameSequenceWidget::stopPlayback()
{
    if (m_playbackTimer)
    {
        m_playbackTimer->stop();
    }
}

bool FrameSequenceWidget::isPlaying() const
{
    return m_playbackTimer && m_playbackTimer->isActive();
}

void FrameSequenceWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateFramePixmap();
}

int FrameSequenceWidget::normalizedToFrameIndex(int normalizedValue) const
{
    const int count = m_framePaths.size();
    if (count <= 1)
    {
        return 0;
    }
    const double ratio = static_cast<double>(std::clamp(normalizedValue, 0, 100)) / 100.0;
    return static_cast<int>(std::llround(ratio * static_cast<double>(count - 1)));
}

int FrameSequenceWidget::frameIndexToNormalized(int frameIndex) const
{
    const int count = m_framePaths.size();
    if (count <= 1)
    {
        return 0;
    }
    const int clampedIndex = std::clamp(frameIndex, 0, count - 1);
    const double ratio = static_cast<double>(clampedIndex) / static_cast<double>(count - 1);
    return static_cast<int>(std::llround(ratio * 100.0));
}

void FrameSequenceWidget::advanceFrame()
{
    if (m_framePaths.size() <= 1)
    {
        return;
    }

    int nextIndex = m_currentFrameIndex + 1;
    if (nextIndex >= m_framePaths.size())
    {
        if (!m_loopPlayback)
        {
            stopPlayback();
            return;
        }
        nextIndex = 0;
    }

    m_currentFrameIndex = nextIndex;
    m_normalizedProgress = frameIndexToNormalized(m_currentFrameIndex);
    updateFramePixmap();
    emit normalizedProgressChanged(m_normalizedProgress);
}

void FrameSequenceWidget::updateFramePixmap()
{
    if (!m_frameLabel)
    {
        return;
    }

    if (m_framePaths.isEmpty())
    {
        m_frameLabel->clear();
        m_frameLabel->setText(QStringLiteral("未加载序列帧"));
        return;
    }

    if (m_currentFrameIndex < 0 || m_currentFrameIndex >= m_framePaths.size())
    {
        m_currentFrameIndex = std::clamp(
            m_currentFrameIndex,
            0,
            static_cast<int>(m_framePaths.size() - 1));
    }

    const QPixmap framePixmap(m_framePaths.at(m_currentFrameIndex));
    if (framePixmap.isNull())
    {
        m_frameLabel->clear();
        m_frameLabel->setText(QStringLiteral("序列帧加载失败"));
        return;
    }

    const QSize targetSize = m_frameLabel->size();
    if (targetSize.isEmpty())
    {
        m_frameLabel->setPixmap(toGrayPreserveAlpha(framePixmap));
        return;
    }

    const QPixmap scaledPixmap =
        framePixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_frameLabel->setPixmap(toGrayPreserveAlpha(scaledPixmap));
}
