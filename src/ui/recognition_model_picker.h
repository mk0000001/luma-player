#pragma once

#include <QWidget>

class QFileSystemWatcher;
class QLineEdit;
class QShowEvent;
class QTimer;

namespace luma {
class ChoiceBox;

class RecognitionModelPicker final : public QWidget {
    Q_OBJECT
public:
    explicit RecognitionModelPicker(QWidget *parent = nullptr);
    QString selectedModel() const;

signals:
    void modelChanged(const QString &path);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void refreshModels();
    void changeFolder();
    void openFolder();
    void showFolderPath();

    ChoiceBox *models_;
    QLineEdit *folderPath_;
    QFileSystemWatcher *watcher_;
    QTimer *refreshTimer_;
    QString folder_;
};
}
