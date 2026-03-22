// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BlankChar.generated.h"

UCLASS()
class CUBEWORLD_API ABlankChar : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ABlankChar();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:
	/** Called for forwards/backward input */
	void MoveForward(float Value);

	/** Called for side to side input */
	void MoveRight(float Value);

	/** Called for yaw (turn) input */
	void Turn(float Value);

	/** Called for pitch (look up) input */
	void LookUp(float Value);

	/** Called for zoom input */
	void ZoomCamera(float Value);

protected:
	/** Updates the procedural animation variables and meshes based on movement */
	void UpdateProceduralAnimation(float DeltaTime);

	// Procedural Animation variables
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Animation")
	float WalkAnimSpeed = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Animation")
	float MaxWalkAngle = 25.0f;

	float WalkTimer = 0.0f;

public:	
	// The separate voxel parts
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Character")
	class UStaticMeshComponent* HeadMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Character")
	class UStaticMeshComponent* TorsoMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Character")
	class UStaticMeshComponent* HandLMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Character")
	class UStaticMeshComponent* HandRMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Character")
	class UStaticMeshComponent* FootLMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Character")
	class UStaticMeshComponent* FootRMesh;

	// Camera setup for Third Person
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	class USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	class UCameraComponent* FollowCamera;

};
