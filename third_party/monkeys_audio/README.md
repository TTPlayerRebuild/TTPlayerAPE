# 固定依赖

- 上游：[Monkey's Audio 开发者页面](https://www.monkeysaudio.com/developers.html)
- 版本：13.30，接口版本 17。
- 下载：`https://monkeysaudio.com/files/MAC_1330_SDK.zip`
- SHA-256：`9f74ffc0300d0b664e0530186d543e060f50e8776ae9325ec7b3f8fb1eb3f230`

源码不提交。`cmake/prepare_mac.py` 校验原始归档后，解压到构建目录并执行以下局部修改：

1. 禁用静态核心的 `__declspec(dllexport)`，DLL 仅保留原插件三个导出。
2. 修复 `CAPECompress::StartEx` 忽略底层 `Start` 返回值的问题；失败时立即返回。
3. 为 SDK 的 `WIN32_LEAN_AND_MEAN` 加条件定义，避免宏重定义。

13.30 官方 ZIP 的相关源码使用 CRCRLF，补丁前统一换行并检查匹配次数；不会跳过失效补丁继续构建。
13.27 → 13.30 的接口版本仍为 17。官方更新涉及内部整理、大端构建及历史兼容条件编译；
不据此承诺压缩率、音质或速度提升。原始 `License.txt` 和 `MD5.h` 中的 RSA 声明均与仓库现有副本相同。

构建排除依赖 MFC 的 `Source/MACDll/MACDll.cpp` 包装层，保留 MAC 核心及历史解码实现。
使用 XP 平台定义及独立的 CPU 运行时探测，不为整个项目启用 AVX2／AVX512。

`LICENSE.txt` 来自原始归档；`MD5-LICENSE.txt` 保留 `Source/MACLib/MD5.h` 的 RSA 版权声明。
