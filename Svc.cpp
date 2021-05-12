#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <stdio.h>
#include <TlHelp32.h>
#include <Shlobj.h>
#include <string>
#include <vector>

#include "lxEventlog.h"
 
using namespace std;

#pragma comment(lib, "advapi32.lib")

#define SVCNAME TEXT("WatchDog")

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  ghSvcStopEvent = NULL;

VOID SvcInstall(void);
VOID WINAPI SvcCtrlHandler(DWORD);
VOID WINAPI SvcMain(DWORD, LPTSTR *);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR *);
VOID SvcReportEvent(LPTSTR);

VOID __stdcall DoDeleteSvc(void);

int get_file_size(FILE*fp);
void get_file_full_path(vector<wstring> &strings, char *buf, int size);
void get_file_from_full_path(vector<wstring> &full_path_strings, vector<wstring> &strings);
string GetExePath(void);
wstring string2wstring(string str);
BOOL AddEventSource(LPCTSTR lpszChannelName, LPCTSTR lpszSourceName, LPCTSTR lpModulePath);
//
// Purpose: 
//   Entry point for the process
//
// Parameters:
//   None
// 
// Return value:
//   None, defaults to 0 (zero)
//
int __cdecl _tmain(int argc, TCHAR *argv[])
{
    

    // If command-line parameter is "install", install the service. 
    // Otherwise, the service is probably being started by the SCM.

    if (lstrcmpi(argv[1], TEXT("install")) == 0)
    {
        SvcInstall();
        return 0;
    }
    if (lstrcmpi(argv[1], TEXT("delete")) == 0)
    {
        DoDeleteSvc();
        return 0;
    }
    if (lstrcmpi(argv[1], TEXT("register")) == 0)
    {
        printf("on register\n");
        AddEventSource(_T("Application"), SVCNAME, (string2wstring(GetExePath()+"\\lxEventlog.dll")).c_str() );
        printf("register over\n");
        return 0;
    }


    // TO_DO: Add any additional services for the process to this table.
    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        { SVCNAME, (LPSERVICE_MAIN_FUNCTION)SvcMain },
        { NULL, NULL }
    };

    // This call returns when the service has stopped. 
    // The process should simply terminate when the call returns.

    if (!StartServiceCtrlDispatcher(DispatchTable))
    {
        //SvcReportEvent(TEXT("StartServiceCtrlDispatcher"));
    }
}


wstring string2wstring(string str)
{
    wstring result;
    //获取缓冲区大小，并申请空间，缓冲区大小按字符计算  
    int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), NULL, 0);
    TCHAR* buffer = new TCHAR[len + 1];
    //多字节编码转换成宽字节编码  
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), buffer, len);
    buffer[len] = '\0';             //添加字符串结尾  
                                    //删除缓冲区并返回值  
    result.append(buffer);
    delete[] buffer;
    return result;
}


