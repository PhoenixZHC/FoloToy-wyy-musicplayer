#!/usr/bin/env python3
"""请求回归使用隔离的本地替身，不连接真实账号。"""
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch
from unittest.mock import Mock

SOURCE = Path(__file__).resolve().parents[1] / "tools/qqmusic_login_probe.py"
SPEC = importlib.util.spec_from_file_location("qqmusic_probe", SOURCE)
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


class Response:
    def __init__(self, value):
        self.value = value

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def read(self, limit):
        return json.dumps(self.value).encode()[:limit]


class LoginRequestTests(unittest.TestCase):
    def test_qr_and_exchange_use_distinct_client_profiles(self):
        response = Response({"code": 0, "req_0": {"code": 0, "data": {}}})
        with patch.object(probe.urllib.request, "urlopen", return_value=response) as send:
            probe.request("CreateQRCode", {})
            qr = json.loads(send.call_args.args[0].data)
            probe.request("Login", {"musicid": 123, "qrCodeID": "test", "token": "test"}, "6")
            exchange = json.loads(send.call_args.args[0].data)
        self.assertEqual((qr["comm"]["ct"], qr["comm"]["cv"]), ("23", "0"))
        self.assertEqual((exchange["comm"]["ct"], exchange["comm"]["cv"]), ("11", "20090008"))
        self.assertEqual(exchange["comm"]["tmeLoginType"], "6")
        self.assertEqual(exchange["comm"]["tmeAppID"], "qqmusic")

    def test_rejection_is_not_success_or_response_dump(self):
        response = Response({"code": 0, "req_0": {"code": 1000, "data": {"private": "must-not-be-logged"}}})
        with patch.object(probe.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(ValueError, "codes 0/1000") as error:
                probe.request("Login", {})
        self.assertNotIn("must-not-be-logged", str(error.exception))

    def test_data_must_be_object(self):
        response = Response({"code": 0, "req_0": {"code": 0, "data": None}})
        with patch.object(probe.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(ValueError, "no data object"):
                probe.request("Login", {})

    def test_authenticated_requests_use_app_credentials(self):
        response = Response({"code": 0, "req_0": {"code": 0, "data": {}}})
        with patch.object(probe.urllib.request, "urlopen", return_value=response) as send:
            probe.request("GetPlaylistByUin", {}, module="music.musicasset.PlaylistBaseRead",
                          credential={"musicid": 123, "musickey": "local-test", "loginType": 6})
        body = json.loads(send.call_args.args[0].data)
        self.assertEqual(body["comm"]["authst"], "local-test")
        self.assertEqual(body["comm"]["qq"], "123")
        self.assertEqual(body["req_0"]["module"], "music.musicasset.PlaylistBaseRead")

    def test_media_url_rejects_foreign_hosts_and_preview_formats(self):
        self.assertEqual(probe.media_url("http://ws.stream.qqmusic.qq.com/", "M500test.mp3?vkey=test"),
                         "https://ws.stream.qqmusic.qq.com/M500test.mp3?vkey=test")
        for path in ("https://example.org/M500test.mp3", "//qq.com.example.org/M500x.mp3",
                     "C400test.m4a", "https://user@ws.stream.qqmusic.qq.com/M500x.mp3"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                probe.media_url("https://ws.stream.qqmusic.qq.com/", path)

    def test_song_discovery_preserves_opaque_identifiers(self):
        song = {"mid": "00opaqueMID", "file": {"media_mid": "00mediaMID"}, "type": 0}
        self.assertEqual(probe.find_songs({"body": {"songlist": [song, {"id": 1}]}}), [song])

    def test_development_session_keeps_credentials_on_verification_failure(self):
        credential = {"musicid": 123, "musickey": "private-local-test"}
        retry = Mock()
        commands = io.StringIO("retry\nquit\n")
        output = io.StringIO()
        with patch.object(probe, "verify_music", side_effect=ValueError("private-local-test")), \
                patch.object(probe.sys, "stdin", commands), \
                patch.object(probe.select, "select", return_value=([commands], [], [])), \
                patch.object(probe.runpy, "run_path", return_value={"verify_music": retry}), \
                patch("sys.stdout", output):
            probe.verify_session(credential, True)
        retry.assert_called_once_with(credential)
        self.assertNotIn("private-local-test", output.getvalue())

    def test_normal_verification_failure_is_not_suppressed(self):
        with patch.object(probe, "verify_music", side_effect=ValueError("failed")):
            with self.assertRaises(ValueError):
                probe.verify_session({}, False)

    def test_app_vkey_uses_separate_cdn_dispatch_when_sip_is_empty(self):
        song = {"mid": "opaque-test", "type": 0, "file": {"media_mid": "media-test", "size_128mp3": 5000}}
        credential = {"musicid": 123, "musickey": "isolated-private-key", "encryptUin": "test", "_refresh_probe_done": True}
        media = Mock()
        media.status = 206
        media.headers = {"Content-Range": "bytes 0-4095/5000"}
        media.read.return_value = b"ID3" + b"\0" * 4093
        response = Mock()
        response.__enter__ = Mock(return_value=media)
        response.__exit__ = Mock(return_value=None)
        opener = Mock()
        opener.open.return_value = response
        replies = [{}, {}, {}, {"songlist": [song]},
                   {"retcode": 0, "sip": ["http://aqqmusic.tc.qq.com/"]},
                   {"sip": [], "midurlinfo": [{"result": 0, "purl": "M500media-test.mp3?vkey=isolated-private-key"}]}]
        output = io.StringIO()
        with patch.object(probe, "request", side_effect=replies) as api, \
                patch.object(probe.urllib.request, "build_opener", return_value=opener), patch("sys.stdout", output):
            probe.verify_music(credential)
        self.assertIn("GetCdnDispatch", [call.args[0] for call in api.call_args_list])
        self.assertEqual(opener.open.call_args.args[0].full_url,
                         "https://aqqmusic.tc.qq.com/M500media-test.mp3?vkey=isolated-private-key")
        self.assertIn("total length matches", output.getvalue())
        self.assertNotIn("isolated-private-key", output.getvalue())


if __name__ == "__main__":
    unittest.main()
