// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "BindGuard.h"
#include "BindGuardLog.h"

DEFINE_LOG_CATEGORY(LogBindGuard);

#define LOCTEXT_NAMESPACE "FBindGuardModule"

void FBindGuardModule::StartupModule()
{
	UE_LOG(LogBindGuard, Log, TEXT("BindGuard started."));
}

void FBindGuardModule::ShutdownModule()
{
	UE_LOG(LogBindGuard, Log, TEXT("BindGuard shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBindGuardModule, BindGuard)
