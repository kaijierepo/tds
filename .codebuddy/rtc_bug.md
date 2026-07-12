## 问题描述
TDS 作为 WebRTC 服务端（本地文件→WebRTC 桥接），通过 Chrome 浏览器播放视频时画面黑屏，无法正常显示视频。
.\out\tds\ui\app\webrtc-debug-client.js 是仿真浏览器webrtc客户端的nodejs程序,可以正常录制视频
.\out\tds\ui\app\webrtc-debug-client.py 是仿真浏览器webrtc客户端的python程序，也可以正常录制视频。他使用了pylibsrtp,按道理工作逻辑和chrome的libsrtp应该是一样的。

已确认正常的部分
ICE 连接：STUN Binding Request/Response 交互正常，ICE candidate 协商成功，UDP 通道已建立。
DTLS 握手：DTLS 1.2 握手成功完成，加密套件协商为 TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256，SRTP profile 协商为 AES_CM_128_HMAC_SHA1_80。
SRTP 密钥导出：DTLS EXTRACTOR-dtls_srtp 成功导出 60 字节 keying material（client_write_key[16] | server_write_key[16] | client_write_salt[14] | server_write_salt[14]）。
数据包已发送：TDS 服务端通过 SRTP protect 加密 RTP 包并通过 UDP 发出，浏览器端 transport 层统计显示收到了大量 UDP 数据包。
问题核心
浏览器收到 SRTP 数据包后，通过chrome dev版本日志看到，全部解密失败（auth_fail，libsrtp err=7），导致没有任何视频帧被解码渲染。浏览器端 inbound-rtp 统计始终为 0。

当前代码状态
srtp_protect.cpp 中的 auth_key 为"末尾补 0x00"。RFC 3711 §4.3: "the auth key is padded to the right with zeros"。该方式应当为libsrtp的处理方式。
服务端以 is_server=true 调用 initFromDtls，使用 server_write_key + server_write_salt 做 protect（加密发给浏览器的包）。
TDS 自检验证通过：protect 后立即用相同 context 做 unprotect，认证和解密都正确，RTP 数据与原包一致。
关键事实
普通版本 Chrome（非 Dev）从来没有能播放过（之前记忆中的记录有误）。
目前所有 Chrome 版本（普通版、Dev 版）表现一致：DTLS 握手成功，SRTP 包全部 auth_fail。
