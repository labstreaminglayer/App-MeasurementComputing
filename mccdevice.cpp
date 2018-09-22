//
//  mccdevice.cpp
//
//  Created by Chadwick Boulay on 2014-03-12.
//
//

#include <iostream>
#include <string>
#include <stdlib.h>
#include <vector>
#include <libusb.h>
#include "mccdevice.h"

/* These definitions are used to build the request type in usb_control_msg */
#define MCC_VID         (0x09db)  // Vendor ID for Measurement Computing
#define CTRL_IN         (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRL_OUT        (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)
#define INTR_LENGTH     64

#define  INPUT_REPORT   (1 << 8)
#define  OUTPUT_REPORT  (2 << 8)

/* Digital I/O Commands */
#define DTRISTATE     (0x00)   // Read/Write Tristate register
#define DPORT         (0x01)   // Read digital port pins
#define DLATCH        (0x02)   // Read/Write Digital port output latch register

/* Description of the requestType byte */
// Data transfer direction D7;  libusb_endpoint_direction
//#define HOST_TO_DEVICE (0x0 << 7)  // LIBUSB_ENDPOINT_IN
//#define DEVICE_TO_HOST (0x1 << 7)  // LIBUSB_ENDPOINT_OUT
// Type D5-D6; libusb_request_type
//#define STANDARD_TYPE (0x0 << 5)  // LIBUSB_REQUEST_TYPE_STANDARD
//#define CLASS_TYPE    (0x1 << 5)  // LIBUSB_REQUEST_TYPE_CLASS
//#define VENDOR_TYPE   (0x2 << 5)  // LIBUSB_REQUEST_TYPE_VENDOR
//#define RESERVED_TYPE (0x3 << 5)
// Recipient D0 - D4; libusb_request_recipient
//#define DEVICE_RECIPIENT    (0x0)  // LIBUSB_RECIPIENT_DEVICE
//#define INTERFACE_RECIPIENT (0x1)  // LIBUSB_RECIPIENT_INTERFACE
//#define ENDPOINT_RECIPIENT  (0x2)  // LIBUSB_RECIPIENT_ENDPOINT
//#define OTHER_RECIPIENT     (0x3)  // LIBUSB_RECIPIENT_OTHER
//#define RESERVED_RECIPIENT  (0x4)

/* MDB Control Transfers */
#define MAX_MESSAGE_LENGTH 64      // max length of MBD Packet in bytes
                                   // Request types:
#define STRING_MESSAGE     (0x80)  // Send string messages to the device
#define RAW_DATA           (0x81)  // Return RAW data from the device
#define FPGADATAREQUEST    (0x51)
#define HS_DELAY            1000   // wjasper uses 20

mcc_err libUSBError(int err)
{
    switch(err)
    {
        case LIBUSB_ERROR_TIMEOUT:
            return MCC_ERR_LIBUSB_TIMEOUT;
        case LIBUSB_ERROR_PIPE:
            return MCC_ERR_PIPE;
        case LIBUSB_ERROR_NO_DEVICE:
            return MCC_ERR_NO_DEVICE;
        default:
            return MCC_ERR_UNKNOWN_LIB_USB_ERR;
    }
}

std::string errorString(int err)
{
    std::stringstream unknownerror;
    
    switch(err)
    {
        case MCC_ERR_ACCESS:
            return "Insufficient USB permisions\n";
        case MCC_ERR_NO_DEVICE:
            return "No Matching Device Found\n";
        case MCC_ERR_INVALID_ID:
            return "Invalid Device ID\n";
        case MCC_ERR_USB_INIT:
            return "Failed to Init USB\n";
        case MCC_ERR_PIPE:
            return "Libusb Pipe Error, possibly invalid command\n";
        case MCC_ERR_LIBUSB_TIMEOUT:
            return "Transfer Timed Out\n";
        case MCC_ERR_UNKNOWN_LIB_USB_ERR:
            return "Unknown LibUSB Error\n";
        case MCC_ERR_INVALID_BUFFER_SIZE:
            return "Buffer must be and integer multiple of 32\n";
        case MCC_ERR_CANT_OPEN_FPGA_FILE:
            return "Cannot open FPGA file\n";
        case MCC_ERR_FPGA_UPLOAD_FAILED:
            return "FPGA firmware could not be uploaded\n";
        default:
            unknownerror << "Error number " << err << " has no text\n";
            return unknownerror.str();
    }
}

