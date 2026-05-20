#include <napi.h>
#include <mutex>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include "OPCHost.h"
#include <eh.h>

#include "opcda.h"
#include <initguid.h>
#include <comdef.h>
#include <iostream>
#include <iomanip>

Napi::FunctionReference OPCDA::constructor;

// GUID definitions
DEFINE_GUID(IID_IOPCServer, 0x39c13a4d, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCItemMgt, 0x39c13a54, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCSyncIO, 0x39c13a52, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCAsyncIO2, 0x39c13a71, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCDataCallback, 0x39c13a70, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IOPCBrowseServerAddressSpace, 0x39c13a4f, 0x011e, 0x11d0, 0x96, 0x75, 0x00, 0x20, 0xaf, 0xd8, 0xad, 0xb3);
DEFINE_GUID(IID_IConnectionPointContainer, 0xB196B284, 0xBAB4, 0x101A, 0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07);
DEFINE_GUID(IID_IConnectionPoint, 0xB196B286, 0xBAB4, 0x101A, 0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07);
DEFINE_GUID(IID_IOPCItemIO, 0x85C0B427, 0x2893, 0x4cbc, 0xBD, 0x78, 0xE5, 0xFC, 0x51, 0x46, 0xF0, 0x8F);

static std::string VarTypeToString(VARTYPE vt) {
    switch (vt) {
        case VT_EMPTY: return "empty";
        case VT_NULL: return "null";
        case VT_I1: return "char";
        case VT_I2: return "short";
        case VT_I4: return "long";
        case VT_R4: return "float";
        case VT_R8: return "double";
        case VT_BOOL: return "boolean";
        case VT_BSTR: return "string";
        case VT_DATE: return "datetime";
        case VT_UI1: return "byte";
        case VT_UI2: return "ushort";
        case VT_UI4: return "ulong";
        case VT_I8: return "int64";
        case VT_UI8: return "uint64";
        default: return "unknown";
    }
}

static Napi::Value VariantToNapi(Napi::Env env, VARIANT* var) {
    if (!var) return env.Null();
    switch (var->vt) {
        case VT_I2: return Napi::Number::New(env, var->iVal);
        case VT_I4: return Napi::Number::New(env, var->lVal);
        case VT_R4: return Napi::Number::New(env, var->fltVal);
        case VT_R8: return Napi::Number::New(env, var->dblVal);
        case VT_BOOL: return Napi::Boolean::New(env, var->boolVal != 0);
        case VT_BSTR: {
            char buffer[256];
            WideCharToMultiByte(CP_UTF8, 0, var->bstrVal, -1, buffer, sizeof(buffer), NULL, NULL);
            return Napi::String::New(env, buffer);
        }
        case VT_UI1: return Napi::Number::New(env, (unsigned int)var->bVal);
        case VT_UI2: return Napi::Number::New(env, var->uiVal);
        case VT_UI4: return Napi::Number::New(env, var->ulVal);
        case VT_I1: return Napi::Number::New(env, (int)var->cVal);
        case VT_I8: return Napi::Number::New(env, (double)var->llVal);
        case VT_UI8: return Napi::Number::New(env, (double)var->ullVal);
        case VT_DATE: {
            _variant_t dt(*var);
            dt.ChangeType(VT_BSTR);
            return Napi::String::New(env, (const char*)_bstr_t(dt));
        }
        default: return env.Null();
    }
}

// ----------------------------------------------------------------------------
// DataCallback implementation
// ----------------------------------------------------------------------------
OPCDA::DataCallback::DataCallback(OPCDA* owner, const std::string& groupName, Napi::ThreadSafeFunction tsfn)
    : owner_(owner), groupName_(groupName), tsfn_(tsfn), refCount_(1) {}

OPCDA::DataCallback::~DataCallback() {}

STDMETHODIMP OPCDA::DataCallback::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IOPCDataCallback) {
        *ppv = static_cast<IOPCDataCallback*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) OPCDA::DataCallback::AddRef() {
    return InterlockedIncrement(&refCount_);
}

STDMETHODIMP_(ULONG) OPCDA::DataCallback::Release() {
    LONG ref = InterlockedDecrement(&refCount_);
    if (ref == 0) delete this;
    return ref;
}

