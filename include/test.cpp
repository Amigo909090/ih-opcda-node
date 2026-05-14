#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <initguid.h>
#include <comdef.h>
#include "opcda.h"

// GUID интерфейсов (как ранее)
DEFINE_GUID(IID_IOPCServer, 0x39c13a4d, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCItemMgt, 0x39c13a54, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCSyncIO, 0x39c13a52, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCBrowseServerAddressSpace, 0x39c13a4f, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCAsyncIO2, 0x39c13a71, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCDataCallback, 0x39c13a70, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IConnectionPointContainer, 0xB196B284, 0xBAB4, 0x101A, 0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07);
DEFINE_GUID(IID_IConnectionPoint, 0xB196B286, 0xBAB4, 0x101A, 0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07);

// Класс обратного вызова
class DataCallback : public IOPCDataCallback {
public:
    DataCallback(const std::map<OPCHANDLE, std::string>& itemMap) : m_itemMap(itemMap), m_refCount(1) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IOPCDataCallback) {
            *ppv = static_cast<IOPCDataCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refCount); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG ref = InterlockedDecrement(&m_refCount);
        if (ref == 0) delete this;
        return ref;
    }

    STDMETHODIMP OnDataChange(
    DWORD dwTransid,
    OPCHANDLE hGroup,
    HRESULT hrMasterquality,
    HRESULT hrMastererror,
    DWORD dwCount,
    OPCHANDLE* phClientItems,
    VARIANT* pvValues,
    WORD* pwQualities,
    FILETIME* pftTimeStamps,
    HRESULT* pErrors) override
    {
        std::cout << "Data change received: " << dwCount << " items" << std::endl;
        for (DWORD i = 0; i < dwCount; ++i) {
            OPCHANDLE hClient = phClientItems[i];
            auto it = m_itemMap.find(hClient);
            std::string itemName = (it != m_itemMap.end()) ? it->second : "unknown";
            std::cout << "  " << itemName << " = ";

            VARIANT& var = pvValues[i];
            switch (var.vt) {
                case VT_BOOL:   std::cout << (var.boolVal ? "true" : "false"); break;
                case VT_UI1:    std::cout << (unsigned int)var.bVal; break;
                case VT_I1:     std::cout << (int)var.cVal; break;
                case VT_UI2:    std::cout << var.uiVal; break;
                case VT_I2:     std::cout << var.iVal; break;
                case VT_UI4:    std::cout << var.ulVal; break;
                case VT_I4:     std::cout << var.lVal; break;
                case VT_UI8:    std::cout << var.ullVal; break;
                case VT_I8:     std::cout << var.llVal; break;
                case VT_R4:     std::cout << std::fixed << std::setprecision(12) << var.fltVal; break;
                case VT_R8:     std::cout << std::fixed << std::setprecision(12) << var.dblVal; break;
                case VT_BSTR:   if (var.bstrVal) std::wcout << var.bstrVal; else std::cout << "<empty>"; break;
                case VT_DATE: {
                    _variant_t dt(var);
                    dt.ChangeType(VT_BSTR);
                    std::wcout << (BSTR)dt.bstrVal;
                    break;
                }
                case VT_EMPTY:
                case VT_NULL:
                    std::cout << "<empty>";
                    break;
                default:
                    std::cout << "type " << var.vt;
            }
            std::cout << ", quality: " << pwQualities[i] << std::endl;
        }
        return S_OK;
    }

    STDMETHODIMP OnReadComplete(DWORD, OPCHANDLE, HRESULT, HRESULT, DWORD, OPCHANDLE*, VARIANT*, WORD*, FILETIME*, HRESULT*) override { return S_OK; }
    STDMETHODIMP OnWriteComplete(DWORD, OPCHANDLE, HRESULT, DWORD, OPCHANDLE*, HRESULT*) override { return S_OK; }
    STDMETHODIMP OnCancelComplete(DWORD, OPCHANDLE) override { return S_OK; }