//
// Purpose: 
//   Installs a service in the SCM database
//
// Parameters:
//   None
// 
// Return value:
//   None
//
VOID SvcInstall()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    TCHAR szPath[MAX_PATH];

    if (!GetModuleFileName(NULL, szPath, MAX_PATH))
    {
        printf("Cannot install service (%d)\n", GetLastError());
        return;
    }

    // Get a handle to the SCM database. 

    schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);  // full access rights 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    // Create the service

    schService = CreateService(
        schSCManager,              // SCM database 
        SVCNAME,                   // name of service 
        SVCNAME,                   // service name to display 
        SERVICE_ALL_ACCESS,        // desired access 
        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_DEMAND_START,      // start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 

    if (schService == NULL)
    {
        printf("CreateService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    else printf("Service installed successfully\n");

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

//
// Purpose: 
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR *lpszArgv)
{
    // Register the handler function for the service

    gSvcStatusHandle = RegisterServiceCtrlHandler(
        SVCNAME,
        SvcCtrlHandler);

    if (!gSvcStatusHandle)
    {
        SvcReportEvent(TEXT("RegisterServiceCtrlHandler"));
        return;
    }

    // These SERVICE_STATUS members remain as set here

    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwServiceSpecificExitCode = 0;

    // Report initial status to the SCM

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Perform service-specific initialization and work.

    SvcInit(dwArgc, lpszArgv);
    
    

}

//
// Purpose: 
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
// 
// Return value:
//   None
//
VOID SvcInit(DWORD dwArgc, LPTSTR *lpszArgv)
{
    // TO_DO: Declare and set any required variables.
    //   Be sure to periodically call ReportSvcStatus() with 
    //   SERVICE_START_PENDING. If initialization fails, call
    //   ReportSvcStatus with SERVICE_STOPPED.

    // Create an event. The control handler function, SvcCtrlHandler,
    // signals this event when it receives the stop control code.

    ghSvcStopEvent = CreateEvent(
        NULL,    // default security attributes
        TRUE,    // manual reset event
        FALSE,   // not signaled
        NULL);   // no name

    if (ghSvcStopEvent == NULL)
    {
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }

    // Report running status when initialization is complete.

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // TO_DO: Perform work until service stops.

    for (;; )
    {
        char *buf = new char[4096];
        memset(buf, 0, 4096);
        LARGE_INTEGER liFileSizeSrc = { 0 }, liFileSizeDst = { 0 };
        OVERLAPPED olw = { 0 };
        //打开源文件
        HANDLE hFileSrc = CreateFile( string2wstring(GetExePath()+"\\config.txt").c_str() , GENERIC_READ,
            0, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
        //获取文件大小
        GetFileSizeEx(hFileSrc, &liFileSizeSrc);
        int file_size = liFileSizeSrc.QuadPart;//get_file_size(fp);
        DWORD temp = 0;
        ReadFile(hFileSrc, buf, file_size, &temp, NULL);

        CloseHandle(hFileSrc);

        vector<wstring> full_path_strings;
        get_file_full_path(full_path_strings, buf, file_size);
        vector<wstring> strings;
        get_file_from_full_path(full_path_strings, strings);



        //列举进程
        HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);//此时该函数的第二个参数被忽略
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);
        Process32First(hSnapShot, &pe);
        vector<DWORD> processIDs;
        vector<wstring> should_be_start_full_path_strings = full_path_strings;
        vector<wstring> should_be_start_strings = strings;

        do
        {
            _tprintf(TEXT("ID    %d ,  Name  %s\n"), pe.th32ProcessID, pe.szExeFile);

            for (int i = 0; i < strings.size(); i++)
            {
                if (strings[i] == wstring{ pe.szExeFile })
                {
                    auto iter = find(should_be_start_full_path_strings.begin(), should_be_start_full_path_strings.end(), full_path_strings[i]);
                    should_be_start_full_path_strings.erase(iter);

                    auto iter1 = find(should_be_start_strings.begin(), should_be_start_strings.end(), strings[i]);
                    should_be_start_strings.erase(iter1);
                }

            }
        } while (Process32Next(hSnapShot, &pe));



        if (!should_be_start_full_path_strings.empty())
        {
            for (int i = 0; i < should_be_start_full_path_strings.size(); i++)
            {
                STARTUPINFO info;
                ZeroMemory(&info, sizeof(info));
                info.cb = sizeof(info);
                PROCESS_INFORMATION si;
                //TCHAR buf[100] = TEXT("D:/项目发布/RecordScreen.exe");

                wstring s = should_be_start_full_path_strings[i].substr(0, should_be_start_full_path_strings[i].size() - should_be_start_strings[i].size());

                CreateProcess(NULL,
                    (LPWSTR)(should_be_start_full_path_strings[i].c_str()),
                    NULL,
                    NULL,
                    FALSE,
                    NULL,
                    NULL,
                    should_be_start_full_path_strings[i].substr(0, should_be_start_full_path_strings[i].size() - should_be_start_strings[i].size()).c_str(),
                    &info,
                    &si);
                SvcReportEvent((LPTSTR)(L" restart  " + should_be_start_full_path_strings[i]).c_str());
            }
        }
        CloseHandle(hSnapShot);
        Sleep(5000);
        delete[]buf;

        // Check whether to stop the service.
        if (WAIT_OBJECT_0 == WaitForSingleObject(ghSvcStopEvent, 0))
        {
            ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
            break;
        }
        
    }

    return;
}

//
// Purpose: 
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation, 
//     in milliseconds
// 
// Return value:
//   None
//
VOID ReportSvcStatus(DWORD dwCurrentState,
    DWORD dwWin32ExitCode,
    DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    // Fill in the SERVICE_STATUS structure.

    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
        gSvcStatus.dwControlsAccepted = 0;
    else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if ((dwCurrentState == SERVICE_RUNNING) ||
        (dwCurrentState == SERVICE_STOPPED))
        gSvcStatus.dwCheckPoint = 0;
    else gSvcStatus.dwCheckPoint = dwCheckPoint++;

    // Report the status of the service to the SCM.
    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//
// Purpose: 
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
// 
// Return value:
//   None
//
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
    // Handle the requested control code. 

    switch (dwCtrl)
    {
    case SERVICE_CONTROL_STOP:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

        // Signal the service to stop.

        SetEvent(ghSvcStopEvent);
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);

        return;

    case SERVICE_CONTROL_INTERROGATE:
        break;

    default:
        break;
    }

}


