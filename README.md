# 原神账号切换器

面向《原神》国服 Windows 客户端的本地账号登录态切换工具。程序只读取当前 Windows 用户下已经存在的登录态，将账号凭据使用 Windows DPAPI 加密保存，并按用户明确选择恢复到注册表。

## 当前实现

第一版源码已经包含：

- 原神国服注册表读取：`HKCU\Software\miHoYo\原神`
- `MIHOYOSDK_ADL_PROD_CN_h3123967166` 保存与恢复
- `__LastUid___h2153286551` 保存与恢复
- `GENERAL_DATA_h2389025596` 备份保存；普通账号切换默认不写回
- 永久 `AccountId / Guid`
- DPAPI `CurrentUser` 加密账号凭据
- 添加当前账号、重复 UID 处理
- 更新账号登录态并校验 UID
- 重命名、删除
- 当前注册表账号匹配
- 切换前自动保存恢复点
- 切换后重新读取并逐字节验证
- 恢复上一次注册表状态
- `YuanShen.exe` 运行检测
- HoYoPlay 路径自动读取与手动指定路径
- 切换并启动原神
- 本地非敏感日志
- 自包含单文件发布配置
- 无第三方 NuGet 依赖

## 数据目录

程序运行后使用：

```text
%LOCALAPPDATA%\GenshinAccountSwitcher\
├─ accounts.json
├─ settings.json
├─ Data\
│  └─ <AccountId>.dat
├─ Recovery\
│  └─ LastRegistrySnapshot.dat
└─ Logs\
   └─ app.log
```

`.dat` 文件使用 Windows DPAPI `CurrentUser` 加密。账号 ADL 原始数据不会写入 JSON 或日志。

## 发布

开发机使用 .NET 8 SDK。在 Windows PowerShell 运行：

```powershell
.\build.ps1
```

脚本依次执行编译、核心烟雾测试和 `win-x64` 自包含单文件发布，最终文件：

```text
dist\GenshinAccountSwitcher.exe
```

最终使用电脑无需另装 .NET Runtime。

## 仍需实机确认的内容

核心逻辑测试可以使用假注册表完成，真实原神登录态仍需要在安装了国服客户端的 Windows 环境验收。重点执行 `TESTING.md` 中的 A～F 测试，尤其是两个真实账号之间连续 `A → B → A → B → A`。

## 当前阶段暂未实现

- AES 密码备份导出/导入
- 系统托盘
- 快捷键
- 登录态失效提醒
- 切换时恢复 `GENERAL_DATA` 的高级开关
