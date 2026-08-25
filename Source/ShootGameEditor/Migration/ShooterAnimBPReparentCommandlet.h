// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ShooterAnimBPReparentCommandlet.generated.h"

/**
 * R6.2 资产迁移 Commandlet：
 * 把第一人称武器 AnimBP 父类迁移到 UShooterFirstPersonAnimInstance，
 * 第三人称 AnimBP 父类迁移/保持为 UShooterThirdPersonAnimInstance，
 * 逐项编译并保存。只处理本计划明确列出的四个资产。
 */
UCLASS()
class UShooterAnimBPReparentCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UShooterAnimBPReparentCommandlet();

	virtual int32 Main(const FString& Params) override;
};
