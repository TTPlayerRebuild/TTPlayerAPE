# 固定依赖

- 上游：[Monkey's Audio 开发者页面](https://www.monkeysaudio.com/developers.html)
- 版本：13.27，接口版本 17。
- 下载：`https://www.monkeysaudio.com/files/MAC_1327_SDK.zip`
- SHA-256：`c47c6b36f6a7bd50d990f2eb36a70915c0074a7b9634be396c94464905e76686`

源码不提交。`cmake/prepare_mac.py` 校验原始归档后，解压到构建目录并执行以下局部修改：

1. 禁用静态核心的 `__declspec(dllexport)`，DLL 仅保留原插件三个导出。
2. 修复 `CAPECompress::StartEx` 忽略底层 `Start` 返回值的问题；失败时立即返回。
3. 为 SDK 的 `WIN32_LEAN_AND_MEAN` 加条件定义，避免宏重定义。

构建排除依赖 MFC 的 `Source/MACDll/MACDll.cpp` 包装层，保留 MAC 核心及历史解码实现。
使用 XP 平台定义及独立的 CPU 运行时探测，不为整个项目启用 AVX2／AVX512。

`LICENSE.txt` 来自原始归档；`MD5-LICENSE.txt` 保留 `Source/MACLib/MD5.h` 的 RSA 版权声明。
