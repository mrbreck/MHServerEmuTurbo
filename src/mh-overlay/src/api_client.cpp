#include "api_client.h"
#include <Windows.h>
#include <winhttp.h>
#include <sstream>
#include <stdexcept>
#include <algorithm>

std::string g_serverBase = "http://127.0.0.1:10000";

struct WinHttpSession
{
    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;
    WinHttpSession(const std::wstring& host, INTERNET_PORT port, bool https)
    {
        hSession = WinHttpOpen(L"MH-Overlay/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) throw std::runtime_error("WinHttpOpen failed");
        hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
        if (!hConnect) throw std::runtime_error("WinHttpConnect failed");
    }
    ~WinHttpSession() {
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
    }
};

static void ParseBase(const std::string& base, std::wstring& host, INTERNET_PORT& port, bool& https)
{
    https = (base.rfind("https://", 0) == 0);
    std::string rest = base.substr(https ? 8 : 7);
    auto colon = rest.rfind(':');
    if (colon != std::string::npos) {
        port = (INTERNET_PORT)std::stoi(rest.substr(colon + 1));
        rest = rest.substr(0, colon);
    } else {
        port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    }
    host = std::wstring(rest.begin(), rest.end());
}

static std::string DoRequest(const std::string& method, const std::string& path, const std::string& body)
{
    std::wstring host; INTERNET_PORT port; bool https;
    ParseBase(g_serverBase, host, port, https);
    WinHttpSession sess(host, port, https);
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    std::wstring wpath(path.begin(), path.end());
    HINTERNET hReq = WinHttpOpenRequest(sess.hConnect,
        std::wstring(method.begin(), method.end()).c_str(),
        wpath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) return {};
    std::wstring hdrs = L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(hReq, hdrs.c_str(), (DWORD)hdrs.size(),
        body.empty() ? nullptr : (LPVOID)body.c_str(),
        (DWORD)body.size(), (DWORD)body.size(), 0);
    std::string result;
    if (ok && WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, nullptr);
        if (status == 200) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail) {
                std::string buf(avail, '\0'); DWORD read = 0;
                WinHttpReadData(hReq, &buf[0], avail, &read);
                result.append(buf.data(), read);
            }
        }
    }
    WinHttpCloseHandle(hReq);
    return result;
}

static std::string JsonEscape(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

// Minimal JSON field parsers
static uint64_t JsonUint64(const std::string& j, const std::string& key) {
    auto pos = j.find("\"" + key + "\""); if (pos == std::string::npos) return 0;
    pos = j.find(':', pos); if (pos == std::string::npos) return 0;
    pos = j.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos || !isdigit((unsigned char)j[pos])) return 0;
    return std::stoull(j.substr(pos));
}
static std::string JsonStr(const std::string& j, const std::string& key) {
    auto pos = j.find("\"" + key + "\""); if (pos == std::string::npos) return {};
    pos = j.find('"', j.find(':', pos)) + 1;
    auto end = j.find('"', pos);
    return j.substr(pos, end - pos);
}
static int JsonInt(const std::string& j, const std::string& key) {
    auto pos = j.find("\"" + key + "\""); if (pos == std::string::npos) return 0;
    pos = j.find_first_not_of(" \t\r\n", j.find(':', pos) + 1);
    return std::stoi(j.substr(pos));
}

static void ParseRarities(const std::string& arr, std::vector<RarityEntry>& out) {
    size_t pos = 0;
    while ((pos = arr.find('{', pos)) != std::string::npos) {
        auto end = arr.find('}', pos); if (end == std::string::npos) break;
        std::string obj = arr.substr(pos, end - pos + 1);
        RarityEntry e;
        e.protoId = JsonUint64(obj, "ProtoId");
        e.name    = JsonStr(obj, "Name");
        e.tier    = JsonInt(obj, "Tier");
        if (e.protoId) out.push_back(e);
        pos = end + 1;
    }
}

// Parse VaporizerSlots array: [{"SlotId":N,"RarityId":N}, ...]
static void ParseVaporizerSlots(const std::string& arr, std::unordered_map<int, uint64_t>& out)
{
    size_t pos = 0;
    while ((pos = arr.find('{', pos)) != std::string::npos) {
        auto end = arr.find('}', pos); if (end == std::string::npos) break;
        std::string obj = arr.substr(pos, end - pos + 1);
        int      slotId   = JsonInt(obj, "SlotId");
        uint64_t rarityId = JsonUint64(obj, "RarityId");
        if (slotId != 0) out[slotId] = rarityId;
        pos = end + 1;
    }
}

bool ApiGetGameOptions(const std::string& email, const std::string& token,
                       std::vector<RarityEntry>& raritiesOut, GameOptions& optionsOut)
{
    std::string path = "/api/gameoptions?email=" + email + "&token=" + token;
    std::string resp = DoRequest("GET", path, "");
    if (resp.empty()) return false;

    // Parse Rarities array
    auto ra = resp.find("\"Rarities\"");
    if (ra == std::string::npos) return false;
    auto rb = resp.find('[', ra), re = resp.find(']', rb);
    if (rb == std::string::npos || re == std::string::npos) return false;
    ParseRarities(resp.substr(rb, re - rb + 1), raritiesOut);

    // Parse VaporizerSlots array
    auto sa = resp.find("\"VaporizerSlots\"");
    if (sa == std::string::npos) return false;
    auto sb = resp.find('[', sa), se = resp.find(']', sb);
    if (sb == std::string::npos || se == std::string::npos) return false;
    ParseVaporizerSlots(resp.substr(sb, se - sb + 1), optionsOut.slotProtoId);

    return true;
}

bool ApiSetGameOptions(const std::string& email, const std::string& token,
                       const GameOptions& options)
{
    std::ostringstream body;
    body << "{\"Email\":\"" << JsonEscape(email) << "\","
         << "\"Token\":\"" << JsonEscape(token) << "\","
         << "\"VaporizerSlots\":[";
    bool first = true;
    for (auto& kv : options.slotProtoId) {
        if (!first) body << ',';
        body << "{\"SlotId\":" << kv.first << ",\"RarityId\":" << kv.second << "}";
        first = false;
    }
    body << "]}";
    DoRequest("POST", "/api/gameoptions", body.str());
    return true;
}
