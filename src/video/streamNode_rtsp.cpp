// ============================================================================
// streamNode_rtsp.cpp - RTSP 信令控制层
// 包含：RTSP 认证、信令方法（DESCRIBE/SETUP/PLAY/TEARDOWN/ANNOUNCE/RECORD/
//       GET_PARAMETER）、SDP 解析/生成、控制流（controlThread/openOriginPullSession/
//       openRelayPushSession/teardown）
// ============================================================================

#include "streamNode.h"
#include "streamServer.h"
#include <logger.h>
#include <sstream>
#include <algorithm>
#include <cstring>

// ============================================================================
// 认证相关函数
// ============================================================================

std::string StreamNode::calculateBasicAuth(const AuthInfo& auth) {
    std::string credentials = auth.username + ":" + auth.password;
    return "Basic " + base64Encode(credentials);
}

std::string StreamNode::calculateDigest(const std::string& method, const std::string& uri,
    const AuthInfo& auth) {
    // 计算HA1 = MD5(username:realm:password)
    std::string ha1_input = auth.username + ":" + auth.realm + ":" + auth.password;
    std::string ha1 = md5Hex(ha1_input);

    // 计算HA2 = MD5(method:uri)
    std::string ha2_input = method + ":" + uri;
    std::string ha2 = md5Hex(ha2_input);

    // 计算response = MD5(HA1:nonce:HA2)
    std::string response_input = ha1 + ":" + auth.nonce + ":" + ha2;
    std::string response = md5Hex(response_input);

    return response;
}

bool StreamNode::parseWWWAuthenticate(const std::string& response, AuthInfo& auth) {
    // 查找WWW-Authenticate头
    size_t www_auth_pos = response.find("WWW-Authenticate: ");
    if (www_auth_pos == std::string::npos) {
        return false;
    }

    size_t line_end = response.find("\r\n", www_auth_pos);
    std::string auth_line = response.substr(www_auth_pos + 18, line_end - www_auth_pos - 18);

    logVerbose("WWW-Authenticate: " + auth_line);

    // 检查认证类型
    if (auth_line.find("Digest") == 0) {
        auth.use_digest = true;

        // 解析Digest参数
        size_t realm_pos = auth_line.find("realm=\"");
        if (realm_pos != std::string::npos) {
            size_t realm_end = auth_line.find("\"", realm_pos + 7);
            if (realm_end != std::string::npos) {
                auth.realm = auth_line.substr(realm_pos + 7, realm_end - realm_pos - 7);
            }
        }

        size_t nonce_pos = auth_line.find("nonce=\"");
        if (nonce_pos != std::string::npos) {
            size_t nonce_end = auth_line.find("\"", nonce_pos + 7);
            if (nonce_end != std::string::npos) {
                auth.nonce = auth_line.substr(nonce_pos + 7, nonce_end - nonce_pos - 7);
            }
        }

        size_t algorithm_pos = auth_line.find("algorithm=\"");
        if (algorithm_pos != std::string::npos) {
            size_t algorithm_end = auth_line.find("\"", algorithm_pos + 11);
            if (algorithm_end != std::string::npos) {
                auth.algorithm = auth_line.substr(algorithm_pos + 11, algorithm_end - algorithm_pos - 11);
            }
        }
        else {
            auth.algorithm = "MD5";
        }

        return true;
    }
    else if (auth_line.find("Basic") == 0) {
        auth.use_digest = false;

        size_t realm_pos = auth_line.find("realm=\"");
        if (realm_pos != std::string::npos) {
            size_t realm_end = auth_line.find("\"", realm_pos + 7);
            if (realm_end != std::string::npos) {
                auth.realm = auth_line.substr(realm_pos + 7, realm_end - realm_pos - 7);
            }
        }

        return true;
    }

    return false;
}

void StreamNode::updateAuthHeader(AuthInfo& auth, const std::string& method, const std::string& uri) {
    if (!auth.hasCredentials()) {
        auth.authorization_header.clear();
        return;
    }

    if (auth.use_digest) {
        std::string response = calculateDigest(method, uri, auth);

        std::stringstream auth_header;
        auth_header << "Authorization: Digest "
            << "username=\"" << auth.username << "\", "
            << "realm=\"" << auth.realm << "\", "
            << "nonce=\"" << auth.nonce << "\", "
            << "uri=\"" << uri << "\", "
            << "response=\"" << response << "\"";

        if (!auth.algorithm.empty()) {
            auth_header << ", algorithm=\"" << auth.algorithm << "\"";
        }

        auth.authorization_header = auth_header.str();
    }
    else {
        auth.authorization_header = "Authorization: " + calculateBasicAuth(auth);
    }
}

