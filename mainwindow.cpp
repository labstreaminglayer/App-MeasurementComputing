#include "ui_mainwindow.h"
#include "mainwindow.h"
#include "mccdevice.h"

#include <fstream>
#include <string>
#include <vector>
#include <QCloseEvent>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
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
		QMessageBox::about(this, "About MCCOutlet", infostr);
	});
	connect(ui->linkButton, &QPushButton::clicked, this, &MainWindow::toggleRecording);

	load_config(config_file);
}

void MainWindow::load_config(const QString& filename) {
	QSettings settings(filename, QSettings::Format::IniFormat);
	ui->nameField->setText(settings.value("BPG/name", "Default name").toString());
	ui->deviceField->setValue(settings.value("BPG/device", 0).toInt());
}
void MainWindow::save_config(const QString& filename) {
	QSettings settings(filename, QSettings::Format::IniFormat);
	settings.beginGroup("BPG");
	settings.setValue("name", ui->nameField->text());
	settings.setValue("device", ui->deviceField->value());
	settings.sync();
}


void MainWindow::closeEvent(QCloseEvent* ev) {
	if (recording_thread) {
		QMessageBox::warning(this, "Recording still running", "Can't quit while recording");
		ev->ignore();
	}
}

void recording_thread_function(std::string name, int32_t device_param,
                               std::atomic<bool>& shutdown) {
	lsl::stream_info info(name, "RawBrainSignal", 6, 16384, lsl::cf_float32);
	// add some description fields
	lsl::xml_element info_xml = info.desc();
	lsl::xml_element manufac_xml = info_xml.append_child_value("manufacturer", "MeasurementComputing");
	lsl::xml_element channels_xml = info.desc().append_child("channels");
	const char *channels[] = { "RAW1","SPK1","RAW2","SPK2","RAW3","SPK3","NC1","NC2" };
	int k;
	for (k = 0; k < 6; k++)
	{
		lsl::xml_element chn = channels_xml.append_child("channel");
		chn.append_child_value("label", channels[k])
			.append_child_value("unit", "V")
			.append_child_value("type", "LFP");
	}

	lsl::stream_outlet outlet(info);
	std::vector<float_t> buffer(1, 20);

	MCCDevice* device = new MCCDevice(USB_1608_FS_PLUS);
	device->sendMessage("AISCAN:STOP");
	device->flushInputData();  // Flush out any old data from the buffer
	device->sendMessage("AISCAN:XFRMODE=BLOCKIO");  // Good for fast acquisitions.
													//device->sendMessage("AISCAN:XFRMODE=SINGLEIO"); // Good for slow acquisitions
	device->sendMessage("AISCAN:SAMPLES=0");  // Set to continuous scan.
	device->sendMessage("AISCAN:RANGE=BIP5V");//Set the voltage range on the device
	device->sendMessage("AISCAN:LOWCHAN=0");
	device->sendMessage("AISCAN:HIGHCHAN=5");
	device->sendMessage("AISCAN:RATE=16384");
	//device->mSamplesPerBlock = 512;
	//device->mScanParams.samplesPerBlock = 32/8;  // nSamples*nChannels must be integer multiple of 32.
	device->reconfigure();

	int dataLengthSamples = 512 * 6;
	std::vector<std::vector<float> > chunk(512, std::vector<float>(6));  // Used by LSL
	unsigned short *data = new unsigned short[dataLengthSamples];  // Pulled from the device.

	device->sendMessage("AISCAN:START");  // Start the scan on the device
	int c, s;
	while (!shutdown) {
		// "Acquire data"
		device->readScanData(data, dataLengthSamples);
		for (c = 0; c<6; c++)
		{
			for (s = 0; s<512; s++)
			{
				chunk[s][c] = device->scaleAndCalibrateData(data[(s * 6) + c], c);
			}
		}
		outlet.push_chunk(chunk);// , timestamp);
	}
	device->sendMessage("AISCAN:STOP");
	delete device;
}

void MainWindow::toggleRecording() {
	if (!recording_thread) {
		// read the configuration from the UI fields
		std::string name = ui->nameField->text().toStdString();
		int32_t device_param = (int32_t)ui->deviceField->value();
		shutdown = false;
		recording_thread = std::make_unique<std::thread>(&recording_thread_function, name,
		                                                 device_param, std::ref(shutdown));
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
