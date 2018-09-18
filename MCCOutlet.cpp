#include "lsl_cpp.h"
#include <iostream>
#include <time.h>
#include <stdlib.h>
#include "mccdevice.h"
using namespace std;

MCCDevice* device;
const char *channels[] = {"RAW1","SPK1","RAW2","SPK2","RAW3","SPK3","NC1","NC2"};
unsigned short * data;

int main(int argc, char* argv[])
{
    string name = "MCCDaq";
    string type = "RawBrainSignal";
    try {
        
        device = new MCCDevice(USB_1608_FS_PLUS);
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

        
        lsl::stream_info info(name, type, 6, 16384, lsl::cf_float32, string(name) += type);

        // add some description fields
        lsl::xml_element info_xml = info.desc();
        lsl::xml_element manufac_xml = info_xml.append_child_value("manufacturer", "MeasurementComputing");
        lsl::xml_element channels_xml = info.desc().append_child("channels");
        int k;
        for (k = 0; k < 6; k++)
        {
            lsl::xml_element chn = channels_xml.append_child("channel");
            chn.append_child_value("label", channels[k])
                .append_child_value("unit", "V")
                .append_child_value("type", "LFP");
        }

        // make a new outlet
        lsl::stream_outlet outlet(info);

        int dataLengthSamples = 512 * 6;
        std::vector<std::vector<float> > chunk(512, std::vector<float>(6));  // Used by LSL
        unsigned short *data = new unsigned short[dataLengthSamples];  // Pulled from the device.
        
        device->sendMessage("AISCAN:START");  // Start the scan on the device
        cout << "Now sending data...";
        
        unsigned t;
        int c, s;
        //double timestamp = lsl::local_clock();
        for (t=0; ; t++) {
            //device->getBlock();
            device->readScanData(data, dataLengthSamples);
            for(c=0; c<6; c++)
            {
                for(s=0; s<512; s++)
                {
                    chunk[s][c] = device->scaleAndCalibrateData( data[(s*6)+c], c);
                }
            }  
            outlet.push_chunk(chunk);// , timestamp);
            //timestamp += 512.0/16384.0;
            //cout << "Pushed chunk at timestamp " << timestamp << " (diff=" << lsl::local_clock()-timestamp << ")." << endl;
        }

    } catch(std::exception &e) {
        cerr << "Got an exception: " << e.what() << endl;
    }
    cout << "Press any key to exit. " << endl; cin.get();
    device->sendMessage("AISCAN:STOP");
    delete device;
    return 0;
}