static int64_t FileTimeToUnixMs(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    const int64_t EPOCH_DIFF = 116444736000000000LL;
    int64_t ns100 = static_cast<int64_t>(uli.QuadPart) - EPOCH_DIFF;
    return ns100 / 10000;
}

STDMETHODIMP OPCDA::DataCallback::OnDataChange(DWORD dwTransid, OPCHANDLE hGroup,
    HRESULT hrMasterquality, HRESULT hrMastererror, DWORD dwCount,
    OPCHANDLE* phClientItems, VARIANT* pvValues, WORD* pwQualities,
    FILETIME* pftTimeStamps, HRESULT* pErrors) {
    
    struct ItemData {
        std::string name;
        VARIANT value;
        WORD quality;
        int64_t timestamp;
    };
    std::vector<ItemData> items;
    items.reserve(dwCount);
    
    {
        std::lock_guard<std::recursive_mutex> lock(owner_->mtx_);
        auto git = owner_->groups_.find(groupName_);
        if (git != owner_->groups_.end()) {
            for (DWORD i = 0; i < dwCount; ++i) {
                OPCHANDLE hClient = phClientItems[i];
                auto it = git->second.clientHandles.find(hClient);
                if (it != git->second.clientHandles.end()) {
                    VARIANT varCopy;
                    VariantInit(&varCopy);
                    VariantCopy(&varCopy, &pvValues[i]);
                    items.push_back({it->second, varCopy, pwQualities[i],
                                     FileTimeToUnixMs(pftTimeStamps[i])});
                }
            }
        }
    }
    
    tsfn_.NonBlockingCall([items, groupName = groupName_](Napi::Env env, Napi::Function cb) {
        Napi::HandleScope scope(env);
        try {
            Napi::Object event = Napi::Object::New(env);
            event.Set("type", Napi::String::New(env, "dataChange"));
            event.Set("group", Napi::String::New(env, groupName));
            Napi::Object data = Napi::Object::New(env);
            for (const auto& item : items) {
                Napi::Object valObj = Napi::Object::New(env);
                valObj.Set("value", VariantToNapi(env, const_cast<VARIANT*>(&item.value)));
                valObj.Set("quality", Napi::Number::New(env, item.quality));
                valObj.Set("timestamp", Napi::Number::New(env, (double)item.timestamp));
                data.Set(item.name, valObj);
                VariantClear(const_cast<VARIANT*>(&item.value));
            }
            event.Set("data", data);
            cb.Call({event});
        } catch (const std::exception& e) {
            fprintf(stderr, "Exception in dataChange callback: %s\n", e.what());
        }
    });
    return S_OK;
}

// ----------------------------------------------------------------------------
// OPCDA methods
// ----------------------------------------------------------------------------
Napi::Object OPCDA::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "OPCDA", {
        InstanceMethod("connect", &OPCDA::Connect),
        InstanceMethod("disconnect", &OPCDA::Disconnect),
        InstanceMethod("createGroup", &OPCDA::CreateGroup),
        InstanceMethod("addItem", &OPCDA::AddItem),
        InstanceMethod("subscribe", &OPCDA::Subscribe),
        InstanceMethod("unsubscribe", &OPCDA::Unsubscribe),
        InstanceMethod("read", &OPCDA::Read),
        InstanceMethod("write", &OPCDA::Write),
        InstanceMethod("browse", &OPCDA::Browse),
    });
    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();
    exports.Set("OPCDA", func);
    return exports;
}

OPCDA::OPCDA(const Napi::CallbackInfo& info) : Napi::ObjectWrap<OPCDA>(info), env_(info.Env()) {
    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(info.Env(), "Expected a callback function").ThrowAsJavaScriptException();
        return;
    }
    Napi::Function cb = info[0].As<Napi::Function>();
    tsfn_ = Napi::ThreadSafeFunction::New(info.Env(), cb, "OPCDATSFN", 0, 1, [](Napi::Env) {});
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        Napi::Error::New(info.Env(), "Failed to initialize COM").ThrowAsJavaScriptException();
        return;
    }
    pServer_ = nullptr;
    connected_ = false;
    clientID_ = "opcda_client";
}

OPCDA::~OPCDA() {
    Cleanup();
    CoUninitialize();
    tsfn_.Release();
}