static std::string toNameString(int idProduct)
{
    switch(idProduct)
    {
        case USB_2001_TC:
            return "USB-2001-TC";
        case USB_7202:
            return "USB-7202";
        case USB_7204:
            return "USB-7204";
        case USB_1608_GX:
            return "USB-1608GX";
        case USB_1608_GX_2AO:
            return "USB-1608GX-2AO";
        case USB_1608_FS_PLUS:
            return "USB-1608-FS-PLUS";
        default:
            return "Invalid Product ID";
    }
}

static bool isMCCProduct(int idProduct)
{
    switch(idProduct)
    {
        case USB_2001_TC: case USB_7202: case USB_7204:
        case USB_1608_FS_PLUS: case USB_1608_GX: case USB_1608_GX_2AO://same for all products
            return true;
        default:
            return false;
            break;
    }
}


//Constructor finds the first available device where product ID == idProduct and optionally serial number == mfgSerialNumber
MCCDevice::MCCDevice(int idProduct)
{
    std::string mfgSerialNumber = "NULL";
    initDevice(idProduct, mfgSerialNumber);
}

MCCDevice::MCCDevice(int idProduct, std::string mfgSerialNumber)
{
    initDevice(idProduct, mfgSerialNumber);
}

//Destructor
MCCDevice::~MCCDevice () {
    //Free memory and devices
    libusb_release_interface(dev_handle, 0);
    libusb_close(dev_handle);
    libusb_free_device_list(list, true);
    libusb_exit(NULL);
    calSlope = nullptr;
    calOffset = nullptr;
    minVoltage = nullptr;
    maxVoltage = nullptr;
    delete [] mData;
    mData = nullptr;
}

//Find the device, opens it, and claims it. Called by constructors.
//Sets idProduct, maxCounts, list, dev_handle
void MCCDevice::initDevice(int idProduct, std::string mfgSerialNumber){
    int i;
    bool found = false;
    ssize_t sizeOfList;
    libusb_device_descriptor desc;
    libusb_device* device;
    std::string mfgsermsg = "?DEV:MFGSER";
    std::string retMessage;
    
    //Check if the product ID is a valid MCC product ID
    if(!isMCCProduct(idProduct))
    {
        throw MCC_ERR_INVALID_ID;
    }
    
    //Initialize USB libraries
    if(libusb_init(NULL) != 0)
    {
        throw MCC_ERR_USB_INIT;
    }
    
    //Get the list of USB devices connected to the PC
    sizeOfList= libusb_get_device_list(NULL, &list);
    
    //Traverse the list of USB devices to find the requested device
    for (i=0; (i<sizeOfList) && (!found); i++)
    {
        device = list[i];
        libusb_get_device_descriptor(device, &desc);
        if (desc.idVendor == MCC_VENDOR_ID && desc.idProduct == idProduct)
        {
            
            //Open the device
			int res = libusb_open(device, &dev_handle);
            //libusb_open(device, &dev_handle) returns -12 in Windows;
            if (!res)
            {
                //Claim interface with the device
                if (!libusb_claim_interface(dev_handle, 0))
                {
                    //Get scan parameters
                    getScanParams(); //sets endpoint_in, endpoint_out, bulkPacketSize
                    
                    try
                    {
                        //get the device serial number
                        retMessage = sendMessage(mfgsermsg); //For 1608-FS-Plus, DEV:MFGSER=018FF921 in response to ?DEV:MFGSER
                    }
                    catch(mcc_err err)
                    {
                        throw err;
                    }
                    
                    //Erase message while keeping serial number
                    retMessage.erase(0, 11); //TODO: Use mfgsermsg.length()
                    //cout << "Found " << toNameString(idProduct) << " with Serial Number " << retMessage << "\n";
                    
                    //If the input serial number was not NULL and retMessage does not match (string.compare returns 0 if matched.
                    if (mfgSerialNumber.compare("NULL")!=0 && retMessage.compare(mfgSerialNumber)!=0)
                    {//serial numbers are not the same, release device and continue on
                        libusb_release_interface(dev_handle, 0);
                    }
                    else
                    { //serial numbers are the same, this is the correct device
                        found = true;
                    }
                }
            }
        }
    }
    
    if (!found)
    {
        throw MCC_ERR_NO_DEVICE;
    }
    else
    {
        this->idProduct = idProduct;
        maxCounts = 0xFFFF;//I deleted the firmware flash, so all devices initialize the same way.
                           //this->getLimits(); //For some reason, the messages do not get responses.
        
        //Always init the internal data buffer. It can be used with getBlock().
        //The data buffer can be ignored if using external data buffer and readScanData();
        mSamplesPerBlock = 1; //Change this value then reconfigure().
        mData = new unsigned short [mSamplesPerBlock * 1]; //This will get overwritten in reconfigure.
        this->reconfigure();
    }
}

