# ttp_ape

供千千静听原版 5.7.9 与 TTPlayer Rebuild 使用的独立 x86 APE 插件。
接口根据原版 `ttp_ape.dll` 的伪代码、导出、虚表和宿主调用链恢复；编解码核心使用固定版本的 Monkey's Audio SDK 13.27。

## 功能

- APE / MAC 播放、格式信息、定位；兼容原插件生成的特殊 4110 文件。
- 增加 APL 音频片段读取、32 位整数和 float32 APE、多声道。
- 恢复 Reader、Encoder 两个工厂以及原版的三个导出。
- 恢复五档压缩设置、原宿主 float64 编码输入；修复整数输入被当作 16 位的错误。
- 自动精度：整数保持输入位深，浮点转为 16 位；可选 16／24／32 位整数输出。
- 标签和封面通过宿主 `CreateStdContent` 接口读写，保持原版宿主的行为和保存策略。
- 同一个 DLL 适用于 XP SP3、Win7 和新系统；CPU 需要 SSE2，无需另外安装 VC 运行库。

详细说明见 [重建与兼容性说明](docs/REBUILD.md)、[验证记录](docs/VALIDATION.md)。

## 构建

需要 Visual Studio 2026 C++ Build Tools（或包含 C++ 工具的 Visual Studio）、Windows SDK、CMake 3.24+、Python 3、PowerShell。

```powershell
./build.ps1 -Package
# 明确指定日期发行版本；同一天的后续发行使用 p1、p2……
./build.ps1 -Package -PackageVersion 2026.10.04
```

默认使用 `Visual Studio 18 2026`、Win32、Release，优先优化 DLL 体积。
依赖在构建时下载并校验 SHA-256：Monkey's Audio SDK 13.27、VC-LTL 5.3.1、YY-Thunks 1.2.2。
SDK 源码只存在于忽略的构建目录；仓库保留下载脚本、确定性补丁和许可证。
离线缓存可通过 `-CMakeArguments '-DTTP_APE_SDK_ARCHIVE=绝对路径/MAC_1327_SDK.zip'` 指定，仍会检查固定哈希。

产物：

- `build/Release/ttp_ape.dll`
- `build/Release/ttp_ape-版本号.zip`
- `build/Release/SHA256SUMS.txt`

ZIP 中只有 `AddIn/ttp_ape.dll` 和 `SHA256SUMS.txt`。
关闭播放器后，将 DLL 放入其 `AddIn` 目录；更新前保留旧 DLL 备份。
本项目独立构建，不依赖 rebuild 的源码、构建目录或测试。

## Actions

手动运行 `.github/workflows/build.yml`；选中 **Release a Version** 时分配北京时间日期版本，并发布 ZIP 与校验文件。
文件版本与发行版本相同。工作流不运行或上传测试。
本地测试和原二进制分析材料存放在相邻工作区 `rebuild/tests/ape_rebuild` 与 `rebuild/tests/ape_analysis`，不属于该插件仓库。

## 许可证

适配层使用 [MIT](LICENSE)。第三方声明保存在仓库，未嵌入 DLL：

- [Monkey's Audio BSD 三条款](third_party/monkeys_audio/LICENSE.txt)
- [RSA Data Security, Inc. MD5 Message-Digest Algorithm](third_party/monkeys_audio/MD5-LICENSE.txt)
- [VC-LTL](docs/licenses/VC-LTL-LICENSE.txt)
- [YY-Thunks](docs/licenses/YY-Thunks-LICENSE.txt)

发行说明附有这些声明的全文，作为随二进制提供的材料。
