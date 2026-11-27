# LuoboDrop

> **MT7925 实验性适配说明：** 本源码包含 MT7925 适配，但目前只完成编译和固件无关检查。Windows 拒绝了本地测试签名证书，驱动尚未在 MT7925 实机加载验证，因此它不是可用的驱动发行版。下方已验证环境和测速数据来自原 MT7922 开发版本，不代表 MT7925 适配结果。源码包不含驱动二进制、测试证书或固件镜像。

LuoboDrop 是一个 Windows 11 实验项目。它用自研 WiFiCx 驱动让一张 MediaTek MT7922 网卡同时维持普通 Wi‑Fi 和 Apple Wireless Direct Link（AWDL），实现 Windows 与 iPad 原生隔空投送双向收发，并提供 Windows 桌面小窗口。

## 社区源码预览

LuoboDrop 免费提供源码，欢迎研究和贡献。这个预览版的普通 Wi‑Fi 环形队列使用 WDK 公开接口，已通过本地编译和模拟队列测试，待实机验证。

欢迎评审收发队列索引、取消与重启行为，以及普通 Wi‑Fi 和 AWDL 共存。构建或复测请参考[构建与恢复](BUILD.md)和[已知问题](KNOWN_ISSUES.md)。

## 已验证环境

参考机器为 ROG Strix G16 G614FR，配备 MT7922，运行 Windows 11 家庭版 x64，系统构建号 26300.9974。对端是一台 iPad，测速记录缺少确切型号与 iPadOS 版本。其他网卡、iPhone、Mac、Windows 版本和信道组合还有待验证。

2026 年 11 月 23 日，两方向均传输同一大小的 57,393,124 字节视频。普通 Wi‑Fi 和 AWDL 共享 5 GHz 主信道 149，带宽 80 MHz。

| 方向 | 正文传输耗时 | 正文平均速度 | 传输期间 Wi‑Fi 探测 |
| --- | ---: | ---: | --- |
| Windows → iPad | 1.495 秒 | 38.40 MB/s | 网关 6/6，HTTPS 6/6 |
| iPad → Windows | 1.494 秒 | 38.43 MB/s | 网关 39/40，HTTPS 40/40 |

速度按十进制 MB 计算，计时从正文传输开始到正文完成。成绩来自实机开发包 0.70.0.84。本源码快照须单独复测才能引用同等成绩。

## 文档

- [构建与恢复](BUILD.md)
- [架构](ARCHITECTURE.md)
- [已知问题](KNOWN_ISSUES.md)
- [第三方声明](THIRD_PARTY_NOTICES.md)
- [安全](SECURITY.md)
- [English](README.md)

## 许可证

LuoboDrop 有权授权且纳入组合驱动和收发组件的项目代码采用[GNU GPL 第 3 版或更新版本](LICENSE)（SPDX：`GPL-3.0-or-later`）。第三方文件保留各自的版权声明与许可条款，详见[第三方声明](THIRD_PARTY_NOTICES.md)。

LuoboDrop 是独立项目，与 Apple、MediaTek、ASUS 或 Microsoft 无官方关联。AirDrop 是 Apple Inc. 的商标。