void OPCDA::Cleanup() {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (pServer_) {
        for (auto& g : groups_) {
            if (g.second.pAsyncIO2) {
                if (g.second.pConnectionPoint) {
                    g.second.pConnectionPoint->Unadvise(g.second.advCookie);
                    g.second.pConnectionPoint->Release();
                }
                g.second.pAsyncIO2->Release();
            }
            if (g.second.pItemMgt) g.second.pItemMgt->Release();
        }
        groups_.clear();
        pServer_->Release();
        pServer_ = nullptr;
    }
    connected_ = false;
}

// ----------------------------------------------------------------------------
// Вспомогательные функции
// ----------------------------------------------------------------------------
IOPCSyncIO* OPCDA::GetSyncIOForItem(const std::string& itemName) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    for (auto& g : groups_) {
        if (g.second.itemHandles.count(itemName)) {
            IOPCSyncIO* pSyncIO = nullptr;
            HRESULT hr = g.second.pItemMgt->QueryInterface(IID_IOPCSyncIO, (void**)&pSyncIO);
            if (SUCCEEDED(hr) && pSyncIO) return pSyncIO;
            break;
        }
    }
    return nullptr;
}

OPCHANDLE OPCDA::FindItemHandle(const std::string& itemName) {
    for (const auto& g : groups_) {
        auto it = g.second.itemHandles.find(itemName);
        if (it != g.second.itemHandles.end())
            return it->second;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// ReadWorker
// ----------------------------------------------------------------------------
OPCDA::ReadWorker::ReadWorker(Napi::Env env, OPCDA* op, const std::string& itemName, Napi::Promise::Deferred deferred)
    : Napi::AsyncWorker(env), op_(op), itemName_(itemName), deferred_(deferred), success_(false) {
    VariantInit(&value_);
}

void OPCDA::ReadWorker::Execute() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        errorMsg_ = "CoInitializeEx failed";
        return;
    }

    OPCHANDLE hServer = op_->FindItemHandle(itemName_);
    if (!hServer) {
        errorMsg_ = "Item not found";
        CoUninitialize();
        return;
    }

    IOPCSyncIO* pSyncIO = op_->GetSyncIOForItem(itemName_);
    if (!pSyncIO) {
        errorMsg_ = "Cannot get IOPCSyncIO";
        CoUninitialize();
        return;
    }

    OPCITEMSTATE* pState = nullptr;
    HRESULT* pErrors = nullptr;
    hr = pSyncIO->Read(OPC_DS_CACHE, 1, &hServer, &pState, &pErrors);
    if (SUCCEEDED(hr) && pState && pErrors && pErrors[0] == S_OK) {
        VariantCopy(&value_, &pState->vDataValue);
        quality_ = pState->wQuality;
        timestamp_ = pState->ftTimeStamp;
        vt_ = pState->vDataValue.vt;
        success_ = true;
        VariantClear(&pState->vDataValue);
        CoTaskMemFree(pState);
    } else {
        char buf[128];
        sprintf_s(buf, "Read failed with HRESULT 0x%08lx", hr);
        errorMsg_ = buf;
    }
    if (pErrors) CoTaskMemFree(pErrors);
    pSyncIO->Release();
    CoUninitialize();
}

void OPCDA::ReadWorker::OnOK() {
    Napi::Env env = Env();
    Napi::HandleScope scope(env);
    try {
        if (success_) {
            Napi::Object result = Napi::Object::New(env);
            result.Set("value", VariantToNapi(env, &value_));
            result.Set("quality", Napi::Number::New(env, quality_));
            result.Set("timestamp", Napi::Number::New(env, (double)FileTimeToUnixMs(timestamp_)));
            result.Set("type", Napi::String::New(env, VarTypeToString(vt_)));
            deferred_.Resolve(result);
        } else {
            deferred_.Reject(Napi::Error::New(env, errorMsg_).Value());
        }
    } catch (const std::exception& e) {
        deferred_.Reject(Napi::Error::New(env, std::string("Exception: ") + e.what()).Value());
    }
    VariantClear(&value_);
}

void OPCDA::ReadWorker::OnError(const Napi::Error& e) {
    Napi::Env env = Env();
    Napi::HandleScope scope(env);
    deferred_.Reject(e.Value());
}