//Get the device input and output endpoints
void MCCDevice::getScanParams()
{
    int numBytesTransferred;
    unsigned char epDescriptor[MAX_MESSAGE_LENGTH];
    uint8_t requesttype = (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE);
    uint16_t wValue = (0x02 << 8) | 0;  // I have no idea where this comes from.
    numBytesTransferred = libusb_control_transfer(dev_handle, requesttype, LIBUSB_REQUEST_GET_DESCRIPTOR,
                                                  wValue, 0, epDescriptor, MAX_MESSAGE_LENGTH, HS_DELAY);
    
    if(numBytesTransferred < 0)
        throw libUSBError(numBytesTransferred);
    
    //Get pieces of information from epDescriptor (result of libusb_control_transfer).
    endpoint_in = getEndpointInAddress(epDescriptor, numBytesTransferred);
    endpoint_out = getEndpointOutAddress(epDescriptor, numBytesTransferred);
    bulkPacketSize = getBulkPacketSize(epDescriptor, numBytesTransferred);
}

//Find the input endpoint from an endpoint descriptor
unsigned char MCCDevice::getEndpointInAddress(unsigned char* data, int data_length)
{
    int descriptorType;
    int length;
    int index = 0;
    
    while (true)
    {
        length = data[index];
        descriptorType = data[index + 1];
        
        if (length == 0)
            break;
        
        if (descriptorType != 0x05)
        {
            index += length;
        }
        else
        {
            if ((data[index + 2] & 0x80) != 0)
                return data[index + 2];
            else
                index += length;
        }
        
        if (index >= data_length)
            break;
    }
    
    return 0;
}

//Find the output endpoint from an endpoint descriptor
unsigned char MCCDevice::getEndpointOutAddress(unsigned char* data, int data_length)
{
    int descriptorType;
    int length;
    int index = 0;
    
    while (true)
    {
        length = data[index];
        descriptorType = data[index + 1];
        
        if (length == 0)
            break;
        
        if (descriptorType != 0x05)
        {
            index += length;
        }
        else
        {
            if ((data[index + 2] & 0x80) == 0)
                return data[index + 2];
            else
                index += length;
        }
        
        if (index >= data_length)
            break;
    }
    
    return 0;
}

//Find the packet size from an endpoint descriptor.
unsigned short MCCDevice::getBulkPacketSize(unsigned char* data, int data_length){
    int descriptorType;
    int length;
    int index = 0;
    
    while(true){
        length = data[index];
        descriptorType = data[index+1];
        
        if(length == 0)
            break;
        
        if(descriptorType != 0x05){
            index += length;
        } else {
            if((data[index+2] & 0x80) != 0){
                //found the packet size
                return (unsigned short)(data[index+5] << 8) | (unsigned short)(data[index + 4]);
            } else {
                index += length;
            }
        }
    }
    
    return 0;
}

//Will return at most a 64 character array.
//Returns response if transfer successful, null if not
std::string MCCDevice::sendMessage(std::string message)
{
    try
    {
        sendControlTransferString(message);
        return getControlTransferString();
    }
    catch(mcc_err err)
    {
        throw err;
    }
}

//Send a message to the device
void MCCDevice::sendControlTransferString(std::string message)
{
    int numBytesTransferred;
    
    //StringUtil::toUpper(message);
    //TODO: Convert message toUpper
    
    //std::cout << "Sending: " << message << std::endl;
    unsigned char data[MAX_MESSAGE_LENGTH]; //64
    copy( message.begin(), message.end(), data );
    data[MAX_MESSAGE_LENGTH - 1] = '\0';
    //std::cout << "Message data: " << data << std::endl;
    
    uint8_t requesttype = (LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE);
    numBytesTransferred = libusb_control_transfer(dev_handle, requesttype,
                                                  STRING_MESSAGE, 0, 0, data,
                                                  MAX_MESSAGE_LENGTH, HS_DELAY);
    
    if(numBytesTransferred < 0)
        throw libUSBError(numBytesTransferred);
}

//Receive a message from the device. This should follow a call to sendControlTransfer.
//It will return a pointer to at most a 64 character array.
std::string MCCDevice::getControlTransferString()
{
    int messageLength;
    unsigned char message[MAX_MESSAGE_LENGTH];
    std::string out_string;
    uint8_t requesttype = (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE);
    messageLength = libusb_control_transfer(dev_handle,  requesttype,
                                            STRING_MESSAGE, 0, 0, message,
                                            MAX_MESSAGE_LENGTH, HS_DELAY);
    if(messageLength < 0)
        throw libUSBError(messageLength);
    
    out_string = (char*)message;
    //std::cout << "Got: " << return_string << "\n\n";
    return out_string; //e.g. DEV:MFGSER=018FF921 in response to ?DEV:MFGSER
}


