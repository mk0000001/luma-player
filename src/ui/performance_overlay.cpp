#include "performance_overlay.h"
#include "native_playback_move.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QIODevice>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <algorithm>

namespace luma {
namespace {

constexpr double mebibyte = 1024.0 * 1024.0;
constexpr double gibibyte = 1024.0 * mebibyte;

QString valueText(std::optional<double> value, const QString &unit, int precision = 0) {
    return value ? QString::number(*value, 'f', precision) + unit : QStringLiteral("—");
}

QJsonValue jsonValue(std::optional<double> value) {
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
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
    timer_.setInterval(1000);
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
    ++generation_;
    if (enabled) {
        if (!worker_) {
            worker_ = new QObject;
            worker_->moveToThread(&thread_);
            connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
            thread_.start();
        }
        history_.clear();
        clock_.start();
        timer_.start();
        requestSample();
    } else {
        timer_.stop();
    }
    syncGeometry();
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
    clock_.start();
    update();
    requestSample();
}

void PerformanceOverlay::requestSample() {
    syncGeometry();
    if (!monitoring_ || !isVisible() || pending_)
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
            if (monitoring_ && isVisible() && generation == generation_)
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
    auto &values = sample.values;
    values[Cpu] = system.cpuPercent;
    values[Gpu] = system.gpuPercent;
    values[Npu] = system.npuPercent;
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

    if (history_.size() == historyLimit)
        history_.removeFirst();
    history_.append(std::move(sample));
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
    const bool compact = anchor_->height() < 400;
    const bool chart = view_ != View::Text;
    const int width = std::min(418, anchor_->width() - 24);
    const int height = compact ? (chart ? 150 : 125) : (chart ? 264 : 194);
    const auto point = anchor_->mapToGlobal(QPoint(leftAligned_ ? 12 : anchor_->width() - width - 12, 10));
    const QRect target(point, QSize(width, height));
    if (geometry() != target)
        setGeometry(target);
    if (!isVisible()) {
        show();
        raise();
    }
}

double PerformanceOverlay::graphMaximum(Metric metric) const {
    double maximum = metric <= Ram ? 100 : metric == Fps ? 30 : 1;
    if (metric > Ram) {
        for (const auto &sample : history_)
            maximum = std::max(maximum, sample.values[metric].value_or(0.0));
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
        for (const auto &sample : history_) {
            const auto value = player ? sample.playerValues[metric] : sample.values[metric];
            const auto age = now - sample.elapsedMs;
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
    line({12, 6, width() - 24, 20}, view_ == View::Text ? "재생 성능  ·  전체 / 플레이어" :
         "재생 성능  ·  전체 실선 / 플레이어 점선", QColor("#dce6f5"), 12, true);
    if (history_.isEmpty()) {
        line({12, 32, width() - 24, 20}, "성능 지표를 수집하는 중…", QColor("#9cacc3"), 12);
        return;
    }

    const auto &sample = history_.constLast();
    const auto &state = sample.input.playback;
    const auto &values = sample.values;
    const auto color = QColor("#9cacc3");
    const bool compact = height() <= 150;
    const bool chart = view_ != View::Text;
    const bool numbers = view_ != View::Graph;
    const int rowHeight = compact ? (chart ? 35 : 27) : (chart ? 59 : 38);
    const int cellWidth = (width() - 30) / 2;
    const std::array<QString, MetricCount> titles{
        "CPU", "GPU", "NPU", "RAM",
        sample.input.disc ? "FPS · 표시" : "FPS · 처리",
        sample.inputMiBps ? "Blu-ray I/O · 입력" : "Blu-ray I/O · 디먹싱"
    };
    const std::array<QString, MetricCount> shown{
        valueText(values[Cpu], "%"), valueText(values[Gpu], "%"), valueText(values[Npu], "%"),
        valueText(values[Ram], "%"), valueText(values[Fps], "", 1), valueText(values[DiscRate], " MiB/s", 1)
    };
    const std::array<QString, MetricCount> details{
        sample.system.includesChildProcesses ? "플레이어와 하위 작업 포함" : "플레이어 본체",
        "디코드 " + valueText(sample.system.gpuDecodePercent, "%") + " / " + valueText(sample.system.playerGpuDecodePercent, "%"),
        sample.system.npuDetected ? (values[Npu] ? "가장 바쁜 엔진" : "드라이버 수치 없음") : "측정 가능한 장치 없음",
        (sample.system.memoryUsed ? QString::number(double(*sample.system.memoryUsed) / gibibyte, 'f', 1) + " GiB" : "—") +
            (sample.system.playerResident ? QString(" / %1 MiB").arg(double(*sample.system.playerResident) / mebibyte, 0, 'f', 0) : " / —"),
        state && state->sourceFps > 0 ? QString("원본 %1 fps").arg(state->sourceFps, 0, 'f', 2) : "원본 —",
        sample.input.disc ? "libVLC 재생 경로" : "Blu-ray 재생 시 표시"
    };
    const std::array<QColor, MetricCount> accents{
        QColor("#73cfff"), QColor("#67e0b1"), QColor("#bdabff"),
        QColor("#edca81"), QColor("#f49ab5"), QColor("#a9c6ff")
    };
    for (int i = 0; i < MetricCount; ++i) {
        const int x = 12 + (i % 2) * (cellWidth + 6);
        const int y = 27 + (i / 2) * rowHeight;
        QString label = titles[i] + (numbers ? "  " + shown[i] : values[i] ? "" : "  —");
        if (numbers && i <= Ram)
            label += " / " + valueText(sample.playerValues[i], "%", 1);
        line({x, y, cellWidth, 17}, label, accents[i], 11, true);
        if (!compact) {
            const auto scale = i <= Ram ? QString("%") : i == Fps ? QString(" fps") : QString(" MiB/s");
            const auto description = numbers || !values[i] ? details[i] :
                QString("0–%1%2").arg(graphMaximum(static_cast<Metric>(i)), 0, 'f', i == DiscRate ? 1 : 0).arg(scale);
            line({x, y + 17, cellWidth, 15}, description, color, 10);
        }
        if (chart)
            paintGraph(painter, QRectF(x, y + (compact ? 18 : 34), cellWidth - 3, compact ? 13 : 20),
                       static_cast<Metric>(i), accents[i]);
    }
    const int footer = 27 + 3 * rowHeight;
    const auto status = sample.input.captionWaiting ? QString("자막 대기") :
        state && (state->buffering || state->fileCacheHolding) ? QString("재생 버퍼링") :
        state && state->failed ? QString("재생 오류") : state && state->playing ? QString("재생 중") : QString("대기");
    const auto dropped = state ? QString::number(state->droppedVideoFrames) : QString("—");
    line({12, footer, width() - 24, 16},
         QString("%1  ·  드롭 %2  ·  자막 버퍼 %3초").arg(status, dropped).arg(sample.input.captionAheadMs / 1000.0, 0, 'f', 1),
         color, 10);
    if (!compact) {
        const auto decode = state && !state->hardwareDecoder.isEmpty() ? state->hardwareDecoder : QString("—");
        line({12, footer + 17, width() - 24, 16},
             QString("디코더 %1  ·  인식 %2  ·  번역 %3").arg(decode,
                 sample.input.recognitionBackend.isEmpty() ? "—" : sample.input.recognitionBackend,
                 sample.input.translationBackend.isEmpty() ? "—" : sample.input.translationBackend), color, 10);
        if (state) {
            const auto extra = sample.input.disc ?
                QString("입력 %1  ·  디먹싱 %2").arg(valueText(sample.inputMiBps, " MiB/s", 1), valueText(sample.demuxMiBps, " MiB/s", 1)) :
                QString("A/V 편차 %1  ·  캐시 %2  ·  주사율 %3").arg(
                    valueText(state->avSyncMs, " ms", 1), valueText(state->cacheSeconds, "초", 1),
                    valueText(state->displayFps > 0 ? std::optional<double>(state->displayFps) : std::nullopt, " Hz", 1));
            line({12, footer + 34, width() - 24, 16}, extra, color, 10);
        }
    }
}

QJsonObject PerformanceOverlay::sampleJson(const Sample &sample) {
    const auto &v = sample.values;
    QJsonObject result{
        {"elapsedMs", sample.elapsedMs}, {"cpuPercent", jsonValue(v[Cpu])}, {"gpuPercent", jsonValue(v[Gpu])},
        {"gpuDecodePercent", jsonValue(sample.system.gpuDecodePercent)}, {"npuPercent", jsonValue(v[Npu])},
        {"npuDetected", sample.system.npuDetected}, {"ramPercent", jsonValue(v[Ram])},
        {"ramUsedMiB", jsonValue(sample.system.memoryUsed ? std::optional<double>(double(*sample.system.memoryUsed) / mebibyte) : std::nullopt)},
        {"ramTotalMiB", jsonValue(sample.system.memoryTotal ? std::optional<double>(double(*sample.system.memoryTotal) / mebibyte) : std::nullopt)},
        {"playerResidentMiB", jsonValue(sample.system.playerResident ? std::optional<double>(double(*sample.system.playerResident) / mebibyte) : std::nullopt)},
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

bool PerformanceOverlay::hasSamples() const {
    return !history_.isEmpty();
}

QJsonObject PerformanceOverlay::summary() const {
    return {
        {"enabled", monitoring_}, {"visible", isVisible()}, {"sampleCount", history_.size()},
        {"view", view_ == View::Text ? "text" : view_ == View::Graph ? "graph" : "both"},
        {"position", leftAligned_ ? "left" : "right"},
        {"latest", history_.isEmpty() ? QJsonObject{} : sampleJson(history_.constLast())}
    };
}

QByteArray PerformanceOverlay::csv() const {
    static const QStringList columns{
        "elapsedMs", "cpuPercent", "playerCpuPercent", "gpuPercent", "playerGpuPercent",
        "gpuDecodePercent", "playerGpuDecodePercent", "npuPercent", "playerNpuPercent",
        "playerRamPercent", "playerIncludesChildProcesses",
        "ramPercent", "ramUsedMiB", "ramTotalMiB", "playerResidentMiB", "discInputMiBps", "discDemuxMiBps",
        "sourceFps", "filteredFps", "presentedFps", "displayHz", "droppedFrames", "lostAudioBuffers",
        "avSyncMs", "fileCacheSeconds", "captionAheadMs", "captionWaiting", "buffering", "videoRecoveries"
    };
    QByteArray output;
    QTextStream stream(&output, QIODevice::WriteOnly);
    stream << columns.join(',') << '\n';
    for (const auto &sample : history_) {
        const auto record = sampleJson(sample);
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