// ----------------------------------------------------------------------------
// WriteWorker
// ----------------------------------------------------------------------------
static HRESULT NapiValueToVariant(Napi::Value value, VARIANT& var) {
    VariantInit(&var);
    if (value.IsUndefined()) { var.vt = VT_EMPTY; return S_OK; }
    if (value.IsNull()) { var.vt = VT_NULL; return S_OK; }
    if (value.IsBoolean()) { var.vt = VT_BOOL; var.boolVal = value.As<Napi::Boolean>() ? VARIANT_TRUE : VARIANT_FALSE; return S_OK; }
    if (value.IsNumber()) {
        double num = value.As<Napi::Number>().DoubleValue();
        double intpart;
        if (std::modf(num, &intpart) == 0.0 && num >= -2147483648.0 && num <= 2147483647.0) {
            var.vt = VT_I4;
            var.lVal = (long)num;
        } else {
            var.vt = VT_R8;
            var.dblVal = num;
        }
        return S_OK;
    }
    if (value.IsString()) {
        std::string str = value.As<Napi::String>().Utf8Value();
        // Пытаемся распарсить как число
        char* end;
        double num = strtod(str.c_str(), &end);
        if (end != str.c_str() && *end == '\0') {
            // Строка представляет собой число
            double intpart;
            if (std::modf(num, &intpart) == 0.0 && num >= -2147483648.0 && num <= 2147483647.0) {
                var.vt = VT_I4;
                var.lVal = (long)num;
            } else {
                var.vt = VT_R8;
                var.dblVal = num;
            }
            return S_OK;
        }
        // Пытаемся распарсить как время "HH:MM:SS"
        int h, m, s;
        if (sscanf(str.c_str(), "%d:%d:%d", &h, &m, &s) == 3 && h >= 0 && h <= 23 && m >= 0 && m <= 59 && s >= 0 && s <= 59) {
            double secondsSinceMidnight = h * 3600.0 + m * 60.0 + s;
            double dateValue = secondsSinceMidnight / 86400.0;
            var.vt = VT_DATE;
            var.date = dateValue;
            return S_OK;
        }
        // Иначе обычная строка
        var.vt = VT_BSTR;
        var.bstrVal = SysAllocStringByteLen(str.c_str(), (UINT)str.length());
        return var.bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    if (value.IsDate()) {
        double jsTimestamp = value.As<Napi::Date>().ValueOf();
        double oleDate = jsTimestamp / 86400000.0 + 25569.0;
        var.vt = VT_DATE;
        var.date = oleDate;
        return S_OK;
    }
    var.vt = VT_EMPTY;
    return S_FALSE;
}

OPCDA::WriteWorker::WriteWorker(Napi::Env env, OPCDA* op, const std::string& itemName, Napi::Value jsValue, Napi::Promise::Deferred deferred)
    : Napi::AsyncWorker(env), op_(op), itemName_(itemName), jsValue_(jsValue), deferred_(deferred), success_(false) {}

void OPCDA::WriteWorker::Execute() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        errorMsg_ = "CoInitializeEx failed";
        fprintf(stderr, "WriteWorker: CoInitializeEx failed, hr=0x%08lx\n", hr);
        return;
    }

    OPCHANDLE hServer = op_->FindItemHandle(itemName_);
    if (!hServer) {
        errorMsg_ = "Item not found";
        fprintf(stderr, "WriteWorker: Item '%s' not found\n", itemName_.c_str());
        CoUninitialize();
        return;
    }

    IOPCSyncIO* pSyncIO = op_->GetSyncIOForItem(itemName_);
    if (!pSyncIO) {
        errorMsg_ = "Cannot get IOPCSyncIO for item";
        fprintf(stderr, "WriteWorker: Cannot get IOPCSyncIO for '%s'\n", itemName_.c_str());
        CoUninitialize();
        return;
    }

    VARIANT var;
    hr = NapiValueToVariant(jsValue_, var);
    if (FAILED(hr)) {
        errorMsg_ = "Failed to convert value to VARIANT";
        fprintf(stderr, "WriteWorker: NapiValueToVariant failed, hr=0x%08lx\n", hr);
        pSyncIO->Release();
        CoUninitialize();
        return;
    }
    if (var.vt == VT_EMPTY || var.vt == VT_NULL) {
        fprintf(stderr, "WriteWorker: Empty or null value for '%s'\n", itemName_.c_str());
        pSyncIO->Release();
        CoUninitialize();
        success_ = true;
        return;
    }

    HRESULT* pErrors = nullptr;
    hr = pSyncIO->Write(1, &hServer, &var, &pErrors);
    VariantClear(&var);
    if (SUCCEEDED(hr) && pErrors && pErrors[0] == S_OK) {
        success_ = true;
        fprintf(stderr, "WriteWorker: Write succeeded for '%s'\n", itemName_.c_str());
    } else {
        char buf[256];
        sprintf_s(buf, "Write failed with HRESULT 0x%08lx, pErrors=0x%08lx", hr, pErrors ? pErrors[0] : 0);
        errorMsg_ = buf;
        fprintf(stderr, "WriteWorker: %s\n", buf);
    }
    if (pErrors) CoTaskMemFree(pErrors);
    pSyncIO->Release();
    CoUninitialize();
}

