#include "auth.h"
#include <Windows.h>
#include <wininet.h>
#include <MinHook.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

#pragma comment(lib, "wininet.lib")

static AuthCredentials g_credentials;
AuthCredentials& GetAuthCredentials() { return g_credentials; }

static AuthLog g_authLog;
AuthLog& GetAuthLog() { return g_authLog; }

static std::mutex g_logMtx;
void AuthLog::Add(const std::string& s)
{
    std::lock_guard<std::mutex> lk(g_logMtx);
    int idx = count % kMax;
    lines[idx] = s;
    count++;
}
std::string AuthLog::Dump() const
{
    std::lock_guard<std::mutex> lk(g_logMtx);
    int total = (count < kMax) ? count : kMax;
    int start = (count <= kMax) ? 0 : (count % kMax);
    std::string out;
    for (int i = 0; i < total; i++) {
        if (!out.empty()) out += '\n';
        out += lines[(start + i) % kMax];
    }
    return out;
}

// Protobuf helpers
static bool ReadVarint(const uint8_t* buf, size_t len, size_t& pos, uint64_t& out)
{
    out=0; int shift=0;
    while(pos<len){uint8_t b=buf[pos++];out|=(uint64_t)(b&0x7F)<<shift;if(!(b&0x80))return true;shift+=7;if(shift>=64)return false;}
    return false;
}
static std::string ProtoReadString(const uint8_t* buf, size_t len, int fieldNum)
{
    size_t pos=0;
    while(pos<len){uint64_t tag=0;if(!ReadVarint(buf,len,pos,tag))break;
        int fnum=(int)(tag>>3),wt=(int)(tag&7);
        if(wt==2){uint64_t sl=0;if(!ReadVarint(buf,len,pos,sl))break;
            if(pos+sl>len)break;
            if(fnum==fieldNum)return std::string((char*)buf+pos,(size_t)sl);
            pos+=(size_t)sl;
        }else if(wt==0){uint64_t v=0;if(!ReadVarint(buf,len,pos,v))break;}
        else if(wt==1){if(pos+8>len)break;pos+=8;}
        else if(wt==5){if(pos+4>len)break;pos+=4;}
        else break;
    }
    return {};
}
static const uint8_t* SkipEnvelope(const uint8_t* buf, size_t len, size_t& protoLen)
{
    size_t pos=0;uint64_t id=0,sz=0;
    if(!ReadVarint(buf,len,pos,id))return nullptr;
    if(!ReadVarint(buf,len,pos,sz))return nullptr;
    if(pos+sz>len)return nullptr;
    protoLen=(size_t)sz;return buf+pos;
}
static void ParseAndStore(const std::string& email, const std::vector<uint8_t>& body)
{
    if(body.empty()||email.empty())return;
    size_t pl=0;
    const uint8_t* pb=SkipEnvelope(body.data(),body.size(),pl);
    std::string token;
    if(pb) token=ProtoReadString(pb,pl,7);
    else   token=ProtoReadString(body.data(),body.size(),7);
    g_authLog.Add("Token len="+std::to_string(token.size()));
    if(!token.empty()&&!g_credentials.ready){
        g_credentials.email=email;
        g_credentials.platformTicket=token;
        g_credentials.ready=true;
        g_authLog.Add("Credentials ready!");
    }
}

// Per-handle state
struct RequestState {
    bool isLogin=false;
    std::string email;
    std::vector<uint8_t> respBody;  // bytes drained on drain-thread
    bool drainDone=false;           // drain thread finished
    size_t replayPos=0;             // how many bytes game has read from cache
};
static std::mutex g_reqMtx;
static std::unordered_map<HINTERNET,RequestState> g_reqs;

// WinINet function types
using FnSetStatusCallback = INTERNET_STATUS_CALLBACK(WINAPI*)(HINTERNET,INTERNET_STATUS_CALLBACK);
using FnHttpOpenRequestA  = HINTERNET(WINAPI*)(HINTERNET,LPCSTR,LPCSTR,LPCSTR,LPCSTR,LPCSTR*,DWORD,DWORD_PTR);
using FnHttpOpenRequestW  = HINTERNET(WINAPI*)(HINTERNET,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR*,DWORD,DWORD_PTR);
using FnHttpSendRequestA  = BOOL(WINAPI*)(HINTERNET,LPCSTR,DWORD,LPVOID,DWORD);
using FnInternetReadFile  = BOOL(WINAPI*)(HINTERNET,LPVOID,DWORD,LPDWORD);
using FnInternetReadFileEx= BOOL(WINAPI*)(HINTERNET,LPINTERNET_BUFFERS,DWORD,DWORD_PTR);
using FnInternetCloseHandle = BOOL(WINAPI*)(HINTERNET);

static FnSetStatusCallback   g_origSetCb  = nullptr;
static FnHttpOpenRequestA    g_origOpenA  = nullptr;
static FnHttpOpenRequestW    g_origOpenW  = nullptr;
static FnHttpSendRequestA    g_origSendA  = nullptr;
static FnInternetReadFile    g_origRead   = nullptr;
static FnInternetReadFileEx  g_origReadEx = nullptr;
static FnInternetCloseHandle g_origClose  = nullptr;