// ============================================================================
// RTSP协议函数
// ============================================================================

bool StreamNode::rtspDescribe(Connection& conn, const std::string& url,
    std::string& sdp, std::string& session) {
    URLComponents url_components;
    if (!URLComponents::parse(url, url_components)) {
        logError("DESCRIBE failed: invalid url format: " + url);
        return false;
    }

    std::string host_header = url_components.host;
    if (host_header.find(':') != std::string::npos) {
        host_header = "[" + host_header + "]";
    }
    host_header += ":" + std::to_string(url_components.port);

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == session_origin_pull_.server_url_ || url.find(session_origin_pull_.server_url_) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    auto do_describe = [&](const std::string& request_uri, bool include_host,
        bool use_auth, std::string& response) -> bool {
            std::stringstream request;
            request << "DESCRIBE " << request_uri << " RTSP/1.0\r\n"
                << "CSeq: " << generateCSeq() << "\r\n"
                << "User-Agent: StreamNode/1.0\r\n";

            if (include_host) {
                request << "Host: " << host_header << "\r\n";
            }

            // 添加认证头
            if (use_auth && auth_info && auth_info->hasCredentials()) {
                updateAuthHeader(*auth_info, "DESCRIBE", request_uri);
                if (!auth_info->authorization_header.empty()) {
                    request << auth_info->authorization_header << "\r\n";
                }
            }

            request << "Accept: application/sdp\r\n"
                << "\r\n";

            const std::string req = request.str();
            logVerbose(">> DESCRIBE " + request_uri +
                (include_host ? "" : " (no Host)") +
                (use_auth ? " (with auth)" : ""));

            int sent = conn.send(req.c_str(), req.size());
            if (sent != static_cast<int>(req.size())) {
                setError("DESCRIBE send failed: sent=" + std::to_string(sent) +
                    " err=" + std::to_string(conn.lastError()));
                response.clear();
                return false;
            }

            response.clear();
            int rc = conn.receiveHttpResp(response, 1000);
            if (rc <= 0) {
                setError("DESCRIBE recv failed: rc=" + std::to_string(rc) +
                    " err=" + std::to_string(conn.lastError()));
                response.clear();
                return false;
            }

            if (response.find("200 OK") != std::string::npos) {
                // 成功
                size_t sdp_start = response.find("\r\n\r\n");
                if (sdp_start != std::string::npos) {
                    sdp = response.substr(sdp_start + 4);
                }
                else {
                    sdp.clear();
                }
                // 注意：虽然 RTSP RFC 规定 Session 应该在 SETUP 响应中返回，
                // 但 ZLMediaKit 在 DESCRIBE 响应中也包含 Session。
                // 为了兼容 ZLM，我们需要从 DESCRIBE 响应中提取 Session。
                // 这样在后续的 SETUP 请求中可以带上 Session。
                std::string session_in_response = extractSessionID(response);
                if (!session_in_response.empty()) {
                    session = session_in_response;
                    logVerbose("Session from DESCRIBE: " + session);
                }
                return true;
            }
            else if (response.find("401 Unauthorized") != std::string::npos) {
                // 需要认证
                if (auth_info && auth_info->hasCredentials()) {
                    // 解析WWW-Authenticate头
                    if (parseWWWAuthenticate(response, *auth_info)) {
                        logInfo("Authentication required, retrying with credentials");
                    }
                    else {
                        logError("Failed to parse WWW-Authenticate header");
                    }
                }
                else {
                    logError("Authentication required but no credentials provided");
                }
                return false;
            }
            else {
                LOG("[StreamNode]tag=%s,DESCRIBE failed:%s",config_.tag.c_str(),response.substr(0, 200).c_str());
                return false;
            }
        };

    // 尝试顺序：无认证 -> 带认证
    std::string response;

    // 第一次尝试：不带认证
    if (do_describe(url, true, false, response)) {
        return true;
    }

    // 第二次尝试：带认证（如果提供了凭据）
    if (auth_info && auth_info->hasCredentials()) {
        if (do_describe(url, true, true, response)) {
            return true;
        }
    }

    // 如果上面失败，尝试不带Host头
    if (do_describe(url, false, false, response)) {
        return true;
    }

    // 如果提供了凭据，尝试不带Host头但带认证
    if (auth_info && auth_info->hasCredentials()) {
        if (do_describe(url, false, true, response)) {
            return true;
        }
    }

    // 一些RTSP服务器期望路径格式的URI
    if (!url_components.path.empty() && url_components.path != url) {
        logVerbose("Retry DESCRIBE with path-only URI: " + url_components.path);

        if (do_describe(url_components.path, true, false, response)) {
            return true;
        }

        if (auth_info && auth_info->hasCredentials()) {
            if (do_describe(url_components.path, true, true, response)) {
                return true;
            }
        }
    }

    return false;
}

