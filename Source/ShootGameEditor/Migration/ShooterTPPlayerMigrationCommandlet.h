// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShooterTPPlayerMigrationCommandlet.generated.h"

/** 阶段一限定迁移：复制 Rifle 主图、绑定资源输入，只保存玩家主图、武器表和角色蓝图。 */
UCLASS()
class UShooterTPPlayerMigrationCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShooterTPPlayerMigrationCommandlet();
	virtual int32 Main(const FString& Params) override;
};