void OPCDA::WriteWorker::OnOK() {
    Napi::Env env = Env();
    Napi::HandleScope scope(env);
    try {
        if (success_) {
            deferred_.Resolve(env.Undefined());
        } else {
            deferred_.Reject(Napi::Error::New(env, errorMsg_).Value());
        }
    } catch (const std::exception& e) {
        deferred_.Reject(Napi::Error::New(env, std::string("Exception in OnOK: ") + e.what()).Value());
    } catch (...) {
        deferred_.Reject(Napi::Error::New(env, "Unknown exception in OnOK").Value());
    }
}

void OPCDA::WriteWorker::OnError(const Napi::Error& e) {
    Napi::Env env = Env();
    Napi::HandleScope scope(env);
    deferred_.Reject(e.Value());
}

// ----------------------------------------------------------------------------
// BrowseWorker
// ----------------------------------------------------------------------------
OPCDA::BrowseWorker::BrowseWorker(Napi::Env env, OPCDA* op, const std::string& starting, Napi::Promise::Deferred deferred)
    : Napi::AsyncWorker(env), op_(op), starting_(starting), deferred_(deferred), success_(false) {}

void OPCDA::BrowseWorker::Execute() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { errorMsg_ = "CoInitializeEx failed"; return; }
    if (!op_->connected_ || !op_->pServer_) { errorMsg_ = "Not connected"; CoUninitialize(); return; }

    IOPCBrowseServerAddressSpace* pBrowse = nullptr;
    hr = op_->pServer_->QueryInterface(IID_IOPCBrowseServerAddressSpace, (void**)&pBrowse);
    if (FAILED(hr)) { errorMsg_ = "Browse interface not supported"; CoUninitialize(); return; }
    IEnumString* pEnum = nullptr;
    hr = pBrowse->BrowseOPCItemIDs(OPC_FLAT, L"", VT_EMPTY, 0, &pEnum);
    if (FAILED(hr) || !pEnum) { errorMsg_ = "BrowseOPCItemIDs failed"; pBrowse->Release(); CoUninitialize(); return; }

    LPOLESTR szItem;
    ULONG fetched;
    while (pEnum->Next(1, &szItem, &fetched) == S_OK) {
        char buffer[256];
        WideCharToMultiByte(CP_UTF8, 0, szItem, -1, buffer, sizeof(buffer), NULL, NULL);
        names_.push_back(buffer);
        CoTaskMemFree(szItem);
    }
    pEnum->Release();
    pBrowse->Release();
    success_ = true;
    CoUninitialize();
}

void OPCDA::BrowseWorker::OnOK() {
    Napi::Env env = Env();
    Napi::HandleScope scope(env);
    if (success_) {
        Napi::Array arr = Napi::Array::New(env, names_.size());
        for (size_t i = 0; i < names_.size(); ++i) {
            Napi::Object obj = Napi::Object::New(env);
            obj.Set("name", Napi::String::New(env, names_[i]));
            obj.Set("type", Napi::String::New(env, "empty"));
            arr.Set(i, obj);
        }
        deferred_.Resolve(arr);
    } else {
        deferred_.Reject(Napi::Error::New(env, errorMsg_).Value());
    }
}

void OPCDA::BrowseWorker::OnError(const Napi::Error& e) {
    deferred_.Reject(e.Value());
}

