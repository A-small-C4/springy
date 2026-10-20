# springy

一个独立的 [Syringe](https://github.com/Phobos-developers/SyringeEx) hook DLL，用于
《命令与征服：尤里的复仇》（`gamemd.exe`），可独立运行，也可与 Ares、Phobos 共存。
它添加两个功能：

1. **弹簧彩蛋** - 一个命令栏按钮，使你（包括心灵控制的）的 VXL/SHP 对象 -
   载具、舰船、飞机、建筑、步兵 - 有节奏地压扁并变宽，同时循环播放主题音乐。仅本地生效。
2. **初始资金小数** - 单人任务 `[<HouseName> House]` 的 `Credits` 值接受最多两位小数
   （`Credits=123.45` -> `12345`）。

## 环境要求

- Windows，32 位构建（Win32/x86）。
- Visual Studio 2022（或 Build Tools 2022），含 C++ 工作负载。
- [YRpp](https://github.com/Phobos-developers/YRpp)（仅头文件）。
- [SyringeEx](https://github.com/Phobos-developers/SyringeEx)（必须使用 Ex 版本）
  用于将 DLL 注入游戏。
- 尤里的复仇 1.001（`gamemd.exe`）。

## 构建

```
build.bat            rem 调试版（默认）
build.bat Release
```

脚本通过 `vswhere` 定位 MSBuild，不需要任何其他仓库。
YRpp 的查找顺序为：`YRPP_DIR` 环境变量、本文件旁的本地 `YRpp\` 目录、
`..\..\YRpp` 的同级检出（见 `springy.vcxproj` 中的 `YRppDir`）。

输出：`Debug\springy.dll` / `Release\springy.dll`。

## 部署

将 `springy.dll` 复制到 `gamemd.exe` 旁边。Syringe 会扫描游戏目录中
带 hook 元数据（`.syhks00`）的 `*.dll` 并自动加载。

```
deploy.bat "D:\Command & Conquer\Red Alert 2" [Debug|Release]
```

## 配置

### `RA2MD.INI` - `[springy]`

```ini
[springy]
Enabled=yes          ; 总开关（仅在 yes 时创建按钮）
Rate=55              ; 一个完整弹簧周期的帧数
Widen=20             ; 最大变宽百分比（0-90）-> 宽/矮极端
Amount=40            ; 最大压扁百分比（0-90）-> 宽/矮极端
Narrow=20            ; 最大变窄百分比（0-90）-> 高/瘦极端
Stretch=40           ; 最大增高百分比（0-90）-> 高/瘦极端
OnlyOwner=yes        ; 仅本地玩家及其心灵控制对象
Shadows=yes          ; VXL 阴影跟随
SHP=yes              ; SHP 对象（步兵、建筑、覆盖层）参与
Sound=SpringyMusic   ; 主题名（见 thememd.ini）
Button.OffsetX=0     ; 命令栏按钮偏移
Button.OffsetY=0
Button.SHP.Allied=button12_al.shp  ; 图标 SHP（帧 0=关，1=开）
Button.SHP.Soviet=button12_so.shp  ; 苏军；未设置尤里 SHP 时尤里也使用
Button.SHP.Yuri=                   ; 留空 = 使用苏军 SHP
Profile=no           ; 将性能摘要追加到 springy_profile.txt

; 各分类默认值（未设置时回退到上方对应的全局值；
; 若某分类没有自己的 Narrow/Stretch，则回退到它自己的 Widen/Amount）：
Infantry.Rate=55
Infantry.Widen=20
Infantry.Amount=40
Infantry.Narrow=20
Infantry.Stretch=40
Vehicle.Rate=55       ; 单位，包含舰船
Vehicle.Widen=20
Vehicle.Amount=40
Vehicle.Narrow=20
Vehicle.Stretch=40
Aircraft.Rate=55
Aircraft.Widen=20
Aircraft.Amount=40
Aircraft.Narrow=20
Aircraft.Stretch=40
BuildingSHP.Rate=55
BuildingSHP.Widen=10
BuildingSHP.Amount=20
BuildingSHP.Narrow=10
BuildingSHP.Stretch=20
BuildingVXL.Rate=55
BuildingVXL.Widen=20
BuildingVXL.Amount=40
BuildingVXL.Narrow=20
BuildingVXL.Stretch=40
Other.Rate=55
Other.Widen=20
Other.Amount=40
Other.Narrow=20
Other.Stretch=40
```

波形按 静止 -> 变窄/变高 -> 静止 -> 变宽/压扁 -> 静止 循环；四个极端值相互独立。
`Narrow`/`Stretch` 默认取同一档的 `Widen`/`Amount`，因此旧配置行为不变。

优先级为 **TechnoType > 分类 > 全局**。建筑按绘制路径分类（建筑的体素炮塔/炮管
属于 `BuildingVXL`，其 SHP 主体和覆盖层属于 `BuildingSHP`）。

### 按 TechnoType 覆盖（在类型的 rules 节中）

```ini
[MTNK]
springy.Rate=55
springy.Widen=20
springy.Amount=40
springy.Narrow=20
springy.Stretch=40
```

### 音乐主题（`thememd.ini`）

```ini
[SpringyMusic]
Sound=springy
Normal=no
Repeat=yes
```

`springy.wav` 以散文件形式放在游戏目录（`Sound=` 不带扩展名）。

### 性能分析（`Profile=yes`）

启用期间，约每 3 秒向游戏 exe 旁的 `springy_profile.txt` 追加一行：帧率/帧时间、
各 springy hook 耗时（`mainloop_us`、`vox_us`、`shp_us`、`anim_us`）、
每帧绘制/构建/标脏计数，以及压扁 SHP 的构建统计。用命令栏按钮切换效果时会在
同一文件写入 `# spring ON/OFF` 标记，因此开关对比就是日志中的两段。

## 共存

- 可与 **Ares** 和 **Phobos** 构建一起使用。

## 目录结构

```
src/Config.h/.cpp        INI 配置 + 按类型缓存
src/Profiler.h/.cpp      游戏内性能探针（Profile=yes）
src/SpringyEffect.h/.cpp 运行时状态、参数、VXL hook、接缝填充、音乐
src/SpringySHP.cpp       SHP 压扁（主体 + 建筑覆盖层）
src/SpringyButton.h/.cpp 命令栏按钮
src/CreditsDecimals.cpp  初始资金小数
src/MatchHooks.cpp       对局开始/结束接线
```

## 鸣谢

- [Phobos](https://github.com/Phobos-developers/Phobos)
- [Ares](https://github.com/Ares-Developers/Ares)

本项目在实现过程中大量参考了以上两个项目的代码，在此表示感谢。

## 许可

源码以 GNU General Public License v3.0 分发。YRpp 是 Phobos-developers 的项目。
Red Alert 2 / Yuri's Revenge 归各自所有者所有；不包含任何游戏资源。
