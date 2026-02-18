#pragma once
/**
 * @file MainWindow.hpp
 * @brief Main window for MCCOutlet GUI application
 */

#include <mccoutlet/Device.hpp>

#include <QMainWindow>
#include <memory>

namespace Ui {
class MainWindow;
}

namespace mccoutlet {
class StreamThread;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& config_file = QString(), QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onLinkButtonClicked();
    void onRefreshDevices();
    void onDeviceChanged(int index);
    void onLoadConfig();
    void onSaveConfig();
    void onAbout();

private:
    void loadConfig(const QString& filename);
    void saveConfig(const QString& filename);
    void refreshDeviceList();
    QString findDefaultConfigFile();
    void updateStatus(const QString& message, bool is_error);
    void setStreaming(bool streaming);

    std::unique_ptr<Ui::MainWindow> ui_;
    std::unique_ptr<mccoutlet::StreamThread> stream_;
    QString last_config_path_;
    mccoutlet::DeviceCapabilities current_caps_;
};