// ----------------------------------------------------------------------------
// Public instance methods
// ----------------------------------------------------------------------------
Napi::Value OPCDA::Connect(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
        Napi::TypeError::New(env, "host, progId/clsid expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string host = info[0].As<Napi::String>().Utf8Value(); // не используется
    std::string idStr = info[1].As<Napi::String>().Utf8Value();

    bool success = false;
    std::string errorMsg;
    IOPCServer* pServer = nullptr;

    CLSID clsid;
    HRESULT hr = S_OK;
    // Проверяем, является ли строка CLSID в формате {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
    if (idStr.size() >= 38 && idStr.front() == '{' && idStr.back() == '}') {
        hr = CLSIDFromString(std::wstring(idStr.begin(), idStr.end()).c_str(), &clsid);
        if (FAILED(hr)) errorMsg = "Invalid CLSID string";
    } else {
        hr = CLSIDFromProgID(std::wstring(idStr.begin(), idStr.end()).c_str(), &clsid);
        if (FAILED(hr)) errorMsg = "CLSIDFromProgID failed";
    }
    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER, IID_IOPCServer, (void**)&pServer);
        if (FAILED(hr) || !pServer) {
            errorMsg = "CoCreateInstance failed";
        } else {
            success = true;
        }
    }

    if (success) {
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        pServer_ = pServer;
        connected_ = true;
    }

    tsfn_.NonBlockingCall([success, errorMsg](Napi::Env env, Napi::Function cb) {
        Napi::HandleScope scope(env);
        try {
            Napi::Object data = Napi::Object::New(env);
            data.Set("success", Napi::Boolean::New(env, success));
            if (!success) data.Set("error", Napi::String::New(env, errorMsg));
            Napi::Object event = Napi::Object::New(env);
            event.Set("type", Napi::String::New(env, "connect"));
            event.Set("data", data);
            cb.Call({event});
        } catch (const std::exception& e) {
            fprintf(stderr, "Exception in connect callback: %s\n", e.what());
        }
    });

    return env.Undefined();
}

Napi::Value OPCDA::Disconnect(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Cleanup();
    tsfn_.NonBlockingCall([](Napi::Env env, Napi::Function cb) {
        Napi::Object event = Napi::Object::New(env);
        event.Set("type", Napi::String::New(env, "disconnect"));
        event.Set("data", Napi::Object::New(env));
        cb.Call({event});
    });
    return env.Undefined();
}

