#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <iostream>
#include <conio.h>
#include <string>
#include <iomanip>
#include <windows.h>
#include <avaldata.transflyer.h>
using namespace avaldata::tf;

// 10MB sheard memory
#define SHARED_MEM_SIZE (10 * 1024 * 1024)
LPVOID g_pSharedMemory = NULL;
HANDLE g_hMapFile = NULL;

// mutex and event for synchronization
HANDLE g_hMutex = NULL;
HANDLE g_hDataReadyEvent = NULL;

void CheckError(uint32_t line, ITraBase* pNode);

// Frame End Callback: pImage->Data() 
void __stdcall CallbackFrameEnd(ITraAcqNode* pAcqNode, ITraImage* pImage, void* pContext)
{
    size_t imageSize = pImage->DataSize();
    if (pImage->Data() != NULL)
    {
        if (imageSize + sizeof(uint32_t) <= SHARED_MEM_SIZE)
        {
            // Using Mutex
            DWORD dwWaitResult = WaitForSingleObject(g_hMutex, INFINITE);
            if (dwWaitResult == WAIT_OBJECT_0)
            {
                // 첫 4바이트에 이미지 크기를 기록하고 이어서 이미지 데이터를 기록
                *((uint32_t*)g_pSharedMemory) = static_cast<uint32_t>(imageSize);
                memcpy((char*)g_pSharedMemory + sizeof(uint32_t), pImage->Data(), imageSize);
				// Release Mutex
                ReleaseMutex(g_hMutex);

                // Event Signal
                SetEvent(g_hDataReadyEvent);

                //std::cout << "Image copied to shared memory (" << imageSize << " bytes)" << std::endl;
            }
            else
            {
                std::cerr << "Failed to acquire mutex." << std::endl;
            }
        }
        else
        {
            std::cerr << "Image size too big for shared memory" << std::endl;
        }
    }
}

