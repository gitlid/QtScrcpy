# UHID 实体键盘

在“启动配置 → 键盘模式”选择 **UHID 实体键盘**，然后启动投屏。手机的实体键盘设置中会出现 **QtScrcpy keyboard**。连接方式可以是 USB 或 Wi-Fi ADB；模式变更在下次连接时生效。

## 使用

1. 连接手机并选择 UHID 模式，点击“启动服务”。
2. 点击“手机键盘设置”可直接打开 Android 的实体键盘设置，选择与电脑键盘匹配的布局。
3. 点击投屏窗口后输入。中文由手机输入法处理，电脑端保持英文输入状态。如果手机仍显示软键盘，可在手机实体键盘设置中关闭“使用屏幕键盘”。
4. 投屏工具栏的动作宏按钮可录制、保存和回放实体按键。手动输入在切换窗口时释放按键；宏停止或连接断开时也会释放按键。宏回放可以在后台继续，`Ctrl+Shift+X` 保留为紧急停止。

游戏映射开启时，映射按键继续由原来的触摸映射处理；切回普通控制后使用 UHID。原有版本 1 动作宏仍以原来的 Android 按键方式回放。含 UHID 的宏保存为版本 2，需要使用本版或后续支持版本 2 的程序。

当前描述符支持六个普通键同时按下，外加左右 Ctrl、Shift、Alt、Meta。长按重复由 Android 处理。Windows 会先按物理扫描码映射，键盘布局由手机决定。系统截获的组合键仍受 Windows 限制。

## 验证记录

2026-09-06，在 Wi-Fi 连接的 vivo V1824A（Android 11）上验证：

- 官方 scrcpy 4.1 服务端的 UHID 创建、Shift 按下/释放、设备销毁全部成功。
- 编译后的 Controller 注册为 Android 字母键盘；一次录制加两次回放收到 3 次 A 按下和 3 次 A 释放。
- 失焦释放及断开移除成功。
- 完整 Windows 界面启动后，手机收到 Tab 和 Ctrl+A 的实体按键事件，关闭投屏后设备被移除。
- 15 组 CTest 全部通过，包含原有动作宏回归和新增的 9 组 UHID 测试。

## 复现

先配置和构建，Qt 路径替换为本机安装目录：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64 -DQSC_BUILD_ACTION_MACRO_TESTS=ON -DBUILD_TESTING=ON
cmake --build build --config RelWithDebInfo --parallel 6
$env:PATH="C:\Qt\5.15.2\msvc2019_64\bin;$PWD\output\x64\RelWithDebInfo;"+$env:PATH
$env:QT_QPA_PLATFORM="offscreen"
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

独立检测手机内核及 Android 输入系统是否支持 UHID：

```powershell
python tests/probe_uhid.py --adb output/x64/RelWithDebInfo/adb.exe --serial PHONE_SERIAL --server output/x64/RelWithDebInfo/scrcpy-server --output build/uhid-probe
```

验证编译后的 C++ 控制器、录制和回放（先关闭其他 UHID 投屏连接）：

```powershell
python tests/verify_uhid_device.py --adb output/x64/RelWithDebInfo/adb.exe --serial PHONE_SERIAL --server output/x64/RelWithDebInfo/scrcpy-server --client build/tests/RelWithDebInfo/uhid_integration.exe --runtime output/x64/RelWithDebInfo --runtime C:/Qt/5.15.2/msvc2019_64/bin --output build/uhid-device-test
```

两个脚本使用独立的 ADB 转发和临时服务端文件，完成后只清理自身资源。检测脚本只发送 Shift；C++ 测试会打开实体键盘设置并发送 Shift、A，不会更改系统设置。

Windows 便携包从任意工作目录启动时均以程序目录定位配置。源码的默认模式保持兼容按键注入；本次提供的便携包预置 UHID 模式。
