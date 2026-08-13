# 原神账号管理

面向《原神》国服 Windows 客户端的本地账号管理与登录态切换工具。当前主版本使用原生 C++20 + WinUI 3 / C++/WinRT 实现。

程序只处理当前 Windows 用户自己的本地登录态，不上传账号数据，不包含遥测、云同步或远程服务。账号凭据通过 Windows DPAPI `CurrentUser` 加密后保存在程序目录旁的 `data` 文件夹中。

## 功能

- 读取《原神》国服当前登录账号
- 保存多个本地账号并通过 UID 识别
- 一键切换已保存账号
- 单独启动游戏，不隐式执行账号切换
- 游戏运行期间禁止修改登录注册表
- 自动检测通过 HoYoPlay、快捷方式或其他方式启动的 `YuanShen.exe`
- 游戏退出后重新读取最终登录态，并在 UID 唯一匹配时自动同步更新后的登录凭据
- 添加、更新、重命名、删除账号
- 切换前创建恢复快照，失败时校验并按需恢复
- 自动读取 HoYoPlay 国服安装路径，也支持手动指定 `YuanShen.exe`
- 本地日志与数据目录快捷入口
- 固定尺寸 WinUI 3 界面

## 数据目录

程序采用便携目录结构。运行后会在 EXE 同级生成：

```text
GenshinAccountSwitcher.exe
data\
├─ accounts.json
├─ settings.json
├─ accounts\
│  ├─ <AccountId>.dat
│  └─ previous\
├─ recovery\
└─ logs\
```

`.dat` 文件使用 Windows DPAPI `CurrentUser` 加密，因此只能由创建这些数据的同一 Windows 用户解密。`accounts.json` 不保存原始登录凭据。

如果程序所在目录不可写，程序会直接提示将其移动到普通可写目录，不会自动改用 `%LOCALAPPDATA%`。

## 使用

1. 从 Releases 下载最新 Windows x64 压缩包并解压。
2. 运行 `GenshinAccountSwitcher.exe`。
3. 如果当前已经登录《原神》，点击“添加当前账号”保存当前登录态。
4. 在“已保存账号”列表选择目标账号后点击“切换账号”。
5. 点击“启动游戏”只负责启动当前注册表状态对应的游戏，不会再次执行账号切换。

游戏运行时，账号凭据相关写入会被阻止。退出游戏后程序会等待注册表状态收敛，再处理最终账号状态。

## 构建

当前主版本位于：

```text
native-winui3/src/
```

主要依赖：

- C++20
- WinUI 3 / Windows App SDK 1.7
- C++/WinRT
- Windows SDK
- Visual Studio 2022 C++ 工具链

仓库内 `.github/workflows/build-native-winui3.yml` 可在 Windows runner 上构建 x64 Release 包。

## 分支

- `main`：当前 Native WinUI 3 版本
- `legacy-wpf`：早期 WPF/.NET 实现归档
- `winui3-ui-prototype`：早期 WinUI 3 界面验证分支

## 隐私与安全

- 不上传账号数据
- 不使用管理员权限
- 不包含遥测
- 不向广告商或第三方服务发送数据
- 登录凭据只保存在本机，并由 DPAPI `CurrentUser` 加密
- 仓库本身不包含用户运行后生成的 `data` 目录或账号凭据

## 说明

本项目为非官方工具，与米哈游 / HoYoverse 无隶属或授权关系。使用前请自行了解并承担本地账号数据管理相关风险。