private:
    std::map<OPCHANDLE, std::string> m_itemMap;
    LONG m_refCount;
};

// Функция браузинга (без изменений)
void BrowseItems(IOPCServer* pServer) {
    IOPCBrowseServerAddressSpace* pBrowse = nullptr;
    HRESULT hr = pServer->QueryInterface(IID_IOPCBrowseServerAddressSpace, (void**)&pBrowse);
    if (FAILED(hr)) { std::cerr << "Failed to get browse interface" << std::endl; return; }
    OPCNAMESPACETYPE nsType;
    pBrowse->QueryOrganization(&nsType);
    std::cout << "Namespace type: " << (nsType == OPC_NS_HIERARCHIAL ? "Hierarchical" : "Flat") << std::endl;
    IEnumString* pEnum = nullptr;
    hr = pBrowse->BrowseOPCItemIDs(OPC_FLAT, L"", VT_EMPTY, 0, &pEnum);
    if (SUCCEEDED(hr) && pEnum) {
        std::cout << "Browsing items:" << std::endl;
        LPOLESTR szItem;
        ULONG fetched;
        while (pEnum->Next(1, &szItem, &fetched) == S_OK) {
            std::wcout << L"  " << szItem << std::endl;
            CoTaskMemFree(szItem);
        }
        pEnum->Release();
    }
    pBrowse->Release();
}

