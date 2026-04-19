#include "BlankChar.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"

// Sets default values
ABlankChar::ABlankChar() {
  // Set this character to call Tick() every frame.  You can turn this off to
  // improve performance if you don't need it.
  PrimaryActorTick.bCanEverTick = true;

  // Don't rotate when the controller rotates. Let that just affect the camera.
  bUseControllerRotationPitch = false;
  bUseControllerRotationYaw = false;
  bUseControllerRotationRoll = false;

  // Configure character movement
  if (GetCharacterMovement()) {
    GetCharacterMovement()->bOrientRotationToMovement =
        true; // Character moves in the direction of input...
    GetCharacterMovement()->RotationRate =
        FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

    // Make the jump slightly higher than default
    GetCharacterMovement()->JumpZVelocity = 700.f;
    GetCharacterMovement()->AirControl = 1.f;
  }

  // By default, the Mesh is located at the absolute center of the capsule.
  // We drop it down by the half-height (-88.0f) so the feet touch the
  // floor. We also rotate it -90 degrees so the character faces "forward"
  // appropriately.
  GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -88.0f),
                                            FRotator(0.0f, -90.0f, 0.0f));

  // Create a camera boom to hold the camera behind the character
  CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
  CameraBoom->SetupAttachment(RootComponent);
  CameraBoom->TargetArmLength = 600.0f; // Camera distance from player
  CameraBoom->bUsePawnControlRotation =
      true; // Rotate the arm based on the controller input

  // Create a follow camera attached to the end of the boom
  FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
  FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
  FollowCamera->bUsePawnControlRotation =
      false; // Camera itself does not rotate, the boom does

  // Attach Header to Mesh (procedurally animated)
  HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
  HeadMesh->SetupAttachment(GetMesh());
  static ConstructorHelpers::FObjectFinder<UStaticMesh> HeadAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Head.Head'"));
  if (HeadAsset.Succeeded())
    HeadMesh->SetStaticMesh(HeadAsset.Object);

  // Attach Torso to Mesh
  TorsoMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TorsoMesh"));
  TorsoMesh->SetupAttachment(GetMesh());
  static ConstructorHelpers::FObjectFinder<UStaticMesh> TorsoAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Torso.Torso'"));
  if (TorsoAsset.Succeeded())
    TorsoMesh->SetStaticMesh(TorsoAsset.Object);

  // Attach hands to Mesh
  HandLMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandLMesh"));
  HandLMesh->SetupAttachment(GetMesh());
  static ConstructorHelpers::FObjectFinder<UStaticMesh> HandLAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Hand_L.Hand_L'"));
  if (HandLAsset.Succeeded())
    HandLMesh->SetStaticMesh(HandLAsset.Object);

  HandRMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandRMesh"));
  HandRMesh->SetupAttachment(GetMesh());
  static ConstructorHelpers::FObjectFinder<UStaticMesh> HandRAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Hand_R.Hand_R'"));
  if (HandRAsset.Succeeded())
    HandRMesh->SetStaticMesh(HandRAsset.Object);

  // Attach feet to Mesh
  FootLMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FootLMesh"));
  FootLMesh->SetupAttachment(GetMesh());
  static ConstructorHelpers::FObjectFinder<UStaticMesh> FootLAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Foot_L.Foot_L'"));
  if (FootLAsset.Succeeded())
    FootLMesh->SetStaticMesh(FootLAsset.Object);

  FootRMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FootRMesh"));
  FootRMesh->SetupAttachment(GetMesh());
  static ConstructorHelpers::FObjectFinder<UStaticMesh> FootRAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Foot_R.Foot_R'"));
  if (FootRAsset.Succeeded())
    FootRMesh->SetStaticMesh(FootRAsset.Object);

  // IMPORTANT: Turn off collision on these body parts so they don't block
  // the character's main outer movement capsule!
  HeadMesh->SetCollisionProfileName(TEXT("NoCollision"));
  TorsoMesh->SetCollisionProfileName(TEXT("NoCollision"));
  HandLMesh->SetCollisionProfileName(TEXT("NoCollision"));
  HandRMesh->SetCollisionProfileName(TEXT("NoCollision"));
  FootLMesh->SetCollisionProfileName(TEXT("NoCollision"));
  FootRMesh->SetCollisionProfileName(TEXT("NoCollision"));
}

// Called when the game starts or when spawned
void ABlankChar::BeginPlay() { Super::BeginPlay(); }

