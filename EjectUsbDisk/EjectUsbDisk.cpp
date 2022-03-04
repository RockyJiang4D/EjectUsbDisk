// EjectUsbDisk.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <windows.h>
#include "ntddscsi.h"
#include "winioctl.h" 
#include "usbioctl.h"
#include "setupapi.h"
#include "Dbt.h"
#include "cfgmgr32.h"
#include "usbioctl.h"


DEVINST GetDrivesDevInstByDiskNumber(long DiskNumber)
{
	GUID* guid = (GUID*)(void*)&GUID_DEVINTERFACE_DISK;

	// Get device interface info set handle for all devices attached to system
	HDEVINFO hDevInfo = SetupDiGetClassDevs(guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	if (hDevInfo == INVALID_HANDLE_VALUE)
	{
		return 0;
	}

	// Retrieve a context structure for a device interface of a device
	// information set.
	DWORD dwIndex = 0;
	SP_DEVICE_INTERFACE_DATA devInterfaceData = {0};
	devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	BOOL bRet = FALSE;

	PSP_DEVICE_INTERFACE_DETAIL_DATA pspdidd;
	SP_DEVICE_INTERFACE_DATA spdid;
	SP_DEVINFO_DATA spdd;
	DWORD dwSize;

	spdid.cbSize = sizeof(spdid);

	while ( true )
	{
		bRet = SetupDiEnumDeviceInterfaces(hDevInfo, NULL, guid, dwIndex, &devInterfaceData);
		if (!bRet)
		{
			break;
		}

		SetupDiEnumInterfaceDevice(hDevInfo, NULL, guid, dwIndex, &spdid);

		dwSize = 0;
		SetupDiGetDeviceInterfaceDetail(hDevInfo, &spdid, NULL, 0, &dwSize,NULL);

		if ( dwSize )
		{
			pspdidd = (PSP_DEVICE_INTERFACE_DETAIL_DATA)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY, dwSize);
			if ( pspdidd == NULL ) 
			{
				continue; // autsch
			}
			pspdidd->cbSize = sizeof(*pspdidd);
			ZeroMemory((PVOID)&spdd, sizeof(spdd));
			spdd.cbSize = sizeof(spdd);


			long res = SetupDiGetDeviceInterfaceDetail(hDevInfo, &spdid,pspdidd, dwSize, &dwSize, &spdd);
			if ( res )
			{
				HANDLE hDrive = CreateFile(pspdidd->DevicePath, 0,
					FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);
				if ( hDrive != INVALID_HANDLE_VALUE )
				{
					STORAGE_DEVICE_NUMBER sdn;
					DWORD dwBytesReturned = 0;
					res = DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER,NULL, 0, &sdn, sizeof(sdn), &dwBytesReturned, NULL);
					if ( res ) 
					{
						if ( DiskNumber == (long)sdn.DeviceNumber )
						{
							CloseHandle(hDrive);
							HeapFree(GetProcessHeap(), 0, pspdidd);
							SetupDiDestroyDeviceInfoList(hDevInfo);
							return spdd.DevInst;
						}
					}
					CloseHandle(hDrive);
				}
			}
			HeapFree(GetProcessHeap(), 0, pspdidd);
		}
		dwIndex++;
	}

	SetupDiDestroyDeviceInfoList(hDevInfo);

	return 0;
}


BOOL EjectUsbDisk(TCHAR driveLetter)
{
	DWORD accessMode = GENERIC_WRITE | GENERIC_READ;
	DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	HANDLE hDevice;
	long bResult = 0;
	DWORD dwBytesReturned;
	TCHAR szDrive[10] = L"\\\\.\\X:";

	if (!(((driveLetter >= 'A') && (driveLetter <= 'Z'))
		|| ((driveLetter >= 'a') && (driveLetter <= 'z'))))
	{
		printf("Invalid drive letter");
		return FALSE;
	}

	szDrive[4] = driveLetter;
	hDevice = CreateFile(szDrive, accessMode, shareMode, NULL, OPEN_EXISTING, 0, NULL);
	if(hDevice == INVALID_HANDLE_VALUE)
	{
		printf(" failed to createfile %d\n", GetLastError());
		return FALSE;
	}

	STORAGE_DEVICE_NUMBER sdn;
	long DiskNumber = -1;
	long res = DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &dwBytesReturned, NULL);
	if(!res)
	{
		printf("DeviceIoControl IOCTL_STORAGE_GET_DEVICE_NUMBER error:%d\n", GetLastError());
		CloseHandle(hDevice);
		return FALSE;
	}

	CloseHandle(hDevice);
	DiskNumber = sdn.DeviceNumber;
	if(DiskNumber == -1)
	{
		return FALSE;
	}

	DEVINST DevInst = GetDrivesDevInstByDiskNumber(DiskNumber);
	if(DevInst == 0)
	{
		printf("GetDrivesDevInstDiskNumber error\r\n");
		return FALSE;
	}

	ULONG Status = 0;
	ULONG ProblemNumber = 0;
	PNP_VETO_TYPE VetoType = PNP_VetoTypeUnknown;
	WCHAR VetoName[MAX_PATH];
	bool bSuccess = false;

	res = CM_Get_Parent(&DevInst, DevInst,0); //disk's parent, e.g. the USB bridge, the SATA controller....
	res = CM_Get_DevNode_Status(&Status, &ProblemNumber, DevInst,0);
	bool IsRemovable = ((Status & DN_REMOVABLE) != 0);

	for(int i = 0; i < 3; i++)
	{
		VetoName[0] = '\0';
		if(IsRemovable)
		{
			res = CM_Request_Device_Eject(DevInst, &VetoType, VetoName, MAX_PATH,0);
		}
		else
		{
			res = CM_Query_And_Remove_SubTree(DevInst, &VetoType, VetoName, MAX_PATH,0);
		}
		bSuccess = (res == CR_SUCCESS && VetoName[0] == '\0');
		if(bSuccess)
		{
			break;
		}

		Sleep(200);
	}

	if(bSuccess)
	{
		printf("Successful to eject usb drive %c:\r\n", driveLetter);
	}
	else
	{
		printf("Failed to eject usb drive %c:\r\n", driveLetter);

	}
	return bSuccess;
}


int _tmain(int argc, _TCHAR* argv[])
{
	if (argc != 2)
	{
		printf("Usage(X -- DriveLetter):\r\n      EjectUsbDisk X: \r\n");
		return 0;
	}

	EjectUsbDisk(argv[1][0]);
	return 0;
}

