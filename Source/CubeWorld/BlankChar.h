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

	/** Called for vertical input (flight) */
	void MoveUp(float Value);

	/** Called for yaw (turn) input */
	void Turn(float Value);

	/** Called for pitch (look up) input */
	void LookUp(float Value);

	/** Called for zoom input */
	void ZoomCamera(float Value);

protected:
	/** Updates the procedural animation variables and meshes based on movement */
	void UpdateProceduralAnimation(float DeltaTime);

	void UpdateJumpPose();
	void UpdateWalkPose(float DeltaTime, float Speed);
	void ResetProceduralPose();

	/** Roll animation */
	void StartRoll();
	void UpdateRollPose(float DeltaTime);
	void ApplyProceduralPose(float DeltaTime);

	// Running & Flying
	void StartRun();
	void StopRun();
	void ToggleFly();

protected:	// Target procedural poses
	FRotator TargetHeadRot;
	FRotator TargetTorsoRot;
	FRotator TargetHandR_Rot;
	FRotator TargetHandL_Rot;
	FRotator TargetFootR_Rot;
	FRotator TargetFootL_Rot;

	FVector TargetTorso_Loc;
	FVector TargetHandR_Loc;
	FVector TargetHandL_Loc;
	FVector TargetFootR_Loc;
	FVector TargetFootL_Loc;

	// Procedural Animation variables
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Animation")
	float WalkAnimSpeed = 2.f;

	float WalkTimer = 0.0f;

	// Movement state
	bool bIsRolling = false;
	bool bIsRunning = false;
	bool bIsUnderwater = false;
	float RollTimer = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	int32 WaterLevel = 10;

	/** World-space size of one voxel in UU. Must match AChunkWorldManager::VoxelSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	float VoxelSize = 100.0f;

	/** Updates camera post-process settings based on underwater state. */
	void UpdateUnderwaterPostProcess(bool bUnderwater);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Animation")
	float RollDuration = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Animation")
	float RollSpeedMultiplier = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Animation")
	float RunSpeedMultiplier = 10.0f;

	float CachedWalkSpeed = 0.0f;
	float CachedFlySpeed = 0.0f;

	/** Height of the torso pivot above the mesh origin (feet). Adjust to match skeleton. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Animation")
	float RollPivotHeight = 110.0f;

	// Root mesh transform targets (used during roll)
	FVector TargetMeshLoc;
	FRotator TargetMeshRot;

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
