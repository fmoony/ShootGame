// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShooterAnimBPGraphAuditCommandlet.generated.h"

/**
 * R6.3 只读资产审计 Commandlet：
 * 输出目标 AnimBP 的父类、蓝图变量、EventGraph 节点与 AnimGraph 节点清单。
 * 不修改任何资产，不 MarkPackageDirty。
 */
UCLASS()
class UShooterAnimBPGraphAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShooterAnimBPGraphAuditCommandlet();

	virtual int32 Main(const FString& Params) override;
};