// Game callbacks per handle
static std::unordered_map<HINTERNET,INTERNET_STATUS_CALLBACK> g_callbacks;

// Drain thread: called AFTER the callback returns, safely reads response bytes
struct DrainArgs { HINTERNET h; };
static DWORD WINAPI DrainThread(LPVOID param)
{
    DrainArgs* args = (DrainArgs*)param;
    HINTERNET h = args->h;
    delete args;

    // Small delay to let the callback return fully before we call InternetReadFile
    Sleep(50);

    std::vector<uint8_t> buf;
    char tmp[8192]; DWORD got=0;
    while(g_origRead(h,tmp,sizeof(tmp),&got)&&got>0)
        buf.insert(buf.end(),(uint8_t*)tmp,(uint8_t*)tmp+got);

    g_authLog.Add("Drain "+std::to_string(buf.size())+"b");

    std::string email;
    {
        std::lock_guard<std::mutex> lk(g_reqMtx);
        auto it=g_reqs.find(h);
        if(it!=g_reqs.end()){
            it->second.respBody=buf;
            it->second.drainDone=true;
            email=it->second.email;
        }
    }
    ParseAndStore(email,buf);
    return 0;
}

// Status callback wrapper
static void CALLBACK OurCallback(HINTERNET h, DWORD_PTR ctx,
    DWORD status, LPVOID info, DWORD infoLen)
{
    // On REQUEST_COMPLETE for a login handle, kick a drain thread
    if(status==INTERNET_STATUS_REQUEST_COMPLETE&&!g_credentials.ready){
        bool isLogin=false;
        {
            std::lock_guard<std::mutex> lk(g_reqMtx);
            auto it=g_reqs.find(h);
            if(it!=g_reqs.end()&&it->second.isLogin&&!it->second.drainDone)
                isLogin=true;
        }
        if(isLogin){
            g_authLog.Add("ReqComplete → drain thread");
            DrainArgs* args=new DrainArgs{h};
            CloseHandle(CreateThread(nullptr,0,DrainThread,args,0,nullptr));
        }
    }

    // Forward to game's real callback
    INTERNET_STATUS_CALLBACK gameCb=nullptr;
    {
        std::lock_guard<std::mutex> lk(g_reqMtx);
        auto it=g_callbacks.find(h);
        if(it!=g_callbacks.end()) gameCb=it->second;
    }
    if(gameCb) gameCb(h,ctx,status,info,infoLen);
}

INTERNET_STATUS_CALLBACK WINAPI HookedSetStatusCallback(
    HINTERNET h, INTERNET_STATUS_CALLBACK cb)
{
    if(!g_credentials.ready&&cb&&cb!=OurCallback){
        std::lock_guard<std::mutex> lk(g_reqMtx);
        g_callbacks[h]=cb;
    }
    return g_origSetCb(h,OurCallback);
}

static void TagHandle(HINTERNET h,const std::string& path)
{
    bool isLogin=path.find("Login")!=std::string::npos||path.find("IndexPB")!=std::string::npos;
    g_authLog.Add(std::string(isLogin?"LOGIN ":"REQ ")+path);
    if(isLogin){
        std::lock_guard<std::mutex> lk(g_reqMtx);
        g_reqs[h].isLogin=true;
        // The game registered its callback before our hook — install our wrapper now
        // on this specific request handle. Save whatever callback is already there.
        INTERNET_STATUS_CALLBACK existing = g_origSetCb(h, OurCallback);
        if(existing && existing != OurCallback)
            g_callbacks[h] = existing;
        g_authLog.Add("Installed cb on login handle");
    }
}

HINTERNET WINAPI HookedOpenA(HINTERNET hC,LPCSTR v,LPCSTR path,
    LPCSTR ver,LPCSTR ref,LPCSTR* types,DWORD fl,DWORD_PTR ctx)
{
    HINTERNET h=g_origOpenA(hC,v,path,ver,ref,types,fl,ctx);
    if(h&&!g_credentials.ready&&path) TagHandle(h,path);
    return h;
}

HINTERNET WINAPI HookedOpenW(HINTERNET hC,LPCWSTR v,LPCWSTR path,
    LPCWSTR ver,LPCWSTR ref,LPCWSTR* types,DWORD fl,DWORD_PTR ctx)
{
    HINTERNET h=g_origOpenW(hC,v,path,ver,ref,types,fl,ctx);
    if(h&&!g_credentials.ready&&path){
        int n=WideCharToMultiByte(CP_UTF8,0,path,-1,nullptr,0,nullptr,nullptr);
        std::string p(n>0?n-1:0,'\0');
        WideCharToMultiByte(CP_UTF8,0,path,-1,&p[0],n,nullptr,nullptr);
        TagHandle(h,p);
    }
    return h;
}

