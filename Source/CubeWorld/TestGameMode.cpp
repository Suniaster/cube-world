// Fill out your copyright notice in the Description page of Project Settings.

#include "TestGameMode.h"
#include "BlankChar.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

ATestGameMode::ATestGameMode()
{
	// Set default pawn class to our custom C++ character
	DefaultPawnClass = ABlankChar::StaticClass();
}

void ATestGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Find the player's controller and pawn
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* PlayerPawn = PC->GetPawn())
		{
			// Start the player a bit above the world origin (0,0,0)
			// This ensures they are above any potential terrain at the start.
			PlayerPawn->SetActorLocation(FVector(0.0f, 0.0f, 5000.0f));
		}
	}
}
