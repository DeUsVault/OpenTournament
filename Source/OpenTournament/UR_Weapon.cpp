// Copyright (c) 2019-2020 Open Tournament Project, All Rights Reserved.

/////////////////////////////////////////////////////////////////////////////////////////////////

#include "UR_Weapon.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Particles/ParticleSystemComponent.h"

#include "OpenTournament.h"
#include "UR_Character.h"
#include "UR_InventoryComponent.h"
#include "UR_Projectile.h"
#include "UR_PlayerController.h"
#include "UR_FunctionLibrary.h"

#include "UR_FireModeBasic.h"
#include "UR_FireModeCharged.h"
#include "UR_FireModeContinuous.h"

/////////////////////////////////////////////////////////////////////////////////////////////////

AUR_Weapon::AUR_Weapon(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    Tbox = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
    Tbox->SetGenerateOverlapEvents(true);
    Tbox->OnComponentBeginOverlap.AddDynamic(this, &AUR_Weapon::OnTriggerEnter);
    Tbox->OnComponentEndOverlap.AddDynamic(this, &AUR_Weapon::OnTriggerExit);

    RootComponent = Tbox;

    Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh1P"));
    Mesh1P->SetupAttachment(RootComponent);
    Mesh1P->bOnlyOwnerSee = true;

    Mesh3P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh3P"));
    Mesh3P->SetupAttachment(RootComponent);
    //Mesh3P->bOwnerNoSee = true;

    //deprecated
    Sound = CreateDefaultSubobject<UAudioComponent>(TEXT("Sound"));
    Sound->SetupAttachment(RootComponent);

    PrimaryActorTick.bCanEverTick = true;

    bReplicates = true;

    FireInterval = 1.0f;

    MuzzleSocketName = FName(TEXT("Muzzle"));
    BringUpTime = 0.3f;
    PutDownTime = 0.3f;
}

/*

// I cannot get this to work. If anyone has an idea.

// I want firemodes to be ActorComponents because they need to replicate.
// However it does not seem to be possible to have ActorComponents as editable defaultproperties.

// The only workaround I can think of, is to add components the traditional way (via the +AddComponent button),
// And then fetch them in code (via GetComponents<UUR_FireModeBase>).

// The ordering of firemodes will need to be specified in component properties.
// This means there are two checks we should perform :
// - weapon should have at least one firemode component
// - all firemode components should have a different index

// Unfortunately I don't know and cannot find any documentation about adding blueprint compile checks.

// The below method is for MapCheck which kind of works but weirdly, and GetComponents returns empty array anyways.

// https://forums.unrealengine.com/unreal-engine/feedback-for-epic/106383-validation-plausibility-checks-for-blueprint-properties


#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "CompilerResultsLog.h"
#include "UR_FireModeBase.h"
void AUR_Weapon::CheckForErrors()
{
    Super::CheckForErrors();

    // BP compile time
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        UE_LOG(LogTemp, Log, TEXT("In ClassDefaultObject"));
        FCompilerResultsLog* MessageLog = FCompilerResultsLog::GetEventTarget();
        if (MessageLog == nullptr)
        {
            UE_LOG(LogTemp, Log, TEXT("MessageLog nullptr"));
            return;
        }

        TArray<UUR_FireModeBase*> Comps;
        GetComponents<UUR_FireModeBase>(Comps);
        UE_LOG(LogTemp, Log, TEXT("Found components: %i"), Comps.Num());
        if (Comps.Num() == 0)
        {
            MessageLog->Warning(TEXT(""))
                ->AddToken(FUObjectToken::Create(this))
                ->AddToken(FTextToken::Create(FText::FromString(TEXT("Weapon does not have any FireMode component"))));
        }
        else
        {

        }
    }
}
#endif
*/

/////////////////////////////////////////////////////////////////////////////////////////////////

void AUR_Weapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION(AUR_Weapon, AmmoCount, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AUR_Weapon, bIsEquipped, COND_SkipOwner);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void AUR_Weapon::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    TArray<UUR_FireModeBase*> FireModeComponents;
    GetComponents<UUR_FireModeBase>(FireModeComponents);
    for (auto FireMode : FireModeComponents)
    {
        // sanity check
        if (FireModes.IsValidIndex(FireMode->Index) && FireModes[FireMode->Index])
        {
            GAME_PRINT(6.f, FColor::Red, "ERROR: %s has multiple firemodes with index %i", *GetName(), FireMode->Index);
        }
        else
        {
            if (!FireModes.IsValidIndex(FireMode->Index))
            {
                FireModes.SetNumZeroed(FireMode->Index + 1);
            }
            FireModes[FireMode->Index] = FireMode;
        }
    }

    // Bind firemodes delegates to our default implementations
    for (auto FireMode : FireModes)
    {
        if (FireMode)
        {
            FireMode->SetCallbackInterface(this);
        }

        /*
        FireMode->OnFireModeChangedStatus.AddDynamic(this, &AUR_Weapon::OnFireModeChangedStatus);
        //FireMode->TimeUntilReadyToFireDelegate.BindUObject(this, &AUR_Weapon::TimeUntilReadyToFire);

        if (auto FireModeBasic = Cast<UUR_FireModeBasic>(FireMode))
        {
            FireModeBasic->SimulateShotDelegate.BindUObject(this, &AUR_Weapon::SimulateShot);
            FireModeBasic->AuthorityShotDelegate.BindUObject(this, &AUR_Weapon::AuthorityShot);
            FireModeBasic->AuthorityHitscanShotDelegate.BindUObject(this, &AUR_Weapon::AuthorityHitscanShot);
            FireModeBasic->OnPlayFireEffects.AddDynamic(this, &AUR_Weapon::PlayFireEffects);
            FireModeBasic->OnPlayHitscanEffects.AddDynamic(this, &AUR_Weapon::PlayHitscanEffects);
        }
        if (auto FireModeCharged = Cast<UUR_FireModeBasic>(FireMode))
        {
            //TODO
        }
        if (auto FireModeContinuous = Cast<UUR_FireModeBasic>(FireMode))
        {
            //TODO
        }
        */
    }
}

