#include "ui_mainwindow.h"
#include "mainwindow.h"
#include "mcc_interface.h"

#include <fstream>
#include <string>
#include <vector>
#include <QCloseEvent>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QtWidgets>
#include <lsl_cpp.h>

MainWindow::MainWindow(QWidget* parent, const char* config_file)
    : QMainWindow(parent), recording_thread(nullptr), ui(new Ui::MainWindow) {
	ui->setupUi(this);
	connect(ui->actionLoad_Configuration, &QAction::triggered, [this]() {
		load_config(QFileDialog::getOpenFileName(this, "Load Configuration File", "",
		                                         "Configuration Files (*.cfg)"));
	});
	connect(ui->actionSave_Configuration, &QAction::triggered, [this]() {
		save_config(QFileDialog::getSaveFileName(this, "Save Configuration File", "",
		                                         "Configuration Files (*.cfg)"));
	});
	connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::close);
	connect(ui->actionAbout, &QAction::triggered, [this]() {
		QString infostr = QStringLiteral("LSL library version: ") +
		                  QString::number(lsl::library_version()) +
		                  "\nLSL library info:" + lsl::lsl_library_info();
		QMessageBox::about(this, "About MeasurementComputingUL", infostr);
	});
	connect(ui->linkButton, &QPushButton::clicked, this, &MainWindow::toggleRecording);

	std::vector<std::pair<std::string, std::string>> device_identifiers = mcc_interface::list_devices();
	ui->deviceTable->setRowCount((int)device_identifiers.size());
	for (int dev_ix = 0; dev_ix < device_identifiers.size(); dev_ix++)
	{
		ui->deviceTable->setItem(dev_ix, 0, new QTableWidgetItem(QString::fromStdString(device_identifiers.at(dev_ix).first)));
		ui->deviceTable->setItem(dev_ix, 1, new QTableWidgetItem(QString::fromStdString(device_identifiers.at(dev_ix).second)));
	}

	load_config(config_file);
}

void MainWindow::load_config(const QString& filename) {
	QSettings settings(filename, QSettings::Format::IniFormat);
	ui->rateField->setValue(settings.value("BPG/rate", 0).toInt());
}
void MainWindow::save_config(const QString& filename) {
	QSettings settings(filename, QSettings::Format::IniFormat);
	settings.beginGroup("BPG");
	settings.setValue("rate", ui->rateField->value());
	settings.sync();
}


void MainWindow::closeEvent(QCloseEvent* ev) {
	if (recording_thread) {
		QMessageBox::warning(this, "Recording still running", "Can't quit while recording");
		ev->ignore();
	}
}

void recording_thread_function(std::string name, std::string id, int srate, int nchans,
                               std::atomic<bool>& shutdown) {
	mcc_interface device(name, id, srate, nchans);
	device.refreshConfig();
	lsl::stream_info info(name, "RawBrainSignal", device.n_chans, device.srate, lsl::cf_double64);

	// Add channels to info.
	info.desc().append_child("acquisition")
		.append_child_value("manufacturer", "Measurement Computing")
		.append_child_value("model", name);
	// Append channel info
	lsl::xml_element channels_root = info.desc().append_child("channels");
	for (size_t ch_ix = 0; ch_ix < device.n_chans; ch_ix++)
	{
		QString chLabel = QString::number(ch_ix);
		channels_root.append_child("channel")
			.append_child_value("label", chLabel.toStdString())
			.append_child_value("type", "RAW")
			.append_child_value("unit", "none");
	}

	lsl::stream_outlet outlet(info);

	device.begin();
	std::vector<double> data;  // It gets resized by getData
	while (!shutdown) {
		if (device.getData(data))
			outlet.push_chunk_multiplexed(data);
	}
}

void MainWindow::toggleRecording() {
	if (!recording_thread) {
		// read the name and id from the table
		shutdown = false;
		QList<QTableWidgetItem *> sel_items = ui->deviceTable->selectedItems();
		std::string name = sel_items.at(0)->text().toStdString();
		std::string id = sel_items.at(1)->text().toStdString();
		int srate = ui->rateField->value();
		int nchans = ui->chansField->value();
		recording_thread = std::make_unique<std::thread>(&recording_thread_function, name, id, srate, nchans, std::ref(shutdown));
		ui->linkButton->setText("Unlink");
	}
	else {
		shutdown = true;
		recording_thread->join();
		recording_thread.reset();
		ui->linkButton->setText("Link");
	}
}

MainWindow::~MainWindow() noexcept = default;
