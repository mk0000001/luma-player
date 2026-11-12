#include "performance_overlay.h"
#include "native_playback_move.h"
#include "video_surface.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QIODevice>
#include <QJsonArray>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace luma {
namespace {

constexpr double mebibyte = 1024.0 * 1024.0;
constexpr double gibibyte = 1024.0 * mebibyte;
constexpr double bytesToMegabits = 8.0 / 1000000.0;
constexpr int metricsTop = 26;
constexpr int hardwareRowHeight = 16;
constexpr int footerRowHeight = 17;

QString valueText(std::optional<double> value, const QString &unit, int precision = 0) {
    return value ? QString::number(*value, 'f', precision) + unit : QStringLiteral("—");
}

QJsonValue jsonValue(std::optional<double> value) {
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

QJsonValue memoryMiB(std::optional<quint64> bytes) {
    return jsonValue(bytes ? std::optional<double>(double(*bytes) / mebibyte) : std::nullopt);
}

void insertMetric(QJsonObject &record,const char *key,std::optional<double> value,
                  double minimum,double maximum,double scale=100.0) {
    if(value && std::isfinite(*value) && *value>=minimum && *value<=maximum)
        record.insert(key,std::round(*value*scale)/scale);
}

void insertMemory(QJsonObject &record,const char *key,std::optional<quint64> bytes) {
    if(bytes)insertMetric(record,key,double(*bytes)/mebibyte,0,1024.0*1024*1024);
}

PlaybackState performancePlaybackState(const PlaybackState &source) {
    PlaybackState state;
    state.positionMs=source.positionMs;
    state.currentTitle=source.currentTitle;
    state.playing=source.playing;
    state.buffering=source.buffering;
    state.failed=source.failed;
    state.fileCacheHolding=source.fileCacheHolding;
    state.discStatisticsAvailable=source.discStatisticsAvailable;
    state.discInputBytes=source.discInputBytes;
    state.discDemuxBytes=source.discDemuxBytes;
    state.displayedPictures=source.displayedPictures;
    state.droppedVideoFrames=source.droppedVideoFrames;
    state.lostAudioBuffers=source.lostAudioBuffers;
    state.sourceFps=source.sourceFps;
    state.filteredFps=source.filteredFps;
    state.displayFps=source.displayFps;
    state.cacheSeconds=source.cacheSeconds;
    state.avSyncMs=source.avSyncMs;
    state.hardwareDecoder=source.hardwareDecoder;
    state.videoCodec=source.videoCodec;
    return state;
}

QString networkRateText(std::optional<double> megabits, bool tight = false) {
    if (megabits && *megabits < 1)
        return valueText(*megabits * 1000, tight ? " kb/s" : " Kbps", tight ? 0 : 1);
    return valueText(megabits, tight ? " Mb/s" : " Mbps", tight ? 1 : 2);
}

QString videoMemoryText(const GraphicsMetrics &adapter) {
    if (!adapter.dedicatedMemoryTotal)
        return QStringLiteral("—");
    const auto total = double(*adapter.dedicatedMemoryTotal);
    const bool useGiB = total >= gibibyte;
    const auto unitSize = useGiB ? gibibyte : mebibyte;
    const int precision = useGiB ? 1 : 0;
    const auto used = adapter.dedicatedMemoryUsed ? std::optional<double>(double(*adapter.dedicatedMemoryUsed) / unitSize) : std::nullopt;
    return valueText(used, "", precision) + '/' + valueText(total / unitSize, useGiB ? " GiB" : " MiB", precision);
}

bool showProcessorTemperatures(const SystemMetrics &system, bool compact) {
    return !compact || system.npuTemperatureCelsius.has_value();
}

int hardwareRows(const SystemMetrics &system, bool compact) {
#ifdef Q_OS_WIN
    return int(std::max<qsizetype>(1, system.graphicsAdapters.size())) +
           int(std::max<qsizetype>(1, system.storageTemperatures.size())) + int(showProcessorTemperatures(system, compact));
#else
    Q_UNUSED(system);
    Q_UNUSED(compact);
    return 0;
#endif
}

std::optional<double> readRate(quint32 before, quint32 after, double seconds) {
    const auto bytes = quint32(after - before);
    // Unsigned subtraction also handles a single wrap of the libVLC counter.
    // A source reset produces an implausible delta instead of a read spike.
    const auto rate = double(bytes) / mebibyte / seconds;
    if (rate > 512 || (before == 0 && after == 0))
        return {};
    return rate;
}

}

PerformanceOverlay::PerformanceOverlay(QWidget *anchor, std::function<PerformanceInput()> input)
    : QWidget(anchor->window(), Qt::Tool | Qt::FramelessWindowHint |
             Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus),
      anchor_(anchor), input_(std::move(input)) {
    setObjectName("performanceOverlay");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    anchor->installEventFilter(this);
    anchor->window()->installEventFilter(this);
    if (auto *surface = qobject_cast<VideoSurface *>(anchor))
        connect(surface, &VideoSurface::videoStackingChanged, this, &PerformanceOverlay::syncGeometry);
    timer_.setInterval(sampleIntervalMs);
    connect(&timer_, &QTimer::timeout, this, &PerformanceOverlay::requestSample);
}

PerformanceOverlay::~PerformanceOverlay() {
    timer_.stop();
    thread_.quit();
    thread_.wait();
}

void PerformanceOverlay::setMonitoring(bool enabled) {
    if (monitoring_ == enabled)
        return;
    monitoring_ = enabled;
    syncSampling();
    syncGeometry();
}

void PerformanceOverlay::setDiagnosticCollection(bool enabled) {
    if(diagnosticCollection_ == enabled)
        return;
    diagnosticCollection_ = enabled;
    syncSampling();
}

void PerformanceOverlay::syncSampling() {
    const bool enabled = monitoring_ || diagnosticCollection_;
    if (timer_.isActive() == enabled)
        return;
    ++generation_;
    if (enabled) {
        if (!worker_) {
            worker_ = new QObject;
            worker_->moveToThread(&thread_);
            connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
            thread_.start();
        }
        clock_.start();
        timer_.start();
        requestSample();
    } else {
        timer_.stop();
        history_.clear();
        clock_.invalidate();
        // Discard the counters on their owning thread once any in-flight read finishes.
        QMetaObject::invokeMethod(worker_, [this] { reader_.reset(); }, Qt::QueuedConnection);
    }
}

void PerformanceOverlay::setView(View view) {
    view_ = view;
    syncGeometry();
    update();
}

void PerformanceOverlay::setLeftAligned(bool left) {
    leftAligned_ = left;
    syncGeometry();
}

void PerformanceOverlay::clearHistory() {
    history_.clear();
    ++generation_;
    update();
    requestSample();
}

qint64 PerformanceOverlay::elapsedMs() const {
    return clock_.isValid() ? clock_.elapsed() : -1;
}

bool PerformanceOverlay::canSample() const {
    return diagnosticCollection_ || (monitoring_ && isVisible());
}

void PerformanceOverlay::requestSample() {
    if (monitoring_)
        syncGeometry();
    if (!worker_ || pending_ || !canSample())
        return;
    pending_ = true;
    const auto generation = generation_;
    QMetaObject::invokeMethod(worker_, [this, generation] {
        if (!reader_ || workerGeneration_ != generation) {
            reader_ = std::make_unique<SystemMetricsReader>();
            workerGeneration_ = generation;
        }
        const auto system = reader_->sample();
        QMetaObject::invokeMethod(this, [this, generation, system] {
            pending_ = false;
            if (generation == generation_ && canSample())
                appendSample(system);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

std::optional<double> PerformanceOverlay::presentedRate(const Sample &sample) const {
    const auto &playback = *sample.input.playback;
    if (!playback.playing || playback.buffering || playback.displayedPictures <= 0)
        return {};
    for (auto it = history_.crbegin(); it != history_.crend(); ++it) {
        const auto &previous = it->input.playback;
        const auto age = sample.elapsedMs - it->elapsedMs;
        if (!previous || !previous->playing || previous->buffering || !previous->discStatisticsAvailable ||
            previous->displayedPictures <= 0 || previous->displayedPictures > playback.displayedPictures ||
            it->input.sourceGeneration != sample.input.sourceGeneration ||
            previous->currentTitle != playback.currentTitle || age > 4500)
            break;
        const auto advance = playback.positionMs - previous->positionMs;
        if (advance < -250 || advance > age * 4 + 250)
            break;
        if (age >= 2500)
            return 1000.0 * (playback.displayedPictures - previous->displayedPictures) / age;
    }
    return {};
}

void PerformanceOverlay::appendSample(SystemMetrics system) {
    Sample sample;
    sample.elapsedMs = clock_.elapsed();
    sample.system = system;
    sample.input = input_();
    if (auto &state = sample.input.playback)
        *state = performancePlaybackState(*state);
    auto &values = sample.values;
    values[Cpu] = system.cpuPercent;
    values[Gpu] = system.gpuPercent;
    values[Npu] = system.npuPercent;
    if (system.networkRxBytesPerSecond)
        values[NetworkRx] = *system.networkRxBytesPerSecond * bytesToMegabits;
    if (system.networkTxBytesPerSecond)
        values[NetworkTx] = *system.networkTxBytesPerSecond * bytesToMegabits;
    sample.playerValues[Cpu] = system.playerCpuPercent;
    sample.playerValues[Gpu] = system.playerGpuPercent;
    sample.playerValues[Npu] = system.playerNpuPercent;
    if (system.memoryTotal && *system.memoryTotal > 0) {
        if (system.memoryUsed)
            values[Ram] = 100.0 * double(*system.memoryUsed) / double(*system.memoryTotal);
        if (system.playerResident)
            sample.playerValues[Ram] = 100.0 * double(*system.playerResident) / double(*system.memoryTotal);
    }

    const auto &playback = sample.input.playback;
    if (playback && !playback->failed) {
        if (sample.input.disc && playback->discStatisticsAvailable && !history_.isEmpty()) {
            const auto &previous = history_.constLast();
            const auto &old = previous.input.playback;
            const double seconds = (sample.elapsedMs - previous.elapsedMs) / 1000.0;
            if (old && old->discStatisticsAvailable && previous.input.sourceGeneration == sample.input.sourceGeneration &&
                old->currentTitle == playback->currentTitle && seconds >= 0.25 && seconds <= 5) {
                sample.inputMiBps = readRate(old->discInputBytes, playback->discInputBytes, seconds);
                sample.demuxMiBps = readRate(old->discDemuxBytes, playback->discDemuxBytes, seconds);
            }
            sample.presentedFps = presentedRate(sample);
            values[DiscRate] = sample.inputMiBps ? sample.inputMiBps : sample.demuxMiBps;
        }
        values[Fps] = sample.presentedFps;
        if (!sample.input.disc && playback->filteredFps > 0)
            values[Fps] = playback->filteredFps;
    }

    const auto limit = diagnosticCollection_ ? diagnosticHistoryLimit : overlayHistoryLimit;
    while (!history_.isEmpty() && (history_.size() >= limit ||
           sample.elapsedMs - history_.constFirst().elapsedMs > diagnosticRetentionMs))
        history_.removeFirst();
    history_.append(std::move(sample));
    if (monitoring_ && anchor_ && height() != contentLayout(anchor_->height()).height)
        syncGeometry();
    if (isVisible())
        update();
}

bool PerformanceOverlay::eventFilter(QObject *, QEvent *event) {
    switch (event->type()) {
    case QEvent::Move:
        if (anchor_ && !playbackWindowsMoving(anchor_->window()))
            syncGeometry();
        break;
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::WinIdChange:
    case QEvent::WindowStateChange:
        QTimer::singleShot(0, this, [this] { syncGeometry(); });
        break;
    default:
        break;
    }
    return false;
}

void PerformanceOverlay::syncGeometry() {
    if (!monitoring_ || !anchor_ || !anchor_->isVisible() || anchor_->window()->isMinimized() ||
        anchor_->width() < 180 || anchor_->height() < 155 || QApplication::activeModalWidget()) {
        hide();
        return;
    }
    const int width = std::min(418, anchor_->width() - 24);
    const int height = contentLayout(anchor_->height()).height;
    const auto point = anchor_->mapToGlobal(QPoint(leftAligned_ ? 12 : anchor_->width() - width - 12, 10));
    const QRect target(point, QSize(width, height));
    if (geometry() != target)
        setGeometry(target);
    if (!isVisible()) {
        show();
        raise();
    }
    keepPlaybackOverlayBelowDialogs(this, anchor_->window());
    if (auto *surface = qobject_cast<VideoSurface *>(anchor_.data()))
        keepPlaybackOverlayAboveVideo(this, surface->videoWindow());
}

PerformanceOverlay::ContentLayout PerformanceOverlay::contentLayout(int anchorHeight) const {
    const bool compact = anchorHeight < 400;
    const bool chart = view_ != View::Text && anchorHeight >= 215;
    const int rowHeight = compact ? (chart ? 29 : 17) : (chart ? 46 : 33);
    if (history_.isEmpty())
        return {compact, chart, rowHeight, 0, 58};
    const auto &sample = history_.constLast();
    const int footerRows = compact ? 1 : (sample.input.playback ? 3 : 2);
    const int baseHeight = metricsTop + (visibleMetricCount + 1) / 2 * rowHeight + footerRows * footerRowHeight + 6;
    const int availableRows = std::max(0, (anchorHeight - 20 - baseHeight) / hardwareRowHeight);
    const int rows = std::min({hardwareRows(sample.system, compact), availableRows, 7});
    return {compact, chart, rowHeight, rows, baseHeight + rows * hardwareRowHeight};
}

QList<PerformanceOverlay::Sample>::const_iterator PerformanceOverlay::overlaySamplesBegin() const {
    return history_.cend() - std::min(history_.size(), overlayHistoryLimit);
}

double PerformanceOverlay::graphMaximum(Metric metric) const {
    if (metric == NetworkRx || metric == NetworkTx) {
        double maximum = 0.01;
        for (auto it = overlaySamplesBegin(); it != history_.cend(); ++it)
            maximum = std::max({maximum, it->values[NetworkRx].value_or(0), it->values[NetworkTx].value_or(0)});
        return maximum;
    }
    double maximum = metric <= Ram ? 100 : metric == Fps ? 30 : 1;
    if (metric > Ram) {
        for (auto it = overlaySamplesBegin(); it != history_.cend(); ++it)
            maximum = std::max(maximum, it->values[metric].value_or(0.0));
    }
    return maximum;
}

void PerformanceOverlay::paintGraph(QPainter &painter, const QRectF &area, Metric metric, const QColor &color) const {
    painter.setPen(QColor("#334154"));
    painter.drawLine(area.bottomLeft(), area.bottomRight());
    const double maximum = graphMaximum(metric);
    const auto now = history_.constLast().elapsedMs;
    const auto draw = [&](bool player, const QPen &pen) {
        QPainterPath path;
        bool connected = false;
        QPointF last;
        for (auto it = overlaySamplesBegin(); it != history_.cend(); ++it) {
            const auto value = player ? it->playerValues[metric] : it->values[metric];
            const auto age = now - it->elapsedMs;
            if (!value || age >= 90000) {
                connected = false;
                continue;
            }
            const QPointF point(area.right() - area.width() * age / 90000.0,
                                area.bottom() - area.height() * std::clamp(*value / maximum, 0.0, 1.0));
            if (connected)
                path.lineTo(point);
            else
                path.moveTo(point);
            connected = true;
            last = point;
        }
        painter.setPen(pen);
        painter.drawPath(path);
        if (connected)
            painter.drawEllipse(last, 1.5, 1.5);
    };
    draw(false, QPen(color, 1.5));
    if (metric <= Ram)
        draw(true, QPen(QColor("#eef5ff"), 1.4, Qt::DashLine));
}

void PerformanceOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#435166"), 1));
    painter.setBrush(QColor(13, 19, 29, 237));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    const auto line = [&](QRect area, const QString &text, const QColor &color, int size, bool bold = false) {
        auto font = painter.font();
        font.setPixelSize(size);
        font.setBold(bold);
        painter.setFont(font);
        painter.setPen(color);
        painter.drawText(area, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(font).elidedText(text, Qt::ElideRight, area.width()));
    };
    const auto layout = contentLayout(anchor_ ? anchor_->height() : height() + 20);
    line({12, 5, width() - 24, 20}, !layout.chart ? "재생 성능  ·  전체 / 플레이어" :
         "재생 성능  ·  전체 실선 / 플레이어 점선", QColor("#dce6f5"), 12, true);
    if (history_.isEmpty()) {
        line({12, 32, width() - 24, 20}, "성능 지표를 수집하는 중…", QColor("#9cacc3"), 12);
        return;
    }

    const auto &sample = history_.constLast();
    const auto &state = sample.input.playback;
    const auto &values = sample.values;
    const auto color = QColor("#9cacc3");
    const bool numbers = view_ != View::Graph || !layout.chart;
    const bool narrow = width() < 330;
    const bool tight = width() < 230;
    const int cellWidth = (width() - 30) / 2;
    const std::array<QString, MetricCount> titles{
        "CPU", "GPU", "NPU", "RAM",
        sample.input.disc ? "FPS · 표시" : "FPS · 처리",
        sample.inputMiBps ? "Blu-ray I/O · 입력" : "Blu-ray I/O · 디먹싱",
        tight ? "↓" : narrow ? "수신" : "네트워크 수신",
        tight ? "↑" : narrow ? "송신" : "네트워크 송신"
    };
    const std::array<QString, MetricCount> shown{
        valueText(values[Cpu], "%"), valueText(values[Gpu], "%"), valueText(values[Npu], "%"),
        valueText(values[Ram], "%"), valueText(values[Fps], "", 1), valueText(values[DiscRate], " MiB/s", 1),
        networkRateText(values[NetworkRx], tight), networkRateText(values[NetworkTx], tight)
    };
    const std::array<QString, MetricCount> details{
        sample.system.includesChildProcesses ? "플레이어와 하위 작업 포함" : "플레이어 본체",
        "디코드 " + valueText(sample.system.gpuDecodePercent, "%") + " / " + valueText(sample.system.playerGpuDecodePercent, "%"),
        sample.system.npuDetected ? (values[Npu] ? "가장 바쁜 엔진" : "드라이버 수치 없음") : "측정 가능한 장치 없음",
        (sample.system.memoryUsed ? QString::number(double(*sample.system.memoryUsed) / gibibyte, 'f', 1) + " GiB" : "—") +
            (sample.system.playerResident ? QString(" / %1 MiB").arg(double(*sample.system.playerResident) / mebibyte, 0, 'f', 0) : " / —"),
        state && state->sourceFps > 0 ? QString("원본 %1 fps").arg(state->sourceFps, 0, 'f', 2) : "원본 —",
        sample.input.disc ? "libVLC 재생 경로" : "Blu-ray 재생 시 표시",
        "활성 물리 연결 합계", {}
    };
    const std::array<QColor, MetricCount> accents{
        QColor("#73cfff"), QColor("#67e0b1"), QColor("#bdabff"),
        QColor("#edca81"), QColor("#f49ab5"), QColor("#a9c6ff"), QColor("#87d9eb"), QColor("#e3afef")
    };
    for (int i = 0; i < visibleMetricCount; ++i) {
        const auto metric = static_cast<Metric>(i);
        const int x = 12 + (i % 2) * (cellWidth + 6);
        const int y = metricsTop + (i / 2) * layout.metricHeight;
        const bool showValue = numbers || i >= NetworkRx;
        const auto spacing = i >= NetworkRx && narrow ? " " : "  ";
        QString label = titles[i] + (showValue ? spacing + shown[i] : values[i] ? "" : "  —");
        if (numbers && i <= Ram)
            label += " / " + valueText(sample.playerValues[i], "%", 1);
        line({x, y, cellWidth, 16}, label, accents[i], i >= NetworkRx && tight ? 10 : 11, true);
        if (!layout.compact) {
            const auto scale = i <= Ram ? QString("%") : i == Fps ? QString(" fps") : QString(" MiB/s");
            const auto description = numbers || !values[i] ? details[i] : QString("0–") +
                (i >= NetworkRx ? networkRateText(graphMaximum(metric)) : valueText(graphMaximum(metric), scale, i == DiscRate ? 1 : 0));
            line({x, y + 15, cellWidth, 14}, description, color, 10);
        }
        if (layout.chart)
            paintGraph(painter, QRectF(x, y + (layout.compact ? 16 : 30), cellWidth - 3, layout.compact ? 10 : 12),
                       metric, accents[i]);
    }
    const int devicesTop = metricsTop + (visibleMetricCount + 1) / 2 * layout.metricHeight;
    if (layout.hardwareRows) {
        const auto &system = sample.system;
        const int totalRows = hardwareRows(system, layout.compact);
        const bool overflow = totalRows > layout.hardwareRows && layout.hardwareRows > 1;
        const int limit = layout.hardwareRows - (overflow ? 1 : 0);
        const int deviceFontSize = tight ? 9 : 10;
        int row = 0;
        auto deviceFont = painter.font();
        deviceFont.setPixelSize(deviceFontSize);
        deviceFont.setBold(false);
        const QFontMetrics deviceFontMetrics(deviceFont);
        const auto deviceLine = [&](const QString &name, const QString &reading) {
            if (row >= limit)
                return;
            const int y = devicesTop + row++ * hardwareRowHeight;
            const int available = width() - 24;
            const int readingWidth = std::min(available, deviceFontMetrics.horizontalAdvance(reading) + 1);
            line({12, y, std::max(0, available - readingWidth - 6), hardwareRowHeight}, name, color, deviceFontSize);
            line({width() - 12 - readingWidth, y, readingWidth, hardwareRowHeight}, reading, QColor("#dce6f5"), deviceFontSize);
        };
        if (system.graphicsAdapters.isEmpty())
            deviceLine("GPU 온도 · VRAM", "—");
        for (const auto &adapter : system.graphicsAdapters) {
            const auto memoryLabel = adapter.integrated == true ? (tight ? "예약" : "예약 메모리") :
                                                                   (tight ? "VRAM" : "전용 VRAM");
            const auto temperature = valueText(adapter.temperatureCelsius, "°C", tight ? 0 : 1);
            deviceLine(tight ? QString("GPU") : adapter.name, QString(tight ? "%1 %2 %3" : "%1  ·  %2 %3")
                                                             .arg(temperature, memoryLabel, videoMemoryText(adapter)));
        }
        if (system.storageTemperatures.isEmpty())
            deviceLine("저장장치 온도", "—");
        for (const auto &disk : system.storageTemperatures) {
            const auto name = disk.sensorIndex ? QString("디스크 %1 · 센서 %2").arg(disk.diskNumber).arg(disk.sensorIndex) :
                                                  QString("디스크 %1").arg(disk.diskNumber);
            deviceLine(name, valueText(disk.temperatureCelsius, "°C"));
        }
        if (row < limit && showProcessorTemperatures(system, layout.compact))
            line({12, devicesTop + row * hardwareRowHeight, width() - 24, hardwareRowHeight},
                 QString("CPU 온도 —  ·  NPU 온도 %1").arg(valueText(system.npuTemperatureCelsius, "°C", 1)), color, 10);
        if (overflow)
            line({12, devicesTop + limit * hardwareRowHeight, width() - 24, hardwareRowHeight},
                 QString("장치 정보 %1줄 더 있음 · CSV").arg(totalRows - limit), color, 10);
    }
    const int footer = devicesTop + layout.hardwareRows * hardwareRowHeight;
    const auto status = sample.input.captionWaiting ? QString("자막 대기") :
        state && (state->buffering || state->fileCacheHolding) ? QString("재생 버퍼링") :
        state && state->failed ? QString("재생 오류") : state && state->playing ? QString("재생 중") : QString("대기");
    const auto dropped = state ? QString::number(state->droppedVideoFrames) : QString("—");
    line({12, footer, width() - 24, 16},
         QString("%1  ·  드롭 %2  ·  자막 버퍼 %3초").arg(status, dropped).arg(sample.input.captionAheadMs / 1000.0, 0, 'f', 1),
         color, 10);
    if (!layout.compact) {
        const auto decode = state && !state->hardwareDecoder.isEmpty() ? state->hardwareDecoder : QString("—");
        line({12, footer + footerRowHeight, width() - 24, 16},
             QString("디코더 %1  ·  인식 %2  ·  번역 %3").arg(decode,
                 sample.input.recognitionBackend.isEmpty() ? "—" : sample.input.recognitionBackend,
                 sample.input.translationBackend.isEmpty() ? "—" : sample.input.translationBackend), color, 10);
        if (state) {
            const auto extra = sample.input.disc ?
                QString("입력 %1  ·  디먹싱 %2").arg(valueText(sample.inputMiBps, " MiB/s", 1), valueText(sample.demuxMiBps, " MiB/s", 1)) :
                QString("A/V 편차 %1  ·  캐시 %2  ·  주사율 %3").arg(
                    valueText(state->avSyncMs, " ms", 1), valueText(state->cacheSeconds, "초", 1),
                    valueText(state->displayFps > 0 ? std::optional<double>(state->displayFps) : std::nullopt, " Hz", 1));
            line({12, footer + 2 * footerRowHeight, width() - 24, 16}, extra, color, 10);
        }
    }
}

QJsonObject PerformanceOverlay::sampleJson(const Sample &sample) {
    const auto &v = sample.values;
    QJsonArray graphics;
    for (const auto &adapter : sample.system.graphicsAdapters) {
        graphics.append(QJsonObject{
            {"luid", QString::number(adapter.adapterLuid, 16)}, {"name", adapter.name},
            {"integrated", adapter.integrated ? QJsonValue(*adapter.integrated) : QJsonValue(QJsonValue::Null)},
            {"temperatureCelsius", jsonValue(adapter.temperatureCelsius)},
            {"dedicatedMemoryUsedMiB", memoryMiB(adapter.dedicatedMemoryUsed)},
            {"dedicatedMemoryTotalMiB", memoryMiB(adapter.dedicatedMemoryTotal)}
        });
    }
    QJsonArray storage;
    for (const auto &disk : sample.system.storageTemperatures) {
        storage.append(QJsonObject{
            {"diskNumber", qint64(disk.diskNumber)}, {"sensorIndex", disk.sensorIndex},
            {"temperatureCelsius", disk.temperatureCelsius}
        });
    }
    QJsonObject result{
        {"elapsedMs", sample.elapsedMs}, {"cpuPercent", jsonValue(v[Cpu])}, {"gpuPercent", jsonValue(v[Gpu])},
        {"gpuDecodePercent", jsonValue(sample.system.gpuDecodePercent)}, {"npuPercent", jsonValue(v[Npu])},
        {"npuDetected", sample.system.npuDetected}, {"ramPercent", jsonValue(v[Ram])},
        {"ramUsedMiB", memoryMiB(sample.system.memoryUsed)}, {"ramTotalMiB", memoryMiB(sample.system.memoryTotal)},
        {"playerResidentMiB", memoryMiB(sample.system.playerResident)},
        {"networkRxBytesPerSecond", jsonValue(sample.system.networkRxBytesPerSecond)},
        {"networkTxBytesPerSecond", jsonValue(sample.system.networkTxBytesPerSecond)},
        {"cpuTemperatureCelsius", QJsonValue(QJsonValue::Null)}, {"npuTemperatureCelsius", jsonValue(sample.system.npuTemperatureCelsius)},
        {"graphicsAdapters", graphics}, {"storageTemperatures", storage},
        {"discInputMiBps", jsonValue(sample.inputMiBps)}, {"discDemuxMiBps", jsonValue(sample.demuxMiBps)},
        {"presentedFps", jsonValue(sample.presentedFps)}, {"captionAheadMs", sample.input.captionAheadMs},
        {"captionWaiting", sample.input.captionWaiting}, {"videoRecoveries", sample.input.recoveries},
        {"recognitionBackend", sample.input.recognitionBackend}, {"translationBackend", sample.input.translationBackend}
    };
    result.insert("playerCpuPercent", jsonValue(sample.system.playerCpuPercent));
    result.insert("playerGpuPercent", jsonValue(sample.system.playerGpuPercent));
    result.insert("playerNpuPercent", jsonValue(sample.system.playerNpuPercent));
    result.insert("playerGpuDecodePercent", jsonValue(sample.system.playerGpuDecodePercent));
    result.insert("playerRamPercent", jsonValue(sample.playerValues[Ram]));
    result.insert("playerIncludesChildProcesses", sample.system.includesChildProcesses);
    if (const auto &state = sample.input.playback) {
        result.insert("engine", sample.input.disc ? "libVLC" : "libmpv");
        result.insert("playing", state->playing);
        result.insert("buffering", state->buffering || state->fileCacheHolding);
        result.insert("failed", state->failed);
        result.insert("droppedFrames", state->droppedVideoFrames);
        result.insert("lostAudioBuffers", state->lostAudioBuffers);
        result.insert("decoder", state->hardwareDecoder);
        result.insert("videoCodec", state->videoCodec);
        result.insert("sourceFps", jsonValue(state->sourceFps > 0 ? std::optional<double>(state->sourceFps) : std::nullopt));
        result.insert("filteredFps", jsonValue(state->filteredFps > 0 ? std::optional<double>(state->filteredFps) : std::nullopt));
        result.insert("displayHz", jsonValue(state->displayFps > 0 ? std::optional<double>(state->displayFps) : std::nullopt));
        result.insert("avSyncMs", jsonValue(state->avSyncMs));
        result.insert("fileCacheSeconds", jsonValue(state->cacheSeconds));
    }
    return result;
}

QJsonObject PerformanceOverlay::diagnosticSample(const Sample &sample) {
    const auto &system = sample.system;
    QJsonObject result{
        {"elapsedMs",sample.elapsedMs},
        {"npuDetected",system.npuDetected},
        {"playerIncludesChildProcesses",system.includesChildProcesses},
        {"captionWaiting",sample.input.captionWaiting},
        {"captionAheadMs",std::clamp<qint64>(sample.input.captionAheadMs,0,24*60*60*1000)},
        {"videoRecoveries",sample.input.recoveries}
    };
    insertMetric(result,"cpuPercent",system.cpuPercent,0,100);
    insertMetric(result,"playerCpuPercent",system.playerCpuPercent,0,100);
    insertMetric(result,"gpuPercent",system.gpuPercent,0,100);
    insertMetric(result,"playerGpuPercent",system.playerGpuPercent,0,100);
    insertMetric(result,"gpuDecodePercent",system.gpuDecodePercent,0,100);
    insertMetric(result,"playerGpuDecodePercent",system.playerGpuDecodePercent,0,100);
    insertMetric(result,"npuPercent",system.npuPercent,0,100);
    insertMetric(result,"playerNpuPercent",system.playerNpuPercent,0,100);
    insertMetric(result,"ramPercent",sample.values[Ram],0,100);
    insertMetric(result,"playerRamPercent",sample.playerValues[Ram],0,10000);
    insertMemory(result,"ramUsedMiB",system.memoryUsed);
    insertMemory(result,"ramTotalMiB",system.memoryTotal);
    insertMemory(result,"playerResidentMiB",system.playerResident);
    insertMetric(result,"networkRxBytesPerSecond",system.networkRxBytesPerSecond,0,1e12,1);
    insertMetric(result,"networkTxBytesPerSecond",system.networkTxBytesPerSecond,0,1e12,1);
    insertMetric(result,"npuTemperatureCelsius",system.npuTemperatureCelsius,-50,200,10);
    insertMetric(result,"discInputMiBps",sample.inputMiBps,0,512);
    insertMetric(result,"discDemuxMiBps",sample.demuxMiBps,0,512);
    insertMetric(result,"presentedFps",sample.presentedFps,0,1000);

    QJsonArray graphics;
    for (const auto &adapter : system.graphicsAdapters) {
        QJsonObject entry;
        if (adapter.integrated)
            entry.insert("integrated",*adapter.integrated);
        insertMetric(entry,"temperatureCelsius",adapter.temperatureCelsius,-50,200,10);
        insertMemory(entry,"dedicatedMemoryUsedMiB",adapter.dedicatedMemoryUsed);
        insertMemory(entry,"dedicatedMemoryTotalMiB",adapter.dedicatedMemoryTotal);
        graphics.append(entry);
        if (graphics.size() == 16)
            break;
    }
    if (!graphics.isEmpty())
        result.insert("graphicsAdapters",graphics);
    QJsonArray storage;
    for (const auto &sensor : system.storageTemperatures) {
        const auto value = sensor.temperatureCelsius;
        if (std::isfinite(value) && value >= -50 && value <= 200)
            storage.append(std::round(value*10)/10);
        if (storage.size() == 32)
            break;
    }
    if (!storage.isEmpty())
        result.insert("storageTemperaturesCelsius",storage);

    const auto insertRuntime=[&result](const char *key,const QString &value){
        if (value=="vulkan" || value=="cuda" || value=="openvino" || value=="llama.cpp" ||
            value=="whisper-cli" || value=="whisper-worker")
            result.insert(key,value);
    };
    insertRuntime("asrRuntime",sample.input.recognitionRuntime);
    insertRuntime("translationRuntime",sample.input.translationRuntime);
    if (const auto &state = sample.input.playback) {
        result.insert("engine",sample.input.disc?"libvlc":"libmpv");
        result.insert("playing",state->playing);
        result.insert("buffering",state->buffering || state->fileCacheHolding);
        result.insert("failed",state->failed);
        result.insert("droppedFrames",state->droppedVideoFrames);
        result.insert("lostAudioBuffers",state->lostAudioBuffers);
        insertMetric(result,"sourceFps",state->sourceFps,0.01,1000);
        insertMetric(result,"filteredFps",state->filteredFps,0.01,1000);
        insertMetric(result,"displayHz",state->displayFps,0.01,1000);
        insertMetric(result,"fileCacheSeconds",state->cacheSeconds,0,24*60*60);
        insertMetric(result,"avSyncMs",state->avSyncMs,-3600000,3600000);
    }
    return result;
}

bool PerformanceOverlay::hasSamples() const {
    return !history_.isEmpty();
}

QJsonObject PerformanceOverlay::summary() const {
    return {
        {"enabled", monitoring_}, {"collecting", timer_.isActive()}, {"visible", isVisible()}, {"sampleCount", history_.size()},
        {"view", view_ == View::Text ? "text" : view_ == View::Graph ? "graph" : "both"},
        {"position", leftAligned_ ? "left" : "right"},
        {"latest", history_.isEmpty() ? QJsonObject{} : sampleJson(history_.constLast())}
    };
}

QJsonObject PerformanceOverlay::diagnosticHistory() const {
    QJsonArray samples;
    if (clock_.isValid()) {
        const auto earliest = clock_.elapsed() - diagnosticRetentionMs;
        for (const auto &sample : history_)
            if (sample.elapsedMs >= earliest)
                samples.append(diagnosticSample(sample));
    }
    return {{"sampleIntervalMs",sampleIntervalMs},{"retentionMs",diagnosticRetentionMs},
        {"sampleLimit",diagnosticHistoryLimit},{"samples",samples}};
}

QByteArray PerformanceOverlay::csv() const {
    static const QStringList standardColumns{
        "elapsedMs", "cpuPercent", "playerCpuPercent", "gpuPercent", "playerGpuPercent",
        "gpuDecodePercent", "playerGpuDecodePercent", "npuPercent", "playerNpuPercent",
        "playerRamPercent", "playerIncludesChildProcesses",
        "ramPercent", "ramUsedMiB", "ramTotalMiB", "playerResidentMiB", "discInputMiBps", "discDemuxMiBps",
        "sourceFps", "filteredFps", "presentedFps", "displayHz", "droppedFrames", "lostAudioBuffers",
        "avSyncMs", "fileCacheSeconds", "captionAheadMs", "captionWaiting", "buffering", "videoRecoveries",
        "networkRxBytesPerSecond", "networkTxBytesPerSecond", "cpuTemperatureCelsius", "npuTemperatureCelsius"
    };
    QMap<QString, QString> deviceColumns;
    QList<QJsonObject> records;
    records.reserve(history_.size());
    for (const auto &sample : history_) {
        auto record = sampleJson(sample);
        const auto addDeviceColumns = [&](const QString &prefix, const QString &label, const QJsonObject &device,
                                          std::initializer_list<const char *> metrics) {
            for (const auto *metric : metrics) {
                const auto key = prefix + metric;
                deviceColumns.insert(key, key + label);
                record.insert(key, device.value(metric));
            }
        };
        const auto graphics = record.take("graphicsAdapters").toArray();
        for (const auto &entry : graphics) {
            const auto adapter = entry.toObject();
            auto name = adapter.value("name").toString().simplified();
            name.replace(',', ' ').replace('"', ' ');
            addDeviceColumns("gpu_" + adapter.value("luid").toString() + '_', " [" + name + ']', adapter,
                             {"temperatureCelsius", "dedicatedMemoryUsedMiB", "dedicatedMemoryTotalMiB", "integrated"});
        }
        const auto storage = record.take("storageTemperatures").toArray();
        for (const auto &entry : storage) {
            const auto disk = entry.toObject();
            addDeviceColumns("disk_" + QString::number(disk.value("diskNumber").toInteger()) + '_', {}, disk,
                             {"temperatureCelsius", "sensorIndex"});
        }
        records.append(std::move(record));
    }
    const auto columns = standardColumns + deviceColumns.keys();
    const auto headers = standardColumns + deviceColumns.values();
    QByteArray output;
    QTextStream stream(&output, QIODevice::WriteOnly);
    stream << headers.join(',') << '\n';
    for (const auto &record : records) {
        QStringList row;
        for (const auto &column : columns) {
            const auto value = record.value(column);
            row.append(value.isDouble() ? QString::number(value.toDouble(), 'g', 10) :
                       value.isBool() ? QString::number(value.toBool() ? 1 : 0) : QString{});
        }
        stream << row.join(',') << '\n';
    }
    stream.flush();
    return output;
}

}