//deprecated
void AUR_Weapon::BeginPlay()
{
    Super::BeginPlay();

    //Sound->SetActive(false);
}

//deprecated
void AUR_Weapon::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

bool AUR_Weapon::CanFire() const
{
    return false;
}

void AUR_Weapon::Pickup()
{
    GAME_LOG(Game, Log, "Pickup Occurred");

    UGameplayStatics::PlaySoundAtLocation(this, PickupSound, URCharOwner->GetActorLocation());

    URCharOwner->InventoryComponent->Add(this);
    AttachWeaponToPawn();
}

void AUR_Weapon::GiveTo(AUR_Character* NewOwner)
{
    SetOwner(NewOwner);
    URCharOwner = NewOwner;
    AttachWeaponToPawn();
    if (NewOwner && NewOwner->InventoryComponent)
    {
        NewOwner->InventoryComponent->Add(this);
    }

    //tmp - prevent Pickup() call
    Tbox->SetGenerateOverlapEvents(false);
}

void AUR_Weapon::OnRep_Owner()
{
    URCharOwner = Cast<AUR_Character>(GetOwner());
    AttachWeaponToPawn();

    // In case Equipped was replicated before Owner
    OnRep_Equipped();
}

void AUR_Weapon::OnRep_Equipped()
{
    if (!GetOwner())
        return;	// owner not replicated yet

    if (IsLocallyControlled())
        return;	// should already be attached locally

    SetEquipped(bIsEquipped);
}

//deprecated
void AUR_Weapon::Fire()
{
    GAME_LOG(Game, Log, "Fire Weapon");

    if (auto World = GetWorld())
    {
        FVector MuzzleLocation{};
        FRotator MuzzleRotation{};

        if (AmmoCount > 0)
        {
            FActorSpawnParameters ProjectileSpawnParameters;

            AUR_Projectile* Projectile = World->SpawnActor<AUR_Projectile>(ProjectileClass, MuzzleLocation, MuzzleRotation, ProjectileSpawnParameters);

            UGameplayStatics::PlaySoundAtLocation(this, FireSound, URCharOwner->GetActorLocation());

            GAME_LOG(Game, Log, "Fire Occurred");

            if (Projectile)
            {
                FVector Direction = MuzzleRotation.Vector();
                Projectile->FireAt(Direction);
                AmmoCount--;
            }
        }
        else
        {
            GAME_PRINT(1.f, FColor::Red, "Ammo Expended for %s", *WeaponName);
        }
    }
}

//deprecated
void AUR_Weapon::GetPlayer(AActor* Player)
{
    URCharOwner = Cast<AUR_Character>(Player);
}

//deprecated
void AUR_Weapon::OnTriggerEnter(UPrimitiveComponent* HitComp, AActor * Other, UPrimitiveComponent * OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult & SweepResult)
{
    bItemIsWithinRange = true;
    GEngine->AddOnScreenDebugMessage(1, 5.f, FColor::Red, FString::Printf(TEXT("Press E to Pickup %s"), *WeaponName));
    GetPlayer(Other);
}

//deprecated
void AUR_Weapon::OnTriggerExit(UPrimitiveComponent* HitComp, AActor * Other, UPrimitiveComponent * OtherComp, int32 OtherBodyIndex)
{
    bItemIsWithinRange = false;
}

int32 AUR_Weapon::GetCurrentAmmo() const
{
    return AmmoCount;
}

//TODO: property
int32 AUR_Weapon::GetMaxAmmo() const
{
    return int32();
}

USkeletalMeshComponent* AUR_Weapon::GetWeaponMesh() const
{
    return Mesh1P;
}

//deprecated
AUR_Character * AUR_Weapon::GetPawnOwner() const
{
    return Cast<AUR_Character>(GetOwner());
}

bool AUR_Weapon::IsLocallyControlled() const
{
    APawn* P = Cast<APawn>(GetOwner());
    return P && P->IsLocallyControlled();
}

void AUR_Weapon::AttachMeshToPawn()
{
    this->SetActorHiddenInGame(false);

    if (URCharOwner)
    {
        // Remove and hide both first and third person meshes
        //DetachMeshFromPawn();

        /*
        // For locally controller players we attach both weapons and let the bOnlyOwnerSee, bOwnerNoSee flags deal with visibility.
        FName AttachPoint = URCharOwner->GetWeaponAttachPoint();
        if (IsLocallyControlled())
        {
            USkeletalMeshComponent* PawnMesh1p = URCharOwner->GetSpecifcPawnMesh(true);
            USkeletalMeshComponent* PawnMesh3p = URCharOwner->GetSpecifcPawnMesh(false);
            Mesh1P->SetHiddenInGame(false);
            Mesh3P->SetHiddenInGame(false);
            Mesh1P->AttachToComponent(PawnMesh1p, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
            Mesh3P->AttachToComponent(PawnMesh3p, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
        }
        else
        {
            USkeletalMeshComponent* UseWeaponMesh = GetWeaponMesh();
            USkeletalMeshComponent* UsePawnMesh = URCharOwner->GetPawnMesh();
            UseWeaponMesh->AttachToComponent(UsePawnMesh, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
            UseWeaponMesh->SetHiddenInGame(false);
        }
        */

        //NOTE: For now, assume that owner(s) are always in 1P, and others always see char from a 3P perspective.
        // That means we will use bOwnerSee/bOwnerNoSee to handle visibility.

        // This will have to be reworked later.
        // Be aware that "owner" means not only the local player, but also anybody looking through character via ViewTarget.
        // And both spectators/localplayer might be in either 1P or 3P, so I believe we cannot rely on bOwnerSee/bOwnerNoSee for this.

        Mesh1P->AttachToComponent(URCharOwner->MeshFirstPerson, FAttachmentTransformRules::KeepRelativeTransform, URCharOwner->GetWeaponAttachPoint());
        Mesh3P->AttachToComponent(URCharOwner->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, FName(TEXT("ik_hand_gun")));
        //Mesh3P->AttachToComponent(URCharOwner->MeshFirstPerson, FAttachmentTransformRules::KeepRelativeTransform, URCharOwner->GetWeaponAttachPoint());

        //UPDATE: Now using this, we shouldn't use bOwnerNoSee anymore on 3p. 1p can keep bOnlyOwnerSee.
        UpdateMeshVisibility();
    }
}

