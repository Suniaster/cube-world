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

  // Attach Header statically to the 'head' bone
  HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
  HeadMesh->SetupAttachment(GetMesh(), FName("head"));
  static ConstructorHelpers::FObjectFinder<UStaticMesh> HeadAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Head.Head'"));
  if (HeadAsset.Succeeded())
    HeadMesh->SetStaticMesh(HeadAsset.Object);

  // Attach Torso to the 'spine' bone
  TorsoMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TorsoMesh"));
  TorsoMesh->SetupAttachment(GetMesh(), FName("spine"));
  static ConstructorHelpers::FObjectFinder<UStaticMesh> TorsoAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Torso.Torso'"));
  if (TorsoAsset.Succeeded())
    TorsoMesh->SetStaticMesh(TorsoAsset.Object);

  // Attach hands
  HandLMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandLMesh"));
  HandLMesh->SetupAttachment(GetMesh(), FName("hand_l"));
  static ConstructorHelpers::FObjectFinder<UStaticMesh> HandLAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Hand_L.Hand_L'"));
  if (HandLAsset.Succeeded())
    HandLMesh->SetStaticMesh(HandLAsset.Object);

  HandRMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandRMesh"));
  HandRMesh->SetupAttachment(GetMesh(), FName("hand_r"));
  static ConstructorHelpers::FObjectFinder<UStaticMesh> HandRAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Hand_R.Hand_R'"));
  if (HandRAsset.Succeeded())
    HandRMesh->SetStaticMesh(HandRAsset.Object);

  // Attach feet (legs)
  FootLMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FootLMesh"));
  FootLMesh->SetupAttachment(GetMesh(), FName("foot_l"));
  static ConstructorHelpers::FObjectFinder<UStaticMesh> FootLAsset(
      TEXT("/Script/Engine.StaticMesh'/Game/BlankChar/Foot_L.Foot_L'"));
  if (FootLAsset.Succeeded())
    FootLMesh->SetStaticMesh(FootLAsset.Object);

  FootRMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FootRMesh"));
  FootRMesh->SetupAttachment(GetMesh(), FName("foot_r"));
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
}