BOOL WINAPI HookedSendA(HINTERNET hReq,LPCSTR hdrs,DWORD hdrsLen,LPVOID body,DWORD bodyLen)
{
    if(!g_credentials.ready&&body&&bodyLen>0){
        std::lock_guard<std::mutex> lk(g_reqMtx);
        auto it=g_reqs.find(hReq);
        if(it!=g_reqs.end()&&it->second.isLogin){
            it->second.email=ProtoReadString((uint8_t*)body,bodyLen,1);
            g_authLog.Add("Send email="+it->second.email+" len="+std::to_string(bodyLen));
        }
    }
    return g_origSendA(hReq,hdrs,hdrsLen,body,bodyLen);
}

// HookedReadFile: passively copy bytes the game reads, then parse when done
BOOL WINAPI HookedReadFile(HINTERNET h,LPVOID buf,DWORD toRead,LPDWORD read)
{
    BOOL ok=g_origRead(h,buf,toRead,read);
    DWORD got=(read&&ok)?*read:0;
    // Log every call so we can see handles and byte counts
    if(!g_credentials.ready)
        g_authLog.Add("RF h="+std::to_string((uintptr_t)h&0xFFFF)+" got="+std::to_string(got));
    if(ok&&got>0&&!g_credentials.ready){
        std::lock_guard<std::mutex> lk(g_reqMtx);
        auto it=g_reqs.find(h);
        if(it!=g_reqs.end()&&it->second.isLogin){
            auto* p=(uint8_t*)buf;
            it->second.respBody.insert(it->second.respBody.end(),p,p+got);
        }
    }
    if(ok&&got==0&&!g_credentials.ready){
        std::string email; std::vector<uint8_t> body;
        {
            std::lock_guard<std::mutex> lk(g_reqMtx);
            auto it=g_reqs.find(h);
            if(it!=g_reqs.end()&&it->second.isLogin&&!it->second.respBody.empty()){
                email=it->second.email; body=it->second.respBody;
            }
        }
        if(!body.empty()) ParseAndStore(email,body);
    }
    return ok;
}

// Hook InternetReadFileEx too — same passive copy approach
BOOL WINAPI HookedReadFileEx(HINTERNET h, LPINTERNET_BUFFERS bufs, DWORD flags, DWORD_PTR ctx)
{
    BOOL ok=g_origReadEx(h,bufs,flags,ctx);
    if(ok&&bufs&&bufs->dwBufferLength>0&&!g_credentials.ready){
        g_authLog.Add("RFEx h="+std::to_string((uintptr_t)h&0xFFFF)+" got="+std::to_string(bufs->dwBufferLength));
        std::lock_guard<std::mutex> lk(g_reqMtx);
        auto it=g_reqs.find(h);
        if(it!=g_reqs.end()&&it->second.isLogin){
            auto* p=(uint8_t*)bufs->lpvBuffer;
            it->second.respBody.insert(it->second.respBody.end(),p,p+bufs->dwBufferLength);
        }
    }
    return ok;
}

BOOL WINAPI HookedCloseHandle(HINTERNET h){
    if(!g_credentials.ready){
        std::string email; std::vector<uint8_t> body;
        {
            std::lock_guard<std::mutex> lk(g_reqMtx);
            auto it=g_reqs.find(h);
            if(it!=g_reqs.end()&&it->second.isLogin&&!it->second.respBody.empty()){
                email=it->second.email; body=it->second.respBody;
            }
            g_reqs.erase(h); g_callbacks.erase(h);
        }
        if(!body.empty()) ParseAndStore(email,body);
    } else {
        std::lock_guard<std::mutex> lk(g_reqMtx);
        g_reqs.erase(h); g_callbacks.erase(h);
    }
    return g_origClose(h);
}

bool InstallAuthHook()
{
    HMODULE hInet=GetModuleHandleW(L"wininet.dll");
    if(!hInet) hInet=LoadLibraryW(L"wininet.dll");
    if(!hInet){g_authLog.Add("wininet.dll not found!");return false;}

    int hooked=0,failed=0;
    auto Hook=[&](const char* name,void* det,void** orig){
        void* target=GetProcAddress(hInet,name);
        if(!target){failed++;return;}
        bool ok=MH_CreateHook(target,det,orig)==MH_OK&&MH_EnableHook(target)==MH_OK;
        if(ok)hooked++;else failed++;
    };

    Hook("InternetSetStatusCallback",(void*)HookedSetStatusCallback,(void**)&g_origSetCb);
    Hook("HttpOpenRequestA",         (void*)HookedOpenA,            (void**)&g_origOpenA);
    Hook("HttpOpenRequestW",         (void*)HookedOpenW,            (void**)&g_origOpenW);
    Hook("HttpSendRequestA",         (void*)HookedSendA,            (void**)&g_origSendA);
    Hook("InternetReadFile",         (void*)HookedReadFile,         (void**)&g_origRead);
    Hook("InternetReadFileExA",      (void*)HookedReadFileEx,       (void**)&g_origReadEx);
    Hook("InternetCloseHandle",      (void*)HookedCloseHandle,      (void**)&g_origClose);

    g_authLog.Add("Hooks: "+std::to_string(hooked)+" ok "+std::to_string(failed)+" fail");
    return hooked>=4;
}