void AUR_Weapon::UpdateMeshVisibility()
{
    if (UUR_FunctionLibrary::IsViewingFirstPerson(URCharOwner))
    {
        Mesh1P->SetHiddenInGame(false);
        Mesh3P->SetHiddenInGame(true);
    }
    else
    {
        Mesh1P->SetHiddenInGame(true);
        Mesh3P->SetHiddenInGame(false);
        Mesh3P->bOwnerNoSee = false;
    }
}

void AUR_Weapon::AttachWeaponToPawn()
{
    this->SetActorHiddenInGame(true);
    Tbox->SetGenerateOverlapEvents(false);

    if (URCharOwner)
    {
        /*
        // For locally controller players we attach both weapons and let the bOnlyOwnerSee, bOwnerNoSee flags deal with visibility.
        FName AttachPoint = URCharOwner->GetWeaponAttachPoint();
        if (IsLocallyControlled())
        {
            //USkeletalMeshComponent* PawnMesh = URCharOwner->GetSpecifcPawnMesh(true);
            //this->AttachToComponent(PawnMesh, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
        }
        else
        {
            //USkeletalMeshComponent* UsePawnMesh = URCharOwner->GetPawnMesh();
            //this->AttachToComponent(UsePawnMesh, FAttachmentTransformRules::KeepRelativeTransform, AttachPoint);
        }
        */
    }
    this->SetActorHiddenInGame(true);
}

void AUR_Weapon::DetachMeshFromPawn()
{
    Mesh1P->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
    Mesh1P->SetHiddenInGame(true);

    Mesh3P->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
    Mesh3P->SetHiddenInGame(true);
}

void AUR_Weapon::OnEquip(AUR_Weapon * LastWeapon)
{
    LastWeapon->DetachMeshFromPawn();
    this->AttachMeshToPawn();
}

void AUR_Weapon::OnUnEquip()
{
    DetachMeshFromPawn();
}

bool AUR_Weapon::IsEquipped() const
{
    return bIsEquipped;
}

void AUR_Weapon::SetEquipped(bool bEquipped)
{
    bIsEquipped = bEquipped;

    if (bIsEquipped)
    {
        AttachMeshToPawn();

        /* old
        if (URCharOwner && URCharOwner->bIsFiring)
        {
            LocalStartFire();
        }
        */

        RequestBringUp();
    }
    else
    {
        SetWeaponState(EWeaponState::Inactive);
        DetachMeshFromPawn();
        LocalStopFire();
    }
}

//deprecated
bool AUR_Weapon::IsAttachedToPawn() const
{
    return false;
}

//============================================================
// Basic firing loop for basic fire mode.
//============================================================

void AUR_Weapon::LocalStartFire()
{
    bFiring = true;

    // Already firing or in cooldown
    if (FireLoopTimerHandle.IsValid())
        return;

    // Start fire loop
    LocalFireLoop();
}

void AUR_Weapon::LocalStopFire()
{
    //NOTE: Do not clear timer here, or repeated clicks will bypass fire interval.
    bFiring = false;
}

void AUR_Weapon::LocalFireLoop()
{
    //UKismetSystemLibrary::PrintString(this, TEXT("LocalFireLoop()"));

    FireLoopTimerHandle.Invalidate();

    // Here we stop the loop if player isn't firing anymore
    if (!bFiring)
        return;

    // Additional checks to stop firing automatically
    if (!URCharOwner || !URCharOwner->bIsFiring || !URCharOwner->IsAlive() || !URCharOwner->GetController() || !bIsEquipped)
    {
        bFiring = false;
        return;
    }

    // Not sure what this is
    //if (!CanFire())
        //return;

    if (AmmoCount <= 0)
    {
        // Play out-of-ammo sound ?
        GEngine->AddOnScreenDebugMessage(1, 5.f, FColor::Red, FString::Printf(TEXT("%s out of ammo"), *WeaponName));
        // Auto switch weapon ?
        return;
    }

    LocalFire();

    GetWorld()->GetTimerManager().SetTimer(FireLoopTimerHandle, this, &AUR_Weapon::LocalFireLoop, FireInterval, false);
}

void AUR_Weapon::LocalFire()
{
    ServerFire();

    if (ProjectileClass)
    {
        Old_PlayFireEffects();
    }
    else
    {
        FHitResult Hit;
        Old_HitscanTrace(Hit);
        Old_PlayFireEffects();
        Old_PlayHitscanEffects(FReplicatedHitscanInfo(Hit.TraceStart, Hit.bBlockingHit ? Hit.Location : Hit.TraceEnd, Hit.ImpactNormal));
    }

    LocalFireTime = GetWorld()->GetTimeSeconds();
}

void AUR_Weapon::ServerFire_Implementation()
{
    //if (!CanFire())
        //return;

    // No ammo, discard this shot
    if (AmmoCount <= 0)
    {
        return;
    }

    // Client asking to fire while not equipped
    // Could be a slightly desynced swap, try to delay a bit
    if (!bIsEquipped)
    {
        FTimerDelegate TimerCallback;
        TimerCallback.BindLambda([this]
        {
            if (bIsEquipped)
                ServerFire_Implementation();
        });
        GetWorld()->GetTimerManager().SetTimer(DelayedFireTimerHandle, TimerCallback, 0.1f, false);
        return;
    }

    // Check if client is asking us to fire too early
    float Delay = FireInterval - GetWorld()->TimeSince(LastFireTime);
    if (Delay > 0.0f)
    {
        if (Delay > FMath::Min(0.200f, FireInterval / 2.f))
            return;	// discard this shot

        // Delay a bit and fire
        GetWorld()->GetTimerManager().SetTimer(DelayedFireTimerHandle, this, &AUR_Weapon::ServerFire_Implementation, Delay, false);
        return;
    }

    if (ProjectileClass)
    {
        SpawnShot_Projectile();
        MulticastFired_Projectile();
    }
    else
    {
        FReplicatedHitscanInfo HitscanInfo;
        SpawnShot_Hitscan(HitscanInfo);
        MulticastFired_Hitscan(HitscanInfo);
    }

    LastFireTime = GetWorld()->GetTimeSeconds();
    Old_ConsumeAmmo();
}