VOID __stdcall DoDeleteSvc()
{
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    SERVICE_STATUS ssStatus;

    // Get a handle to the SCM database. 

    schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);  // full access rights 

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return;
    }

    // Get a handle to the service.

    schService = OpenService(
        schSCManager,       // SCM database 
        SVCNAME,          // name of service 
        DELETE);            // need delete access 

    if (schService == NULL)
    {
        printf("OpenService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }

    // Delete the service.

    if (!DeleteService(schService))
    {
        printf("DeleteService failed (%d)\n", GetLastError());
    }
    else printf("Service deleted successfully\n");

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}


 //Purpose: 
 //  Logs messages to the event log

 //Parameters:
 //  szFunction - name of function that failed
 //
 //Return value:
 //  None

 //Remarks:
 //  The service must have an entry in the Application event log.

VOID SvcReportEvent(LPTSTR szFunction)
{
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    TCHAR Buffer[80];

    hEventSource = RegisterEventSource(NULL, SVCNAME);

    if (NULL != hEventSource)
    {
        StringCchPrintf(Buffer, 80, TEXT("%s  with %d"), szFunction, GetLastError());

        lpszStrings[0] = SVCNAME;
        lpszStrings[1] = Buffer;

        ReportEvent(hEventSource,        // event log handle
            EVENTLOG_INFORMATION_TYPE, // event type
            0,                   // event category
            SVC_ERROR,           // event identifier
            NULL,                // no security identifier
            2,                   // size of lpszStrings array
            0,                   // no binary data
            lpszStrings,         // array of strings
            NULL);               // no binary data

        DeregisterEventSource(hEventSource);
    }
}

int get_file_size(FILE*fp)
{
    SvcReportEvent(L"6666666666666666666");
    fseek(fp, 0, SEEK_END); //定位到文件末 
    SvcReportEvent(L"777777777777777");
    int nFileLen = ftell(fp); //文件长度
    SvcReportEvent(L"8888888888888888");
    fseek(fp, 0, SEEK_SET); //恢复到文件头
    SvcReportEvent(L"99999999999999999");
    return nFileLen;
}

void get_file_full_path(vector<wstring> &strings, char *buf, int size)
{
    for (int i = 0; i < size; i++)
    {
        if (buf[i] == '<')
        {
            i++;
            wstring str;
            bool is_normal = false;
            for (; i < size; i++)
            {
                if (buf[i] == '>')
                {
                    is_normal = true;
                    break;
                }
                else
                {
                    str += buf[i];
                }
            }
            if (is_normal == true)
                strings.push_back(str);
        }
    }


}

void get_file_from_full_path(vector<wstring> &full_path_strings, vector<wstring> &strings)
{

    for (int i = 0; i < full_path_strings.size(); i++)
    {
        wstring str;
        for (int j = 1; j <= full_path_strings[i].size(); j++)
        {

            if ((*(full_path_strings[i].end() - j)) != '\\')
            {
                str += *(full_path_strings[i].end() - j);
            }
            else
            {
                reverse(str.begin(), str.end());
                strings.push_back(str);
                break;
            }
        }
    }
}


string GetExePath(void)
{
    char szFilePath[MAX_PATH + 1] = { 0 };
    GetModuleFileNameA(NULL, szFilePath, MAX_PATH);
    (strrchr(szFilePath, '\\'))[0] = 0; // 删除文件名，只获得路径
    string path = szFilePath;

    return path;
}

BOOL AddEventSource(LPCTSTR lpszChannelName, LPCTSTR lpszSourceName, LPCTSTR lpModulePath)
{
    BOOL bResult = FALSE;
    DWORD dwCategoryNum = 1;
    HKEY hk;
    DWORD dwData, dwDisp;
    TCHAR szBuf[MAX_PATH] = { 0 };
    size_t cchSize = MAX_PATH;

    __try
    {
        // Create the event source as a subkey of the log. 
        _stprintf(szBuf,
            _T("SYSTEM\\CurrentControlSet\\Services\\EventLog\\%s\\%s"),
            lpszChannelName, lpszSourceName);

        if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, szBuf,
            0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_WRITE, NULL, &hk, &dwDisp))
        {
            printf("Could not create the registry key.\n");
            __leave;
        }

        // Set the name of the message file. 

        if (RegSetValueEx(hk,             // subkey handle 
            _T("EventMessageFile"),        // value name 
            0,                         // must be zero 
            REG_EXPAND_SZ,             // value type 
            (LPBYTE)lpModulePath,          // pointer to value data 
            (DWORD)(lstrlen(lpModulePath) + 1) * sizeof(TCHAR))) // data size
        {
            printf("Could not set the event message file.\n");
            __leave;
        }

        // Set the supported event types. 

        dwData = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;

        if (RegSetValueEx(hk,      // subkey handle 
            _T("TypesSupported"),  // value name 
            0,                 // must be zero 
            REG_DWORD,         // value type 
            (LPBYTE)&dwData,  // pointer to value data 
            sizeof(DWORD)))    // length of value data 
        {
            printf("Could not set the supported types.\n");
            __leave;
        }

        // Set the category message file and number of categories.

        if (RegSetValueEx(hk,              // subkey handle 
            _T("CategoryMessageFile"),     // value name 
            0,                         // must be zero 
            REG_EXPAND_SZ,             // value type 
            (LPBYTE)lpModulePath,          // pointer to value data 
            (DWORD)(lstrlen(lpModulePath) + 1) * sizeof(TCHAR))) // data size
        {
            printf("Could not set the category message file.\n");
            __leave;
        }

        if (RegSetValueEx(hk,            // subkey handle 
            _T("CategoryCount"),         // value name 
            0,                       // must be zero 
            REG_DWORD,               // value type 
            (LPBYTE)&dwCategoryNum, // pointer to value data 
            sizeof(DWORD)))          // length of value data 
        {
            printf("Could not set the category count.\n");
            __leave;
        }

        bResult = TRUE;
    }
    __finally
    {
        if (hk != NULL)
        {
            RegCloseKey(hk);
        }
    }

    return bResult;
}