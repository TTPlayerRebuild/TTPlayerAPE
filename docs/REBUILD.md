# APE 插件重建说明

## 1. 恢复依据

原始文件：`AddIn/ttp_ape.dll`，x86，116,872 字节。
SHA-256：`e89483dd7ec03b2b5f4dff25403411aff92e8490e7b300c513d28081e24615f0`。
工厂显示 MAC v4.11。原二进制没有被覆盖。

恢复依据包括 Ghidra 伪代码、导出表、虚表、GUID、反汇编和原宿主实际调用。
这是接口及行为兼容的重新实现，编解码核心已换为 13.27；不宣称与旧 DLL 逐字节相同，也不宣称压缩输出文件大小相同。

## 2. 接口与原版对应

| 项目 | 原版位置／行为 | 重建实现 |
|---|---|---|
| AddIn 导出 | `60102CE6`，枚举两个工厂 | `src/plugin.cpp`，Reader 在索引 0，Encoder 在索引 1 |
| Reader Open | `601024D3`，IStream、flags 位 0 | flags=0 仅信息；flags=1 解码，恢复 16 槽接口 |
| Reader Read | `6010284A`，按 block 读取 | 容量向 block 对齐；先清空长度；EOF 和错误分开返回 |
| Seek | `601028E4`，32 位 MulDiv 且忽略返回码 | 内部 64 位块位置；越界到末尾；返回实际毫秒及真实错误 |
| Metadata | `60102927` 起，宿主通用对象 | 保留相同 GUID、槽位及字符串所有权 |
| Reader 析构 | `601023CE` | 先结束解码、释放输入流，再释放封面与标签对象 |
| Encoder OpenStream | `601014C7` | 原 IStream 编码入口及两种 Encoder GUID |
| Encoder Start / Finish | `601015B0` / `601015DE` | 检查 SDK 和 I/O 结果；Finish 只尝试一次 |
| Encoder Write | `601015F4` | 校验有效长度、容量和块对齐；报告已消费的输入字节数 |
| float64 转整数 | `60101C60` | 默认转 16 位；最近偶数舍入、截幅；NaN 为静音 |
| 工厂设置 | 原版五档、`[Compress] Level` | 保留 1000～5000、默认 2000；增加 `OutputBits` |
| 辅助导出 | `_FillWaveFormatEx@16`、`_FillWaveHeader@16` | 原 x86 调用约定、参数、名称和序号 |

原宿主并非只使用 APE Reader：插件枚举 `004C8954` 收集 Encoder，转换窗口 `0047D682` 显示它，`0047D9A4` 打开设置。
转换工作线程 `00412723` 调用 Start、Write、Finish；非 Wave 编码器收到 float64 PCM。因此必须保留 Encoder 和 float64 分支。

## 3. 有意修正的旧行为

- 原整数编码分支把不同位深按 16 位处理，可能改变时长或样本内容。自动模式保持 8／16／24／32 位整数输入精度。
- 原 Read 将损坏文件错误伪装成 EOF，且可能保留上次长度。现在错误返回失败 HRESULT，输出长度为 0。
- 容量不足一个 block 返回 `E_INVALIDARG`，不消耗采样；非整块输入拒绝编码。
- 文件结束位置不再反复读出最后一个 block；Seek 使用 64 位中间值，避免 `0xFFFFFFFF` 毫秒回绕。
- 修正新版 SDK `StartEx` 忽略初始化错误的路径；IStream 短写、读取失败、Commit 失败分别保留 HRESULT。
- Finish、失败后的 Finish、Release 不会重复写入。未完成的编码对象直接释放代表取消，不会假装成功提交。
- 支持 Unicode 文件路径及内容识别；正常 APE 即使扩展名不符也可由 Reader 解码。

## 4. 扩展与边界

Reader 支持新版核心提供的 float32 APE、32 位整数、多声道及旧 4110 文件。
APL 使用独立的有界解析器，支持相对路径、UTF-8（含 BOM）、UTF-16LE BOM、系统 ANSI 文本和尾部 APEv2／ID3v1 标签。
片段范围用 64 位数检查，时长按片段计算。音频从 image 读取，标签仍属于 APL 本身。

头部在交给 SDK 前检查：定位表不超过 16 MiB，帧 PCM 大小不超过 256 MiB，声道、位深、采样率及文件范围必须有效。
这些是有意设置的资源上限；多 GB 文件、极端时长、所有历史 APE 版本和 32 声道最高压缩尚未穷举验证。

编码设置保存在 DLL 同目录 `ttp_ape.ini`：

```ini
[Compress]
Level=2000
OutputBits=0
```

`OutputBits=0` 表示整数保持位深、浮点转 16 位；16／24／32 表示指定整数精度。降低位深会降低精度。
不提供 float64 原样存储；SDK 本身不接受这种 APE 压缩输入。
旧流式接口没有预告总采样数，SDK 因此预留未知长度的定位表，短文件可能明显大于同版 SDK 在已知长度输入下生成的文件。

## 5. 标签与封面保存

按原版动态查找已加载宿主的 `CreateStdContent`，以参数 4 创建通用标签对象，再查询 Thumbnail。
独立加载 DLL、没有该宿主导出时，Reader 的 Metadata / Thumbnail QI 返回 `E_NOINTERFACE`；音频编解码仍可运行。
标签格式、多值策略、封面格式及数量上限由宿主实现决定，插件不绕过这些规则。

实际宿主测试发现并修正了重要顺序问题：原标签对象在最后 Release 时重新打开文件保存；必须先释放读取锁。
Encoder 在音频 Finish 后才把暂存标签送到宿主，避免较早写入的标签被编码头覆盖；随后 Commit、释放输出流，再释放标签对象。

旧接口仍有不能由插件彻底解决的限制：

1. 原宿主的标签对象在 Release 时保存，没有 HRESULT 返回值。成功 Set 不能证明最后写盘成功。
2. 原播放器 `00412897` 调用 Finish 后忽略 HRESULT。插件已返回真实编码错误，但不能改变未修改的旧 EXE 的错误提示逻辑。
3. 重建版宿主能使用 Set / Commit 的失败结果。这里没有另加不完整的“可选提交”接口，也未改变其宿主标签策略。

## 6. 构建与体积

核心静态链接，排除 SDK 的 MFC DLL 包装，不携带额外 MACDll。
使用 `/O1 /Os`、函数和数据分节、Release LTCG、`/OPT:REF /OPT:ICF`，只导出原来的三个符号。
CPU SIMD 路径由 SDK 运行时选择；保留旧解码核心，未用全局 AVX2 换取体积或速度。
VC-LTL／YY-Thunks 固定版本、哈希校验，PE 子系统最低为 5.01；构建后检查 XP 和 Win7 导入集合。

构建、版本分配、打包和 Action 均独立于 rebuild。许可证保留在仓库与发行说明中，不嵌入 DLL；测试不上传、不在 Action 中运行。
