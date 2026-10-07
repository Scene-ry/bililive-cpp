// bili_api.cpp - B 站接口封装实现（对应原 bili-proxy 的 6 个代理路由）
#include "bili_api.h"
#include "http.h"
#include "json.h"
#include "util.h"

#include <vector>

namespace bili {

namespace {

const wchar_t* kUserAgent =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    L"Chrome/137.0.0.0 Safari/537.36 Edg/137.0.0.0";

std::vector<HttpHeader> PassportHeaders() {
    return {
        {L"Referer", L"https://www.bilibili.com/"},
        {L"Origin", L"https://www.bilibili.com"},
        {L"Accept", L"application/json, text/plain, */*"},
        {L"Accept-Language", L"zh-CN,zh;q=0.9,en;q=0.8"},
    };
}

std::vector<HttpHeader> LiveApiHeaders(bool withCookie, const std::string& cookie) {
    std::vector<HttpHeader> headers = {
        {L"Accept", L"application/json, text/plain, */*"},
        {L"Accept-Language", L"zh-CN,zh;q=0.9,en;q=0.8"},
        {L"Origin", L"https://link.bilibili.com"},
        {L"Referer", L"https://link.bilibili.com/p/center/index"},
    };
    if (withCookie && !cookie.empty()) {
        headers.push_back({L"Cookie", util::Utf8ToWide(cookie)});
    }
    return headers;
}

std::vector<HttpHeader> WebApiHeaders(const std::string& cookie) {
    std::vector<HttpHeader> headers = {
        {L"Accept", L"application/json, text/plain, */*"},
        {L"Accept-Language", L"zh-CN,zh;q=0.9,en;q=0.8"},
        {L"Referer", L"https://www.bilibili.com/"},
    };
    if (!cookie.empty()) {
        headers.push_back({L"Cookie", util::Utf8ToWide(cookie)});
    }
    return headers;
}

// 解析响应体为 JSON；失败时返回 false
bool ParseBody(const HttpResult& http, JsonValue& out, std::string& err) {
    if (!JsonValue::Parse(http.body, out)) {
        err = "JSON 解析失败";
        return false;
    }
    return true;
}

}  // namespace

std::string ExtractCookieJar(const std::string& rawHeaders) {
    // 逐行扫描 "Set-Cookie:"（大小写不敏感），取每条的 "name=value" 部分
    std::string jar;
    size_t pos = 0;
    while (pos < rawHeaders.size()) {
        size_t eol = rawHeaders.find('\n', pos);
        if (eol == std::string::npos) eol = rawHeaders.size();
        std::string line = util::Trim(rawHeaders.substr(pos, eol - pos));
        pos = eol + 1;
        std::string lower = util::ToLowerAscii(line);
        if (!util::StartsWith(lower, "set-cookie:")) continue;
        std::string value = util::Trim(line.substr(11));
        size_t semi = value.find(';');
        std::string pair = (semi == std::string::npos) ? value : value.substr(0, semi);
        pair = util::Trim(pair);
        size_t eq = pair.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string name = util::Trim(pair.substr(0, eq));
        // 跳过 cookie 属性行（通常不出现于值首，但防御）
        if (name.empty()) continue;
        if (!jar.empty()) jar += "; ";
        jar += pair;
    }
    return jar;
}

QrGenerateResult QrGenerate() {
    QrGenerateResult r;
    HttpResult http = HttpClient::Request(L"GET", L"passport.bilibili.com",
                                          L"/x/passport-login/web/qrcode/generate",
                                          PassportHeaders(), nullptr, kUserAgent);
    if (!http.transportOk) {
        r.error = http.errorMessage.empty() ? "网络请求失败" : http.errorMessage;
        return r;
    }
    JsonValue json;
    if (!ParseBody(http, json, r.error)) return r;
    r.ok = json.Find("code") ? json.Find("code")->AsInt(-1) == 0 : false;
    if (!r.ok) {
        const JsonValue* msg = json.Find("message");
        r.error = msg ? msg->AsString("接口返回错误") : "接口返回错误";
        return r;
    }
    const JsonValue* data = json.Find("data");
    if (!data) {
        r.ok = false;
        r.error = "接口数据缺失";
        return r;
    }
    const JsonValue* url = data->Find("url");
    const JsonValue* key = data->Find("qrcode_key");
    if (!url || !key || url->AsString().empty() || key->AsString().empty()) {
        r.ok = false;
        r.error = "二维码数据缺失";
        return r;
    }
    r.url = url->AsString();
    r.qrcodeKey = key->AsString();
    return r;
}

QrPollResult QrPoll(const std::string& qrcodeKey) {
    QrPollResult r;
    std::wstring path = L"/x/passport-login/web/qrcode/poll?qrcode_key=" +
                        util::Utf8ToWide(util::UrlEncode(qrcodeKey));
    HttpResult http =
        HttpClient::Request(L"GET", L"passport.bilibili.com", path, PassportHeaders(), nullptr, kUserAgent);
    if (!http.transportOk) {
        r.error = http.errorMessage.empty() ? "网络请求失败" : http.errorMessage;
        return r;
    }
    r.transportOk = true;
    r.cookieJar = ExtractCookieJar(http.rawHeaders);

    JsonValue json;
    if (!ParseBody(http, json, r.error)) return r;
    if (const JsonValue* c = json.Find("code")) r.code = (int)c->AsInt(-1);
    const JsonValue* data = json.Find("data");
    if (data) {
        if (const JsonValue* dc = data->Find("code")) r.dataCode = (int)dc->AsInt(-2);
        if (const JsonValue* m = data->Find("message")) r.message = m->AsString();
    }
    r.uid = util::CookieValue(r.cookieJar, "DedeUserID");
    return r;
}

RoomInfoResult RoomIdByUid(const std::string& uid) {
    RoomInfoResult r;
    std::wstring path = L"/room/v2/Room/room_id_by_uid?uid=" + util::Utf8ToWide(util::UrlEncode(uid));
    HttpResult http = HttpClient::Request(L"GET", L"api.live.bilibili.com", path,
                                          LiveApiHeaders(false, ""), nullptr, kUserAgent);
    if (!http.transportOk) {
        r.error = http.errorMessage.empty() ? "网络请求失败" : http.errorMessage;
        return r;
    }
    r.transportOk = true;
    JsonValue json;
    if (!ParseBody(http, json, r.error)) return r;
    if (const JsonValue* c = json.Find("code")) r.code = (int)c->AsInt(-1);
    if (const JsonValue* m = json.Find("message")) r.message = m->AsString();
    const JsonValue* data = json.Find("data");
    if (data) {
        if (const JsonValue* rid = data->Find("room_id")) r.roomId = rid->AsInt(0);
    }
    r.hasRoom = (r.code == 0 && r.roomId > 0);
    return r;
}

NavResult NavCheck(const std::string& cookie) {
    NavResult r;
    HttpResult http = HttpClient::Request(L"GET", L"api.bilibili.com", L"/x/web-interface/nav",
                                          WebApiHeaders(cookie), nullptr, kUserAgent);
    if (!http.transportOk) {
        r.error = http.errorMessage.empty() ? "网络请求失败" : http.errorMessage;
        return r;
    }
    r.transportOk = true;
    JsonValue json;
    if (!ParseBody(http, json, r.error)) return r;
    if (const JsonValue* c = json.Find("code")) r.code = (int)c->AsInt(-1);
    const JsonValue* data = json.Find("data");
    if (data) {
        if (const JsonValue* v = data->Find("isLogin")) r.isLogin = v->AsBool(false);
        if (const JsonValue* mid = data->Find("mid")) r.mid = mid->AsInt(0);
    }
    // code != 0（-101 未登录）即视为未登录
    if (r.code != 0) r.isLogin = false;
    return r;
}

StartLiveResult StartLive(const std::string& cookie, const std::string& csrf,
                          const std::string& roomId, const std::string& partitionId) {
    StartLiveResult r;
    std::string body;
    body += "room_id=" + util::UrlEncode(roomId);
    body += "&platform=pc_link";
    body += "&backup_stream=0";
    body += "&area_v2=" + util::UrlEncode(partitionId);
    body += "&csrf_token=" + util::UrlEncode(csrf);
    body += "&csrf=" + util::UrlEncode(csrf);
    body += "&version=1.0.0";
    body += "&build=1234";

    std::vector<HttpHeader> headers = LiveApiHeaders(true, cookie);
    headers.push_back({L"Content-Type", L"application/x-www-form-urlencoded; charset=UTF-8"});

    HttpResult http = HttpClient::Request(L"POST", L"api.live.bilibili.com",
                                          L"/room/v1/Room/startLive", headers, &body, kUserAgent);
    if (!http.transportOk) {
        r.error = http.errorMessage.empty() ? "网络请求失败" : http.errorMessage;
        return r;
    }
    r.transportOk = true;
    JsonValue json;
    if (!ParseBody(http, json, r.error)) return r;
    if (const JsonValue* c = json.Find("code")) r.code = (int)c->AsInt(-1);
    if (const JsonValue* m = json.Find("message")) r.message = m->AsString();

    const JsonValue* data = json.Find("data");
    if (data) {
        if (const JsonValue* nfa = data->Find("need_face_auth")) {
            r.needFaceAuth = nfa->type == JsonValue::Type::Bool ? nfa->boolValue : nfa->AsInt(0) != 0;
        }
        if (const JsonValue* qr = data->Find("qr")) r.faceQrUrl = qr->AsString();
        if (const JsonValue* rtmp = data->Find("rtmp")) {
            if (const JsonValue* addr = rtmp->Find("addr")) r.rtmpAddr = addr->AsString();
            if (const JsonValue* code = rtmp->Find("code")) r.rtmpCode = code->AsString();
        }
    }
    return r;
}

SimpleApiResult StopLive(const std::string& cookie, const std::string& csrf,
                         const std::string& roomId) {
    SimpleApiResult r;
    std::string body;
    body += "room_id=" + util::UrlEncode(roomId);
    body += "&platform=pc_link";
    body += "&csrf_token=" + util::UrlEncode(csrf);
    body += "&csrf=" + util::UrlEncode(csrf);

    std::vector<HttpHeader> headers = LiveApiHeaders(true, cookie);
    headers.push_back({L"Content-Type", L"application/x-www-form-urlencoded; charset=UTF-8"});

    HttpResult http = HttpClient::Request(L"POST", L"api.live.bilibili.com",
                                          L"/room/v1/Room/stopLive", headers, &body, kUserAgent);
    if (!http.transportOk) {
        r.error = http.errorMessage.empty() ? "网络请求失败" : http.errorMessage;
        return r;
    }
    r.transportOk = true;
    JsonValue json;
    if (!ParseBody(http, json, r.error)) return r;
    if (const JsonValue* c = json.Find("code")) r.code = (int)c->AsInt(-1);
    if (const JsonValue* m = json.Find("message")) r.message = m->AsString();
    return r;
}

SimpleApiResult Logout(const std::string& cookie, const std::string& csrf) {
    SimpleApiResult r;
    std::string body;
    body += "biliCSRF=" + util::UrlEncode(csrf);
    body += "&gourl=" + util::UrlEncode("/bililive/login");

    std::vector<HttpHeader> headers = {
        {L"Content-Type", L"application/x-www-form-urlencoded; charset=UTF-8"},
        {L"Origin", L"https://www.bilibili.com"},
        {L"Referer", L"https://www.bilibili.com/"},
        {L"Accept", L"application/json, text/plain, */*"},
    };
    if (!cookie.empty()) headers.push_back({L"Cookie", util::Utf8ToWide(cookie)});

    HttpResult http = HttpClient::Request(L"POST", L"passport.bilibili.com", L"/login/exit/v2",
                                          headers, &body, kUserAgent);
    if (!http.transportOk) {
        r.error = http.errorMessage.empty() ? "网络请求失败" : http.errorMessage;
        return r;
    }
    r.transportOk = true;
    JsonValue json;
    if (!ParseBody(http, json, r.error)) return r;
    if (const JsonValue* c = json.Find("code")) r.code = (int)c->AsInt(-1);
    if (const JsonValue* m = json.Find("message")) r.message = m->AsString();
    return r;
}

}  // namespace bili
