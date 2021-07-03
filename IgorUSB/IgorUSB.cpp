#include <windows.h>

#include "IgorUSB.h"

#pragma warning(push)
#pragma warning(disable:4200 4510 4512 4610)
#include "libusb.h"
#pragma warning(pop)

#define VENDOR_ATMEL    0x03eb
#define DEVICE_IGORPLUG 0x0002

#define DO_SET_INFRA_BUFFER_EMPTY 0x1
#define DO_GET_INFRA_CODE 0x2

#define HEADER_LENGTH 3
#define MAX_BUFFER_SIZE 256

enum TransferStatus {
    NoError,
    ErrorNoDevice,
    ErrorTransfer
};

// Globals
libusb_context* gContext = NULL;
libusb_device_handle* gDevice = NULL;

// 
static TransferStatus SendToDevice(uint8_t fn, uint16_t param1, uint16_t param2, unsigned char *buf, uint16_t buf_size, uint16_t * transf_bytes = NULL);

// implementation
bool OpenDevice()
{
    if(gDevice)
        return true;

    if(!gDevice)
    {
        if(!gContext)
        {
            int err = libusb_init(&gContext);
            if(err)
            {
                OutputDebugString(TEXT("Failed to initialize libusb."));
                gContext = NULL;
                return false;
            }
        }

        gDevice = libusb_open_device_with_vid_pid(gContext, VENDOR_ATMEL, DEVICE_IGORPLUG);
        if(gDevice)
        {
            OutputDebugString(TEXT("Found IgorPlugUSB device."));
            return true;
        }
    }

    return false;
}

void CloseDevice()
{
    if(gContext)
    {
        if (gDevice)
        {
            libusb_close(gDevice);
            gDevice = NULL;
        }
        libusb_exit(gContext);
    }
}

TransferStatus SendToDevice(uint8_t fn, uint16_t param1, uint16_t param2, unsigned char *buf, uint16_t buf_size, uint16_t * transf_bytes)
{
    if (transf_bytes)
        *transf_bytes = 0;

    if(OpenDevice())
    {
        int result = libusb_control_transfer(gDevice, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN, fn, param1, param2, buf, buf_size, 500);
        if(result < LIBUSB_SUCCESS)
        {
            const char * error_str = libusb_strerror(libusb_error(result));
#if defined(UNICODE)
            size_t newsize = strlen(error_str) + 1;
            wchar_t * wcstring = new wchar_t[newsize];
            // Convert char* string to a wchar_t* string.
            size_t convertedChars = 0;
            mbstowcs_s(&convertedChars, wcstring, newsize, error_str, _TRUNCATE);
            OutputDebugString(wcstring);
            delete[] wcstring;
#else
            OutputDebugString(error_str);
#endif
            CloseDevice();

            if (result == LIBUSB_ERROR_NO_DEVICE)
                return ErrorNoDevice;
            else
                return ErrorTransfer;
        }
        else
        {
            if(result > UINT16_MAX)
            {
                OutputDebugString(TEXT("Unexpected: too many bytes transferrer"));
                if (transf_bytes)
                    *transf_bytes = UINT16_MAX;
            }
            else if (transf_bytes)
                *transf_bytes = uint16_t(result);
            return NoError;
        }
    }
    return ErrorNoDevice;
}

IGORUSB_API int __stdcall DoSetInfraBufferEmpty()
{
    int result = IGORUSB_DEVICE_NOT_PRESENT;

    unsigned char buf[2];

    if(SendToDevice(DO_SET_INFRA_BUFFER_EMPTY, 0, 0, buf, 1, NULL) == NoError)
        result = NO_ERROR;

    return result;
}

IGORUSB_API int __stdcall DoGetInfraCode(unsigned char * TimeCodeDiagram, int /*DummyInt*/, int * DiagramLength)
{
    static int last_read = -1;

    unsigned char buf[MAX_BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));

    if(DiagramLength)
        *DiagramLength = 0;

    uint16_t recvd;
    if (SendToDevice(DO_GET_INFRA_CODE, 0, 0, buf, 3, &recvd) != NoError)
    {
        last_read = -1;
        return IGORUSB_DEVICE_NOT_PRESENT;
    }

    if(recvd != 3)
    {
        // Nothing to do
        return NO_ERROR;
    }

    uint16_t bytes_to_read = buf[0];
    if(bytes_to_read == 0)
        return NO_ERROR;

    int msg_idx = buf[1];
    int last_written_idx = buf[2];

    uint16_t buf_size;
    uint16_t i = 0;
    while(i < bytes_to_read)
    {
        buf_size = bytes_to_read - i;
        if(buf_size > MAX_BUFFER_SIZE)
        {
            OutputDebugString(TEXT("Buffer is too small."));
            break;
        }

        TransferStatus stat = SendToDevice(DO_GET_INFRA_CODE, i + HEADER_LENGTH, 0, &buf[i], buf_size, &recvd);
        if (stat != NoError)
        {
            last_read = -1;
            return (stat == ErrorNoDevice)? IGORUSB_DEVICE_NOT_PRESENT : DoSetInfraBufferEmpty();
        }

        i += recvd;
    }

    if(msg_idx != last_read)
    {
        // new message
        uint16_t j = last_written_idx % bytes_to_read;
        int k = 0;
        for (i = j; i < bytes_to_read; ++i)
            TimeCodeDiagram[k++] = buf[i];

        for (i = 0; i < j; ++i)
            TimeCodeDiagram[k++] = buf[i];

        if(DiagramLength)
            *DiagramLength = bytes_to_read;
    }
    else
    {
        // message is repeated (has same index as before)
        // -> do nothing
        if (DiagramLength)
            *DiagramLength = 0;
    }
    last_read = msg_idx;

    return DoSetInfraBufferEmpty();
}

IGORUSB_API int __stdcall DoSetDataPortDirection(unsigned char /*DirectionByte*/)
{
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoGetDataPortDirection(unsigned char * DataDirectionByte)
{
    if (DataDirectionByte)
        *DataDirectionByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoSetOutDataPort(unsigned char /*DataOutByte*/)
{
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoGetOutDataPort(unsigned char * DataOutByte)
{
    if (DataOutByte)
        *DataOutByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoGetInDataPort(unsigned char * DataInByte)
{
    if (DataInByte)
        *DataInByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoEEPROMRead(unsigned char /*Address*/, unsigned char * DataInByte)
{
    if (DataInByte)
        *DataInByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoEEPROMWrite(unsigned char /*Address*/, unsigned char /*DataOutByte*/)
{
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoRS232Send(unsigned char /*DataOutByte*/)
{
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoRS232Read(unsigned char * DataInByte)
{
    if (DataInByte)
        *DataInByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoSetRS232Baud(int /*BaudRate*/)
{
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoGetRS232Baud(int * BaudRate)
{
    if (BaudRate)
        *BaudRate = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}
