// Fill out your copyright notice in the Description page of Project Settings.


#include "C_PaperZDAnimBP.h"

UC_PaperZDAnimBP::UC_PaperZDAnimBP()
{
	SupportedAnimationSource = AnimSource;
}

void UC_PaperZDAnimBP::SyncSpeed(float speed)
{
	Speed = speed;
}
