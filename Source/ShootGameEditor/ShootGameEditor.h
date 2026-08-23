// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * ShootGameEditor：Editor-only 模块。
 * 承载 AnimBP 编辑器节点外壳（UAnimGraphNode_ShooterAimIK 等），不进入 Game / Shipping Target。
 */
class FShootGameEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