void AUR_Weapon::Old_ConsumeAmmo()
{
    AmmoCount -= 1;
}

void AUR_Weapon::MulticastFired_Projectile_Implementation()
{
    if (IsNetMode(NM_Client))
    {
        if (URCharOwner && URCharOwner->IsLocallyControlled())
        {
            LocalConfirmFired();
        }
        else
        {
            Old_PlayFireEffects();
        }
    }
}

void AUR_Weapon::MulticastFired_Hitscan_Implementation(const FReplicatedHitscanInfo& HitscanInfo)
{
    if (IsNetMode(NM_Client))
    {
        if (URCharOwner && URCharOwner->IsLocallyControlled())
        {
            LocalConfirmFired();
        }
        else
        {
            Old_PlayFireEffects();
            Old_PlayHitscanEffects(HitscanInfo);
        }
    }
}

void AUR_Weapon::LocalConfirmFired()
{
    // Server just fired, adjust our fire loop accordingly
    float FirePing = GetWorld()->TimeSince(LocalFireTime);
    float Delay = FireInterval - FirePing / 2.f;
    if (Delay > 0.0f)
        GetWorld()->GetTimerManager().SetTimer(FireLoopTimerHandle, this, &AUR_Weapon::LocalFireLoop, Delay, false);
    else
        LocalFireLoop();
}

void AUR_Weapon::Old_PlayFireEffects()
{
    if (UUR_FunctionLibrary::IsViewingFirstPerson(URCharOwner))
    {
        UGameplayStatics::SpawnSoundAttached(FireSound, Mesh1P, MuzzleSocketName, FVector(0, 0, 0), EAttachLocation::SnapToTarget);
        UGameplayStatics::SpawnEmitterAttached(MuzzleFlashFX, Mesh1P, MuzzleSocketName, FVector(0, 0, 0), FRotator(0, 0, 0), EAttachLocation::SnapToTargetIncludingScale);
        URCharOwner->MeshFirstPerson->PlayAnimation(URCharOwner->FireAnimation, false);
    }
    else
    {
        UGameplayStatics::SpawnSoundAttached(FireSound, Mesh3P, MuzzleSocketName, FVector(0, 0, 0), EAttachLocation::SnapToTarget);
        UGameplayStatics::SpawnEmitterAttached(MuzzleFlashFX, Mesh3P, MuzzleSocketName, FVector(0, 0, 0), FRotator(0, 0, 0), EAttachLocation::SnapToTargetIncludingScale);
        //TODO: play 3p anim
    }
}

void AUR_Weapon::Old_PlayHitscanEffects(const FReplicatedHitscanInfo& HitscanInfo)
{
    FVector BeamStart;
    if (UUR_FunctionLibrary::IsViewingFirstPerson(URCharOwner))
    {
        BeamStart = Mesh1P->GetSocketLocation(MuzzleSocketName);
    }
    else
    {
        BeamStart = Mesh3P->GetSocketLocation(MuzzleSocketName);
    }

    FVector BeamEnd = HitscanInfo.End;
    FVector BeamVector = BeamEnd - BeamStart;

    UFXSystemComponent* BeamComp = UUR_FunctionLibrary::SpawnEffectAtLocation(GetWorld(), BeamTemplate, FTransform(BeamStart));
    if (BeamComp)
    {
        //TODO: configurable
        FName BeamVectorParamName = FName(TEXT("User.BeamVector"));
        BeamComp->SetVectorParameter(BeamVectorParamName, BeamVector);
    }

    // Impact fx & sound
    UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), BeamImpactTemplate, FTransform(HitscanInfo.ImpactNormal.Rotation(), BeamEnd));
    UGameplayStatics::PlaySoundAtLocation(GetWorld(), BeamImpactSound, BeamEnd);
}

//============================================================
// Helpers
//============================================================

void AUR_Weapon::Old_GetFireVector(FVector& FireLoc, FRotator& FireRot)
{
    if (URCharOwner)
    {
        // Careful, in URCharacter we are using a custom 1p camera.
        // This means GetActorEyesViewPoint is wrong because it uses a hardcoded offest.
        // Either access camera directly, or override GetActorEyesViewPoint.
        FVector CameraLoc = URCharOwner->CharacterCameraComponent->GetComponentLocation();
        FireLoc = CameraLoc;
        FireRot = URCharOwner->GetViewRotation();

        if (ProjectileClass)
        {
            // Use centered projectiles as it is a lot simpler with less edge cases.
            FireLoc += FireRot.Vector() * URCharOwner->MuzzleOffset.Size();	//TODO: muzzle offset should be part of weapon, not character

            // Avoid spawning projectile within/behind geometry because of the offset.
            FCollisionQueryParams TraceParams(FCollisionQueryParams::DefaultQueryParam);
            TraceParams.AddIgnoredActor(this);
            TraceParams.AddIgnoredActor(URCharOwner);
            FHitResult Hit;
            if (GetWorld()->LineTraceSingleByChannel(Hit, CameraLoc, FireLoc, ECollisionChannel::ECC_Visibility, TraceParams))
            {
                FireLoc = Hit.Location;
            }
        }
        else
        {
            // For hitscan, use straight line from camera to crosshair.

            // Muzzle offset should be used only to adjust the fire effect (beam) start loc.
        }
    }
    else
    {
        GetActorEyesViewPoint(FireLoc, FireRot);
    }
}