// Called every frame
void ABlankChar::Tick(float DeltaTime) {
  Super::Tick(DeltaTime);

  UpdateProceduralAnimation(DeltaTime);

  // Check if camera is underwater
  const bool bWasUnderwater = bIsUnderwater;
  bIsUnderwater = (FollowCamera->GetComponentLocation().Z < WaterLevel * VoxelSize);

  if (bIsUnderwater != bWasUnderwater)
  {
    UpdateUnderwaterPostProcess(bIsUnderwater);
  }
}

void ABlankChar::UpdateUnderwaterPostProcess(bool bUnderwater)
{
  FollowCamera->PostProcessSettings.bOverride_ColorSaturation = bUnderwater;
  FollowCamera->PostProcessSettings.bOverride_SceneColorTint = bUnderwater;
  FollowCamera->PostProcessSettings.bOverride_DepthOfFieldFocalDistance = bUnderwater;

  if (bUnderwater)
  {
    FollowCamera->PostProcessSettings.ColorSaturation = FVector4(0.3f, 0.4f, 1.2f, 1.0f);
    FollowCamera->PostProcessSettings.SceneColorTint = FLinearColor(0.2f, 0.4f, 0.8f, 1.0f);
    FollowCamera->PostProcessSettings.DepthOfFieldFocalDistance = 100.0f;
  }
}

void ABlankChar::UpdateProceduralAnimation(float DeltaTime) {
  // Procedural Walking Animation
  if (GetCharacterMovement()) {
    float Speed = GetVelocity().Size2D();
    bool bIsFalling = GetCharacterMovement()->IsFalling();

    ResetProceduralPose();

    if (bIsRolling) {
      UpdateRollPose(DeltaTime);
    } else if (bIsFalling) {
      UpdateJumpPose();
    } else if (Speed > 10.0f) {
      UpdateWalkPose(DeltaTime, Speed);
    } else {
      // Smoothly return to default pose
      WalkTimer = 0.0f;
    }

    ApplyProceduralPose(DeltaTime);
  }
}

void ABlankChar::ResetProceduralPose() {
  TargetHandR_Rot = FRotator::ZeroRotator;
  TargetHandL_Rot = FRotator::ZeroRotator;
  TargetFootR_Rot = FRotator::ZeroRotator;
  TargetFootL_Rot = FRotator::ZeroRotator;
  TargetTorsoRot = FRotator::ZeroRotator;
  TargetHeadRot = FRotator::ZeroRotator;

  TargetTorso_Loc = FVector::ZeroVector;
  TargetHandR_Loc = FVector::ZeroVector;
  TargetHandL_Loc = FVector::ZeroVector;
  TargetFootR_Loc = FVector::ZeroVector;
  TargetFootL_Loc = FVector::ZeroVector;

  // Default mesh transform
  TargetMeshLoc = FVector(0.0f, 0.0f, -88.0f);
  TargetMeshRot = FRotator(0.0f, -90.0f, 0.0f);
}

void ABlankChar::UpdateJumpPose() {
  // Mario-style jump pose
  WalkTimer = 0.0f; // Reset walk timer so landing is smooth

  // Hands stretch to match max walk swing
  TargetHandR_Loc = FVector(0.0f, -30.0f, 0.0f);
  TargetHandL_Loc = FVector(0.0f, 30.0f, 0.0f);

  // Exaggerate jump leg split
  float JumpAngle = 45.0f;
  TargetFootR_Rot = FRotator(0.0f, 0.0f, JumpAngle);
  TargetFootL_Rot = FRotator(0.0f, 0.0f, -JumpAngle);

  // Match walk leg length scale
  float LegLength = 70.0f;
  float RRad = FMath::DegreesToRadians(JumpAngle);
  float LRad = FMath::DegreesToRadians(-JumpAngle);
  
  TargetFootR_Loc = FVector(0.0f, -LegLength * FMath::Sin(RRad),
                            LegLength * (1.0f - FMath::Cos(RRad)));
  TargetFootL_Loc = FVector(0.0f, -LegLength * FMath::Sin(LRad),
                            LegLength * (1.0f - FMath::Cos(LRad)));
}

