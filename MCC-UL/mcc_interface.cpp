#include "mcc_interface.h"
#include <chrono>
#include <thread>
#include "cbw.h"

#define MAXNUMDEVS 100

std::vector<std::pair<std::string, std::string>> mcc_interface::list_devices()
{
	int UDStat = 0;
	DaqDeviceDescriptor inventory[MAXNUMDEVS];
	int numberOfDevices = MAXNUMDEVS;
	float RevLevel = (float)CURRENTREVNUM;
	int board_num = 0;
	std::vector<std::pair<std::string, std::string>> device_ident;

	/* Ignore InstaCal device discovery */
	cbIgnoreInstaCal();

	/* Initiate error handling
	Parameters:
	PRINTALL :all warnings and errors encountered will be printed
	DONTSTOP :program will continue even if error occurs.
	Note that STOPALL and STOPFATAL are only effective in
	Windows applications, not Console applications.
	*/
	cbErrHandling(PRINTALL, DONTSTOP);

	/* Declare UL Revision Level */
	UDStat = cbDeclareRevision(&RevLevel);

	/* Discover DAQ devices with cbGetDaqDeviceInventory()
	Parameters:
	InterfaceType   :interface type of DAQ devices to be discovered
	inventory[]		:array for the discovered DAQ devices
	numberOfDevices	:number of DAQ devices discovered */
	UDStat = cbGetDaqDeviceInventory(ANY_IFC, inventory, &numberOfDevices);

	if (numberOfDevices > 0)
	{
		for (board_num = 0; board_num < numberOfDevices; board_num++)
		{
			device_ident.push_back(std::make_pair(inventory[board_num].ProductName, inventory[board_num].UniqueID));
		}
	}
	return device_ident;
}


mcc_interface::mcc_interface(std::string name, std::string id, int rate, int nchans)
	: srate(rate), n_chans(nchans)
{
	int UDStat = 0;
	DaqDeviceDescriptor inventory[MAXNUMDEVS];
	int numberOfDevices = MAXNUMDEVS;
	float RevLevel = (float)CURRENTREVNUM;
	int board_num = 0;
	std::vector<std::pair<std::string, std::string>> device_ident;

	cbErrHandling(PRINTALL, DONTSTOP);
	UDStat = cbDeclareRevision(&RevLevel);
	cbIgnoreInstaCal();
	UDStat = cbGetDaqDeviceInventory(ANY_IFC, inventory, &numberOfDevices);
	bool match_found = false;
	if (numberOfDevices > 0)
	{
		for (board_num = 0; board_num < numberOfDevices; board_num++)
		{
			if ((inventory[board_num].ProductName == name) && (inventory[board_num].UniqueID == id))
			{
				BoardNum = board_num;
				match_found = true;
				break;
			}
		}
	}

	if (match_found)
	{
		UDStat = cbCreateDaqDevice(BoardNum, inventory[BoardNum]);
		UDStat = cbFlashLED(BoardNum);
		// UDStat = cbSetConfig(BOARDINFO, BoardNum, 0, BIADDATARATE, rate);
		refreshConfig();
	}
}

mcc_interface::~mcc_interface() {
	int ULStat = cbStopBackground(BoardNum, AIFUNCTION);
	// Release resources associated with the specified board number within the Universal Library
	cbWinBufFree(MemHandle);
	int UDStat = cbReleaseDaqDevice(BoardNum);
}

bool mcc_interface::refreshConfig()
{
	int ULStat = 0;
	int num_chans;

	ULStat = cbGetConfig(BOARDINFO, BoardNum, 0, BINUMADCHANS, &num_chans);
	n_chans = min(int32_t(num_chans), n_chans);

	//int rate;
	//ULStat = cbGetConfig(BOARDINFO, BoardNum, 0, BIADDATARATE, &rate);
	//srate = double(rate);
	int fifo_size = 32768;
	int fifo_per_channel = fifo_size / n_chans;  // Floored.
	int buffer_per_channel = fifo_per_channel / 2;
	total_samps = buffer_per_channel * n_chans;
	
	return true;
}

bool mcc_interface::begin()
{
	int ULStat = 0;
	int LowChan = 0;
	int Gain = BIP10VOLTS;
	long rate = (long)srate;
	unsigned Options = CONVERTDATA + BACKGROUND + CONTINUOUS + SCALEDATA;  // needs different buffer.

	MemHandle = cbScaledWinBufAlloc(total_samps);
	ADData = (double*)MemHandle;
	//buf_vec = std::vector<double>(total_samps, 0);

	/* Collect the values with cbAInScan() in BACKGROUND mode
	Parameters:
	BoardNum    :the number used by CB.CFG to describe this board
	LowChan     :low channel of the scan
	HighChan    :high channel of the scan
	Count       :the total number of A/D samples to collect
	Rate        :sample rate in samples per second
	Gain        :the gain for the board
	ADData[]    :the array for the collected data values
	Options     :data collection options */
	ULStat = cbAInScan(BoardNum, LowChan, n_chans - 1, total_samps, &rate,
		Gain, MemHandle, Options);
	srate = (double)rate;  // Copy value back.
	return true;
}

bool mcc_interface::getData(std::vector<double> &buff_out)
{
	int ULStat = 0;
	short Status = 0;
	long CurCount = 0;
	long CurIndex = 0;

	/* Check the status of the current background operation
	Parameters:
	BoardNum  :the number used by CB.CFG to describe this board
	Status    :current status of the operation (IDLE or RUNNING)
	CurCount  :current number of samples collected
	CurIndex  :index to the last data value transferred
	FunctionType: A/D operation (AIFUNCTIOM)*/
	ULStat = cbGetStatus(BoardNum, &Status, &CurCount, &CurIndex, AIFUNCTION);

	const long n_samples = CurCount - lastCount;
	if ((Status == RUNNING) && (n_samples > 0))
	{
		buff_out.resize(n_samples);
		for (int samp_ix = 0; samp_ix < n_samples; samp_ix++)
		{
			buff_out[samp_ix] = ADData[(lastIndex + samp_ix) % total_samps];
		}
		//buff_out.assign(&ADData[CurIndex], &ADData[CurIndex + n_samples]);
		//buff_out.assign(&buf_vec[CurIndex], &buf_vec[CurIndex + n_samples]);
		/*
		cbScaledWinBufToArray(MemHandle, buff_out.data(), lastIndex, min(n_samples, total_samps - lastIndex));
		if (n_samples > (total_samps - lastIndex))
		{
			cbScaledWinBufToArray(MemHandle, &buff_out[total_samps - lastIndex], 0, lastIndex + n_samples - total_samps);
		}
		*/
		lastCount = CurCount;
		lastIndex = (lastIndex + n_samples) % total_samps;
	}
	return n_samples > 0;
}