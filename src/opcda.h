// opcda.h
#ifndef OPCDA_H
#define OPCDA_H

#include <napi.h>
#include <mutex>
#include <map>
#include <string>
#include <vector>
#include <windows.h>
#include <objbase.h>
#include "opcda.h"   // from include


class OPCDA : public Napi::ObjectWrap<OPCDA> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    OPCDA(const Napi::CallbackInfo& info);
    ~OPCDA();

    // Instance methods
    Napi::Value Connect(const Napi::CallbackInfo& info);
    Napi::Value Disconnect(const Napi::CallbackInfo& info);
    Napi::Value CreateGroup(const Napi::CallbackInfo& info);
    Napi::Value AddItem(const Napi::CallbackInfo& info);
    Napi::Value Subscribe(const Napi::CallbackInfo& info);
    Napi::Value Unsubscribe(const Napi::CallbackInfo& info);
    Napi::Value Read(const Napi::CallbackInfo& info);
    Napi::Value Write(const Napi::CallbackInfo& info);
    Napi::Value Browse(const Napi::CallbackInfo& info);
    

private:
    void Cleanup();  // метод для очистки без использования CallbackInfo
    struct GroupInfo {
        IOPCItemMgt* pItemMgt = nullptr;
        OPCHANDLE hServerGroup = 0;
        DWORD revisedUpdateRate = 0;
        std::map<std::string, OPCHANDLE> itemHandles;  // itemName -> server handle
        std::map<OPCHANDLE, std::string> clientHandles; // client handle -> itemName (добавляем)
        bool subscribed = false;
        DWORD advCookie = 0;
        IConnectionPoint* pConnectionPoint = nullptr;
        IOPCAsyncIO2* pAsyncIO2 = nullptr;
        OPCHANDLE nextClientHandle = 1; // генератор клиентских хендлов
    };

    class DataCallback : public IOPCDataCallback {
    public:
        DataCallback(OPCDA* owner, const std::string& groupName, Napi::ThreadSafeFunction tsfn);
        virtual ~DataCallback();

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
        STDMETHODIMP_(ULONG) AddRef() override;
        STDMETHODIMP_(ULONG) Release() override;

        // IOPCDataCallback
        STDMETHODIMP OnDataChange(DWORD dwTransid, OPCHANDLE hGroup, HRESULT hrMasterquality,
            HRESULT hrMastererror, DWORD dwCount, OPCHANDLE* phClientItems,
            VARIANT* pvValues, WORD* pwQualities, FILETIME* pftTimeStamps,
            HRESULT* pErrors) override;
        STDMETHODIMP OnReadComplete(DWORD, OPCHANDLE, HRESULT, HRESULT, DWORD, OPCHANDLE*, VARIANT*, WORD*, FILETIME*, HRESULT*) override { return S_OK; }
        STDMETHODIMP OnWriteComplete(DWORD, OPCHANDLE, HRESULT, DWORD, OPCHANDLE*, HRESULT*) override { return S_OK; }
        STDMETHODIMP OnCancelComplete(DWORD, OPCHANDLE) override { return S_OK; }

    private:
        OPCDA* owner_;
        std::string groupName_;
        Napi::ThreadSafeFunction tsfn_;
        LONG refCount_;
    };

    static Napi::FunctionReference constructor;  // <-- добавлено

    Napi::ThreadSafeFunction tsfn_;
    std::recursive_mutex mtx_;
    Napi::Env env_;
    std::string clientID_;

    IOPCServer* pServer_;
    std::map<std::string, GroupInfo> groups_;
    bool connected_;

    OPCHANDLE FindItemHandle(const std::string& itemName);
    IOPCSyncIO* GetSyncIOForItem(const std::string& itemName);

    // Async workers
    /*class ConnectWorker : public Napi::AsyncWorker {
    public:
        ConnectWorker(Napi::Env env, OPCDA* op, const std::string& progId);
        void Execute() override;
        void OnOK() override;
    private:
        OPCDA* op_;
        std::string progId_;
        bool success_;
        std::string errorMsg_;
        IOPCServer* pServer_;
    };*/

    class ReadWorker : public Napi::AsyncWorker {
    public:
        ReadWorker(Napi::Env env, OPCDA* op, const std::string& itemName, Napi::Promise::Deferred deferred);
        void Execute() override;
        void OnOK() override;
        void OnError(const Napi::Error& e) override;
    private:
        OPCDA* op_;
        std::string itemName_;
        Napi::Promise::Deferred deferred_;
        VARIANT value_;
        WORD quality_;
        FILETIME timestamp_;
        VARTYPE vt_;
        bool success_;
        std::string errorMsg_;
    };

    class WriteWorker : public Napi::AsyncWorker {
    public:
        WriteWorker(Napi::Env env, OPCDA* op, const std::string& itemName, Napi::Value jsValue, Napi::Promise::Deferred deferred);
        void Execute() override;
        void OnOK() override;
        void OnError(const Napi::Error& e) override;
    private:
        OPCDA* op_;
        std::string itemName_;
        Napi::Value jsValue_;
        Napi::Promise::Deferred deferred_;
        bool success_;
        std::string errorMsg_;
    };

    class BrowseWorker : public Napi::AsyncWorker {
    public:
        BrowseWorker(Napi::Env env, OPCDA* op, const std::string& starting, Napi::Promise::Deferred deferred);
        void Execute() override;
        void OnOK() override;
        void OnError(const Napi::Error& e) override;
    private:
        OPCDA* op_;
        std::string starting_;
        Napi::Promise::Deferred deferred_;       
        std::vector<std::string> names_;
        bool success_;
        std::string errorMsg_;
    };
};

#endif