void AUR_Weapon::SpawnShot_Projectile()
{
    FVector FireLoc;
    FRotator FireRot;
    GetFireVector(FireLoc, FireRot);

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = GetOwner();
    SpawnParams.Instigator = GetInstigator() ? GetInstigator() : Cast<APawn>(GetOwner());
    //SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AUR_Projectile* Projectile = GetWorld()->SpawnActor<AUR_Projectile>(ProjectileClass, FireLoc, FireRot, SpawnParams);
    if (Projectile)
    {
        Projectile->FireAt(FireRot.Vector());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to spawn projectile ??"));
    }
}

void AUR_Weapon::SpawnShot_Hitscan(FReplicatedHitscanInfo& OutHitscanInfo)
{
    /*
    FHitResult Hit;
    Old_HitscanTrace(Hit);

    if (HasAuthority() && Hit.bBlockingHit && Hit.GetActor())
    {
        //TODO: config
        float Damage = 70;
        TSubclassOf<UDamageType> DamageType = UDamageType::StaticClass();

        FVector Dir = Hit.TraceEnd - Hit.TraceStart;
        Dir.Normalize();

        UGameplayStatics::ApplyPointDamage(Hit.GetActor(), Damage, Dir, Hit, GetInstigatorController(), this, DamageType);
    }

    OutHitscanInfo.Start = Hit.TraceStart;
    OutHitscanInfo.End = Hit.bBlockingHit ? Hit.Location : Hit.TraceEnd;
    OutHitscanInfo.ImpactNormal = Hit.ImpactNormal;
    */
}

void AUR_Weapon::Old_HitscanTrace(FHitResult& OutHit)
{
    FVector TraceStart;
    FRotator FireRot;
    GetFireVector(TraceStart, FireRot);

    //TODO: these might need to be configurable to some extent
    float MaxDist = 10000;
    FVector TraceEnd = TraceStart + MaxDist * FireRot.Vector();
    ECollisionChannel TraceChannel = ECollisionChannel::ECC_GameTraceChannel2;  //WeaponTrace
    FCollisionShape SweepShape = FCollisionShape::MakeSphere(5.f);

    // fill in info in case we get 0 results from sweep
    OutHit.TraceStart = TraceStart;
    OutHit.TraceEnd = TraceEnd;
    OutHit.bBlockingHit = false;
    OutHit.ImpactNormal = TraceStart - TraceEnd;
    OutHit.ImpactNormal.Normalize();

    TArray<FHitResult> Hits;
    GetWorld()->SweepMultiByChannel(Hits, TraceStart, TraceEnd, FQuat(), TraceChannel, SweepShape);
    for (const FHitResult& Hit : Hits)
    {
        if (Hit.bBlockingHit || HitscanShouldHitActor(Hit.GetActor()))
        {
            OutHit = Hit;
            OutHit.bBlockingHit = true;
            break;
        }
    }
}

bool AUR_Weapon::HitscanShouldHitActor_Implementation(AActor* Other)
{
    //NOTE: here we can implement firing through teammates

    if (APawn* Pawn = Cast<APawn>(Other))
    {
        return Pawn != GetInstigator();
    }
    else if (AUR_Projectile* Proj = Cast<AUR_Projectile>(Other))
    {
        return Proj->CanBeDamaged();
    }
    return false;
}


//============================================================
// Helpers v2
//============================================================

void AUR_Weapon::GetFireVector(FVector& FireLoc, FRotator& FireRot)
{
    if (URCharOwner)
    {
        // Careful, in URCharacter we are using a custom 1p camera.
        // This means GetActorEyesViewPoint is wrong because it uses a hardcoded offest.
        // Either access camera directly, or override GetActorEyesViewPoint.
        FVector CameraLoc = URCharOwner->CharacterCameraComponent->GetComponentLocation();
        FireLoc = CameraLoc;
        FireRot = URCharOwner->GetViewRotation();
    }
    else
    {
        GetActorEyesViewPoint(FireLoc, FireRot);
    }
}

AUR_Projectile* AUR_Weapon::SpawnProjectile(TSubclassOf<AUR_Projectile> InProjectileClass, const FVector& StartLoc, const FRotator& StartRot)
{
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = GetOwner();
    SpawnParams.Instigator = GetInstigator() ? GetInstigator() : Cast<APawn>(GetOwner());
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AUR_Projectile* Projectile = GetWorld()->SpawnActor<AUR_Projectile>(InProjectileClass, StartLoc, StartRot, SpawnParams);
    if (Projectile)
    {
        Projectile->FireAt(StartRot.Vector());
        return Projectile;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to spawn projectile ??"));
    }

    return nullptr;
}

void AUR_Weapon::HitscanTrace(const FVector& TraceStart, const FVector& TraceEnd, FHitResult& OutHit)
{
    ECollisionChannel TraceChannel = ECollisionChannel::ECC_GameTraceChannel2;  //WeaponTrace
    FCollisionShape SweepShape = FCollisionShape::MakeSphere(5.f);

    // fill in info in case we get 0 results from sweep
    OutHit.TraceStart = TraceStart;
    OutHit.TraceEnd = TraceEnd;
    OutHit.bBlockingHit = false;
    OutHit.ImpactNormal = TraceStart - TraceEnd;
    OutHit.ImpactNormal.Normalize();

    TArray<FHitResult> Hits;
    GetWorld()->SweepMultiByChannel(Hits, TraceStart, TraceEnd, FQuat(), TraceChannel, SweepShape);
    for (const FHitResult& Hit : Hits)
    {
        if (Hit.bBlockingHit || HitscanShouldHitActor(Hit.GetActor()))
        {
            OutHit = Hit;
            OutHit.bBlockingHit = true;
            break;
        }
    }
}

bool AUR_Weapon::HasEnoughAmmoFor(UUR_FireModeBase* FireMode)
{
    return AmmoCount >= 1;
}

void AUR_Weapon::ConsumeAmmo(UUR_FireModeBase* FireMode)
{
    AmmoCount -= 1;
}


//============================================================
// WeaponStates core
//============================================================

