#include "main_window.h"

#include "fullscreen_controls.h"
#include "player_controls.h"
#include <QLabel>
#include <QLayout>
#include <QScreen>
#include <QStyle>
#include <QWindow>

namespace luma {
namespace {
void keepAbove(QWidget *window,bool enabled){
    if(auto *handle=window->windowHandle())handle->setFlag(Qt::WindowStaysOnTopHint,enabled);
}
}

void MainWindow::updatePresentationLayout(){
    const bool showPanels=!full_ && !pictureInPicture_;
    for(auto *panel:std::initializer_list<QWidget*>{sidebar_,panel_,header_,status_,transportTitleRow_})
        panel->setVisible(showPanels);
    for(auto *control:controlsHiddenInPip_)
        control->setVisible(!pictureInPicture_);
    if(pictureInPicture_)transport_->layout()->setContentsMargins(14,3,14,3);
    else transport_->layout()->setContentsMargins(18,7,18,7);
    transport_->setFixedHeight(pictureInPicture_?60:full_?74:108);
    static_cast<IconButton*>(fullButton_)->setPlayerIcon(full_?PlayerIcon::ExitFullscreen:PlayerIcon::Fullscreen);
    fullButton_->setToolTip(full_?"전체 화면 나가기 · F / Esc":"전체 화면 · F");
    const QString pipLabel=pictureInPicture_?"작은 화면 나가기 · I / Esc":"작은 화면 · I";
    pipButton_->setToolTip(pipLabel);
    pipButton_->setAccessibleName(pipLabel);
    fullscreenControls_->setActive(full_ || pictureInPicture_);
}

void MainWindow::toggleFullScreen(){
    if(pictureInPicture_)togglePictureInPicture();
    if(!full_)fullscreenWasMaximized_=isMaximized();
    full_=!full_;
    updatePresentationLayout();
    if(full_)showFullScreen();else if(fullscreenWasMaximized_)showMaximized();else showNormal();
    updateDisplayStatus();
}

void MainWindow::togglePictureInPicture(){
    if(pictureInPicture_){
        pictureInPicture_=false;
        keepAbove(this,false);
        surface_->setMinimumSize(500,280);
        setMinimumSize(1080,680);
        updatePresentationLayout();
        setGeometry(windowedGeometry_);
        if(windowedMaximized_)showMaximized();
    }else{
        if(!active_ || active_->state().failed || !active_->state().timelineReady)return;
        if(full_)toggleFullScreen();
        windowedMaximized_=isMaximized();
        windowedGeometry_=windowedMaximized_?normalGeometry():geometry();
        if(windowedMaximized_)showNormal();
        pictureInPicture_=true;
        surface_->setMinimumSize(0,0);
        setMinimumSize(300,190);
        updatePresentationLayout();
        const auto area=screen()->availableGeometry().adjusted(16,16,-16,-16);
        const auto size=QSize(440,270).boundedTo(area.size());
        setGeometry(QStyle::alignedRect(Qt::LeftToRight,Qt::AlignRight|Qt::AlignBottom,size,area));
        keepAbove(this,true);
    }
    surface_->setFocus(Qt::OtherFocusReason);
    updateDisplayStatus();
}
}
