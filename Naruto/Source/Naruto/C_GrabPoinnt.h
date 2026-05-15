
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "C_GrabPoinnt.generated.h"

UCLASS()
class NARUTO_API AC_GrabPoinnt : public AActor
{
	GENERATED_BODY()
	
public:	
	AC_GrabPoinnt();

	UPROPERTY(BlueprintReadWrite)
	bool bIsUsing = false;
};