/*Reads analog in scan data.
 length is the length of the data array (max bytes to transfer)
 */
void MCCDevice::readScanData(unsigned short* data, int length)
{
    //A short is 16 bits. A char is 8 bits.
    int err = 0, totalTransferred = 0, transferred;
    unsigned char* dataAsByte = (unsigned char*)data; //Change the type of the pointer to data.
    unsigned int timeout = 2000000;///(bulkPacketSize*rate);
    
    do{
        //TODO: Convert to asynchronous I/O API
        err =  libusb_bulk_transfer(dev_handle, endpoint_in, &dataAsByte[totalTransferred], bulkPacketSize, &transferred, timeout);
        totalTransferred += transferred;
        //std::cout << "Transferred " << totalTransferred << "of " << length*2 << std::endl;
        /*if(err == LIBUSB_ERROR_TIMEOUT && transferred > 0)//a timeout may indicate that some data was transferred, but not all
         err = 0;*/
    }while (totalTransferred < length*2 && err >= 0); //TODO: Change 2 to bytes per sample.
    
    if (err < 0)
        throw libUSBError(err);
}

void MCCDevice::getBlock()
{
    readScanData(mData, mSamplesPerBlock*mChannelCount);
}

/*
 void MCCDevice::getLimits()
 {
 string respValidChans, respRate, respThru;
 vector<int> result;
 
 //Get the maximum scan rate (max per channel) and the maxthruput (total max).
 respRate = sendMessage("@AISCAN:MAXSCANRATE");//AISCAN:MAXSCANRATE=FIXED%100000
 myLimits.maxScanRate = fromString<int32_t>(respRate.erase(0, 25) );
 respThru = sendMessage("@AISCAN:MAXSCANTHRUPUT");//AISCAN:MAXSCANTHRUPUT=FIXED%400000
 myLimits.maxScanThruput = fromString<int32_t>( respThru.erase(0, 28) );
 cout << "maxScanRate=" << myLimits.maxScanRate << "; maxScanThruPut=" << myLimits.maxScanThruput << "\n\n";
 
 //Get the list of valid chans and pull out the first and last.
 respValidChans = sendMessage("?AI:VALIDCHANS");//AI:VALIDCHANS=0,1,2,3,4,5,6,7
 stringstream ss( respValidChans.erase(0, 14) );
 while( ss.good() )
 {
 string substr;
 getline( ss, substr, ',' );
 result.push_back( fromString<int>(substr) );
 }
 myLimits.lowChan = *min_element(result.begin(), result.end());
 myLimits.highChan = *max_element(result.begin(), result.end());
 cout << "Channels " << myLimits.lowChan << ":" << myLimits.highChan << "\n";
 }
 */

