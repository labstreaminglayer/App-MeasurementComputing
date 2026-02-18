/**
 * @file MainWindow.cpp
 * @brief Main window implementation for MCCOutlet GUI application
 */

#include "MainWindow.hpp"
#include "ui_MainWindow.h"

#include <mccoutlet/Config.hpp>
#include <mccoutlet/Device.hpp>
#include <mccoutlet/StreamThread.hpp>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStandardPaths>

#include <lsl_cpp.h>

MainWindow::MainWindow(const QString& config_file, QWidget* parent)
    : QMainWindow(parent)
    , ui_(std::make_unique<Ui::MainWindow>())
{
    ui_->setupUi(this);

    connect(ui_->linkButton, &QPushButton::clicked, this, &MainWindow::onLinkButtonClicked);
    connect(ui_->refreshDevicesButton, &QPushButton::clicked, this, &MainWindow::onRefreshDevices);
    connect(ui_->input_device, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDeviceChanged);
    connect(ui_->actionLoad_Configuration, &QAction::triggered, this, &MainWindow::onLoadConfig);
    connect(ui_->actionSave_Configuration, &QAction::triggered, this, &MainWindow::onSaveConfig);
    connect(ui_->actionQuit, &QAction::triggered, this, &QMainWindow::close);
    connect(ui_->actionAbout, &QAction::triggered, this, &MainWindow::onAbout);

    refreshDeviceList();

    QString cfg_path = config_file.isEmpty() ? findDefaultConfigFile() : config_file;
    if (!cfg_path.isEmpty()) {
        loadConfig(cfg_path);
    }
}