void ABlankChar::UpdateWalkPose(float DeltaTime, float Speed) {
  // Increment timer based on speed
  WalkTimer += DeltaTime * (Speed / 100.0f) * WalkAnimSpeed;

  // Bobbing effect
  float BobOffset = FMath::Abs(FMath::Sin(WalkTimer)) * 5.0f;
  TargetTorso_Loc = FVector(0.0f, 0.0f, BobOffset);

  // Wobbling rotation on Z axis (Yaw) for fluidity
  float TorsoYaw = FMath::Sin(WalkTimer) * 10.0f;

  // Incline forward slightly while moving and apply wobble
  TargetTorsoRot = FRotator(0.0f, TorsoYaw, 15.0f);
  TargetHeadRot = FRotator(0.0f, TorsoYaw * 0.5f, 15.0f);

  // Calculate hands sliding forward and backward
  float HandSwing = FMath::Sin(WalkTimer) * 30.0f;

  TargetHandR_Rot = TargetTorsoRot;
  TargetHandL_Rot = TargetTorsoRot;
  TargetHandR_Loc = FVector(0.0f, -HandSwing, 0.0f);
  TargetHandL_Loc = FVector(0.0f, HandSwing, 0.0f);

  // Feet make a semi circle motion (-90 to 90 degrees)
  float FootSwingAngle = FMath::Sin(WalkTimer) * 90.0f;
  TargetFootR_Rot = FRotator(0.0f, 0.0f, -FootSwingAngle);
  TargetFootL_Rot = FRotator(0.0f, 0.0f, FootSwingAngle);

  float LegLength = 70.0f;
  float RRad = FMath::DegreesToRadians(-FootSwingAngle);
  float LRad = FMath::DegreesToRadians(FootSwingAngle);

  // Offset feet forward to stay centered under the leaning torso
  float LeanOffset = -15.0f;

  TargetFootR_Loc =
      FVector(0.0f, -LegLength * FMath::Sin(RRad) - LeanOffset,
              LegLength * (1.0f - FMath::Cos(RRad)));
  TargetFootL_Loc =
      FVector(0.0f, -LegLength * FMath::Sin(LRad) - LeanOffset,
              LegLength * (1.0f - FMath::Cos(LRad)));
}

void ABlankChar::ApplyProceduralPose(float DeltaTime) {
  // Apply smooth interpolation for all body parts
  float PartInterp = 15.0f;
  HandRMesh->SetRelativeRotation(FMath::RInterpTo(
      HandRMesh->GetRelativeRotation(), TargetHandR_Rot, DeltaTime, PartInterp));
  HandLMesh->SetRelativeRotation(FMath::RInterpTo(
      HandLMesh->GetRelativeRotation(), TargetHandL_Rot, DeltaTime, PartInterp));

  HandRMesh->SetRelativeLocation(FMath::VInterpTo(
      HandRMesh->GetRelativeLocation(), TargetHandR_Loc, DeltaTime, PartInterp));
  HandLMesh->SetRelativeLocation(FMath::VInterpTo(
      HandLMesh->GetRelativeLocation(), TargetHandL_Loc, DeltaTime, PartInterp));

  FootRMesh->SetRelativeRotation(FMath::RInterpTo(
      FootRMesh->GetRelativeRotation(), TargetFootR_Rot, DeltaTime, PartInterp));
  FootLMesh->SetRelativeRotation(FMath::RInterpTo(
      FootLMesh->GetRelativeRotation(), TargetFootL_Rot, DeltaTime, PartInterp));

  FootRMesh->SetRelativeLocation(FMath::VInterpTo(
      FootRMesh->GetRelativeLocation(), TargetFootR_Loc, DeltaTime, PartInterp));
  FootLMesh->SetRelativeLocation(FMath::VInterpTo(
      FootLMesh->GetRelativeLocation(), TargetFootL_Loc, DeltaTime, PartInterp));

  TorsoMesh->SetRelativeLocation(FMath::VInterpTo(
      TorsoMesh->GetRelativeLocation(), TargetTorso_Loc, DeltaTime, PartInterp));
  HeadMesh->SetRelativeLocation(FMath::VInterpTo(
      HeadMesh->GetRelativeLocation(), TargetTorso_Loc, DeltaTime, PartInterp));

  TorsoMesh->SetRelativeRotation(FMath::RInterpTo(
      TorsoMesh->GetRelativeRotation(), TargetTorsoRot, DeltaTime, PartInterp));
  HeadMesh->SetRelativeRotation(FMath::RInterpTo(
      HeadMesh->GetRelativeRotation(), TargetHeadRot, DeltaTime, PartInterp));

  // Apply root mesh transform (snaps during roll, lerps back after)
  if (bIsRolling) {
    GetMesh()->SetRelativeLocation(TargetMeshLoc);
    GetMesh()->SetRelativeRotation(TargetMeshRot);
  } else {
    FVector DefaultMeshLoc(0.0f, 0.0f, -88.0f);
    FRotator DefaultMeshRot(0.0f, -90.0f, 0.0f);
    GetMesh()->SetRelativeLocation(FMath::VInterpTo(
        GetMesh()->GetRelativeLocation(), DefaultMeshLoc, DeltaTime, PartInterp));
    GetMesh()->SetRelativeRotation(FMath::RInterpTo(
        GetMesh()->GetRelativeRotation(), DefaultMeshRot, DeltaTime, PartInterp));
  }
}