void AUR_Weapon::SetWeaponState(EWeaponState NewState)
{
    if (NewState != WeaponState)
    {
        WeaponState = NewState;
        UKismetSystemLibrary::PrintString(this, FString::Printf(TEXT("WeaponState: %s"), *UUR_FunctionLibrary::GetEnumValueAsString(TEXT("EWeaponState"), WeaponState)), true, false, FColor::Purple, 2.f);
        OnWeaponStateChanged.Broadcast(this, NewState);
    }

    switch (WeaponState)
    {

    case EWeaponState::BringUp:
        // On BringUp, read current desired fire mode from player
        if (IsLocallyControlled() && URCharOwner)
        {
            if (URCharOwner->DesiredFireModeNum.Num() > 0)
            {
                RequestStartFire(URCharOwner->DesiredFireModeNum[0]);
            }
        }
        break;

    case EWeaponState::Idle:
        if (GetWorld()->GetTimerManager().IsTimerActive(PutDownDelayTimerHandle))
        {
            // if cooldown delays putdown by 100%, the timer can be slightly late.
            // we can force it to happen now.
            GetWorld()->GetTimerManager().ClearTimer(PutDownDelayTimerHandle);
            RequestPutDown();
        }
        else if (DesiredFireModes.Num() > 0)
        {
            TryStartFire(DesiredFireModes[0]);
        }
        break;

    case EWeaponState::PutDown:
    case EWeaponState::Inactive:
        StopAllFire();
        break;

    }
}

void AUR_Weapon::BringUp(float FromPosition)
{
    GetWorld()->GetTimerManager().ClearTimer(SwapAnimTimerHandle);

    SetWeaponState(EWeaponState::BringUp);

    if (BringUpMontage && URCharOwner && URCharOwner->MeshFirstPerson && URCharOwner->MeshFirstPerson->GetAnimInstance())
    {
        float Duration = BringUpMontage->GetPlayLength();
        float PlayRate = Duration / BringUpTime;
        float StartTime = FromPosition * Duration;
        //TODO: not sure if start time accounts for the play rate or not. need check
        URCharOwner->MeshFirstPerson->GetAnimInstance()->Montage_Play(BringUpMontage, PlayRate, EMontagePlayReturnType::MontageLength, StartTime);
    }

    float Delay = (1.f - FromPosition)*BringUpTime;
    if (Delay > 0.f)
    {
        GetWorld()->GetTimerManager().SetTimer(SwapAnimTimerHandle, this, &AUR_Weapon::BringUpCallback, Delay, false);
    }
    else
    {
        BringUpCallback();
    }
}

void AUR_Weapon::BringUpCallback()
{
    if (WeaponState == EWeaponState::BringUp)
    {
        // Weird use-case where weapon swaps are faster than cooldown times, firemode might still be busy
        if (CurrentFireMode && CurrentFireMode->IsBusy())
        {
            SetWeaponState(EWeaponState::Firing);
        }
        else
        {
            SetWeaponState(EWeaponState::Idle);
        }
    }
}

void AUR_Weapon::PutDown(float FromPosition)
{
    GetWorld()->GetTimerManager().ClearTimer(SwapAnimTimerHandle);

    SetWeaponState(EWeaponState::PutDown);

    if (PutDownMontage && URCharOwner && URCharOwner->MeshFirstPerson && URCharOwner->MeshFirstPerson->GetAnimInstance())
    {
        float Duration = PutDownMontage->GetPlayLength();
        float PlayRate = Duration / PutDownTime;
        float StartTime = FromPosition * Duration;
        //TODO: not sure if start time accounts for the play rate or not. need check
        URCharOwner->MeshFirstPerson->GetAnimInstance()->Montage_Play(PutDownMontage, PlayRate, EMontagePlayReturnType::MontageLength, StartTime);
    }

    float Delay = FromPosition * PutDownTime;
    if (Delay > 0.f)
    {
        GetWorld()->GetTimerManager().SetTimer(SwapAnimTimerHandle, this, &AUR_Weapon::PutDownCallback, Delay, false);
    }
    else
    {
        PutDownCallback();
    }
}

void AUR_Weapon::PutDownCallback()
{
    if (WeaponState == EWeaponState::PutDown)
    {
        SetWeaponState(EWeaponState::Inactive);
    }
}

void AUR_Weapon::StopAllFire()
{
    DesiredFireModes.Empty();

    // this should be enough
    if (CurrentFireMode)
    {
        CurrentFireMode->StopFire();
    }

    /*
    // Normally, only CurrentFireMode should be firing.
    // But iterate anyways just to make sure.
    for (auto FireMode : FireModes)
    {
        if (FireMode && FireMode->IsBusy())
        {
            FireMode->StopFire();
        }
    }
    */
}

// Factor method for ammo checking before starting fire
// And looping when out of ammo
void AUR_Weapon::TryStartFire(UUR_FireModeBase* FireMode)
{
    if (WeaponState == EWeaponState::Idle)
    {
        if (HasEnoughAmmoFor(FireMode))
        {
            FireMode->StartFire();
        }
        else
        {
            // Out of ammo
            UGameplayStatics::PlaySound2D(GetWorld(), OutOfAmmoSound);

            // loop as long as user is holding fire
            FTimerDelegate TimerCallback;
            TimerCallback.BindLambda([this, FireMode]
            {
                if (DesiredFireModes.Num() > 0 && DesiredFireModes[0] == FireMode)
                {
                    TryStartFire(FireMode);
                }
            });
            GetWorld()->GetTimerManager().SetTimer(RetryStartFireTimerHandle, TimerCallback, 0.5f, false);
            return;
        }
    }
}


//============================================================
// External API
//============================================================

void AUR_Weapon::RequestStartFire(uint8 FireModeIndex)
{
    if (FireModes.IsValidIndex(FireModeIndex))
    {
        auto FireMode = FireModes[FireModeIndex];
        if (FireMode)
        {
            DesiredFireModes.Remove(FireMode);
            DesiredFireModes.Insert(FireMode, 0);
            TryStartFire(FireMode);
        }
    }
}

