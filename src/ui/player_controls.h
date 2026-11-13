#pragma once
#include <QComboBox>
#include <QIcon>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
class QMenu;
namespace luma {
enum class PlayerIcon { Play,Pause,Previous,Next,Rewind,Forward,Fullscreen,ExitFullscreen,PictureInPicture,Volume,Folder,Disc,Sun,Moon,Up,Down,Left,Right,Confirm };
QIcon playerApplicationIcon();
class SeekSlider final:public QSlider {
public:
    explicit SeekSlider(QWidget *parent=nullptr);
    void setBuffer(int value,int fillingPercent=-1);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    int bufferedValue_=-1;
    int fillingPercent_=-1;
};
class BrandMark final:public QWidget {
public:
    explicit BrandMark(QWidget *parent=nullptr);
protected:
    void paintEvent(QPaintEvent *)override;
};
class IconButton final:public QPushButton {
public:
    explicit IconButton(PlayerIcon icon,const QString &description,QWidget *parent=nullptr);
    void setPlayerIcon(PlayerIcon icon);
protected:
    void paintEvent(QPaintEvent *)override;
private:
    PlayerIcon icon_;
};
// The model and keyboard behavior remain QComboBox's; only the visual popup is custom.
class ChoiceBox final:public QComboBox {
public:
    explicit ChoiceBox(QWidget *parent=nullptr);
    void showPopup()override;
    void hidePopup()override;
    QSize sizeHint()const override;
    QSize minimumSizeHint()const override;
protected:
    void paintEvent(QPaintEvent *)override;
private:
    QPointer<QMenu> popup_;
    void wheelEvent(QWheelEvent *)override;
};
}