// ── Roll Animation ──────────────────────────────────────────────────────

void ABlankChar::StartRoll() {
  // Only roll when grounded and not already rolling
  if (bIsRolling) return;
  if (!GetCharacterMovement() || GetCharacterMovement()->IsFalling()) return;

  bIsRolling = true;
  RollTimer = 0.0f;

  // Boost movement speed during the roll
  CachedWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
  GetCharacterMovement()->MaxWalkSpeed = CachedWalkSpeed * RollSpeedMultiplier;
}

void ABlankChar::UpdateRollPose(float DeltaTime) {
  // Advance normalized timer (0 -> 1)
  RollTimer += DeltaTime / FMath::Max(RollDuration, 0.01f);

  if (RollTimer >= 1.0f) {
    RollTimer = 1.0f;
    bIsRolling = false;
    // Restore original walk speed
    if (GetCharacterMovement()) {
      GetCharacterMovement()->MaxWalkSpeed = CachedWalkSpeed;
    }
    return; // ResetProceduralPose already set defaults; will lerp back
  }

  // Roll angle: 0 -> 360 degrees (full forward somersault)
  float RollAngle = RollTimer * 360.0f;

  // ── Rotate the ROOT MESH around the torso-center pivot ──
  // The mesh origin is at the feet (placed at -88 in capsule space).
  // The torso center (spine bone) is RollPivotHeight above the mesh origin.
  // We rotate the entire mesh around this pivot so all parts stay above ground.

  FVector DefaultMeshLoc(0.0f, 0.0f, -88.0f);

  // Pivot point in capsule space
  FVector PivotCapsule = DefaultMeshLoc + FVector(0.0f, 0.0f, RollPivotHeight);
  // Vector from pivot to mesh origin
  FVector PivotToOrigin = FVector(0.0f, 0.0f, -RollPivotHeight);

  // Forward somersault = negative pitch in UE (head goes forward)
  FQuat PitchQ = FRotator(-RollAngle, 0.0f, 0.0f).Quaternion();
  FQuat DefaultQ = FRotator(0.0f, -90.0f, 0.0f).Quaternion();

  // Add a Z-axis wobble (yaw oscillation) for a dynamic feel
  float WobbleAngle = -5.0f;
  FQuat WobbleQ = FRotator(0.0f, WobbleAngle, 0.0f).Quaternion();

  // Compose: default rotation, then pitch somersault, then yaw wobble
  FQuat FinalQ = WobbleQ * PitchQ * DefaultQ;
  TargetMeshRot = FinalQ.Rotator();

  // Rotate the pivot-to-origin vector to find new mesh origin position
  FVector RotatedOffset = PitchQ.RotateVector(PivotToOrigin);
  TargetMeshLoc = PivotCapsule + RotatedOffset;

  // Individual parts get expressive poses while the root mesh rolls.

  // Hands raised up high (Z = up in bone-relative space)
  TargetHandR_Loc = FVector(0.0f, 25.0f, 100.0f);
  TargetHandL_Loc = FVector(0.0f, -25.0f, 100.0f);

  // Feet in jump-split pose (same as UpdateJumpPose)
  float JumpAngle = 45.0f;
  TargetFootR_Rot = FRotator(0.0f, 0.0f, JumpAngle);
  TargetFootL_Rot = FRotator(0.0f, 0.0f, -JumpAngle);

  float LegLength = 70.0f;
  float RRad = FMath::DegreesToRadians(JumpAngle);
  float LRad = FMath::DegreesToRadians(-JumpAngle);
  TargetFootR_Loc = FVector(0.0f, -LegLength * FMath::Sin(RRad),
                            LegLength * (1.0f - FMath::Cos(RRad)));
  TargetFootL_Loc = FVector(0.0f, -LegLength * FMath::Sin(LRad),
                            LegLength * (1.0f - FMath::Cos(LRad)));

  WalkTimer = 0.0f;
}