void AUR_Weapon::RequestStopFire(uint8 FireModeIndex)
{
    if (FireModes.IsValidIndex(FireModeIndex))
    {
        auto FireMode = FireModes[FireModeIndex];
        if (FireMode)
        {
            DesiredFireModes.Remove(FireMode);

            if (FireMode->IsBusy())
            {
                FireMode->StopFire();
            }
        }
    }
}

/**
* TODO: RequestPutDown()
*
* weapon swap procedure :
* - character requests inventory to swap
* - inventory requests weapon to putdown
* - weapon putdown when possible
* - weapon notify inventory when done (event dispatcher?)
* - inventory changes active weapon
* - inventory requests new weapon to bring up
*/

void AUR_Weapon::RequestBringUp()
{
    GetWorld()->GetTimerManager().ClearTimer(PutDownDelayTimerHandle);

    switch (WeaponState)
    {

    case EWeaponState::Inactive:
        BringUp(0.f);
        break;

    case EWeaponState::PutDown:
        BringUp(GetWorld()->GetTimerManager().GetTimerRemaining(SwapAnimTimerHandle) / PutDownTime);
        break;

    }
}

void AUR_Weapon::RequestPutDown()
{
    switch (WeaponState)
    {

    case EWeaponState::BringUp:
        PutDown(GetWorld()->GetTimerManager().GetTimerElapsed(SwapAnimTimerHandle) / BringUpTime);
        return;

    case EWeaponState::Idle:
        PutDown(1.f);
        return;

    case EWeaponState::Firing:
    {
        float Delay = 0.f;

        float CooldownStartTime = CurrentFireMode->GetCooldownStartTime();
        float CooldownRemaining = CurrentFireMode->GetTimeUntilIdle();

        // Bit of an edge case - if fire mode returns future cooldown start, this is an indication to prevent put down.
        // Used by charging firemode so we dont allow swap while charging, even if CooldownPercent is at 0.
        if (GetWorld()->TimeSince(CooldownStartTime) < 0.f)
        {
            Delay = FMath::Max(CooldownRemaining * CooldownDelaysPutDownByPercent, 0.1f);
        }
        else if (CooldownRemaining > 0.f && CooldownDelaysPutDownByPercent > 0.f)
        {
            float TotalCooldown = GetWorld()->TimeSince(CooldownStartTime) + CooldownRemaining;
            float TotalPutDownDelay = TotalCooldown * CooldownDelaysPutDownByPercent;
            if (bReducePutDownDelayByPutDownTime)
            {
                TotalPutDownDelay -= PutDownTime;
            }
            float PutDownStartTime = CooldownStartTime + TotalPutDownDelay;
            Delay = PutDownStartTime - GetWorld()->GetTimeSeconds();
        }

        if (Delay > 0.f)
        {
            // We call back RequestPutDown until delay is 0, and only then we will call PutDown.
            // This is because some fire modes may not have proper cooldown information at all times (eg. charging).
            // NOTE: this loop can be canceled anytime by a subsequent RequestBringUp() call.
            GetWorld()->GetTimerManager().SetTimer(PutDownDelayTimerHandle, this, &AUR_Weapon::RequestPutDown, Delay, false);
        }
        else
        {
            PutDown(1.f);
        }
        break;
    }

    case EWeaponState::Busy:
        // Stub. Just wait. SetWeaponState(Idle) will notice and cancel the loop, and call this back.
        GetWorld()->GetTimerManager().SetTimer(PutDownDelayTimerHandle, this, &AUR_Weapon::RequestPutDown, 1.f, false);
        break;

    }
}


//============================================================
// FireModeBase callbacks
//============================================================

void AUR_Weapon::FireModeChangedStatus_Implementation(UUR_FireModeBase* FireMode)
{
    if (FireMode->IsBusy())
    {
        CurrentFireMode = FireMode;
        SetWeaponState(EWeaponState::Firing);
    }
    else if (FireMode == CurrentFireMode)
    {
        CurrentFireMode = nullptr;
        if (WeaponState == EWeaponState::Firing)
        {
            SetWeaponState(EWeaponState::Idle);
        }
    }
}

float AUR_Weapon::TimeUntilReadyToFire_Implementation(UUR_FireModeBase* FireMode)
{
    float Delay;
    switch (WeaponState)
    {
    case EWeaponState::BringUp:
        Delay = GetWorld()->GetTimerManager().GetTimerRemaining(SwapAnimTimerHandle);
        break;

    case EWeaponState::Idle:
        Delay = 0.f;
        break;

    case EWeaponState::Firing:
        Delay = CurrentFireMode->GetTimeUntilIdle();
        break;

    default:
        Delay = 1.f;    //prevent
        break;
    }

    if (Delay <= 0.f && !HasEnoughAmmoFor(FireMode))
    {
        Delay = 1.f;    //prevent
    }

    return Delay;
}


//============================================================
// FireModeBasic callbacks
//============================================================

void AUR_Weapon::SimulateShot_Implementation(UUR_FireModeBasic* FireMode, FSimulatedShotInfo& OutSimulatedInfo)
{
    FVector FireLoc;
    FRotator FireRot;
    GetFireVector(FireLoc, FireRot);

    if (FireMode->ProjectileClass)
    {
        FVector MuzzleLoc = Mesh1P->GetSocketLocation(FireMode->MuzzleSocketName);
        FVector MuzzleOffset = MuzzleLoc - FireLoc;
        if (!MuzzleOffset.IsNearlyZero())
        {
            FVector OriginalFireLoc = FireLoc;

            // Offset projectile forward but stay centered
            FireLoc += FireRot.Vector() * MuzzleOffset.Size();

            // Avoid spawning projectile within/behind geometry because of the offset.
            FCollisionQueryParams TraceParams(FCollisionQueryParams::DefaultQueryParam);
            TraceParams.AddIgnoredActor(this);
            TraceParams.AddIgnoredActor(URCharOwner);
            FHitResult Hit;
            if (GetWorld()->LineTraceSingleByChannel(Hit, OriginalFireLoc, FireLoc, ECollisionChannel::ECC_Visibility, TraceParams))
            {
                FireLoc = Hit.Location;
            }
        }
    }

    OutSimulatedInfo.Vectors.EmplaceAt(0, FireLoc);
    OutSimulatedInfo.Vectors.EmplaceAt(1, FireRot.Vector());
}