Napi::Value OPCDA::CreateGroup(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3) {
        Napi::TypeError::New(env, "groupName, rate, deadband expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string groupName = info[0].As<Napi::String>().Utf8Value();
    int rate = info[1].As<Napi::Number>().Int32Value();
    double deadband = info[2].As<Napi::Number>().DoubleValue();

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (!connected_ || !pServer_) {
        Napi::Error::New(env, "Not connected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (groups_.count(groupName)) {
        Napi::Error::New(env, "Group already exists").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    IOPCItemMgt* pItemMgt = nullptr;
    OPCHANDLE hServerGroup;
    DWORD revisedRate;
    HRESULT hr = pServer_->AddGroup(
        std::wstring(groupName.begin(), groupName.end()).c_str(),
        TRUE, (DWORD)rate, 0, NULL, NULL, 0,
        &hServerGroup, &revisedRate, IID_IOPCItemMgt, (IUnknown**)&pItemMgt);
    if (FAILED(hr) || !pItemMgt) {
        Napi::Error::New(env, "Failed to create group").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    GroupInfo gi;
    gi.pItemMgt = pItemMgt;
    gi.hServerGroup = hServerGroup;
    gi.revisedUpdateRate = revisedRate;
    gi.subscribed = false;
    groups_[groupName] = gi;
    return env.Undefined();
}

Napi::Value OPCDA::AddItem(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "groupName, itemName expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string groupName = info[0].As<Napi::String>().Utf8Value();
    std::string itemName = info[1].As<Napi::String>().Utf8Value();

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto git = groups_.find(groupName);
    if (git == groups_.end()) {
        Napi::Error::New(env, "Group not found").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (git->second.itemHandles.count(itemName)) {
        Napi::Error::New(env, "Item already exists").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    OPCITEMDEF def = {0};
    std::wstring wItemName(itemName.begin(), itemName.end());
    def.szItemID = const_cast<LPWSTR>(wItemName.c_str());
    def.bActive = TRUE;
    def.hClient = git->second.nextClientHandle++;
    OPCITEMRESULT* pResults = nullptr;
    HRESULT* pErrors = nullptr;
    HRESULT hr = git->second.pItemMgt->AddItems(1, &def, &pResults, &pErrors);
    if (FAILED(hr) || !pResults) {
        Napi::Error::New(env, "Failed to add item").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    git->second.itemHandles[itemName] = pResults->hServer;
    git->second.clientHandles[def.hClient] = itemName;
    CoTaskMemFree(pResults);
    CoTaskMemFree(pErrors);
    return env.Undefined();
}

Napi::Value OPCDA::Subscribe(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "groupName expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string groupName = info[0].As<Napi::String>().Utf8Value();

    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto git = groups_.find(groupName);
    if (git == groups_.end()) {
        Napi::Error::New(env, "Group not found").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (git->second.subscribed) return env.Undefined();
    
    IOPCAsyncIO2* pAsyncIO2 = nullptr;
    HRESULT hr = git->second.pItemMgt->QueryInterface(IID_IOPCAsyncIO2, (void**)&pAsyncIO2);
    if (FAILED(hr)) {
        Napi::Error::New(env, "AsyncIO2 not supported").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    IConnectionPointContainer* pCPC = nullptr;
    hr = pAsyncIO2->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC);
    if (FAILED(hr)) {
        pAsyncIO2->Release();
        Napi::Error::New(env, "Connection point not supported").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    IConnectionPoint* pCP = nullptr;
    hr = pCPC->FindConnectionPoint(IID_IOPCDataCallback, &pCP);
    pCPC->Release();
    if (FAILED(hr)) {
        pAsyncIO2->Release();
        Napi::Error::New(env, "Cannot find connection point").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    DataCallback* pCallback = new DataCallback(this, groupName, tsfn_);
    DWORD dwCookie;
    hr = pCP->Advise(pCallback, &dwCookie);
    if (FAILED(hr)) {
        pCP->Release();
        pAsyncIO2->Release();
        delete pCallback;
        Napi::Error::New(env, "Advise failed").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    git->second.pAsyncIO2 = pAsyncIO2;
    git->second.pConnectionPoint = pCP;
    git->second.advCookie = dwCookie;
    git->second.subscribed = true;
    pAsyncIO2->SetEnable(TRUE);
    return env.Undefined();
}

Napi::Value OPCDA::Unsubscribe(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "groupName expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string groupName = info[0].As<Napi::String>().Utf8Value();
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto git = groups_.find(groupName);
    if (git == groups_.end() || !git->second.subscribed) return env.Undefined();
    git->second.pAsyncIO2->SetEnable(FALSE);
    git->second.pConnectionPoint->Unadvise(git->second.advCookie);
    git->second.pConnectionPoint->Release();
    git->second.pAsyncIO2->Release();
    git->second.subscribed = false;
    return env.Undefined();
}

Napi::Value OPCDA::Read(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "itemName expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string itemName = info[0].As<Napi::String>().Utf8Value();
    auto deferred = Napi::Promise::Deferred::New(env);
    auto* worker = new ReadWorker(env, this, itemName, deferred);
    worker->Queue();
    return deferred.Promise();
}

Napi::Value OPCDA::Write(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsString()) {
        Napi::TypeError::New(env, "itemName, value expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string itemName = info[0].As<Napi::String>().Utf8Value();
    Napi::Value jsValue = info[1];
    auto deferred = Napi::Promise::Deferred::New(env);
    auto* worker = new WriteWorker(env, this, itemName, jsValue, deferred);
    worker->Queue();
    return deferred.Promise();
}

Napi::Value OPCDA::Browse(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string starting = info.Length() > 0 ? info[0].As<Napi::String>().Utf8Value() : "";
    auto deferred = Napi::Promise::Deferred::New(env);
    auto* worker = new BrowseWorker(env, this, starting, deferred);
    worker->Queue();
    return deferred.Promise();
}

// Module init
Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    return OPCDA::Init(env, exports);
}
NODE_API_MODULE(opcda, InitAll)