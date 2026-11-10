#!/usr/bin/env python3
"""直连QQ音乐的开发验证工具；凭据仅保存在进程内，不打印或写盘。

通过ctypes运行与固件相同的独立MQTT编解码器。默认只验证握手；--scan
才把临时二维码保存到指定的私有目录，供用户用QQ音乐App确认登录。
"""
import argparse
import base64
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import secrets
import select
import socket
import ssl
import struct
import sys
import time
import urllib.request
import urllib.parse

MAX_PACKET = 8192


class Packet(C.Structure):
    _fields_ = [("type", C.c_uint8), ("reason", C.c_uint8), ("qos", C.c_uint8),
                ("packet_id", C.c_uint16), ("server_keepalive", C.c_uint16),
                ("has_server_keepalive", C.c_bool), ("server_reference", C.c_char * 256),
                ("event", C.c_char * 32), ("topic", C.c_void_p), ("payload", C.c_void_p),
                ("topic_length", C.c_size_t), ("payload_length", C.c_size_t),
                ("consumed", C.c_size_t)]


def request(method, params, login_type=None, *, module="music.login.LoginServer", credential=None):
    comm = {"ct": "23", "cv": "0", "format": "json"}
    if method == "Login" or credential is not None:
        # 创建二维码的特殊comm不能沿用到App凭据交换。
        comm = {"ct": "11", "cv": "20090008", "v": "20090008", "tmeAppID": "qqmusic", "format": "json"}
    if login_type is not None:
        comm["tmeLoginType"] = login_type
    if credential is not None:
        comm.update(qq=str(credential["musicid"]), authst=credential["musickey"],
                    tmeLoginType=credential.get("loginType", 6))
    body = json.dumps({"comm": comm, "req_0": {"module": module,
                       "method": method, "param": params}}, separators=(",", ":")).encode()
    req = urllib.request.Request("https://u.y.qq.com/cgi-bin/musicu.fcg", data=body,
          headers={"Content-Type": "application/json", "Referer": "https://y.qq.com/",
                   "User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=20) as response:
        raw = response.read(131073)
        if len(raw) > 131072:
            raise ValueError("QQ response exceeds limit")
        obj = json.loads(raw)
    part = obj.get("req_0", {})
    if obj.get("code") != 0 or part.get("code") != 0:
        raise ValueError(f"QQ {method} rejected: codes {obj.get('code')}/{part.get('code')}")
    if not isinstance(part.get("data"), dict):
        raise ValueError("QQ response has no data object")
    return part["data"]


def find_songs(value):
    """只识别含真实MID及媒体文件字段的歌曲对象，不制造歌曲编号。"""
    found = []
    if isinstance(value, dict):
        if isinstance(value.get("mid"), str) and isinstance(value.get("file"), dict):
            found.append(value)
        else:
            for child in value.values():
                found.extend(find_songs(child))
    elif isinstance(value, list):
        for child in value:
            found.extend(find_songs(child))
    return found


def media_url(base, path):
    # 凭据化URL只能发到QQ音乐CDN；不接受API返回的任意外站或跳转。
    url = urllib.parse.urljoin(base, path)
    parsed = urllib.parse.urlsplit(url)
    host = parsed.hostname or ""
    if (parsed.scheme not in {"http", "https"} or parsed.username or parsed.password or
            not (host.endswith(".qq.com") or host.endswith(".qqmusic.qq.com")) or
            not parsed.path.rsplit("/", 1)[-1].startswith("M500") or
            not parsed.path.endswith(".mp3")):
        raise ValueError("QQ media is not an approved MP3 CDN URL")
    return urllib.parse.urlunsplit(parsed._replace(scheme="https"))


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError("QQ media redirect requires separate validation")


def verify_music(credential):
    """同一进程内验证真实账号与媒体；不保存响应、曲名或带凭据的URL。"""
    def api(module, method, params):
        return request(method, params, module=module, credential=credential)

    api("music.UserInfo.userInfoServer", "GetLoginUserInfo", {})
    print("QQ saved-credential check: PASS (in memory; no disk persistence tested)", flush=True)
    if not credential.get("_refresh_probe_done"):
        refresh_params = {name: credential.get(name, "") for name in
                          ("openid", "access_token", "refresh_token", "musickey", "unionid", "refresh_key")}
        refresh_params.update(loginMode=2, musicid=credential["musicid"],
                              str_musicid=credential.get("str_musicid") or str(credential["musicid"]),
                              expired_in=credential.get("expired_at", 0))
        refreshed = api("music.login.LoginServer", "Login", refresh_params)
        if not refreshed.get("musickey") or str(refreshed.get("musicid")) != str(credential["musicid"]):
            raise ValueError("QQ credential refresh returned an inconsistent account")
        credential.clear()
        credential.update(refreshed)
        credential["_refresh_probe_done"] = True
        api("music.UserInfo.userInfoServer", "GetLoginUserInfo", {})
        print("QQ credential refresh and recheck: PASS (memory only)", flush=True)

    created = api("music.musicasset.PlaylistBaseRead", "GetPlaylistByUin",
                  {"uin": str(credential["musicid"])})
    print("QQ created playlists: PASS", flush=True)
    encrypted_uin = credential.get("encryptUin")
    if not encrypted_uin:
        raise ValueError("QQ credential lacks encrypted account identifier")
    favorites = api("music.musicasset.PlaylistFavRead", "CgiGetPlaylistFavInfo",
        {"uin": encrypted_uin, "offset": 0, "size": 8})
    print("QQ favorite playlists page: PASS", flush=True)
    # 喜欢的歌曲由真实账号目录201提供；即使没有自建歌单也能验证歌曲分页。
    detail = api("music.srfDissInfo.DissInfo", "CgiGetDiss",
                 {"disstid": 0, "dirid": 201, "song_begin": 0, "song_num": 8,
                  "tag": False, "userinfo": False, "orderlist": True,
                  "enc_host_uin": encrypted_uin})
    songs = find_songs(detail)
    if not songs:
        raise ValueError("QQ liked-songs page is empty or has an unsupported schema")
    if len(songs) > 8:
        raise ValueError("QQ song page exceeds requested bound")
    print(f"QQ songs page: PASS; received {len(songs)} real tracks", flush=True)
    dispatch = api("music.audioCdnDispatch.cdnDispatch", "GetCdnDispatch",
                   {"guid": str(secrets.randbelow(10**10)), "uid": "0", "use_new_domain": 1, "use_ipv6": 0})
    cdn_bases = dispatch.get("sip", [])
    if dispatch.get("retcode") != 0 or not isinstance(cdn_bases, list) or not cdn_bases:
        raise ValueError("QQ CDN dispatch returned no usable base URL")
    print(f"QQ CDN dispatch: PASS; bases={len(cdn_bases)}", flush=True)
    available = 0
    missing_media = missing_size = empty_urls = 0
    for song in songs:
        media_mid = song["file"].get("media_mid")
        size = song["file"].get("size_128mp3")
        if not isinstance(media_mid, str) or not media_mid:
            missing_media += 1
            continue
        if not isinstance(size, int) or size <= 0:
            missing_size += 1
            continue
        result = api("music.vkey.GetVkey", "UrlGetVkey",
                     {"uin": str(credential["musicid"]), "guid": str(secrets.randbelow(10**10)),
                      "filename": ["M500" + media_mid + ".mp3"], "songmid": [song["mid"]],
                      "songtype": [song.get("type", 0)], "ctx": 0})
        entries = result.get("midurlinfo", [])
        # App取流只保证授权路径，CDN根地址来自独立调度接口。
        bases = result.get("sip") or cdn_bases
        if not entries or not bases or not entries[0].get("purl"):
            empty_urls += 1
            # 只记录结构和状态码，不记录账号、曲目、凭据或URL。
            statuses = [item.get("result") for item in entries if isinstance(item, dict)]
            statuses = [code for code in statuses if isinstance(code, int)]
            print(f"QQ vkey diagnostic: entries={len(entries)}, bases={len(bases)}, results={statuses}", flush=True)
            continue
        url = media_url(bases[0], entries[0]["purl"])
        opener = urllib.request.build_opener(NoRedirect())
        req = urllib.request.Request(url, headers={"Range": "bytes=0-4095", "Referer": "https://y.qq.com/"})
        with opener.open(req, timeout=20) as response:
            header = response.headers.get("Content-Range", "")
            data = response.read(4096)
            match = re.fullmatch(r"bytes 0-\d+/(\d+)", header)
            if response.status != 206 or not match or int(match[1]) != size:
                raise ValueError("QQ CDN full-file length could not be verified")
            mpeg = any(data[i] == 255 and data[i + 1] & 0xE0 == 0xE0 for i in range(len(data) - 1))
            if not (data.startswith(b"ID3") or mpeg):
                raise ValueError("QQ CDN response is not recognizable MP3")
        available += 1
        print("QQ MP3 CDN range: PASS; total length matches full MP3 metadata (not decoded or played)", flush=True)
        break
    if not available:
        print(f"QQ media diagnostic: missing_media={missing_media}, missing_size={missing_size}, empty_urls={empty_urls}", flush=True)
        raise ValueError("No full MP3 available in this page; permissions or more tracks need validation")


def verify_session(credential, keep_session=False):
    """开发时最多保留15分钟内存会话，允许修复验证器后重试，避免重复扫码。"""
    deadline = time.monotonic() + 900
    verifier = verify_music
    while True:
        try:
            verifier(credential)
        except Exception as error:
            if not keep_session:
                raise
            print("Music verification failed: " + type(error).__name__, flush=True)
        if not keep_session:
            return
        print("Memory session retained; enter retry to reload verifier, or quit (15-minute limit)", flush=True)
        # 只接受固定命令；不提供带凭据的任意表达式执行接口。
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([sys.stdin], [], [], max(0, remaining))[0]:
                return
            command = sys.stdin.readline().strip()
            if command == "quit" or not command:
                return
            if command == "retry":
                verifier = runpy.run_path(str(Path(__file__).resolve()))["verify_music"]
                break


class WebSocket:
    def __init__(self, path):
        self.sock = None
        self.buffer = bytearray()
        self.fragmented = False
        raw = socket.create_connection(("mu.y.qq.com", 443), timeout=15)
        try:
            self.sock = ssl.create_default_context().wrap_socket(raw, server_hostname="mu.y.qq.com")
            self.sock.settimeout(15)
            key = base64.b64encode(secrets.token_bytes(16)).decode()
            handshake = (f"GET {path} HTTP/1.1\r\nHost: mu.y.qq.com\r\nUpgrade: websocket\r\n"
                         f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                         "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: mqtt\r\n"
                         "Origin: https://y.qq.com\r\nReferer: https://y.qq.com/\r\n"
                         "User-Agent: Mozilla/5.0\r\n\r\n")
            self.sock.sendall(handshake.encode())
            header = bytearray()
            while not header.endswith(b"\r\n\r\n"):
                if len(header) >= 8192:
                    raise ValueError("WebSocket header too large")
                byte = self.sock.recv(1)
                if not byte:
                    raise ConnectionError("WebSocket header ended early")
                header.extend(byte)
            lines = header.decode("iso-8859-1").split("\r\n")
            if lines[0].split()[1] != "101":
                raise ConnectionError("WebSocket upgrade failed: " + lines[0])
            headers = {}
            for line in lines[1:]:
                if ":" in line:
                    name, value = line.split(":", 1)
                    headers[name.lower()] = value.strip()
            expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
            if (headers.get("sec-websocket-accept") != expected or
                    headers.get("upgrade", "").lower() != "websocket" or
                    headers.get("sec-websocket-protocol") != "mqtt"):
                raise ConnectionError("WebSocket upgrade verification failed")
        except BaseException:
            if self.sock:
                self.sock.close()
            else:
                raw.close()
            raise

    def send(self, payload, opcode=2):
        if len(payload) > MAX_PACKET:
            raise ValueError("WebSocket send exceeds limit")
        mask = secrets.token_bytes(4)
        header = bytes([0x80 | opcode])
        if len(payload) < 126:
            header += bytes([0x80 | len(payload)])
        else:
            header += b"\xfe" + struct.pack("!H", len(payload))
        self.sock.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def fill(self, count):
        while len(self.buffer) < count:
            chunk = self.sock.recv(min(4096, count - len(self.buffer)))
            if not chunk:
                raise ConnectionError("WebSocket closed")
            self.buffer.extend(chunk)

    def receive(self):
        self.fill(2)
        first, second = self.buffer[:2]
        if first & 0x70 or second & 0x80:
            raise ValueError("Invalid WebSocket frame flags")
        opcode = first & 15
        length = second & 127
        header_length = 2
        if length == 126:
            self.fill(4)
            length = struct.unpack("!H", self.buffer[2:4])[0]
            header_length = 4
            if length < 126:
                raise ValueError("Invalid WebSocket frame length")
        elif length == 127:
            self.fill(10)
            length = struct.unpack("!Q", self.buffer[2:10])[0]
            header_length = 10
            if length < 65536:
                raise ValueError("Invalid WebSocket frame length")
        if length > MAX_PACKET or (opcode >= 8 and (length > 125 or not first & 128)):
            raise ValueError("WebSocket frame exceeds limit")
        self.fill(header_length + length)
        data = bytes(self.buffer[header_length:header_length + length])
        del self.buffer[:header_length + length]
        if opcode == 8:
            raise ConnectionError("Server closed WebSocket")
        if opcode == 9:
            self.send(data, 10)
            return b""
        if opcode == 10:
            return b""
        if opcode not in (0, 2):
            raise ValueError("Unsupported WebSocket opcode")
        if (opcode == 0) != self.fragmented:
            raise ValueError("Invalid WebSocket continuation")
        self.fragmented = not bool(first & 128)
        return data

    def close(self):
        self.sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codec", required=True, type=Path)
    parser.add_argument("--scan", action="store_true")
    parser.add_argument("--keep-session", action="store_true", help="retain credentials in memory up to 15 minutes for local retry")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if args.scan and args.output_dir is None:
        parser.error("--scan requires --output-dir for the temporary QR PNG")
    codec = C.CDLL(str(args.codec.resolve()))
    codec.qqmusic_mqtt_connect.argtypes = [C.c_char_p, C.c_char_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
    codec.qqmusic_mqtt_subscribe.argtypes = [C.c_char_p, C.c_uint16, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
    codec.qqmusic_mqtt_parse.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Packet)]
    qr = request("CreateQRCode", {"tmeAppID": "qqmusic", "ct": 11, "cv": 20090008})
    qr_id = str(qr.get("qrcodeID", ""))
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", qr_id):
        raise ValueError("Invalid QR identity")
    expires = qr.get("expiresIn", 180)
    if not isinstance(expires, int) or not 1 <= expires <= 1800:
        raise ValueError("Invalid QR expiry")
    encoded_png = qr.get("qrcode", "")
    if encoded_png.startswith("data:image/png;base64,"):
        encoded_png = encoded_png[len("data:image/png;base64,"):]
    png = base64.b64decode(encoded_png, validate=True)
    if (len(png) < 33 or len(png) > 65536 or png[:8] != b"\x89PNG\r\n\x1a\n" or
            png[12:16] != b"IHDR" or any(n < 1 or n > 1024 for n in struct.unpack("!II", png[16:24]))):
        raise ValueError("Invalid/big QR PNG")
    print(f"CreateQRCode: PASS; PNG bytes={len(png)}; expires={expires}s", flush=True)
    deadline = time.monotonic() + expires
    if args.scan:
        args.output_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        os.chmod(args.output_dir, 0o700)
        path = args.output_dir / ("qqmusic-qr-" + str(int(time.time())) + ".png")
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as file:
            file.write(png)
        print("QR_IMAGE=" + str(path.resolve()), flush=True)

    client_id = str(int(time.time() * 1000)) + str(secrets.randbelow(9000) + 1000)
    path = "/ws/handshake"
    ws = None
    connected = subscribed = False
    redirects = 0
    buffer = bytearray()
    last_ping = time.monotonic()
    keepalive = 45
    try:
        while time.monotonic() < deadline:
            if ws is None:
                ws = WebSocket(path)
                out, n = C.create_string_buffer(2048), C.c_size_t()
                if codec.qqmusic_mqtt_connect(client_id.encode(), qr_id.encode(), out, len(out), C.byref(n)):
                    raise ValueError("MQTT CONNECT encode failed")
                ws.send(out.raw[:n.value])
            if keepalive and time.monotonic() - last_ping >= min(20, keepalive / 2):
                ws.send(b"\xc0\x00")
                last_ping = time.monotonic()
            ws.sock.settimeout(min(1, max(0.1, deadline - time.monotonic())))
            try:
                fragment = ws.receive()
            except socket.timeout:
                continue
            buffer.extend(fragment)
            if len(buffer) > MAX_PACKET:
                raise ValueError("MQTT receive exceeds limit")
            while buffer:
                raw = C.create_string_buffer(bytes(buffer))
                packet = Packet()
                result = codec.qqmusic_mqtt_parse(raw, len(buffer), C.byref(packet))
                if result == 1:
                    break
                if result:
                    raise ValueError(f"MQTT parse rejected ({result})")
                payload = C.string_at(packet.payload, packet.payload_length) if packet.payload_length else b""
                topic = C.string_at(packet.topic, packet.topic_length) if packet.topic_length else b""
                del buffer[:packet.consumed]
                if packet.type == 2:
                    reference = bytes(packet.server_reference).decode()
                    if packet.reason in (0x9c, 0x9d) and reference:
                        if redirects >= 3 or not re.fullmatch(r"[A-Za-z0-9.:\[\]_-]{1,200}", reference):
                            raise ValueError("Invalid/excessive MQTT redirect")
                        redirects += 1
                        path = "/ws/handshake/" + reference
                        ws.close()
                        ws = None
                        buffer.clear()
                        print(f"MQTT redirect {redirects}", flush=True)
                        break
                    if packet.reason:
                        raise ConnectionError(f"MQTT CONNACK rejected: {packet.reason}")
                    connected = True
                    if packet.has_server_keepalive:
                        keepalive = packet.server_keepalive
                    out, n = C.create_string_buffer(1024), C.c_size_t()
                    if codec.qqmusic_mqtt_subscribe(qr_id.encode(), 1, out, len(out), C.byref(n)):
                        raise ValueError("MQTT SUBSCRIBE encode failed")
                    ws.send(out.raw[:n.value])
                    print("MQTT CONNACK: PASS", flush=True)
                elif packet.type == 9:
                    if not connected or packet.packet_id != 1 or packet.reason > 1:
                        raise ConnectionError(f"MQTT SUBACK rejected: {packet.reason}")
                    subscribed = True
                    print("MQTT SUBACK: PASS", flush=True)
                    if not args.scan:
                        return
                    print("Waiting for QQ Music App scan and phone confirmation", flush=True)
                elif packet.type == 3:
                    if not subscribed or topic != ("management.qrcode_login/" + qr_id).encode():
                        raise ValueError("Unexpected MQTT publication")
                    if packet.qos == 1:
                        ws.send(b"\x40\x02" + struct.pack("!H", packet.packet_id))
                    event = bytes(packet.event).decode()
                    if event not in {"scanned", "canceled", "timeout", "loginFailed", "cookies"}:
                        raise ValueError("Unknown login event")
                    print("QQ login event: " + event, flush=True)
                    if event in {"canceled", "timeout", "loginFailed"}:
                        raise ValueError("QQ login ended: " + event)
                    if event == "cookies":
                        message = json.loads(payload)
                        cookies = message.get("cookies", {})
                        uin = cookies.get("qqmusic_uin", {}).get("value")
                        key = cookies.get("qqmusic_key", {}).get("value")
                        if not str(uin).isdigit() or not isinstance(key, str) or not key or len(key) > 2048:
                            raise ValueError("Invalid login token message")
                        credential = request("Login", {"musicid": int(uin), "qrCodeID": qr_id, "token": key}, "6")
                        if not credential.get("musickey") or not credential.get("musicid"):
                            raise ValueError("Login returned no final credentials")
                        print("QQ credential exchange: PASS; credentials retained only in memory", flush=True)
                        ws.close()
                        ws = None
                        verify_session(credential, args.keep_session)
                        return
                elif packet.type == 14:
                    raise ConnectionError(f"MQTT server disconnected: {packet.reason}")
                elif packet.type != 13:
                    raise ValueError("Unexpected MQTT packet")
        raise TimeoutError("QR login expired")
    finally:
        if ws:
            ws.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # 不输出可能含凭据的响应或异常对象。
        print("Probe failed: " + type(error).__name__ + ": " + str(error) if isinstance(error, (ValueError, ConnectionError, TimeoutError)) else "Probe failed: " + type(error).__name__, flush=True)
        raise SystemExit(1)
