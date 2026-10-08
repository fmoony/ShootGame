# 脚步声音源素材

- 作者：Kenney。
- 包名：Impact Sounds 1.0，2019。
- 官方来源：[Impact Sounds](https://kenney.nl/assets/impact-sounds)。
- 授权：CC0；原包授权文本保存在 `License.txt`。
- 使用条目：`footstep_concrete_000.ogg` 至 `footstep_concrete_004.ogg`。
- 本目录保存导入源：44.1kHz、单声道、16-bit PCM WAV。

原素材是双声道；使用 FFmpeg 合并为单声道，适合空间化脚步声。
未使用付费素材、其他游戏提取素材或生成式音频。
声音资产位于 `/Game/Shooter/Audio/Footsteps`。

## 重建声音资产

先确认同项目编辑器内容已保存，并关闭其他 ShootGame 编辑器实例。
通过完整编辑器初始化后运行下列导入脚本，完成后进程自动退出。
脚本会覆盖本方案的五个 SoundWave，保留现有衰减资产并更新报告。

```powershell
& 'E:/Unreal_Engine/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
	'D:/Unreal_Projects/ShootGame/ShootGame.uproject' `
	-ExecutePythonScript='D:/Unreal_Projects/ShootGame/Scripts/Development/ImportFootstepAudio.py' `
	-EnablePlugins=PythonScriptPlugin -DisablePlugins=McpAutomationBridge `
	-unattended -nop4 -nosplash -NullRHI -DDC-ForceMemoryCache
```

本机 `-run=pythonscript` 导入会出现 BINKA decoder 缺失的 ensure。
不得根据产生了资产文件就判定该导入运行通过。
使用上述完整编辑器入口，且不要添加 `-NoSound`。

## 验证与试听

规则与资产检查：`Scripts/Tests/RunAutomation.ps1 -TestFilter ShootGame.Audio.Footsteps`。
网络探针通过 `-ExecCmds=ShootGame.Footsteps.Probe` 显式启用。
探针使用正常 `AddMovementInput` 往返移动八秒，验证本地、远端脚步和停步静音。
正常游戏不会启用该探针；Shipping 构建不包含该开发测试代码。

有声音设备时额外传入 `-ShootGameFootstepRecord`，记录十秒游戏主混音。
录制原始输出为 `Saved/Footsteps/Capture.f32`。
对应 `Capture.txt` 保存声道数和采样率，可以转换成 WAV 试听。
后台录音需传入 `-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0`。
这是测试进程专用覆盖，不修改项目默认的后台静音策略。
录音完成后测试进程正常退出；无声音设备的测试只验证请求。

当前使用 TP 左右脚 Notify，距离组件已删除，见 `Docs/TP脚步Notify与基础音效.md`。
本机无渲染录音可能没有采样；脚步试听使用 `Lvl_Test` 加 `-RenderOffscreen`。
最终录音位于 `Saved/FootstepsNotify/TPNotifyFootsteps.wav`，检查无 NPC 射击提交。