void MCCDevice::reconfigure()
{
    int lowChan, highChan;
    std::string respLow, respHigh, respOff, respSlope, respRange, respRate;
    std::stringstream strOff, strSlope, strRange;
    respLow = sendMessage("?AISCAN:LOWCHAN");
    lowChan = fromString<int>(respLow.erase(0, 15));
    respHigh = sendMessage("?AISCAN:HIGHCHAN");
    highChan = fromString<int>(respHigh.erase(0, 16));
    mChannelCount = highChan - lowChan + 1;
    respRate = sendMessage("?AISCAN:RATE");
    sampRate = fromString<float>(respRate.erase(0, 12));
    delete [] mData;
    mData = new unsigned short [mChannelCount * mSamplesPerBlock];
    
    //stringstream strBuff;
    //strBuff << "AISCAN:BUFSIZE=" << mChannelCount*mSamplesPerBlock*2;
    //sendMessage(strBuff.str());
    //sendMessage("AISCAN:BUFOVERWRITE=DISABLE");
    
    //Reset members that are per-channel arrays.
    calSlope = nullptr; calSlope = new float[mChannelCount];
    calOffset = nullptr; calOffset = new float[mChannelCount];
    minVoltage = nullptr; minVoltage = new int[mChannelCount];
    maxVoltage = nullptr; maxVoltage = new int[mChannelCount];
    
    for (int chanIdx = lowChan; chanIdx<=highChan; chanIdx++)
    {
        //Reset string streams.
        strOff.str("");
        strSlope.str("");
        strRange.str("");
        //strSlope = string("?AI{") + "}:SLOPE";
        strSlope << "?AI{" << chanIdx << "}:SLOPE";
        respSlope = sendMessage(strSlope.str());
        calSlope[chanIdx - lowChan] = fromString<float>(respSlope.erase(0,12));
        
        strOff << "?AI{" << chanIdx << "}:OFFSET";
        respOff = sendMessage(strOff.str());
        calOffset[chanIdx - lowChan] = fromString<float>(respOff.erase(0,13));
        //        calOffset[chanIdx-lowChan] = atof(respSlope.erase(0, 13).c_str());
        
        strRange << "?AI{" << chanIdx << "}:RANGE";
        respRange = sendMessage(strRange.str());
        respRange = respRange.erase(0, 12);
        if (respRange == "BIP10V"){
            minVoltage[chanIdx - lowChan] = -10;
            maxVoltage[chanIdx - lowChan] = 10;
        }else if (respRange == "BIP5V"){
            minVoltage[chanIdx - lowChan] = -5;
            maxVoltage[chanIdx - lowChan] = 5;
        }else if (respRange == "BIP2V"){
            minVoltage[chanIdx - lowChan] = -2;
            maxVoltage[chanIdx - lowChan] = 2;
        }else if (respRange == "BIP1V"){
            minVoltage[chanIdx - lowChan] = -1;
            maxVoltage[chanIdx - lowChan] = 1;
        }
        //cout << "Channel " << chanIdx << " Slope: " << calSlope[chanIdx-lowChan] << " Offset: " << calOffset[chanIdx-lowChan] << " in Range " << minVoltage[chanIdx-lowChan] << ":" << maxVoltage[chanIdx-lowChan] << "\n\n";
    }
}

//scale and calibrate data
float MCCDevice::scaleAndCalibrateData(unsigned short data, int chanIdx){
    float calibratedData;
    float scaledAndCalibratedData;
    int fullScale = maxVoltage[chanIdx] - minVoltage[chanIdx];
    
    //Calibrate the data
    calibratedData = (float)data*calSlope[chanIdx] + calOffset[chanIdx];
    
    //Scale the data
    scaledAndCalibratedData = (calibratedData/(float)maxCounts)*fullScale + minVoltage[chanIdx];
    
    return scaledAndCalibratedData;
}

void MCCDevice::flushInputData()
{
    int bytesTransfered = 0;
    int status;
    unsigned char * buf = new unsigned char [bulkPacketSize];
    do
    {
        status = libusb_bulk_transfer(dev_handle, endpoint_in, buf, bulkPacketSize, &bytesTransfered, 200);
    } while (bytesTransfered > 0 && status == 0);
    delete[] buf;
}

uint8_t MCCDevice::getDIOTristate()
{
    uint8_t requesttype = (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE);
    uint8_t data = 0x0;
    int res = libusb_control_transfer(dev_handle, requesttype, DTRISTATE,
                                      0x0, 0x0, (unsigned char *) &data,
                                      sizeof(data), HS_DELAY);
    if (res < 0)
    {
        throw libUSBError(res);
    }
    return data;
}

void MCCDevice::setDIOTristate(uint8_t chanMask)
{
    uint8_t requesttype = (LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE);
    int res = libusb_control_transfer(dev_handle, requesttype, DTRISTATE,
                                      chanMask, 0x0, NULL, 0x0, HS_DELAY);
    if (res < 0)
    {
        throw libUSBError(res);
    }
}

uint8_t MCCDevice::getDIOPort()
{
    uint8_t requesttype = (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE);
    uint8_t data;
    int res = libusb_control_transfer(dev_handle, requesttype, DPORT,
                                      0x0, 0x0, (unsigned char *) &data,
                                      sizeof(data), HS_DELAY);
    if (res < 0)
    {
        throw libUSBError(res);
    }
    return data;
}

uint8_t MCCDevice::getDIOLatch()
{
    uint8_t requesttype = (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE);
    uint8_t data;
    int res = libusb_control_transfer(dev_handle, requesttype, DLATCH,
                                      0x0, 0x0, (unsigned char *) &data,
                                      sizeof(data), HS_DELAY);
    if (res < 0)
    {
        throw libUSBError(res);
    }
    return data;
}

void MCCDevice::setDIOLatch(uint8_t value)
{
    uint8_t requesttype = (LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE);
    int res = libusb_control_transfer(dev_handle, requesttype, DLATCH, value,
                                      0x0, NULL, 0x0, HS_DELAY);
    if (res < 0)
    {
        throw libUSBError(res);
    }
}





