// Copyright (c) 2019-2020 Open Tournament Project, All Rights Reserved.

#include "UR_FireModeBasic.h"

#include "Engine/World.h"
#include "TimerManager.h"

void UUR_FireModeBasic::StartFire_Implementation()
{
    SetBusy(true);

    FSimulatedShotInfo SimulatedInfo;
    /*
    SimulateShotDelegate.ExecuteIfBound(this, SimulatedInfo);

    OnPlayFireEffects.Broadcast(this);

    if (bIsHitscan)
    {
        FHitscanVisualInfo HitscanInfo(SimulatedInfo);
        OnPlayHitscanEffects.Broadcast(this, HitscanInfo);
    }
    */

    if (BasicInterface)
    {
        IUR_FireModeBasicInterface::Execute_PlayFireEffects(BasicInterface.GetObject(), this);

        if (bIsHitscan)
        {
            FHitscanVisualInfo HitscanInfo;
            IUR_FireModeBasicInterface::Execute_SimulateHitscanShot(BasicInterface.GetObject(), this, SimulatedInfo, HitscanInfo);
            IUR_FireModeBasicInterface::Execute_PlayHitscanEffects(BasicInterface.GetObject(), this, HitscanInfo);
        }
        else
        {
            IUR_FireModeBasicInterface::Execute_SimulateShot(BasicInterface.GetObject(), this, SimulatedInfo);
        }
    }

    if (GetNetMode() == NM_Client)
    {
        LocalFireTime = GetWorld()->GetTimeSeconds();
        if (FireInterval > 0.f)
        {
            GetWorld()->GetTimerManager().SetTimer(CooldownTimerHandle, this, &UUR_FireModeBasic::CooldownTimer, FireInterval, false);
        }
        else
        {
            //sanity function, but this should be avoided. use FireModeContinuous instead
            GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UUR_FireModeBasic::CooldownTimer);
        }
    }

    ServerFire(SimulatedInfo);
}

void UUR_FireModeBasic::CooldownTimer()
{
    SetBusy(false);
    /**
    * NOTE: There is no actual timer loop here, the fire loop is event-based.
    * When weapon receives OnFireModeChangedStatus(false),
    * it should check if player is still firing, and call StartFire again if so.
    */
}

float UUR_FireModeBasic::GetTimeUntilIdle_Implementation()
{
    if (bIsBusy)
    {
        return GetWorld()->GetTimerManager().GetTimerRemaining(CooldownTimerHandle);
    }
    return 0.f;
}

float UUR_FireModeBasic::GetCooldownStartTime_Implementation()
{
    if (GetWorld()->GetTimerManager().TimerExists(CooldownTimerHandle))
    {
        return GetWorld()->GetTimeSeconds() - GetWorld()->GetTimerManager().GetTimerElapsed(CooldownTimerHandle);
    }
    return 0.f;
}

void UUR_FireModeBasic::ServerFire_Implementation(const FSimulatedShotInfo& SimulatedInfo)
{
    float Delay;
    /*
    if (TimeUntilReadyToFireDelegate.IsBound())
    {
        Delay = TimeUntilReadyToFireDelegate.Execute();
    }
    */
    if (BaseInterface)
    {
        Delay = IUR_FireModeBaseInterface::Execute_TimeUntilReadyToFire(BaseInterface.GetObject(), this);
    }
    else
    {
        Delay = GetTimeUntilIdle();
    }

    if (Delay > 0.f)
    {
        if (Delay > FMath::Min(0.200f, FireInterval / 2.f))
        {
            // Too much delay, discard this shot
            return;
        }

        // Delay a bit and fire
        FTimerDelegate Callback;
        Callback.BindLambda([this, SimulatedInfo]
        {
            ServerFire_Implementation(SimulatedInfo);
        });
        GetWorld()->GetTimerManager().SetTimer(DelayedFireTimerHandle, Callback, Delay, false);
        return;
    }

    SetBusy(true);

    if (bIsHitscan)
    {
        FHitscanVisualInfo HitscanInfo;
        //AuthorityHitscanShotDelegate.ExecuteIfBound(this, SimulatedInfo, HitscanInfo);
        if (BasicInterface)
        {
            IUR_FireModeBasicInterface::Execute_AuthorityHitscanShot(BasicInterface.GetObject(), this, SimulatedInfo, HitscanInfo);
        }
        MulticastFiredHitscan(HitscanInfo);
    }
    else
    {
        //AuthorityShotDelegate.ExecuteIfBound(this, SimulatedInfo);
        if (BasicInterface)
        {
            IUR_FireModeBasicInterface::Execute_AuthorityShot(BasicInterface.GetObject(), this, SimulatedInfo);
        }
        MulticastFired();
    }

    GetWorld()->GetTimerManager().SetTimer(CooldownTimerHandle, this, &UUR_FireModeBasic::CooldownTimer, FireInterval, false);
}

void UUR_FireModeBasic::MulticastFired_Implementation()
{
    if (GetNetMode() == NM_Client)
    {
        if (LocalFireTime > 0.f)
        {
            LocalConfirmFired();
        }
        else
        {
            //OnPlayFireEffects.Broadcast(this);
            if (BasicInterface)
            {
                IUR_FireModeBasicInterface::Execute_PlayFireEffects(BasicInterface.GetObject(), this);
            }
        }
    }
}

void UUR_FireModeBasic::MulticastFiredHitscan_Implementation(const FHitscanVisualInfo& HitscanInfo)
{
    if (GetNetMode() == NM_Client)
    {
        if (LocalFireTime > 0.f)
        {
            LocalConfirmFired();
        }
        else
        {
            //OnPlayFireEffects.Broadcast(this);
            //OnPlayHitscanEffects.Broadcast(this, HitscanInfo);
            if (BasicInterface)
            {
                IUR_FireModeBasicInterface::Execute_PlayFireEffects(BasicInterface.GetObject(), this);
                IUR_FireModeBasicInterface::Execute_PlayHitscanEffects(BasicInterface.GetObject(), this, HitscanInfo);
            }
        }
    }
}

void UUR_FireModeBasic::LocalConfirmFired()
{
    // Server just fired, adjust our fire loop accordingly
    if (bIsBusy)
    {
        float FirePing = GetWorld()->TimeSince(LocalFireTime);
        float Delay = FireInterval - FirePing / 2.f;
        if (Delay > 0.f)
        {
            GetWorld()->GetTimerManager().SetTimer(CooldownTimerHandle, this, &UUR_FireModeBasic::CooldownTimer, Delay, false);
        }
    }
}
