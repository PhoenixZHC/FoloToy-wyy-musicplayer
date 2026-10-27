<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 方案 B 原生预览

主机程序链接生产 LVGL 渲染器和生成字库。固定电量、歌曲和二维码是隔离测试夹具，不是生产数据；预览码不能登录。

从固件目录运行 cmake -S tests/ui_preview -B /tmp/scheme-b-preview，再运行 cmake --build /tmp/scheme-b-preview。预览命令 /tmp/scheme-b-preview/scheme_b_preview player day /tmp/player.ppm。需要本机 C/C++ 编译器，无需显示服务器。

图集包含 49 个页面的日夜模式（98 次渲染），覆盖歌词、封面、超长文本、简体/名字用字和英文字母；渲染前检查每个标签的缺字。输出 240×320 RGB PPM，不能替代网络、按键、电量、音频、性能或容量验收。

字体生成 tools/generate_scheme_b_fonts.py --full 需要 Pillow、FreeType 共享库和脚本指定的 Noto CJK、Nimbus Roman。每个动态字号含 6774 汉字（完整 GB2312 加 11 个姓名用字）、52 个英文大小写字母、数字及界面标点，不额外扩充繁体和其他语种。清单记录源哈希、字符数和位图分块；当前各字号仅一块位图，生成器仍支持在 1 MiB 上限分块为静态 fallback 链，兼容 LVGL 小描述符。保持原行高基线，逐字压缩回读比较。许可证位于 main/fonts/font-licenses，沿用 font_24 符号实际渲染 25px。使用带 Pillow 的 Python 环境运行 python3 tests/test_scheme_b_fonts.py 执行 3 个专项测试。

使用 python3 tests/ui_preview/render_gallery.py /tmp/scheme-b-preview/scheme_b_preview /tmp/scheme-b-gallery.png 重现图集，需要 Pillow。渲染或标签缺字失败会返回非零退出码。

配置时添加 -DPREVIEW_DEVICE_HEAP=ON 可启用接近 ESP32-C3 的受限 LVGL 堆，检查高密度页面是否出现控件分配回归。主机和设备的指针宽度不同，剩余字节不可直接比较。

生成器同时按实际动态字符集更新仓库根目录的 server/src/generated/deviceCharset.ts。仓库校验确保服务端每个非 ASCII 字符在全部动态字体中都有字形；缩减语种时不得绕过此契约。