// ── Input Bindings ──────────────────────────────────────────────────────

// Called to bind functionality to input
void ABlankChar::SetupPlayerInputComponent(
    UInputComponent *PlayerInputComponent) {
  Super::SetupPlayerInputComponent(PlayerInputComponent);

  // Bind jump events
  PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
  PlayerInputComponent->BindAction("Jump", IE_Released, this,
                                   &ACharacter::StopJumping);

  // Bind roll
  PlayerInputComponent->BindAction("Roll", IE_Pressed, this,
                                   &ABlankChar::StartRoll);

  // Bind run
  PlayerInputComponent->BindAction("Run", IE_Pressed, this, &ABlankChar::StartRun);
  PlayerInputComponent->BindAction("Run", IE_Released, this, &ABlankChar::StopRun);

  // Bind fly toggle
  PlayerInputComponent->BindAction("Fly", IE_Pressed, this, &ABlankChar::ToggleFly);

  // Bind movement events
  PlayerInputComponent->BindAxis("MoveForward", this, &ABlankChar::MoveForward);
  PlayerInputComponent->BindAxis("MoveRight", this, &ABlankChar::MoveRight);
  PlayerInputComponent->BindAxis("MoveUp", this, &ABlankChar::MoveUp);

  // Bind camera events
  PlayerInputComponent->BindAxis("Turn", this, &ABlankChar::Turn);
  PlayerInputComponent->BindAxis("LookUp", this, &ABlankChar::LookUp);
  PlayerInputComponent->BindAxis("ZoomCamera", this, &ABlankChar::ZoomCamera);
}

void ABlankChar::MoveForward(float Value) {
  if ((Controller != nullptr) && (Value != 0.0f)) {
    // find out which way is forward
    const FRotator Rotation = Controller->GetControlRotation();

    if (GetCharacterMovement()->IsFlying()) {
      // In flight, move in the looking direction
      const FVector Direction = Rotation.Vector();
      AddMovementInput(Direction, Value);
    } else {
      // On ground, move horizontally
      const FRotator YawRotation(0, Rotation.Yaw, 0);
      const FVector Direction =
          FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
      AddMovementInput(Direction, Value);
    }
  }
}

void ABlankChar::MoveRight(float Value) {
  if ((Controller != nullptr) && (Value != 0.0f)) {
    // find out which way is right
    const FRotator Rotation = Controller->GetControlRotation();
    const FRotator YawRotation(0, Rotation.Yaw, 0);

    // get right vector
    const FVector Direction =
        FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
    // add movement in that direction
    AddMovementInput(Direction, Value);
  }
}

void ABlankChar::MoveUp(float Value) {
  if (Value != 0.0f) {
    AddMovementInput(FVector::UpVector, Value);
  }
}

void ABlankChar::Turn(float Value) { AddControllerYawInput(Value); }

void ABlankChar::LookUp(float Value) { AddControllerPitchInput(Value); }

void ABlankChar::ZoomCamera(float Value) {
  if (Value != 0.0f && CameraBoom) {
    float NewLength = CameraBoom->TargetArmLength - (Value * 50.0f);
    CameraBoom->TargetArmLength = FMath::Clamp(NewLength, 150.0f, 3000.0f);
  }
}

void ABlankChar::StartRun() {
  if (bIsRunning) return;
  bIsRunning = true;

  if (GetCharacterMovement()) {
    CachedWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
    CachedFlySpeed = GetCharacterMovement()->MaxFlySpeed;
    GetCharacterMovement()->MaxWalkSpeed = CachedWalkSpeed * RunSpeedMultiplier;
    GetCharacterMovement()->MaxFlySpeed = CachedFlySpeed * RunSpeedMultiplier;
  }
}

void ABlankChar::StopRun() {
  if (!bIsRunning) return;
  bIsRunning = false;

  if (GetCharacterMovement()) {
    GetCharacterMovement()->MaxWalkSpeed = CachedWalkSpeed;
    GetCharacterMovement()->MaxFlySpeed = CachedFlySpeed;
  }
}

void ABlankChar::ToggleFly() {
  if (GetCharacterMovement()) {
    if (GetCharacterMovement()->MovementMode == MOVE_Walking) {
      GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    } else {
      GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
  }
}
