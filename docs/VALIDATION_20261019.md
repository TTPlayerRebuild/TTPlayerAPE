# Commit 兼容修复与 SDK 13.30 验证

日期：2026-10-19。以下为本次重新执行的本机 Windows 11 结果；历史 XP／Win7 宿主矩阵见 `VALIDATION.md`，不代替本次新产物的虚拟机实测。

## 改动

- Encoder `Finish` 的有标签／无标签两条路径接受直接写入流的 `E_NOTIMPL`、`STG_E_INVALIDFUNCTION`。
- `STG_E_MEDIUMFULL`、`STG_E_WRITEFAULT`、`E_ACCESSDENIED`、`E_UNEXPECTED` 继续原样返回；重复 `Finish` 和 `Release` 不重复提交或写入。
- Monkey's Audio SDK 从 13.27 升至官方 [13.30](https://www.monkeysaudio.com/versionhistory.html)，接口版本仍为 17。
- 固定官方 ZIP `MAC_1330_SDK.zip`，SHA-256 `9f74ffc0300d0b664e0530186d543e060f50e8776ae9325ec7b3f8fb1eb3f230`。
- SDK 的 `StartEx` 仍没有传播底层 `Start` 结果，因此继续保留现有补丁。对官方归档中的 CRCRLF 先规范化，再严格匹配补丁；不修改用户媒体或原始旧 DLL。
- BSD 三条款许可和 RSA MD5 声明与现有仓库副本一致，均保留于仓库；SDK 源码仍只下载到构建目录。

## 产物

- `build/Release/ttp_ape.dll`：239,616 字节，文件／产品版本 `2026.10.19`。
- SHA-256：`674e8bc08e256156a307c8c01bb516279116b6112a66b67a852e163dd064ef7d`。
- `ttp_ape-2026.10.19.zip`：130,945 字节；仅 `AddIn/ttp_ape.dll` 与 `SHA256SUMS.txt`。
- 静态 XP／Win7 导入检查通过：x86，子系统 5.01，5 个系统 DLL、109 项导入，无额外 VC 运行库依赖；这不等于已运行 XP／Win7。

## 回归结果

1. 既有编解码／接口矩阵 **70／70 通过**。覆盖整数 8/16/24/32 位、float32/64 输入、六声道、旧插件 4110 输出、五档压缩、SDK与旧插件交叉解码、APL及中文路径、错误后缀、定位、碎片读、小缓冲、损坏头、读写故障和标签／封面转发。
2. 新增受控 Commit 矩阵 **14／14 通过**：有标签／无标签两种状态分别测试正常、未实现、不支持该操作、磁盘已满、写故障、拒绝访问及一般状态错误。逐项检查精确 HRESULT、只提交一次、重复 Finish 和释放不重试。
3. 14 份编码结果均重新打开并解码，与输入的 65,536 字节整数 PCM 完全一致；编码文件的 SHA-256 也全部相同。这证明未实现刷新不应被误报为编码失败；真实刷新错误依旧上报，即使缓冲写入数据已经完整。
4. XP 与 Win7 各运行同一 14 项 Commit 矩阵，均为 0 个失败；各自 14 个输出 APE 的 SHA-256 与 Win11 完全相同。记录保存在私有测试目录的 `xp-common-hashes.txt`、`win7-common-hashes.txt`。

新 Commit 测试的标签服务为受控 ABI 桩，验证的是调用顺序与两条路径，不宣称替代原版／重建版真实属性窗口的标签持久化测试。损坏媒体、完整历史 APE 版本及极端长文件仍不在本次新增覆盖内。

测试代码、样本、日志位于本机 `rebuild/tests/audio_plugins_fix_20261019/ape`；不提交插件仓库，不加入 Action 或发行包。原始 `AddIn/ttp_ape.dll` 未修改。
