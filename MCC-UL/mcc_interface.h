#ifndef MCC_INTERFACE_H
#define MCC_INTERFACE_H
#include <windows.h>
#include <cstdint>
#include <vector>


class mcc_interface {
public:
	int32_t n_chans = 0;
	double srate = 0;
	static std::vector<std::pair<std::string, std::string>> mcc_interface::list_devices();
	explicit mcc_interface(std::string name, std::string id, int rate, int nchans);
	~mcc_interface();
	bool getStatus() { return true; }
	bool refreshConfig();
	bool begin();
	bool getData(std::vector<double> &buff_out);

private:
	long lastIndex = 0;
	long lastCount = 0;
	int BoardNum = 0;
	HANDLE MemHandle;
	double* ADData;
	int total_samps;
	//std::vector<double> buf_vec;
};

#endif // MCC_INTERFACE_H
