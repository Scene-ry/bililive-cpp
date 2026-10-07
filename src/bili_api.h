// bili_api.h - B 站接口封装（对应原 bili-proxy 的 6 个服务端代理路由）
#pragma once

#include <string>
#include <cstdint>

namespace bili {

// GET /x/passport-login/web/qrcode/generate
struct QrGenerateResult {
    bool ok = false;
    std::string error;       // 网络或解析错误
    std::string url;         // 用于生成二维码的 URL
    std::string qrcodeKey;   // 轮询凭据
};

// GET /x/passport-login/web/qrcode/poll
struct QrPollResult {
    bool transportOk = false;
    std::string error;
    int code = -1;           // 外层 code
    int dataCode = -2;       // 0=成功 / 86038=过期 / 86090=已扫码待确认 / 86101=未扫码
    std::string message;
    std::string cookieJar;   // 从 Set-Cookie 提取的 "k=v; k2=v2"
    std::string uid;         // DedeUserID
};

// GET /room/v2/Room/room_id_by_uid
struct RoomInfoResult {
    bool transportOk = false;
    std::string error;
    int code = -1;
    std::string message;
    int64_t roomId = 0;
    bool hasRoom = false;
};

// GET /x/web-interface/nav （带 cookie 验证登录态）
struct NavResult {
    bool transportOk = false;
    std::string error;
    int code = -1;
    bool isLogin = false;
    int64_t mid = 0;
};

// POST /room/v1/Room/startLive
struct StartLiveResult {
    bool transportOk = false;
    std::string error;
    int code = -1;
    std::string message;
    bool needFaceAuth = false;
    std::string faceQrUrl;   // need_face_auth 时的认证二维码 URL
    std::string rtmpAddr;
    std::string rtmpCode;
};

// POST /room/v1/Room/stopLive、POST /login/exit/v2
struct SimpleApiResult {
    bool transportOk = false;
    std::string error;
    int code = -1;
    std::string message;
};

QrGenerateResult QrGenerate();
QrPollResult QrPoll(const std::string& qrcodeKey);
RoomInfoResult RoomIdByUid(const std::string& uid);
NavResult NavCheck(const std::string& cookie);
StartLiveResult StartLive(const std::string& cookie, const std::string& csrf,
                          const std::string& roomId, const std::string& partitionId);
SimpleApiResult StopLive(const std::string& cookie, const std::string& csrf, const std::string& roomId);
SimpleApiResult Logout(const std::string& cookie, const std::string& csrf);

// 工具：从原始响应头解析全部 Set-Cookie，拼成 "k=v; k2=v2"
std::string ExtractCookieJar(const std::string& rawHeaders);

}  // namespace bili