bool StreamNode::rtspSetup(Connection& conn, const std::string& url,
    std::string& session, STREAM_SESSION& stream, bool record_mode) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == session_origin_pull_.server_url_ || url.find(session_origin_pull_.server_url_) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    // 根据 ZLM 的 SDP 格式，正确拼接 SETUP URL
    // ZLM SDP: a=control:* 表示 base URL, a=control:streamid=0 表示相对路径
    std::string setup_url = url;
    
    if (!stream.control_url.empty()) {
        if (stream.control_url.rfind("rtsp://", 0) == 0 || 
            stream.control_url.rfind("rtsps://", 0) == 0) {
            // 绝对 URL：直接使用
            setup_url = stream.control_url;
        }
        else if (stream.control_url.front() == '/') {
            // 以 / 开头的绝对路径：rtsp://host:port/path
            URLComponents src_url;
            if (URLComponents::parse(url, src_url)) {
                setup_url = src_url.protocol + "://" + src_url.host + ":" + 
                           std::to_string(src_url.port) + stream.control_url;
            }
        }
        else {
            // 相对路径（如 streamid=0）：拼接到原始 URL 后面
            // ZLM 格式：/stream/1 + streamid=0 = /stream/1/streamid=0
            if (!url.empty()) {
                if (url.back() == '*') {
                    // a=control:* 表示用 base URL
                    setup_url = url.substr(0, url.length() - 1) + stream.control_url;
                }
                else if (url.back() == '/') {
                    setup_url = url + stream.control_url;
                }
                else {
                    setup_url = url + "/" + stream.control_url;
                }
            }
            else {
                setup_url = stream.control_url;
            }
        }
    }

    std::stringstream request;
    request << "SETUP " << setup_url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: StreamNode/1.0\r\n";

    (void)host_header;  // 抑制未使用变量警告

    if (!session.empty()) {
        request << "Session: " << session << "\r\n";
    }

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "SETUP", setup_url);
        request << auth_info->authorization_header << "\r\n";
    }

    // UDP传输模式：使用RTP/AVP/UDP
    if (record_mode) {
        // 推流（发送）：服务端接收
        if (session_relay_push_.transport_mode == TransportMode::UDP) {
            request << "Transport: RTP/AVP/UDP;unicast;mode=record;"
                << "client_port=" << stream.client_port;
            if (session_relay_push_.udp_ttl != 64) {
                request << ";ttl=" << session_relay_push_.udp_ttl;
            }
            request << "\r\n";
        }
        else {
            // TCP推流
            request << "Transport: RTP/AVP/TCP;unicast;mode=record;interleaved=0-1\r\n";
        }
    }
    else {
        // 拉流（接收）：客户端接收
        if (session_origin_pull_.transport_mode == TransportMode::UDP) {
            request << "Transport: RTP/AVP/UDP;unicast;"
                << "client_port=" << stream.client_port;
            if (session_origin_pull_.udp_ttl != 64) {
                request << ";ttl=" << session_origin_pull_.udp_ttl;
            }
            request << "\r\n";
        }
    else {
        // TCP拉流 - 尝试多种格式
        // 格式1: 标准 RFC 格式
        request << "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
    }
    }

    request << "\r\n";

    const std::string req = request.str();
    // 打印完整请求内容，便于调试
    std::string req_for_log = req;
    std::replace(req_for_log.begin(), req_for_log.end(), '\r', '~');
    std::replace(req_for_log.begin(), req_for_log.end(), '\n', '~');
    logVerbose(">> SETUP REQUEST:\n" + req_for_log);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("SETUP send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    // 增加超时时间，因为 ZLM 可能延迟发送 SETUP 响应
    int rc = conn.receiveHttpResp(response, 10000);
    if (rc <= 0) {
        // 调试：打印收到的原始数据
        if (!response.empty()) {
            std::string resp_for_log = response;
            std::replace(resp_for_log.begin(), resp_for_log.end(), '\r', '~');
            std::replace(resp_for_log.begin(), resp_for_log.end(), '\n', '~');
            logVerbose("<< SETUP PARTIAL DATA: " + resp_for_log);
        }
        logError("SETUP recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    // 调试：打印收到的响应
    {
        std::string resp_for_log = response;
        std::replace(resp_for_log.begin(), resp_for_log.end(), '\r', '~');
        std::replace(resp_for_log.begin(), resp_for_log.end(), '\n', '~');
        logVerbose("<< SETUP RESPONSE (rc=" + std::to_string(rc) + "): " + resp_for_log);
    }

    if (response.find("200 OK") == std::string::npos) {
        if (response.find("401 Unauthorized") != std::string::npos) {
            logError("SETUP authentication failed");
        }
        else {
            logError("SETUP failed: " + response.substr(0, 200));
        }
        return false;
    }

    stream.transport = extractTransport(response);

    std::string new_session = extractSessionID(response);
    if (!new_session.empty() && session.empty()) {
        session = new_session;
    }

    return true;
}

bool StreamNode::rtspPlay(Connection& conn, const std::string& url,
    const std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == session_origin_pull_.server_url_ || url.find(session_origin_pull_.server_url_) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "PLAY " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: StreamNode/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "PLAY", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Session: " << session << "\r\n"
        << "Range: npt=0.000-\r\n"
        << "\r\n";

    const std::string req = request.str();
    logVerbose(">> PLAY " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("PLAY send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    int rc = conn.receiveHttpResp(response, 5000);
    if (rc <= 0) {
        logError("PLAY recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    if (response.find("200 OK") == std::string::npos) {
        logError("PLAY failed: " + response.substr(0, 200));
        return false;
    }

    return true;
}

bool StreamNode::rtspTeardown(Connection& conn, const std::string& url,
    const std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == session_origin_pull_.server_url_ || url.find(session_origin_pull_.server_url_) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "TEARDOWN " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: StreamNode/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "TEARDOWN", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Session: " << session << "\r\n"
        << "\r\n";

    const std::string req = request.str();
    logVerbose(">> TEARDOWN " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("TEARDOWN send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    conn.receiveHttpResp(response, 5000);

    return true;
}

bool StreamNode::rtspAnnounce(Connection& conn, const std::string& url,
    const std::string& sdp, std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == session_relay_push_.server_url_ || url.find(session_relay_push_.server_url_) == 0) {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "ANNOUNCE " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: StreamNode/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "ANNOUNCE", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Content-Type: application/sdp\r\n"
        << "Content-Length: " << sdp.size() << "\r\n"
        << "\r\n"
        << sdp;

    const std::string req = request.str();
    logVerbose(">> ANNOUNCE " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("ANNOUNCE send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    int rc = conn.receiveHttpResp(response, 5000);
    if (rc <= 0) {
        logError("ANNOUNCE recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    if (response.find("200 OK") == std::string::npos) {
        logError("ANNOUNCE failed: " + response.substr(0, 200));
        return false;
    }

    session = extractSessionID(response);

    return true;
}

bool StreamNode::rtspRecord(Connection& conn, const std::string& url,
    const std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == session_relay_push_.server_url_ || url.find(session_relay_push_.server_url_) == 0) {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "RECORD " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: StreamNode/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "RECORD", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Session: " << session << "\r\n"
        << "Range: npt=0.000-\r\n"
        << "\r\n";

    const std::string req = request.str();
    logVerbose(">> RECORD " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("RECORD send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    int rc = conn.receiveHttpResp(response, 5000);
    if (rc <= 0) {
        logError("RECORD recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    if (response.find("200 OK") == std::string::npos) {
        logError("RECORD failed: " + response.substr(0, 200));
        return false;
    }

    return true;
}

bool StreamNode::rtspGetParameter(Connection& conn, const std::string& url,
    const std::string& session) {
    URLComponents url_components;
    std::string host_header;
    if (URLComponents::parse(url, url_components)) {
        host_header = url_components.host;
        if (host_header.find(':') != std::string::npos) {
            host_header = "[" + host_header + "]";
        }
        host_header += ":" + std::to_string(url_components.port);
    }

    // 确定认证信息
    AuthInfo* auth_info = nullptr;
    if (url == session_origin_pull_.server_url_ || url.find(session_origin_pull_.server_url_) == 0) {
        auth_info = &source_auth_;
    }
    else {
        auth_info = &target_auth_;
    }

    std::stringstream request;
    request << "GET_PARAMETER " << url << " RTSP/1.0\r\n"
        << "CSeq: " << generateCSeq() << "\r\n"
        << "User-Agent: StreamNode/1.0\r\n"
        << (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

    // 添加认证头
    if (auth_info && auth_info->hasCredentials() && !auth_info->authorization_header.empty()) {
        updateAuthHeader(*auth_info, "GET_PARAMETER", url);
        request << auth_info->authorization_header << "\r\n";
    }

    request << "Session: " << session << "\r\n"
        << "Content-Length: 0\r\n"
        << "\r\n";

    const std::string req = request.str();
    logVerbose(">> GET_PARAMETER " + url);

    int sent = conn.send(req.c_str(), req.size());
    if (sent != static_cast<int>(req.size())) {
        logError("GET_PARAMETER send failed: sent=" + std::to_string(sent) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    std::string response;
    int rc = conn.receiveHttpResp(response, 5000);
    if (rc <= 0) {
        logError("GET_PARAMETER recv failed: rc=" + std::to_string(rc) +
            " err=" + std::to_string(conn.lastError()));
        return false;
    }

    if (response.find("200 OK") == std::string::npos) {
        logError("GET_PARAMETER failed: " + response.substr(0, 200));
        return false;
    }

    return true;
}

// ============================================================================
// 控制流
// ============================================================================

void StreamNode::controlThread() {
    while (running_ && !stopping_) {
            // 启动拉流与推流
            if (isPulling_ == false) {
                if (openOriginPullSession()) {
                    rtp_handle_thread_ = std::thread(&StreamNode::rtpHandleThread,this);
                    rtp_handle_thread_.detach();
                    open_time_ = std::chrono::system_clock::now();
                    isPulling_ = true;
                }
                else {
                    doReconnect(session_origin_pull_);
                    teardown();
                }
            }
 
            if (isPulling_ == true && isPushing_ == false && session_relay_push_.server_url_ != "") {
                if (openRelayPushSession()) {
                    isPushing_ = true;
				}
                else {
                    doReconnect(session_relay_push_);
                    teardown();
                }
            }

            // 心跳保活
            if (isPulling_) {
                if (session_origin_pull_.conn_ && !session_origin_pull_.rtsp_session_id_.empty()) {
                    if (!rtspGetParameter(*session_origin_pull_.conn_, session_origin_pull_.server_url_, session_origin_pull_.rtsp_session_id_)) {
                        setError("Source RTSP keepalive failed", 1002);
                        isPulling_ = false;
                        teardown();
                    }
                }
            }

            if (isPushing_) {
            if (session_relay_push_.conn_ && !session_relay_push_.rtsp_session_id_.empty()) {
                if (!rtspGetParameter(*session_relay_push_.conn_, session_relay_push_.server_url_, session_relay_push_.rtsp_session_id_)) {
                        setError("Target RTSP keepalive failed", 1002);
                        session_relay_push_.state_ = SESSION_STATE::SESSION_ERROR;
                        isPushing_ = false;
                        teardown();
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

bool StreamNode::openRelayPushSession() {
	session_relay_push_.state_ = SESSION_STATE::SESSION_CONNECTING;
    // 连接到目标服务器
    URLComponents relay_push_url;
    if (!URLComponents::parse(session_relay_push_.server_url_, relay_push_url)) {
        session_relay_push_.state_ = SESSION_STATE::SESSION_ERROR;
        setError("Invalid target URL format", 3005);
        return false;
    }

    session_relay_push_.conn_ = std::make_unique<Connection>();
    if (!session_relay_push_.conn_->connect(relay_push_url.host, relay_push_url.port)) {
        setError("Failed to connect to target server: " + relay_push_url.host + ":" + std::to_string(relay_push_url.port) +
            " (err=" + std::to_string(session_relay_push_.conn_->lastError()) +
            "). Is an RTSP server listening on that port?",
            3006);
        return false;
    }

    // 生成目标SDP（保留 relay push 自身状态）
    {
        SESSION_STATE saved_state = session_relay_push_.state_;
        session_relay_push_ = session_origin_pull_;
        session_relay_push_.state_ = saved_state;
    }
    {
        std::string track_control = "trackID=0";
        if (!session_origin_pull_.control_url.empty()) {
            std::string src = session_origin_pull_.control_url;
            auto pos = src.find_last_of('/');
            track_control = (pos == std::string::npos) ? src : src.substr(pos + 1);
            if (track_control.empty() || track_control == "*" ||
                track_control.rfind("rtsp://", 0) == 0 ||
                track_control.rfind("rtsps://", 0) == 0) {
                track_control = "trackID=0";
            }
        }
        session_relay_push_.control_url = track_control;
    }

    std::string target_sdp = generateSDP(session_relay_push_, STREAM_SESSION());

    // 发送ANNOUNCE到目标
    if (!rtspAnnounce(*session_relay_push_.conn_, session_relay_push_.server_url_, target_sdp, session_relay_push_.rtsp_session_id_)) {
        session_relay_push_.state_ = SESSION_STATE::SESSION_ERROR;
        setError("ANNOUNCE failed", 3007);
        return false;
    }

    // 如果使用 UDP 推流，应先创建并绑定本地 RTP/RTCP sockets，
    // 并将 client_port 写入 target_video_info_，再发送 SETUP。
    if (session_relay_push_.transport_mode == TransportMode::UDP) {
        if (!createUDPPushSocket()) {
            logError("Failed to create UDP push socket, falling back to TCP");
            session_relay_push_.transport_mode = TransportMode::TCP;
        }
        else {
            // 填写 client_port，格式 "RTP-RTCP"
            session_relay_push_.client_port = std::to_string(session_relay_push_.client_rtp_port) + "-" + std::to_string(session_relay_push_.client_rtcp_port);
            logInfo("Push stream: Using UDP mode, client_port=" + session_relay_push_.client_port);
        }
    }

    if (session_relay_push_.transport_mode == TransportMode::TCP) {
        logInfo("Push stream: Using TCP mode (RTP over RTSP)");
    }

    // 发送SETUP到目标
    if (!rtspSetup(*session_relay_push_.conn_, session_relay_push_.server_url_, session_relay_push_.rtsp_session_id_, session_relay_push_, true)) {
        session_relay_push_.state_ = SESSION_STATE::SESSION_ERROR;
        setError("SETUP failed for target", 3008);
        return false;
    }

    // 解析目标服务器端口
    {
        logInfo("Target SETUP Transport: " + session_relay_push_.transport);
        const std::string key = "server_port=";
        size_t pos = session_relay_push_.transport.find(key);
        if (pos != std::string::npos) {
            pos += key.size();
            while (pos < session_relay_push_.transport.size() &&
                (session_relay_push_.transport[pos] == ' ' || session_relay_push_.transport[pos] == '\t')) {
                ++pos;
            }
            int port = 0;
            while (pos < session_relay_push_.transport.size() &&
                session_relay_push_.transport[pos] >= '0' && session_relay_push_.transport[pos] <= '9') {
                port = port * 10 + (session_relay_push_.transport[pos] - '0');
                ++pos;
            }
            if (port > 0 && port <= 65535) {
                session_relay_push_.server_rtp_port = port;
            }
        }
    }

    logInfo("Target RTP port: " + std::to_string(session_relay_push_.server_rtp_port));
    if (session_relay_push_.server_rtp_port == 0) {
        setError("Missing/invalid server_port in target Transport: " + session_relay_push_.transport, 3010);
        return false;
    }

    // 保存目标RTP地址信息（用于UDP推流）
    target_rtp_host_ = relay_push_url.host;
    logInfo("Target RTP host: " + target_rtp_host_);


    // 发送RECORD到目标
    if (!rtspRecord(*session_relay_push_.conn_, session_relay_push_.server_url_, session_relay_push_.rtsp_session_id_)) {
        session_relay_push_.state_ = SESSION_STATE::SESSION_ERROR;
        setError("RECORD failed", 3009);
        return false;
    }

	LOG("[keyinfo][StreamNode]tag=%s,stream forward success,pushToUrl:%s", config_.tag.c_str(), session_relay_push_.server_url_.c_str());
	session_relay_push_.state_ = SESSION_STATE::SESSION_STREAMING;
	session_relay_push_.open_time_ = std::chrono::system_clock::now();

    return true;
}

bool StreamNode::openOriginPullSession() {
    setState(SESSION_STATE::SESSION_CONNECTING, "Connecting to source");

    // 解析源URL
    URLComponents src_url;
    if (!URLComponents::parse(session_origin_pull_.server_url_, src_url)) {
        setError("Invalid source URL format", 2001);
        return false;
    }

    // 连接到源服务器
    session_origin_pull_.conn_ = std::make_unique<Connection>();
    if (!session_origin_pull_.conn_->connect(src_url.host, src_url.port)) {
        setError("Failed to connect to source server: " + src_url.host + ":" + std::to_string(src_url.port), 2002);
        return false;
    }

    LOG("[StreamNode]tag=%s,Connect to source success,%s",config_.tag.c_str(),(src_url.host + ":" + std::to_string(src_url.port)).c_str());

    // 发送DESCRIBE
    std::string sdp;
    if (!rtspDescribe(*session_origin_pull_.conn_, session_origin_pull_.server_url_, sdp, session_origin_pull_.rtsp_session_id_)) {
        setError("DESCRIBE failed", 2003);
        return false;
    }

    // 解析SDP
    if (!parseSDP(sdp, session_origin_pull_, pull_audio_session_)) {
        setError("Failed to parse SDP", 2004);
        return false;
    }

    // 调试：打印收到的 SDP 内容
    std::string sdp_for_log = sdp;
    std::replace(sdp_for_log.begin(), sdp_for_log.end(), '\r', '~');
    std::replace(sdp_for_log.begin(), sdp_for_log.end(), '\n', '~');
    LOG("[StreamNode]tag=%s, sdp received: %s,streamInfo:%s,audioControl:%s",
        config_.tag.c_str(), 
        sdp_for_log.c_str(),
        session_origin_pull_.control_url.c_str(),
        pull_audio_session_.control_url.c_str());

    setState(SESSION_STATE::SESSION_HANDSHAKING, "Source connected");
    setState(SESSION_STATE::SESSION_HANDSHAKING, "Setting up streams");

    // 清理之前的UDP sockets
    closeUDPSockets();

    // 根据拉流模式决定是否创建UDP socket
    bool pullUseUDP = (session_origin_pull_.transport_mode == TransportMode::UDP);

    if (pullUseUDP) {
        // 创建专用的UDP socket用于拉流（接收RTP）
        if (!createUDPPullSocket()) {
            logError("Failed to create UDP pull socket");
            pullUseUDP = false;
        }
    }

    if (pullUseUDP) {
        session_origin_pull_.client_port = std::to_string(session_origin_pull_.client_rtp_port) + "-" + std::to_string(session_origin_pull_.client_rtcp_port);
    }
    else {
        session_origin_pull_.client_port = "0-0";  // TCP模式不需要client_port
    }

    LOG("[StreamNode]tag=%s,SETUP,mode=%s,local rtp/rtcp port=%s",
        config_.tag.c_str(),
        pullUseUDP ? "udp" : "tcp",
        session_origin_pull_.client_port.c_str()
    );

    // 发送SETUP到源
    if (!rtspSetup(*session_origin_pull_.conn_, session_origin_pull_.server_url_, session_origin_pull_.rtsp_session_id_, session_origin_pull_)) {
        setError("SETUP failed for source", 3003);
        return false;
    }

    // 解析传输信息
    std::istringstream transport_stream(session_origin_pull_.transport);
    std::string token;
    while (std::getline(transport_stream, token, ';')) {
        if (token.find("server_port=") != std::string::npos) {
            size_t pos = token.find('=');
            session_origin_pull_.server_port = token.substr(pos + 1);

            // 解析RTP端口
            size_t dash = session_origin_pull_.server_port.find('-');
            if (dash != std::string::npos) {
                session_origin_pull_.server_rtp_port = std::stoi(session_origin_pull_.server_port.substr(0, dash));
            }
        }
        else if (token.find("source=") != std::string::npos) {
            size_t pos = token.find('=');
            session_origin_pull_.remote_host = token.substr(pos + 1);
        }
    }


    LOG("[StreamNode]tag=%s,SETUP success,mode=%s,server port=%s",
        config_.tag.c_str(),
        pullUseUDP ? "udp" : "tcp",
        session_origin_pull_.server_port.c_str()
    );

    // 发送PLAY
    if (!rtspPlay(*session_origin_pull_.conn_, session_origin_pull_.server_url_, session_origin_pull_.rtsp_session_id_)) {
        setError("PLAY failed", 3004);
        return false;
    }

    return true;
}

void StreamNode::teardown() {
    if (session_origin_pull_.conn_ && !session_origin_pull_.rtsp_session_id_.empty()) {
        rtspTeardown(*session_origin_pull_.conn_, session_origin_pull_.server_url_, session_origin_pull_.rtsp_session_id_);
    }

    if (session_relay_push_.conn_ && !session_relay_push_.rtsp_session_id_.empty()) {
        rtspTeardown(*session_relay_push_.conn_, session_relay_push_.server_url_, session_relay_push_.rtsp_session_id_);
    }

    if (session_origin_pull_.conn_) {
        session_origin_pull_.conn_->disconnect();
    }

    if (session_relay_push_.conn_) {
        session_relay_push_.conn_->disconnect();
    }

    session_origin_pull_.rtsp_session_id_.clear();
    session_relay_push_.rtsp_session_id_.clear();
    target_rtp_host_.clear();

    session_origin_pull_.client_rtp_port = 0;
    session_origin_pull_.client_rtcp_port = 0;
    session_relay_push_.client_rtp_port = 0;
    session_relay_push_.client_rtcp_port = 0;

    // 清除认证信息（保留用户名密码）
    source_auth_.realm.clear();
    source_auth_.nonce.clear();
    source_auth_.authorization_header.clear();

    target_auth_.realm.clear();
    target_auth_.nonce.clear();
    target_auth_.authorization_header.clear();

    // 关闭UDP sockets
    closeUDPSockets();
}

// ============================================================================
// SDP处理函数
// ============================================================================

// 前向声明 base64Decode（定义在 streamNode.cpp 中）
extern std::vector<uint8_t> base64Decode(const std::string& input);

bool StreamNode::parseSDP(const std::string & sdp, STREAM_SESSION & video_info, STREAM_SESSION & audio_info) {
    std::istringstream ss(sdp);
    std::string line;
    STREAM_SESSION* current_info = nullptr;

    while (std::getline(ss, line)) {
        if (line.length() < 2 || line[1] != '=') continue;

        char type = line[0];
        std::string value = line.substr(2, line.length() - 3);

        switch (type) {
        case 'm': {
            std::istringstream mstream(value);
            std::string media_type, port_str, proto, fmt;
            mstream >> media_type >> port_str >> proto >> fmt;

            if (media_type == "video") {
                current_info = &video_info;
                video_info.payload_type = std::stoi(fmt);
            }
            else if (media_type == "audio") {
                current_info = &audio_info;
                audio_info.payload_type = std::stoi(fmt);
            }
            else {
                current_info = nullptr;
            }
            break;
        }

        case 'a':
            if (!current_info) break;

            if (value.find("rtpmap:") == 0) {
                size_t colon = value.find(':');
                size_t space = value.find(' ', colon);
                size_t slash = value.find('/', space);

                if (slash != std::string::npos) {
                    std::string codec_str = value.substr(space + 1, slash - space - 1);
                    current_info->codec = codec_str;

                    std::string rate_str = value.substr(slash + 1);
                    size_t second_slash = rate_str.find('/');
                    if (second_slash != std::string::npos) {
                        rate_str = rate_str.substr(0, second_slash);
                    }
                    current_info->clock_rate = std::stoi(rate_str);
                }
            }
            else if (value.find("fmtp:") == 0) {
                size_t fmtp_start = value.find(' ');
                if (fmtp_start != std::string::npos) {
                    current_info->fmtp = value.substr(fmtp_start + 1);

                    // 解析 sprop-parameter-sets（如果存在），格式类似：sprop-parameter-sets=Z0IAH5WoFAFuQA==,aM48gA==
                    size_t sprop_pos = current_info->fmtp.find("sprop-parameter-sets=");
                    if (sprop_pos != std::string::npos) {
                        size_t start = sprop_pos + strlen("sprop-parameter-sets=");
                        size_t end = current_info->fmtp.find(';', start);
                        std::string sprop = (end == std::string::npos) ? current_info->fmtp.substr(start) : current_info->fmtp.substr(start, end - start);

                        // 去掉可能的空格
                        while (!sprop.empty() && sprop.front() == ' ') sprop.erase(sprop.begin());

                        // sprop 通常为 base64_sps,base64_pps
                        size_t comma = sprop.find(',');
                        if (comma != std::string::npos) {
                            std::string sps_b64 = sprop.substr(0, comma);
                            std::string pps_b64 = sprop.substr(comma + 1);
                            auto sps_dec = base64Decode(sps_b64);
                            auto pps_dec = base64Decode(pps_b64);
                            if (!sps_dec.empty()) current_info->sps = std::move(sps_dec);
                            if (!pps_dec.empty()) current_info->pps = std::move(pps_dec);
                        }
                    }
                }
            }
            else if (value.find("control:") == 0) {
                current_info->control_url = value.substr(8);
            }
            break;
        }
    }

    return true;
}

std::string StreamNode::generateSDP(const STREAM_SESSION & video_info, const STREAM_SESSION & audio_info) {
    std::stringstream sdp;

    sdp << "v=0\r\n"
        << "o=- 0 0 IN IP4 0.0.0.0\r\n"
        << "s=RTSP Relay Stream\r\n"
        << "c=IN IP4 0.0.0.0\r\n"
        << "t=0 0\r\n"
        << "a=control:*\r\n";

    if (video_info.payload_type > 0) {
        sdp << "m=video 0 RTP/AVP " << video_info.payload_type << "\r\n"
            << "a=rtpmap:" << video_info.payload_type << " "
            << video_info.codec << "/" << video_info.clock_rate << "\r\n";

        if (!video_info.fmtp.empty()) {
            sdp << "a=fmtp:" << video_info.payload_type << " " << video_info.fmtp << "\r\n";
        }

        sdp << "a=control:" << video_info.control_url << "\r\n";
    }

    return sdp.str();
}