int main() {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    CLSID clsid;
    HRESULT hr = CLSIDFromProgID(L"opcserversim.Instance.1", &clsid);
    if (FAILED(hr)) { std::cerr << "CLSIDFromProgID failed" << std::endl; CoUninitialize(); return 1; }

    IOPCServer* pServer = nullptr;
    hr = CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER, IID_IOPCServer, (void**)&pServer);
    if (FAILED(hr)) { std::cerr << "CoCreateInstance failed" << std::endl; CoUninitialize(); return 1; }
    std::cout << "Server created" << std::endl;

    BrowseItems(pServer);

    IOPCItemMgt* pItemMgt = nullptr;
    DWORD dwRevisedUpdateRate;
    OPCHANDLE hServerGroup;
    hr = pServer->AddGroup(L"TestGroup", TRUE, 1000, 0, NULL, NULL, 0,
        &hServerGroup, &dwRevisedUpdateRate, IID_IOPCItemMgt, (IUnknown**)&pItemMgt);
    if (FAILED(hr)) { std::cerr << "AddGroup failed" << std::endl; pServer->Release(); CoUninitialize(); return 1; }
    std::cout << "Group created, revised rate: " << dwRevisedUpdateRate << std::endl;

    // Определяем теги с явными типами данных
    struct TagInfo {
        std::string name;
        OPCHANDLE clientHandle;
        VARTYPE vtRequested;
    };
    std::vector<TagInfo> tags = {
        {"BooleanValue", 1, VT_BOOL},
        {"ByteValue", 2, VT_UI1},
        {"DateTimeValue", 3, VT_DATE},
        {"DateValue", 4, VT_DATE},
        {"DoubleValue", 5, VT_R8},
        {"Int64Value", 6, VT_I8},
        {"IntegerValue", 7, VT_I4},
        {"LongWordValue", 8, VT_UI4},
        {"ShortIntValue", 9, VT_I2},
        {"SingleValue", 10, VT_R4},
        {"SmallIntValue", 11, VT_I2},
        {"StringValue", 12, VT_BSTR},
        {"TimeValue", 13, VT_DATE},
        {"WordValue", 14, VT_UI2}
    };

    std::vector<OPCITEMDEF> itemDefs;
    std::map<OPCHANDLE, std::string> itemMap;
    std::map<OPCHANDLE, OPCHANDLE> serverHandles;

    for (const auto& tag : tags) {
        OPCITEMDEF def = {0};
        std::wstring wname(tag.name.begin(), tag.name.end());
        def.szItemID = const_cast<LPWSTR>(wname.c_str());
        def.bActive = TRUE;
        def.hClient = tag.clientHandle;        
        itemDefs.push_back(def);
        itemMap[tag.clientHandle] = tag.name;
    }

    OPCITEMRESULT* pResults = nullptr;
    HRESULT* pErrors = nullptr;
    hr = pItemMgt->AddItems((DWORD)itemDefs.size(), itemDefs.data(), &pResults, &pErrors);
    if (FAILED(hr)) {
        std::cerr << "AddItems failed, hr = 0x" << std::hex << hr << std::endl;
        pItemMgt->Release();
        pServer->Release();
        CoUninitialize();
        return 1;
    }
    std::cout << "All items added" << std::endl;

    for (size_t i = 0; i < tags.size(); ++i)
        serverHandles[tags[i].clientHandle] = pResults[i].hServer;

    // Синхронные операции (чтение StringValue и запись IntegerValue)
    IOPCSyncIO* pSyncIO = nullptr;
    hr = pItemMgt->QueryInterface(IID_IOPCSyncIO, (void**)&pSyncIO);
    if (SUCCEEDED(hr)) {
        // Чтение StringValue
        OPCHANDLE hString = serverHandles[12];
        OPCITEMSTATE* pState = nullptr;
        HRESULT* pReadErrors = nullptr;
        hr = pSyncIO->Read(OPC_DS_CACHE, 1, &hString, &pState, &pReadErrors);
        if (SUCCEEDED(hr)) {
            std::wcout << L"Read StringValue: ";
            if (pState->vDataValue.vt == VT_BSTR && pState->vDataValue.bstrVal)
                std::wcout << pState->vDataValue.bstrVal;
            else if (pState->vDataValue.vt == VT_EMPTY)
                std::wcout << L"<empty>";
            else
                std::wcout << L"<unknown>";
            std::wcout << std::endl;
            VariantClear(&pState->vDataValue);
            CoTaskMemFree(pState);
            CoTaskMemFree(pReadErrors);
        } else {
            std::cerr << "Read failed, hr = 0x" << std::hex << hr << std::endl;
        }

        // Запись в IntegerValue
        OPCHANDLE hInteger = serverHandles[7];
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_I4;
        var.lVal = 12345;
        hr = pSyncIO->Write(1, &hInteger, &var, &pReadErrors);
        if (SUCCEEDED(hr)) {
            std::cout << "Write to IntegerValue: 12345" << std::endl;
            CoTaskMemFree(pReadErrors);
        } else {
            std::cerr << "Write failed, hr = 0x" << std::hex << hr << std::endl;
        }
        VariantClear(&var);
        pSyncIO->Release();
    }

    // Асинхронная подписка
    IOPCAsyncIO2* pAsyncIO2 = nullptr;
    hr = pItemMgt->QueryInterface(IID_IOPCAsyncIO2, (void**)&pAsyncIO2);
    if (SUCCEEDED(hr)) {
        DataCallback* pCallback = new DataCallback(itemMap);
        pCallback->AddRef();
        IConnectionPointContainer* pCPC = nullptr;
        hr = pAsyncIO2->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC);
        if (SUCCEEDED(hr)) {
            IConnectionPoint* pCP = nullptr;
            hr = pCPC->FindConnectionPoint(IID_IOPCDataCallback, &pCP);
            if (SUCCEEDED(hr)) {
                DWORD dwCookie;
                hr = pCP->Advise(pCallback, &dwCookie);
                if (SUCCEEDED(hr)) {
                    std::cout << "Async callback connected, waiting for data changes (30 sec)..." << std::endl;
                    pAsyncIO2->SetEnable(TRUE);
                    Sleep(30000);
                    pAsyncIO2->SetEnable(FALSE);
                    pCP->Unadvise(dwCookie);
                }
                pCP->Release();
            }
            pCPC->Release();
        }
        pCallback->Release();
        pAsyncIO2->Release();
    }

    CoTaskMemFree(pResults);
    CoTaskMemFree(pErrors);
    pItemMgt->Release();
    pServer->Release();
    CoUninitialize();
    return 0;
}