void ABlankChar::UpdateProceduralAnimation(float DeltaTime) {
  // Procedural Walking Animation
  if (GetCharacterMovement()) {
    float Speed = GetVelocity().Size2D();
    bool bIsFalling = GetCharacterMovement()->IsFalling();

    FRotator TargetHandR = FRotator::ZeroRotator;
    FRotator TargetHandL = FRotator::ZeroRotator;
    FRotator TargetFootR = FRotator::ZeroRotator;
    FRotator TargetFootL = FRotator::ZeroRotator;
    FVector TargetTorso = FVector::ZeroVector;
    FRotator TargetTorsoRot = FRotator::ZeroRotator;
    FRotator TargetHeadRot = FRotator::ZeroRotator;

    FVector TargetFootR_Loc = FVector::ZeroVector;
    FVector TargetFootL_Loc = FVector::ZeroVector;

    FVector TargetHandR_Loc = FVector::ZeroVector;
    FVector TargetHandL_Loc = FVector::ZeroVector;

    if (bIsFalling) {
      // Mario-style jump pose
      WalkTimer = 0.0f; // Reset walk timer so landing is smooth

      TargetHandR = FRotator::ZeroRotator;
      TargetHandL = FRotator::ZeroRotator;
      TargetHandR_Loc = FVector(0.0f, -20.0f, 0.0f);
      TargetHandL_Loc = FVector(0.0f, 20.0f, 0.0f);

      TargetFootR = FRotator(0.0f, 0.0f, MaxWalkAngle);
      TargetFootL = FRotator(0.0f, 0.0f, -MaxWalkAngle);

      float LegLength = 40.0f;
      float RRad = FMath::DegreesToRadians(MaxWalkAngle);
      float LRad = FMath::DegreesToRadians(-MaxWalkAngle);
      TargetFootR_Loc = FVector(0.0f, -LegLength * FMath::Sin(RRad),
                                LegLength * (1.0f - FMath::Cos(RRad)));
      TargetFootL_Loc = FVector(0.0f, -LegLength * FMath::Sin(LRad),
                                LegLength * (1.0f - FMath::Cos(LRad)));
    } else if (Speed > 10.0f) {
      // Increment timer based on speed
      WalkTimer += DeltaTime * (Speed / 100.0f) * WalkAnimSpeed;

      // Bobbing effect
      float BobOffset = FMath::Abs(FMath::Sin(WalkTimer)) * 5.0f;
      TargetTorso = FVector(0.0f, 0.0f, BobOffset);

      // Wobbling rotation on Z axis (Yaw) for fluidity
      float TorsoYaw = FMath::Sin(WalkTimer) * 10.0f;

      // Incline forward slightly while moving and apply wobble
      TargetTorsoRot = FRotator(0.0f, TorsoYaw, 15.0f);
      TargetHeadRot = FRotator(0.0f, TorsoYaw * 0.5f, 15.0f);

      // Calculate hands sliding forward and backward
      float HandSwing = FMath::Sin(WalkTimer) * 30.0f;

      TargetHandR = TargetTorsoRot;
      TargetHandL = TargetTorsoRot;
      TargetHandR_Loc = FVector(0.0f, -HandSwing, 0.0f);
      TargetHandL_Loc = FVector(0.0f, HandSwing, 0.0f);

      // Feet make a semi circle motion (-90 to 90 degrees)
      float FootSwingAngle = FMath::Sin(WalkTimer) * 90.0f;
      TargetFootR = FRotator(0.0f, 0.0f, -FootSwingAngle);
      TargetFootL = FRotator(0.0f, 0.0f, FootSwingAngle);

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
    } else {
      // Smoothly return to default pose
      WalkTimer = 0.0f;
    }

    // Apply smooth interpolation for all parts
    HandRMesh->SetRelativeRotation(FMath::RInterpTo(
        HandRMesh->GetRelativeRotation(), TargetHandR, DeltaTime, 15.0f));
    HandLMesh->SetRelativeRotation(FMath::RInterpTo(
        HandLMesh->GetRelativeRotation(), TargetHandL, DeltaTime, 15.0f));

    HandRMesh->SetRelativeLocation(FMath::VInterpTo(
        HandRMesh->GetRelativeLocation(), TargetHandR_Loc, DeltaTime, 15.0f));
    HandLMesh->SetRelativeLocation(FMath::VInterpTo(
        HandLMesh->GetRelativeLocation(), TargetHandL_Loc, DeltaTime, 15.0f));
    FootRMesh->SetRelativeRotation(FMath::RInterpTo(
        FootRMesh->GetRelativeRotation(), TargetFootR, DeltaTime, 15.0f));
    FootLMesh->SetRelativeRotation(FMath::RInterpTo(
        FootLMesh->GetRelativeRotation(), TargetFootL, DeltaTime, 15.0f));

    FootRMesh->SetRelativeLocation(FMath::VInterpTo(
        FootRMesh->GetRelativeLocation(), TargetFootR_Loc, DeltaTime, 15.0f));
    FootLMesh->SetRelativeLocation(FMath::VInterpTo(
        FootLMesh->GetRelativeLocation(), TargetFootL_Loc, DeltaTime, 15.0f));

    TorsoMesh->SetRelativeLocation(FMath::VInterpTo(
        TorsoMesh->GetRelativeLocation(), TargetTorso, DeltaTime, 15.0f));
    HeadMesh->SetRelativeLocation(FMath::VInterpTo(
        HeadMesh->GetRelativeLocation(), TargetTorso, DeltaTime, 15.0f));

    TorsoMesh->SetRelativeRotation(FMath::RInterpTo(
        TorsoMesh->GetRelativeRotation(), TargetTorsoRot, DeltaTime, 15.0f));
    HeadMesh->SetRelativeRotation(FMath::RInterpTo(
        HeadMesh->GetRelativeRotation(), TargetHeadRot, DeltaTime, 15.0f));
  }
}

// Called to bind functionality to input
void ABlankChar::SetupPlayerInputComponent(
    UInputComponent *PlayerInputComponent) {
  Super::SetupPlayerInputComponent(PlayerInputComponent);

  // Bind jump events
  PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
  PlayerInputComponent->BindAction("Jump", IE_Released, this,
                                   &ACharacter::StopJumping);

  // Bind movement events
  PlayerInputComponent->BindAxis("MoveForward", this, &ABlankChar::MoveForward);
  PlayerInputComponent->BindAxis("MoveRight", this, &ABlankChar::MoveRight);

  // Bind camera events
  PlayerInputComponent->BindAxis("Turn", this, &ABlankChar::Turn);
  PlayerInputComponent->BindAxis("LookUp", this, &ABlankChar::LookUp);
  PlayerInputComponent->BindAxis("ZoomCamera", this, &ABlankChar::ZoomCamera);
}

void ABlankChar::MoveForward(float Value) {
  if ((Controller != nullptr) && (Value != 0.0f)) {
    // find out which way is forward
    const FRotator Rotation = Controller->GetControlRotation();
    const FRotator YawRotation(0, Rotation.Yaw, 0);

    // get forward vector
    const FVector Direction =
        FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
    AddMovementInput(Direction, Value);
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

void ABlankChar::Turn(float Value) { AddControllerYawInput(Value); }

void ABlankChar::LookUp(float Value) { AddControllerPitchInput(Value); }

void ABlankChar::ZoomCamera(float Value) {
  if (Value != 0.0f && CameraBoom) {
    float NewLength = CameraBoom->TargetArmLength - (Value * 50.0f);
    CameraBoom->TargetArmLength = FMath::Clamp(NewLength, 150.0f, 3000.0f);
  }
}