void AUR_Weapon::SimulateHitscanShot_Implementation(UUR_FireModeBasic* FireMode, FSimulatedShotInfo& OutSimulatedInfo, FHitscanVisualInfo& OutHitscanInfo)
{
    FVector FireLoc;
    FRotator FireRot;
    GetFireVector(FireLoc, FireRot);

    OutSimulatedInfo.Vectors.EmplaceAt(0, FireLoc);
    OutSimulatedInfo.Vectors.EmplaceAt(1, FireRot.Vector());

    FVector TraceEnd = FireLoc + FireMode->HitscanTraceDistance * FireRot.Vector();

    FHitResult Hit;
    HitscanTrace(FireLoc, TraceEnd, Hit);

    OutHitscanInfo.Vectors.EmplaceAt(0, Hit.bBlockingHit ? Hit.Location : Hit.TraceEnd);
    OutHitscanInfo.Vectors.EmplaceAt(1, Hit.ImpactNormal);
}

void AUR_Weapon::AuthorityShot_Implementation(UUR_FireModeBasic* FireMode, const FSimulatedShotInfo& SimulatedInfo)
{
    if (FireMode->ProjectileClass)
    {
        //TODO: validate passed in fire location - use server location if bad - needs a basic rewinding implementation to check
        FVector FireLoc = SimulatedInfo.Vectors[0];

        // Fire direction doesn't need validation
        const FVector& FireDir = SimulatedInfo.Vectors[1];

        SpawnProjectile(FireMode->ProjectileClass, FireLoc, FireDir.Rotation());

        ConsumeAmmo(FireMode);
    }
}

void AUR_Weapon::AuthorityHitscanShot_Implementation(UUR_FireModeBasic* FireMode, const FSimulatedShotInfo& SimulatedInfo, FHitscanVisualInfo& OutHitscanInfo)
{
    //TODO: validate passed in start location
    FVector TraceStart = SimulatedInfo.Vectors[0];

    FVector FireDir = SimulatedInfo.Vectors[1];
    FireDir.Normalize();

    FVector TraceEnd = TraceStart + FireMode->HitscanTraceDistance * FireDir;

    FHitResult Hit;
    HitscanTrace(TraceStart, TraceEnd, Hit);

    if (Hit.bBlockingHit && Hit.GetActor())
    {
        float Damage = FireMode->HitscanDamage;
        auto DamType = FireMode->HitscanDamageType;
        UGameplayStatics::ApplyPointDamage(Hit.GetActor(), Damage, FireDir, Hit, GetInstigatorController(), this, DamType);
    }

    OutHitscanInfo.Vectors.EmplaceAt(0, Hit.bBlockingHit ? Hit.Location : Hit.TraceEnd);
    OutHitscanInfo.Vectors.EmplaceAt(1, Hit.ImpactNormal);

    ConsumeAmmo(FireMode);
}

void AUR_Weapon::PlayFireEffects_Implementation(UUR_FireModeBasic* FireMode)
{
    if (UUR_FunctionLibrary::IsViewingFirstPerson(URCharOwner))
    {
        UGameplayStatics::SpawnSoundAttached(FireMode->FireSound, Mesh1P, FireMode->MuzzleSocketName, FVector(0, 0, 0), EAttachLocation::SnapToTarget);
        UGameplayStatics::SpawnEmitterAttached(FireMode->MuzzleFlashFX, Mesh1P, FireMode->MuzzleSocketName, FVector(0, 0, 0), FRotator(0, 0, 0), EAttachLocation::SnapToTargetIncludingScale);
        URCharOwner->MeshFirstPerson->PlayAnimation(URCharOwner->FireAnimation, false);
    }
    else
    {
        UGameplayStatics::SpawnSoundAttached(FireMode->FireSound, Mesh3P, FireMode->MuzzleSocketName, FVector(0, 0, 0), EAttachLocation::SnapToTarget);
        UGameplayStatics::SpawnEmitterAttached(FireMode->MuzzleFlashFX, Mesh3P, FireMode->MuzzleSocketName, FVector(0, 0, 0), FRotator(0, 0, 0), EAttachLocation::SnapToTargetIncludingScale);
        //TODO: play 3p anim
    }
}

void AUR_Weapon::PlayHitscanEffects_Implementation(UUR_FireModeBasic* FireMode, const FHitscanVisualInfo& HitscanInfo)
{
    FVector BeamStart;
    if (UUR_FunctionLibrary::IsViewingFirstPerson(URCharOwner))
    {
        BeamStart = Mesh1P->GetSocketLocation(FireMode->MuzzleSocketName);
    }
    else
    {
        BeamStart = Mesh3P->GetSocketLocation(FireMode->MuzzleSocketName);
    }

    const FVector& BeamEnd = HitscanInfo.Vectors[0];
    FVector BeamVector = BeamEnd - BeamStart;

    UFXSystemComponent* BeamComp = UUR_FunctionLibrary::SpawnEffectAtLocation(GetWorld(), FireMode->BeamTemplate, FTransform(BeamStart));
    if (BeamComp)
    {
        BeamComp->SetVectorParameter(FireMode->BeamVectorParamName, BeamVector);
    }

    // Impact fx & sound
    const FVector& ImpactNormal = HitscanInfo.Vectors[1];
    UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), FireMode->BeamImpactTemplate, FTransform(ImpactNormal.Rotation(), BeamEnd));
    UGameplayStatics::PlaySoundAtLocation(GetWorld(), FireMode->BeamImpactSound, BeamEnd);
}


//============================================================
// FireModeCharged callbacks
//============================================================

void AUR_Weapon::ChargeLevel_Implementation(UUR_FireModeCharged* FireMode)
{

}


//============================================================
// FireModeContinuous callbacks
//============================================================

void AUR_Weapon::FiringTick(UUR_FireModeContinuous* FireMode)
{

}
