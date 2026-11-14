#pragma once

#include <QImage>
#include <QtGui/qwindowdefs.h>
#include <memory>

namespace luma {
class DiscMenuPreview {
public:
    DiscMenuPreview();
    ~DiscMenuPreview();
    QImage latestFrame(WId window);
    void reset();

private:
    struct Capture;
    std::unique_ptr<Capture> capture_;
};
}
