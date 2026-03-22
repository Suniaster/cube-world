// Fill out your copyright notice in the Description page of Project Settings.


#include "TestGameMode.h"
#include "BlankChar.h"

ATestGameMode::ATestGameMode()
{
	// Set default pawn class to our custom C++ character
	DefaultPawnClass = ABlankChar::StaticClass();
}
