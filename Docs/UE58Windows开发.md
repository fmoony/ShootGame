# UE 5.8.4 Windows 开发入口

本分支使用已构建的 UE 5.8.4 源码版 Windows Editor。请按本机安装位置传入
`-EngineRoot`，或设置 `SHOOTGAME_ENGINE_ROOT`。下列命令从工程根目录执行。

```powershell
.\Scripts\Development\StartShootGameEditor.ps1 -EngineRoot <引擎根目录>
.\Scripts\Development\RefreshVisualStudioFiles.ps1 -EngineRoot <引擎根目录>
.\Scripts\Tests\BuildEditor.ps1 -EngineRoot <引擎根目录>
.\Scripts\Tests\RunAutomation.ps1 -EngineRoot <引擎根目录>
```

如果使用资源管理器打开项目，可以为当前 Windows 用户注册项目的共享关联标识。
注册仅写入当前用户的 Unreal Engine Builds 映射，路径按各开发机填写。

```powershell
.\Scripts\Development\ResolveShootGameEngine.ps1 -EngineRoot <引擎根目录> -RegisterAssociation
```

EngineAssociation 用于定位安装。引擎和项目模块的 Build ID 由 UBT 管理。
团队复用已验收构建时，应同时核对引擎版本和模块清单。

全量门禁以 `Scripts/Tests/ShootGame.AutomationTests.txt` 为发现基准。
新增、移除或重命名用例时同步审核清单。