MainWindow::~MainWindow() {
    if (stream_) {
        stream_->stop();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (stream_ && stream_->isRunning()) {
        auto result = QMessageBox::question(
            this,
            "Streaming Active",
            "Streaming is still active. Stop and quit?",
            QMessageBox::Yes | QMessageBox::No
        );

        if (result == QMessageBox::No) {
            event->ignore();
            return;
        }

        stream_->stop();
    }

    event->accept();
}

void MainWindow::onLinkButtonClicked() {
    if (stream_ && stream_->isRunning()) {
        // Stop streaming
        stream_->stop();
        stream_.reset();
        setStreaming(false);
    } else {
        // Start streaming
        int deviceIndex = ui_->input_device->currentData().toInt();
        mccoutlet::MCCDevice::Config device_config{
            .stream_name = ui_->input_name->text().toStdString(),
            .stream_type = ui_->input_type->text().toStdString(),
            .device_index = deviceIndex,
            .low_channel = ui_->input_low_chan->value(),
            .high_channel = ui_->input_high_chan->value(),
            .sample_rate = ui_->input_srate->value(),
            .range = ui_->input_range->currentData().toInt(),
            .scaled = (ui_->input_data_format->currentIndex() == 0)
        };

        auto callback = [this](const std::string& message, bool is_error) {
            QMetaObject::invokeMethod(this, [this, message, is_error]() {
                updateStatus(QString::fromStdString(message), is_error);
            });
        };

        auto device = std::make_unique<mccoutlet::MCCDevice>(device_config, callback);

        stream_ = std::make_unique<mccoutlet::StreamThread>(std::move(device), callback);

        if (stream_->start()) {
            setStreaming(true);
        } else {
            stream_.reset();
            QMessageBox::warning(this, "Error", "Failed to start streaming. Check device connection.");
        }
    }
}

void MainWindow::onLoadConfig() {
    QString filename = QFileDialog::getOpenFileName(
        this,
        "Load Configuration",
        last_config_path_,
        "Configuration Files (*.cfg);;All Files (*)"
    );

    if (!filename.isEmpty()) {
        loadConfig(filename);
    }
}

void MainWindow::onSaveConfig() {
    QString filename = QFileDialog::getSaveFileName(
        this,
        "Save Configuration",
        last_config_path_,
        "Configuration Files (*.cfg);;All Files (*)"
    );

    if (!filename.isEmpty()) {
        saveConfig(filename);
    }
}

void MainWindow::onRefreshDevices() {
    refreshDeviceList();
}

void MainWindow::onDeviceChanged(int index) {
    ui_->input_range->clear();
    ui_->label_resolution_value->setText("--");
    current_caps_ = {};

    if (index < 0) return;

    int deviceIndex = ui_->input_device->currentData().toInt();
    if (deviceIndex < 0) return;

    try {
        mccoutlet::MCCDevice::Config cfg;
        cfg.device_index = deviceIndex;

        mccoutlet::MCCDevice tempDevice(cfg);
        tempDevice.connect();

        current_caps_ = tempDevice.getCapabilities();

        for (const auto& r : current_caps_.available_ranges) {
            ui_->input_range->addItem(QString::fromStdString(r.label), r.id);
        }

        if (current_caps_.min_scan_rate > 0) {
            ui_->input_srate->setMinimum(current_caps_.min_scan_rate);
        }
        if (current_caps_.max_scan_rate > 0) {
            ui_->input_srate->setMaximum(current_caps_.max_scan_rate);
        }

        if (current_caps_.resolution_bits > 0) {
            ui_->label_resolution_value->setText(
                QString::number(current_caps_.resolution_bits) + " bits");
        }

        tempDevice.disconnect();
    } catch (const std::exception& e) {
        updateStatus(QString("Could not query device: %1").arg(e.what()), true);
    }
}

void MainWindow::refreshDeviceList() {
    {
        QSignalBlocker blocker(ui_->input_device);
        int previousIndex = ui_->input_device->currentData().toInt();
        ui_->input_device->clear();

        auto devices = mccoutlet::MCCDevice::discover();
        if (devices.empty()) {
            ui_->input_device->addItem("(no devices found)", -1);
            updateStatus("No MCC devices found. Connect a device and click Refresh.", true);
        } else {
            for (const auto& dev : devices) {
                QString label = QString::fromStdString(dev.product_name)
                    + " (" + QString::fromStdString(dev.unique_id) + ")"
                    + " [" + QString::fromStdString(dev.interface_name) + "]";
                ui_->input_device->addItem(label, dev.index);
            }

            // Try to restore previous selection
            int restoreIdx = ui_->input_device->findData(previousIndex);
            if (restoreIdx >= 0) {
                ui_->input_device->setCurrentIndex(restoreIdx);
            }

            updateStatus(QString("Found %1 device(s)").arg(devices.size()), false);
        }
    }

    // Manually trigger capability query for the current device
    onDeviceChanged(ui_->input_device->currentIndex());
}

void MainWindow::onAbout() {
    QString info = QString(
        "<h3>MCCOutlet</h3>"
        "<p>Version 2.0.0</p>"
        "<p>Streams analog input data from Measurement Computing "
        "DAQ devices into Lab Streaming Layer.</p>"
        "<hr>"
        "<p><b>LSL Library:</b> %1</p>"
        "<p><b>Protocol:</b> %2</p>"
    ).arg(QString::number(lsl::library_version()),
          QString::fromStdString(lsl::library_info()));

    QMessageBox::about(this, "About MCCOutlet", info);
}

void MainWindow::loadConfig(const QString& filename) {
    auto config = mccoutlet::ConfigManager::load(filename.toStdString());

    if (config) {
        ui_->input_name->setText(QString::fromStdString(config->stream_name));
        ui_->input_type->setText(QString::fromStdString(config->stream_type));
        int comboIdx = ui_->input_device->findData(config->device_index);
        if (comboIdx >= 0) {
            ui_->input_device->setCurrentIndex(comboIdx);
        }
        ui_->input_low_chan->setValue(config->low_channel);
        ui_->input_high_chan->setValue(config->high_channel);
        ui_->input_srate->setValue(config->sample_rate);

        if (config->range >= 0) {
            int rangeIdx = ui_->input_range->findData(config->range);
            if (rangeIdx >= 0) {
                ui_->input_range->setCurrentIndex(rangeIdx);
            }
        }

        ui_->input_data_format->setCurrentIndex(config->scaled ? 0 : 1);

        last_config_path_ = filename;
        updateStatus("Loaded: " + filename, false);
    } else {
        QMessageBox::warning(
            this,
            "Load Failed",
            QString("Failed to load configuration from:\n%1").arg(filename)
        );
    }
}

void MainWindow::saveConfig(const QString& filename) {
    mccoutlet::AppConfig config{
        .stream_name = ui_->input_name->text().toStdString(),
        .stream_type = ui_->input_type->text().toStdString(),
        .device_index = ui_->input_device->currentData().toInt(),
        .low_channel = ui_->input_low_chan->value(),
        .high_channel = ui_->input_high_chan->value(),
        .sample_rate = ui_->input_srate->value(),
        .range = ui_->input_range->currentData().toInt(),
        .scaled = (ui_->input_data_format->currentIndex() == 0)
    };

    if (mccoutlet::ConfigManager::save(config, filename.toStdString())) {
        last_config_path_ = filename;
        updateStatus("Saved: " + filename, false);
    } else {
        QMessageBox::warning(
            this,
            "Save Failed",
            QString("Failed to save configuration to:\n%1").arg(filename)
        );
    }
}

QString MainWindow::findDefaultConfigFile() {
    QFileInfo exe_info(QCoreApplication::applicationFilePath());
    QString default_name = exe_info.completeBaseName() + ".cfg";

    QStringList search_paths = {
        QDir::currentPath(),
        exe_info.absolutePath()
    };
    search_paths.append(QStandardPaths::standardLocations(QStandardPaths::ConfigLocation));

    for (const auto& path : search_paths) {
        QString full_path = path + QDir::separator() + default_name;
        if (QFileInfo::exists(full_path)) {
            return full_path;
        }
    }

    return QString();
}

void MainWindow::updateStatus(const QString& message, bool is_error) {
    ui_->statusbar->showMessage(message, is_error ? 0 : 5000);

    if (is_error) {
        ui_->statusbar->setStyleSheet("color: red;");
    } else {
        ui_->statusbar->setStyleSheet("");
    }
}

void MainWindow::setStreaming(bool streaming) {
    ui_->linkButton->setText(streaming ? "Unlink" : "Link");

    ui_->input_name->setEnabled(!streaming);
    ui_->input_type->setEnabled(!streaming);
    ui_->input_device->setEnabled(!streaming);
    ui_->refreshDevicesButton->setEnabled(!streaming);
    ui_->input_low_chan->setEnabled(!streaming);
    ui_->input_high_chan->setEnabled(!streaming);
    ui_->input_srate->setEnabled(!streaming);
    ui_->input_range->setEnabled(!streaming);
    ui_->input_data_format->setEnabled(!streaming);
}
