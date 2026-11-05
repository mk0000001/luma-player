#pragma once
#include <QComboBox>
#include <QIcon>
#include <QPointer>
#include <QPushButton>
class QMenu;
namespace luma {
enum class PlayerIcon { Play,Pause,Previous,Next,Rewind,Forward,Fullscreen,ExitFullscreen,Volume,Folder,Disc,Sun,Moon,Up,Down,Left,Right,Confirm };
QIcon playerApplicationIcon();
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
};
}
