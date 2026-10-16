// ShootGame 独立服务器构建入口；沿用当前游戏模块与 UE5.6 编译设置。

using UnrealBuildTool;

public class ShootGameServerTarget : TargetRules
{
	public ShootGameServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
		ExtraModuleNames.Add("ShootGame");
	}
}