int _tmain(int argc, _TCHAR* argv[])
{
    std::string input_string;

    std::cout << "------------------------------------------------------------ " << std::endl;
    std::cout << " This code continuously grabs image data and writes it to shared memory " << std::endl;
    std::cout << " with synchronization using a mutex and an event." << std::endl;
    std::cout << " Press 'ESC' to stop grabbing." << std::endl;
    std::cout << "------------------------------------------------------------ " << std::endl;

    ITraFactory* factory = NULL;
    ITraAcqGrabber* grabber = NULL;
    ITraAcqCamera* camera = NULL;
    char grabberName[MaxFeatureNameSize] = { 0 };
    char cameraName[MaxFeatureNameSize] = { 0 };
    uint32_t width = 0, height = 0;
    char pixelFormat[MaxNameSize] = { 0 };
    size_t pixelFormatSize = MaxNameSize;

    // 공유 메모리 생성 (10MB, Global 네임스페이스 사용)
    g_hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SHARED_MEM_SIZE, TEXT("Global\\MySharedImageMapping"));
    if (g_hMapFile == NULL)
    {
        std::cerr << "Could not create file mapping object: " << GetLastError() << std::endl;
        return -1;
    }
    g_pSharedMemory = MapViewOfFile(g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_MEM_SIZE);
    if (g_pSharedMemory == NULL)
    {
        std::cerr << "Could not map view of file: " << GetLastError() << std::endl;
        CloseHandle(g_hMapFile);
        return -1;
    }

    // 뮤텍스 생성 (공유 메모리 접근 동기화용)
    g_hMutex = CreateMutex(NULL, FALSE, TEXT("Global\\MySharedImageMutex"));
    if (g_hMutex == NULL)
    {
        std::cerr << "Could not create mutex: " << GetLastError() << std::endl;
        return -1;
    }

    // auto reset event
    g_hDataReadyEvent = CreateEvent(NULL, FALSE, FALSE, TEXT("Global\\MyDataReadyEvent"));
    if (g_hDataReadyEvent == NULL)
    {
        std::cerr << "Could not create event: " << GetLastError() << std::endl;
        return -1;
    }

	// TransFlyer Initialize
    factory = avaldata::tf::TraCreateFactory();
    if (factory == NULL)
    {
        std::cout << "TraCreateFactory Failed" << std::endl;
        return -1;
    }
    if (factory->UpdateInterfaceList(5000) == false)
    {
        std::cout << "factory->UpdateInterfaceList Failed" << std::endl;
        CheckError(__LINE__, factory);
        goto FreeHandles;
    }
    if (factory->GetFirstGrabber(&grabber) == false || grabber == NULL)
    {
        std::cout << "factory->GetFirstGrabber Failed" << std::endl;
        CheckError(__LINE__, factory);
        goto FreeHandles;
    }
    if (grabber->Open() == false)
    {
        std::cout << "grabber->Open Failed" << std::endl;
        CheckError(__LINE__, grabber);
        goto FreeHandles;
    }
    if (grabber->GetFirstCamera(&camera) == false || camera == NULL)
    {
        std::cout << "grabber->GetFirstCamera Failed" << std::endl;
        CheckError(__LINE__, grabber);
        goto FreeHandles;
    }
    if (camera->Open() == false)
    {
        std::cout << "camera->Open Failed" << std::endl;
        CheckError(__LINE__, grabber);
        goto FreeHandles;
    }

    
    // Grabber/Camera 정보 획득
    if (grabber->GetModel(grabberName, MaxFeatureNameSize) == false)
        std::cout << "grabber->GetModel Failed" << std::endl;
    if (camera->GetModel(cameraName, MaxFeatureNameSize) == false)
        std::cout << "camera->GetModel Failed" << std::endl;
    if (camera->GetValue("PixelFormat", pixelFormat, &pixelFormatSize) == false)
        std::cout << "camera->GetValue(PixelFormat) Failed" << std::endl;
    if (camera->GetValue("Width", &width) == false)
        std::cout << "camera->GetValue(Width) Failed" << std::endl;
    if (camera->GetValue("Height", &height) == false)
        std::cout << "camera->GetValue(Height) Failed" << std::endl;

    std::cout << cameraName << " by " << grabberName
        << " [w" << width << " h" << height << " " << pixelFormat << "]" << std::endl;

    // Frame End Callback 등록 (이미지 데이터를 공유 메모리에 기록)
    if (grabber->FrameEndHandler((TraFrameCallback)&CallbackFrameEnd, NULL) == false)
    {
        std::cout << "Register FrameEndHandler failed" << std::endl;
        CheckError(__LINE__, grabber);
        goto FreeHandles;
    }

    // Grab 준비 (10개의 링 버퍼 할당)
    if (grabber->GrabPrepared(10) == false)
    {
        std::cout << "grabber->GrabPrepared Failed" << std::endl;
        CheckError(__LINE__, grabber);
        goto FreeHandles;
    }

    // 무한으로 Grab 시작 (NumberOfImagesInfinity)
    if (grabber->Grab(NumberOfImagesInfinity, AsynchronousFrist, OldestFirstOverwrite, (size_t)TimeoutDefault) == false)
    {
        std::cout << "grabber->Grab Failed" << std::endl;
        CheckError(__LINE__, grabber);
        goto FreeHandles;
    }

    std::cout << "Grabbing... Press ESC to stop." << std::endl;

    while (true)
    {
        if (std::getline(std::cin, input_string))
        {
            if (grabber->GrabStop(AsynchronousAbort, TimeoutDefault) == false)
            {
                
                std::cout << "grabber->GrabStop Failed" << std::endl;
                CheckError(__LINE__, grabber);
                goto FreeHandles;
            }
            break;
        }
        Sleep(10);
    }

    std::cout << "Grabbing stopped." << std::endl;

FreeHandles:
    // 콜백 해제
    if (grabber)
    {
        grabber->FrameEndHandler(NULL, NULL);
    }
    if (camera) camera->Close();
    if (grabber) grabber->Close();
    if (factory) factory->Destroy();

    // 공유 메모리 및 동기화 객체 해제
    if (g_pSharedMemory)
        UnmapViewOfFile(g_pSharedMemory);
    if (g_hMapFile)
        CloseHandle(g_hMapFile);
    if (g_hMutex)
        CloseHandle(g_hMutex);
    if (g_hDataReadyEvent)
        CloseHandle(g_hDataReadyEvent);

    return 0;
}

void CheckError(uint32_t line, ITraBase* pNode)
{
    StatusCode __code = StatusSuccess;
    char __sStatus[MaxReportNameSize] = { 0 };
    size_t __szStatus = MaxReportNameSize;
    pNode->GetLastError(&__code, __sStatus, &__szStatus);
    std::cerr << "[" << __code << "] " << __sStatus << " on line " << line << std::endl;
}
