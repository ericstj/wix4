// Copyright (c) .NET Foundation and contributors. All rights reserved. Licensed under the Microsoft Reciprocal License. See LICENSE.TXT file in the project root for full license information.

#include "precomp.h"

// constants

#define INITIAL_STRINGDICT_SIZE 48
const LPCWSTR vcszIgnoreDependenciesDelim = L";";


// internal function declarations

static HRESULT DetectPackageDependents(
    __in BURN_PACKAGE* pPackage,
    __in STRINGDICT_HANDLE sdIgnoredDependents,
    __in const BURN_REGISTRATION* pRegistration
    );

static HRESULT SplitIgnoreDependencies(
    __in_z LPCWSTR wzIgnoreDependencies,
    __deref_inout_ecount_opt(*pcDependencies) DEPENDENCY** prgDependencies,
    __inout LPUINT pcDependencies,
    __out BOOL* pfIgnoreAll
    );

static HRESULT JoinIgnoreDependencies(
    __out_z LPWSTR* psczIgnoreDependencies,
    __in_ecount(cDependencies) const DEPENDENCY* rgDependencies,
    __in UINT cDependencies
    );

static HRESULT GetIgnoredDependents(
    __in const BURN_PACKAGE* pPackage,
    __in const BURN_PLAN* pPlan,
    __deref_inout STRINGDICT_HANDLE* psdIgnoredDependents
    );

static BOOL GetProviderExists(
    __in HKEY hkRoot,
    __in_z LPCWSTR wzProviderKey
    );

static void CalculateDependencyActionStates(
    __in const BURN_PACKAGE* pPackage,
    __in const BOOTSTRAPPER_ACTION action,
    __out BURN_DEPENDENCY_ACTION* pDependencyExecuteAction,
    __out BURN_DEPENDENCY_ACTION* pDependencyRollbackAction
    );

static HRESULT AddPackageDependencyActions(
    __in_opt DWORD *pdwInsertSequence,
    __in const BURN_PACKAGE* pPackage,
    __in BURN_PLAN* pPlan,
    __in const BURN_DEPENDENCY_ACTION dependencyExecuteAction,
    __in const BURN_DEPENDENCY_ACTION dependencyRollbackAction
    );

static HRESULT RegisterPackageProvider(
    __in const BURN_PACKAGE* pPackage
    );

static void UnregisterPackageProvider(
    __in const BURN_PACKAGE* pPackage
    );

static HRESULT RegisterPackageDependency(
    __in BOOL fPerMachine,
    __in const BURN_PACKAGE* pPackage,
    __in_z LPCWSTR wzDependentProviderKey
    );

static void UnregisterPackageDependency(
    __in BOOL fPerMachine,
    __in const BURN_PACKAGE* pPackage,
    __in_z LPCWSTR wzDependentProviderKey
    );


// functions

extern "C" void DependencyUninitializeProvider(
    __in BURN_DEPENDENCY_PROVIDER* pProvider
    )
{
    ReleaseStr(pProvider->sczKey);
    ReleaseStr(pProvider->sczVersion);
    ReleaseStr(pProvider->sczDisplayName);
    ReleaseDependencyArray(pProvider->rgDependents, pProvider->cDependents);

    memset(pProvider, 0, sizeof(BURN_DEPENDENCY_PROVIDER));
}

extern "C" HRESULT DependencyParseProvidersFromXml(
    __in BURN_PACKAGE* pPackage,
    __in IXMLDOMNode* pixnPackage
    )
{
    HRESULT hr = S_OK;
    IXMLDOMNodeList* pixnNodes = NULL;
    DWORD cNodes = 0;
    IXMLDOMNode* pixnNode = NULL;

    // Select dependency provider nodes.
    hr = XmlSelectNodes(pixnPackage, L"Provides", &pixnNodes);
    ExitOnFailure(hr, "Failed to select dependency provider nodes.");

    // Get dependency provider node count.
    hr = pixnNodes->get_length((long*)&cNodes);
    ExitOnFailure(hr, "Failed to get the dependency provider node count.");

    if (!cNodes)
    {
        ExitFunction1(hr = S_OK);
    }

    // Allocate memory for dependency provider pointers.
    pPackage->rgDependencyProviders = (BURN_DEPENDENCY_PROVIDER*)MemAlloc(sizeof(BURN_DEPENDENCY_PROVIDER) * cNodes, TRUE);
    ExitOnNull(pPackage->rgDependencyProviders, hr, E_OUTOFMEMORY, "Failed to allocate memory for dependency providers.");

    pPackage->cDependencyProviders = cNodes;

    // Parse dependency provider elements.
    for (DWORD i = 0; i < cNodes; i++)
    {
        BURN_DEPENDENCY_PROVIDER* pDependencyProvider = &pPackage->rgDependencyProviders[i];

        hr = XmlNextElement(pixnNodes, &pixnNode, NULL);
        ExitOnFailure(hr, "Failed to get the next dependency provider node.");

        // @Key
        hr = XmlGetAttributeEx(pixnNode, L"Key", &pDependencyProvider->sczKey);
        ExitOnFailure(hr, "Failed to get the Key attribute.");

        // @Version
        hr = XmlGetAttributeEx(pixnNode, L"Version", &pDependencyProvider->sczVersion);
        if (E_NOTFOUND != hr)
        {
            ExitOnFailure(hr, "Failed to get the Version attribute.");
        }

        // @DisplayName
        hr = XmlGetAttributeEx(pixnNode, L"DisplayName", &pDependencyProvider->sczDisplayName);
        if (E_NOTFOUND != hr)
        {
            ExitOnFailure(hr, "Failed to get the DisplayName attribute.");
        }

        // @Imported
        hr = XmlGetYesNoAttribute(pixnNode, L"Imported", &pDependencyProvider->fImported);
        if (E_NOTFOUND != hr)
        {
            ExitOnFailure(hr, "Failed to get the Imported attribute.");
        }
        else
        {
            pDependencyProvider->fImported = FALSE;
            hr = S_OK;
        }

        // Prepare next iteration.
        ReleaseNullObject(pixnNode);
    }

    hr = S_OK;

LExit:
    ReleaseObject(pixnNode);
    ReleaseObject(pixnNodes);

    return hr;
}

extern "C" HRESULT DependencyInitialize(
    __in BURN_ENGINE_COMMAND* pInternalCommand,
    __in BURN_DEPENDENCIES* pDependencies,
    __in BURN_REGISTRATION* pRegistration
    )
{
    AssertSz(!pDependencies->cIgnoredDependencies, "Dependencies already initalized.");

    HRESULT hr = S_OK;

    // If no parent was specified at all, use the bundle id as the self dependent.
    if (!pInternalCommand->sczActiveParent)
    {
        pDependencies->wzSelfDependent = pRegistration->sczId;
    }
    else if (*pInternalCommand->sczActiveParent) // if parent was specified use that as the self dependent.
    {
        pDependencies->wzSelfDependent = pInternalCommand->sczActiveParent;
    }
    // else parent:none was used which means we should not register a dependency on ourself.

    pDependencies->wzActiveParent = pInternalCommand->sczActiveParent;

    // The current bundle provider key should always be ignored for dependency checks.
    hr = DepDependencyArrayAlloc(&pDependencies->rgIgnoredDependencies, &pDependencies->cIgnoredDependencies, pRegistration->sczProviderKey, NULL);
    ExitOnFailure(hr, "Failed to add the bundle provider key to the list of dependencies to ignore.");

    // Add the list of dependencies to ignore.
    if (pInternalCommand->sczIgnoreDependencies)
    {
        hr = SplitIgnoreDependencies(pInternalCommand->sczIgnoreDependencies, &pDependencies->rgIgnoredDependencies, &pDependencies->cIgnoredDependencies, &pDependencies->fIgnoreAllDependents);
        ExitOnFailure(hr, "Failed to split the list of dependencies to ignore.");
    }

    pDependencies->fSelfDependent = NULL != pDependencies->wzSelfDependent;
    pDependencies->fActiveParent = NULL != pInternalCommand->sczActiveParent && NULL != *pInternalCommand->sczActiveParent;

LExit:
    return hr;
}

extern "C" void DependencyUninitialize(
    __in BURN_DEPENDENCIES* pDependencies
    )
{
    if (pDependencies->rgIgnoredDependencies)
    {
        ReleaseDependencyArray(pDependencies->rgIgnoredDependencies, pDependencies->cIgnoredDependencies);
    }

    memset(pDependencies, 0, sizeof(BURN_DEPENDENCIES));
}

extern "C" HRESULT DependencyDetectProviderKeyBundleId(
    __in BURN_REGISTRATION* pRegistration
    )
{
    HRESULT hr = S_OK;

    hr = DepGetProviderInformation(pRegistration->hkRoot, pRegistration->sczProviderKey, &pRegistration->sczDetectedProviderKeyBundleId, NULL, NULL);
    if (E_NOTFOUND == hr)
    {
        ExitFunction();
    }
    ExitOnFailure(hr, "Failed to get provider key bundle id.");

    // If a bundle id was not explicitly set, default the provider key bundle id to this bundle's provider key.
    if (!pRegistration->sczDetectedProviderKeyBundleId || !*pRegistration->sczDetectedProviderKeyBundleId)
    {
        hr = StrAllocString(&pRegistration->sczDetectedProviderKeyBundleId, pRegistration->sczProviderKey, 0);
        ExitOnFailure(hr, "Failed to initialize provider key bundle id.");
    }

LExit:
    return hr;
}

extern "C" HRESULT DependencyDetect(
    __in BURN_DEPENDENCIES* pDependencies,
    __in BURN_PACKAGES* pPackages,
    __in BURN_REGISTRATION* pRegistration
    )
{
    HRESULT hr = S_OK;
    STRINGDICT_HANDLE sdIgnoredDependents = NULL;
    BURN_PACKAGE* pPackage = NULL;

    // Always leave this empty so that all dependents get detected. Plan will ignore dependents based on its own logic.
    hr = DictCreateStringList(&sdIgnoredDependents, INITIAL_STRINGDICT_SIZE, DICT_FLAG_CASEINSENSITIVE);
    ExitOnFailure(hr, "Failed to create the string dictionary.");

    hr = DepCheckDependents(pRegistration->hkRoot, pRegistration->sczProviderKey, 0, sdIgnoredDependents, &pRegistration->rgDependents, &pRegistration->cDependents);
    if (E_FILENOTFOUND != hr)
    {
        ExitOnFailure(hr, "Failed dependents check on bundle");
    }
    else
    {
        hr = S_OK;
    }

    for (DWORD iPackage = 0; iPackage < pPackages->cPackages; ++iPackage)
    {
        pPackage = pPackages->rgPackages + iPackage;
        hr = DetectPackageDependents(pPackage, sdIgnoredDependents, pRegistration);
        ExitOnFailure(hr, "Failed to detect dependents for package '%ls'", pPackage->sczId);
    }

    for (DWORD iRelatedBundle = 0; iRelatedBundle < pRegistration->relatedBundles.cRelatedBundles; ++iRelatedBundle)
    {
        BURN_RELATED_BUNDLE* pRelatedBundle = pRegistration->relatedBundles.rgRelatedBundles + iRelatedBundle;
        if (!pRelatedBundle->fPlannable)
        {
            continue;
        }

        pPackage = &pRelatedBundle->package;
        hr = DetectPackageDependents(pPackage, sdIgnoredDependents, pRegistration);
        ExitOnFailure(hr, "Failed to detect dependents for related bundle '%ls'", pPackage->sczId);
    }

    if (pDependencies->fSelfDependent || pDependencies->fActiveParent)
    {
        for (DWORD i = 0; i < pRegistration->cDependents; ++i)
        {
            DEPENDENCY* pDependent = pRegistration->rgDependents + i;

            if (pDependencies->fActiveParent && CSTR_EQUAL == ::CompareStringW(LOCALE_NEUTRAL, NORM_IGNORECASE, pDependencies->wzActiveParent, -1, pDependent->sczKey, -1))
            {
                pRegistration->fParentRegisteredAsDependent = TRUE;
            }

            if (pDependencies->fSelfDependent && CSTR_EQUAL == ::CompareStringW(LOCALE_NEUTRAL, NORM_IGNORECASE, pDependencies->wzSelfDependent, -1, pDependent->sczKey, -1))
            {
                pRegistration->fSelfRegisteredAsDependent = TRUE;
            }
        }
    }

LExit:
    ReleaseDict(sdIgnoredDependents);

    return hr;
}

extern "C" HRESULT DependencyPlanInitialize(
    __in BURN_DEPENDENCIES* pDependencies,
    __in BURN_PLAN* pPlan
    )
{
    HRESULT hr = S_OK;

    // TODO: After adding enumeration to STRINGDICT, a single STRINGDICT_HANDLE can be used everywhere.
    for (DWORD i = 0; i < pDependencies->cIgnoredDependencies; ++i)
    {
        DEPENDENCY* pDependency = pDependencies->rgIgnoredDependencies + i;

        hr = DepDependencyArrayAlloc(&pPlan->rgPlannedProviders, &pPlan->cPlannedProviders, pDependency->sczKey, pDependency->sczName);
        ExitOnFailure(hr, "Failed to add the detected provider to the list of dependencies to ignore.");
    }

LExit:
    return hr;
}

extern "C" HRESULT DependencyAllocIgnoreDependencies(
    __in const BURN_PLAN *pPlan,
    __out_z LPWSTR* psczIgnoreDependencies
    )
{
    HRESULT hr = S_OK;

    // Join the list of dependencies to ignore for each related bundle.
    if (0 < pPlan->cPlannedProviders)
    {
        hr = JoinIgnoreDependencies(psczIgnoreDependencies, pPlan->rgPlannedProviders, pPlan->cPlannedProviders);
        ExitOnFailure(hr, "Failed to join the list of dependencies to ignore.");
    }

LExit:
    return hr;
}

extern "C" HRESULT DependencyAddIgnoreDependencies(
    __in STRINGDICT_HANDLE sdIgnoreDependencies,
    __in_z LPCWSTR wzAddIgnoreDependencies
    )
{
    HRESULT hr = S_OK;
    LPWSTR wzContext = NULL;

    // Parse through the semicolon-delimited tokens and add to the array.
    for (LPCWSTR wzToken = ::wcstok_s(const_cast<LPWSTR>(wzAddIgnoreDependencies), vcszIgnoreDependenciesDelim, &wzContext); wzToken; wzToken = ::wcstok_s(NULL, vcszIgnoreDependenciesDelim, &wzContext))
    {
        hr = DictKeyExists(sdIgnoreDependencies, wzToken);
        if (E_NOTFOUND != hr)
        {
            ExitOnFailure(hr, "Failed to check the dictionary of unique dependencies.");
        }
        else
        {
            hr = DictAddKey(sdIgnoreDependencies, wzToken);
            ExitOnFailure(hr, "Failed to add \"%ls\" to the string dictionary.", wzToken);
        }
    }

LExit:
    return hr;
}

extern "C" HRESULT DependencyPlanPackageBegin(
    __in BOOL fPerMachine,
    __in BURN_PACKAGE* pPackage,
    __in BURN_PLAN* pPlan
    )
{
    HRESULT hr = S_OK;
    STRINGDICT_HANDLE sdIgnoredDependents = NULL;
    BURN_DEPENDENCY_ACTION dependencyExecuteAction = BURN_DEPENDENCY_ACTION_NONE;
    BURN_DEPENDENCY_ACTION dependencyRollbackAction = BURN_DEPENDENCY_ACTION_NONE;

    pPackage->dependencyExecute = BURN_DEPENDENCY_ACTION_NONE;
    pPackage->dependencyRollback = BURN_DEPENDENCY_ACTION_NONE;

    // Make sure the package defines at least one provider.
    if (0 == pPackage->cDependencyProviders)
    {
        LogId(REPORT_VERBOSE, MSG_DEPENDENCY_PACKAGE_SKIP_NOPROVIDERS, pPackage->sczId);
        ExitFunction1(hr = S_OK);
    }

    // Make sure the package is in the same scope as the bundle.
    if (fPerMachine != pPackage->fPerMachine)
    {
        LogId(REPORT_STANDARD, MSG_DEPENDENCY_PACKAGE_SKIP_WRONGSCOPE, pPackage->sczId, LoggingPerMachineToString(fPerMachine), LoggingPerMachineToString(pPackage->fPerMachine));
        ExitFunction1(hr = S_OK);
    }

    // If we're uninstalling the package, check if any dependents are registered.
    if (BOOTSTRAPPER_ACTION_STATE_UNINSTALL == pPackage->execute)
    {
        // Build up a list of dependents to ignore, including the current bundle.
        hr = GetIgnoredDependents(pPackage, pPlan, &sdIgnoredDependents);
        ExitOnFailure(hr, "Failed to build the list of ignored dependents.");

        // Skip the dependency check if "ALL" was authored for IGNOREDEPENDENCIES.
        hr = DictKeyExists(sdIgnoredDependents, L"ALL");
        if (E_NOTFOUND != hr)
        {
            ExitOnFailure(hr, "Failed to check if \"ALL\" was set in IGNOREDEPENDENCIES.");
        }
        else
        {
            hr = S_OK;

            for (DWORD i = 0; i < pPackage->cDependencyProviders; ++i)
            {
                const BURN_DEPENDENCY_PROVIDER* pProvider = pPackage->rgDependencyProviders + i;

                for (DWORD j = 0; j < pProvider->cDependents; ++j)
                {
                    const DEPENDENCY* pDependency = pProvider->rgDependents + j;

                    hr = DictKeyExists(sdIgnoredDependents, pDependency->sczKey);
                    if (E_NOTFOUND == hr)
                    {
                        hr = S_OK;

                        if (!pPackage->fDependencyManagerWasHere)
                        {
                            pPackage->fDependencyManagerWasHere = TRUE;

                            LogId(REPORT_STANDARD, MSG_DEPENDENCY_PACKAGE_HASDEPENDENTS, pPackage->sczId);
                        }

                        LogId(REPORT_VERBOSE, MSG_DEPENDENCY_PACKAGE_DEPENDENT, pDependency->sczKey, LoggingStringOrUnknownIfNull(pDependency->sczName));
                    }
                    ExitOnFailure(hr, "Failed to check the dictionary of ignored dependents.");
                }
            }
        }
    }

    // Calculate the dependency actions before the package itself is planned.
    CalculateDependencyActionStates(pPackage, pPlan->action, &dependencyExecuteAction, &dependencyRollbackAction);

    // If dependents were found, change the action to not uninstall the package.
    if (pPackage->fDependencyManagerWasHere)
    {
        pPackage->execute = BOOTSTRAPPER_ACTION_STATE_NONE;
        pPackage->rollback = BOOTSTRAPPER_ACTION_STATE_NONE;
    }
    else
    {
        // Use the calculated dependency actions as the provider actions if there
        // are any non-imported providers that need to be registered and the package
        // is current (not obsolete).
        if (BOOTSTRAPPER_PACKAGE_STATE_OBSOLETE != pPackage->currentState)
        {
            BOOL fAllImportedProviders = TRUE; // assume all providers were imported.
            for (DWORD i = 0; i < pPackage->cDependencyProviders; ++i)
            {
                const BURN_DEPENDENCY_PROVIDER* pProvider = &pPackage->rgDependencyProviders[i];
                if (!pProvider->fImported)
                {
                    fAllImportedProviders = FALSE;
                    break;
                }
            }

            if (!fAllImportedProviders)
            {
                pPackage->providerExecute = dependencyExecuteAction;
                pPackage->providerRollback = dependencyRollbackAction;
            }
        }

        // If the package will be removed, add its providers to the growing list in the plan.
        if (BOOTSTRAPPER_ACTION_STATE_UNINSTALL == pPackage->execute)
        {
            for (DWORD i = 0; i < pPackage->cDependencyProviders; ++i)
            {
                const BURN_DEPENDENCY_PROVIDER* pProvider = &pPackage->rgDependencyProviders[i];

                hr = DepDependencyArrayAlloc(&pPlan->rgPlannedProviders, &pPlan->cPlannedProviders, pProvider->sczKey, NULL);
                ExitOnFailure(hr, "Failed to add the package provider key \"%ls\" to the planned list.", pProvider->sczKey);
            }
        }
    }

    pPackage->dependencyExecute = dependencyExecuteAction;
    pPackage->dependencyRollback = dependencyRollbackAction;

LExit:
    ReleaseDict(sdIgnoredDependents);

    return hr;
}

extern "C" HRESULT DependencyPlanPackage(
    __in_opt DWORD *pdwInsertSequence,
    __in const BURN_PACKAGE* pPackage,
    __in BURN_PLAN* pPlan
    )
{
    HRESULT hr = S_OK;
    BURN_EXECUTE_ACTION* pAction = NULL;

    // If the dependency execution action is to unregister, add the dependency actions to the plan
    // *before* the provider key is potentially removed.
    if (BURN_DEPENDENCY_ACTION_UNREGISTER == pPackage->dependencyExecute)
    {
        hr = AddPackageDependencyActions(pdwInsertSequence, pPackage, pPlan, pPackage->dependencyExecute, pPackage->dependencyRollback);
        ExitOnFailure(hr, "Failed to plan the dependency actions for package: %ls", pPackage->sczId);
    }

    // Add the provider rollback plan.
    if (BURN_DEPENDENCY_ACTION_NONE != pPackage->providerRollback)
    {
        hr = PlanAppendRollbackAction(pPlan, &pAction);
        ExitOnFailure(hr, "Failed to append provider rollback action.");

        pAction->type = BURN_EXECUTE_ACTION_TYPE_PACKAGE_PROVIDER;
        pAction->packageProvider.pPackage = const_cast<BURN_PACKAGE*>(pPackage);
        pAction->packageProvider.action = pPackage->providerRollback;

        // Put a checkpoint before the execute action so that rollback happens
        // if execute fails.
        hr = PlanExecuteCheckpoint(pPlan);
        ExitOnFailure(hr, "Failed to plan provider checkpoint action.");
    }

    // Add the provider execute plan. This comes after rollback so if something goes wrong
    // rollback will try to clean up after us.
    if (BURN_DEPENDENCY_ACTION_NONE != pPackage->providerExecute)
    {
        if (NULL != pdwInsertSequence)
        {
            hr = PlanInsertExecuteAction(*pdwInsertSequence, pPlan, &pAction);
            ExitOnFailure(hr, "Failed to insert provider execute action.");

            // Always move the sequence after this dependency action so the provider registration
            // stays in front of the inserted actions.
            ++(*pdwInsertSequence);
        }
        else
        {
            hr = PlanAppendExecuteAction(pPlan, &pAction);
            ExitOnFailure(hr, "Failed to append provider execute action.");
        }

        pAction->type = BURN_EXECUTE_ACTION_TYPE_PACKAGE_PROVIDER;
        pAction->packageProvider.pPackage = const_cast<BURN_PACKAGE*>(pPackage);
        pAction->packageProvider.action = pPackage->providerExecute;
    }

LExit:
    return hr;
}

extern "C" HRESULT DependencyPlanPackageComplete(
    __in BURN_PACKAGE* pPackage,
    __in BURN_PLAN* pPlan
    )
{
    HRESULT hr = S_OK;

    // Registration of dependencies happens here, after the package is planned to be
    // installed and all that good stuff.
    if (BURN_DEPENDENCY_ACTION_REGISTER == pPackage->dependencyExecute)
    {
        // Recalculate the dependency actions in case other operations may have changed
        // the package execution state.
        CalculateDependencyActionStates(pPackage, pPlan->action, &pPackage->dependencyExecute, &pPackage->dependencyRollback);

        // If the dependency execution action is *still* to register, add the dependency actions to the plan.
        if (BURN_DEPENDENCY_ACTION_REGISTER == pPackage->dependencyExecute)
        {
            hr = AddPackageDependencyActions(NULL, pPackage, pPlan, pPackage->dependencyExecute, pPackage->dependencyRollback);
            ExitOnFailure(hr, "Failed to plan the dependency actions for package: %ls", pPackage->sczId);
        }
    }

LExit:
    return hr;
}

extern "C" HRESULT DependencyExecutePackageProviderAction(
    __in const BURN_EXECUTE_ACTION* pAction
    )
{
    AssertSz(BURN_EXECUTE_ACTION_TYPE_PACKAGE_PROVIDER == pAction->type, "Execute action type not supported by this function.");

    HRESULT hr = S_OK;
    const BURN_PACKAGE* pPackage = pAction->packageProvider.pPackage;

    // Register or unregister the package provider(s).
    if (BURN_DEPENDENCY_ACTION_REGISTER == pAction->packageProvider.action)
    {
        hr = RegisterPackageProvider(pPackage);
        ExitOnFailure(hr, "Failed to register the package providers.");
    }
    else if (BURN_DEPENDENCY_ACTION_UNREGISTER == pAction->packageProvider.action)
    {
        UnregisterPackageProvider(pPackage);
    }

LExit:
    if (!pPackage->fVital)
    {
        hr = S_OK;
    }

    return hr;
}

extern "C" HRESULT DependencyExecutePackageDependencyAction(
    __in BOOL fPerMachine,
    __in const BURN_EXECUTE_ACTION* pAction
    )
{
    AssertSz(BURN_EXECUTE_ACTION_TYPE_PACKAGE_DEPENDENCY == pAction->type, "Execute action type not supported by this function.");

    HRESULT hr = S_OK;
    const BURN_PACKAGE* pPackage = pAction->packageDependency.pPackage;

    // Register or unregister the bundle as a dependent of each package dependency provider.
    if (BURN_DEPENDENCY_ACTION_REGISTER == pAction->packageDependency.action)
    {
        hr = RegisterPackageDependency(fPerMachine, pPackage, pAction->packageDependency.sczBundleProviderKey);
        ExitOnFailure(hr, "Failed to register the dependency on the package provider.");
    }
    else if (BURN_DEPENDENCY_ACTION_UNREGISTER == pAction->packageDependency.action)
    {
        UnregisterPackageDependency(fPerMachine, pPackage, pAction->packageDependency.sczBundleProviderKey);
    }

LExit:
    if (!pPackage->fVital)
    {
        hr = S_OK;
    }

    return hr;
}

extern "C" HRESULT DependencyRegisterBundle(
    __in const BURN_REGISTRATION* pRegistration
    )
{
    HRESULT hr = S_OK;

    LogId(REPORT_VERBOSE, MSG_DEPENDENCY_BUNDLE_REGISTER, pRegistration->sczProviderKey, pRegistration->pVersion->sczVersion);

    // Register the bundle provider key.
    hr = DepRegisterDependency(pRegistration->hkRoot, pRegistration->sczProviderKey, pRegistration->pVersion->sczVersion, pRegistration->sczDisplayName, pRegistration->sczId, 0);
    ExitOnFailure(hr, "Failed to register the bundle dependency provider.");

LExit:
    return hr;
}

extern "C" HRESULT DependencyProcessDependentRegistration(
    __in const BURN_REGISTRATION* pRegistration,
    __in const BURN_DEPENDENT_REGISTRATION_ACTION* pAction
    )
{
    HRESULT hr = S_OK;

    switch (pAction->type)
    {
    case BURN_DEPENDENT_REGISTRATION_ACTION_TYPE_REGISTER:
        hr = DepRegisterDependent(pRegistration->hkRoot, pRegistration->sczProviderKey, pAction->sczDependentProviderKey, NULL, NULL, 0);
        ExitOnFailure(hr, "Failed to register dependent: %ls", pAction->sczDependentProviderKey);
        break;

    case BURN_DEPENDENT_REGISTRATION_ACTION_TYPE_UNREGISTER:
        hr = DepUnregisterDependent(pRegistration->hkRoot, pRegistration->sczProviderKey, pAction->sczDependentProviderKey);
        ExitOnFailure(hr, "Failed to unregister dependent: %ls", pAction->sczDependentProviderKey);
        break;

    default:
        hr = E_INVALIDARG;
        ExitOnRootFailure(hr, "Unrecognized registration action type: %d", pAction->type);
    }

LExit:
    return hr;
}

extern "C" void DependencyUnregisterBundle(
    __in const BURN_REGISTRATION* pRegistration,
    __in const BURN_PACKAGES* pPackages
    )
{
    HRESULT hr = S_OK;
    LPCWSTR wzDependentProviderKey = pRegistration->sczId;

    // Remove the bundle provider key.
    hr = DepUnregisterDependency(pRegistration->hkRoot, pRegistration->sczProviderKey);
    if (SUCCEEDED(hr))
    {
        LogId(REPORT_VERBOSE, MSG_DEPENDENCY_BUNDLE_UNREGISTERED, pRegistration->sczProviderKey);
    }
    else if (FAILED(hr) && E_FILENOTFOUND != hr)
    {
        LogId(REPORT_VERBOSE, MSG_DEPENDENCY_BUNDLE_UNREGISTERED_FAILED, pRegistration->sczProviderKey, hr);
    }

    // Best effort to make sure this bundle is not registered as a dependent for anything.
    for (DWORD i = 0; i < pPackages->cPackages; ++i)
    {
        const BURN_PACKAGE* pPackage = pPackages->rgPackages + i;
        UnregisterPackageDependency(pPackage->fPerMachine, pPackage, wzDependentProviderKey);
    }

    for (DWORD i = 0; i < pRegistration->relatedBundles.cRelatedBundles; ++i)
    {
        const BURN_PACKAGE* pPackage = &pRegistration->relatedBundles.rgRelatedBundles[i].package;
        UnregisterPackageDependency(pPackage->fPerMachine, pPackage, wzDependentProviderKey);
    }
}

// internal functions


static HRESULT DetectPackageDependents(
    __in BURN_PACKAGE* pPackage,
    __in STRINGDICT_HANDLE sdIgnoredDependents,
    __in const BURN_REGISTRATION* pRegistration
    )
{
    HRESULT hr = S_OK;
    HKEY hkHive = pPackage->fPerMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    BOOL fCanIgnorePresence = pPackage->fCanAffectRegistration && 0 < pPackage->cDependencyProviders &&
                              (BURN_PACKAGE_REGISTRATION_STATE_PRESENT == pPackage->cacheRegistrationState || BURN_PACKAGE_REGISTRATION_STATE_PRESENT == pPackage->installRegistrationState);
    BOOL fBundleRegisteredAsDependent = FALSE;

    // There's currently no point in getting the dependents if the scope doesn't match,
    // because they will just get ignored.
    if (pRegistration->fPerMachine != pPackage->fPerMachine)
    {
        ExitFunction();
    }

    for (DWORD i = 0; i < pPackage->cDependencyProviders; ++i)
    {
        BURN_DEPENDENCY_PROVIDER* pProvider = &pPackage->rgDependencyProviders[i];

        hr = DepCheckDependents(hkHive, pProvider->sczKey, 0, sdIgnoredDependents, &pProvider->rgDependents, &pProvider->cDependents);
        if (E_FILENOTFOUND != hr)
        {
            ExitOnFailure(hr, "Failed dependents check on package provider: %ls", pProvider->sczKey);

            if (!pPackage->fPackageProviderExists && (0 < pProvider->cDependents || GetProviderExists(hkHive, pProvider->sczKey)))
            {
                pPackage->fPackageProviderExists = TRUE;
            }

            if (fCanIgnorePresence && !fBundleRegisteredAsDependent)
            {
                for (DWORD iDependent = 0; iDependent < pProvider->cDependents; ++iDependent)
                {
                    DEPENDENCY* pDependent = pProvider->rgDependents + iDependent;

                    if (CSTR_EQUAL == ::CompareStringW(LOCALE_NEUTRAL, NORM_IGNORECASE, pRegistration->sczId, -1, pDependent->sczKey, -1))
                    {
                        fBundleRegisteredAsDependent = TRUE;
                        break;
                    }
                }
            }
        }
        else
        {
            hr = S_OK;

            if (!pPackage->fPackageProviderExists && GetProviderExists(hkHive, pProvider->sczKey))
            {
                pPackage->fPackageProviderExists = TRUE;
            }
        }
    }

    // Older bundles may not have written the id so try the default.
    if (!pPackage->fPackageProviderExists && BURN_PACKAGE_TYPE_MSI == pPackage->type && pPackage->Msi.sczProductCode && GetProviderExists(hkHive, pPackage->Msi.sczProductCode))
    {
        pPackage->fPackageProviderExists = TRUE;
    }

    if (fCanIgnorePresence && !fBundleRegisteredAsDependent)
    {
        if (BURN_PACKAGE_REGISTRATION_STATE_PRESENT == pPackage->cacheRegistrationState)
        {
            pPackage->cacheRegistrationState = BURN_PACKAGE_REGISTRATION_STATE_IGNORED;
        }
        if (BURN_PACKAGE_REGISTRATION_STATE_PRESENT == pPackage->installRegistrationState)
        {
            pPackage->installRegistrationState = BURN_PACKAGE_REGISTRATION_STATE_IGNORED;
        }
        if (BURN_PACKAGE_TYPE_MSP == pPackage->type)
        {
            for (DWORD i = 0; i < pPackage->Msp.cTargetProductCodes; ++i)
            {
                BURN_MSPTARGETPRODUCT* pTargetProduct = pPackage->Msp.rgTargetProducts + i;

                if (BURN_PACKAGE_REGISTRATION_STATE_PRESENT == pTargetProduct->registrationState)
                {
                    pTargetProduct->registrationState = BURN_PACKAGE_REGISTRATION_STATE_IGNORED;
                }
            }
        }
    }

LExit:
    return hr;
}

/********************************************************************
 SplitIgnoreDependencies - Splits a semicolon-delimited
  string into a list of unique dependencies to ignore.

*********************************************************************/
static HRESULT SplitIgnoreDependencies(
    __in_z LPCWSTR wzIgnoreDependencies,
    __deref_inout_ecount_opt(*pcDependencies) DEPENDENCY** prgDependencies,
    __inout LPUINT pcDependencies,
    __out BOOL* pfIgnoreAll
    )
{
    HRESULT hr = S_OK;
    LPWSTR wzContext = NULL;
    STRINGDICT_HANDLE sdIgnoreDependencies = NULL;
    *pfIgnoreAll = FALSE;

    // Create a dictionary to hold unique dependencies.
    hr = DictCreateStringList(&sdIgnoreDependencies, INITIAL_STRINGDICT_SIZE, DICT_FLAG_CASEINSENSITIVE);
    ExitOnFailure(hr, "Failed to create the string dictionary.");

    // Parse through the semicolon-delimited tokens and add to the array.
    for (LPCWSTR wzToken = ::wcstok_s(const_cast<LPWSTR>(wzIgnoreDependencies), vcszIgnoreDependenciesDelim, &wzContext); wzToken; wzToken = ::wcstok_s(NULL, vcszIgnoreDependenciesDelim, &wzContext))
    {
        hr = DictKeyExists(sdIgnoreDependencies, wzToken);
        if (E_NOTFOUND != hr)
        {
            ExitOnFailure(hr, "Failed to check the dictionary of unique dependencies.");
        }
        else
        {
            hr = DepDependencyArrayAlloc(prgDependencies, pcDependencies, wzToken, NULL);
            ExitOnFailure(hr, "Failed to add \"%ls\" to the list of dependencies to ignore.", wzToken);

            hr = DictAddKey(sdIgnoreDependencies, wzToken);
            ExitOnFailure(hr, "Failed to add \"%ls\" to the string dictionary.", wzToken);

            if (!*pfIgnoreAll && CSTR_EQUAL == ::CompareStringW(LOCALE_NEUTRAL, NORM_IGNORECASE, L"ALL", -1, wzToken, -1))
            {
                *pfIgnoreAll = TRUE;
            }
        }
    }

LExit:
    ReleaseDict(sdIgnoreDependencies);

    return hr;
}

/********************************************************************
 JoinIgnoreDependencies - Joins a list of dependencies
  to ignore into a semicolon-delimited string of unique values.

*********************************************************************/
static HRESULT JoinIgnoreDependencies(
    __out_z LPWSTR* psczIgnoreDependencies,
    __in_ecount(cDependencies) const DEPENDENCY* rgDependencies,
    __in UINT cDependencies
    )
{
    HRESULT hr = S_OK;
    STRINGDICT_HANDLE sdIgnoreDependencies = NULL;

    // Make sure we pass back an empty string if there are no dependencies.
    if (0 == cDependencies)
    {
        ExitFunction1(hr = S_OK);
    }

    // Create a dictionary to hold unique dependencies.
    hr = DictCreateStringList(&sdIgnoreDependencies, INITIAL_STRINGDICT_SIZE, DICT_FLAG_CASEINSENSITIVE);
    ExitOnFailure(hr, "Failed to create the string dictionary.");

    for (UINT i = 0; i < cDependencies; ++i)
    {
        const DEPENDENCY* pDependency = &rgDependencies[i];

        hr = DictKeyExists(sdIgnoreDependencies, pDependency->sczKey);
        if (E_NOTFOUND != hr)
        {
            ExitOnFailure(hr, "Failed to check the dictionary of unique dependencies.");
        }
        else
        {
            if (0 < i)
            {
                hr = StrAllocConcat(psczIgnoreDependencies, vcszIgnoreDependenciesDelim, 1);
                ExitOnFailure(hr, "Failed to append the string delimiter.");
            }

            hr = StrAllocConcat(psczIgnoreDependencies, pDependency->sczKey, 0);
            ExitOnFailure(hr, "Failed to append the key \"%ls\".", pDependency->sczKey);

            hr = DictAddKey(sdIgnoreDependencies, pDependency->sczKey);
            ExitOnFailure(hr, "Failed to add \"%ls\" to the string dictionary.", pDependency->sczKey);
        }
    }

LExit:
    ReleaseDict(sdIgnoreDependencies);

    return hr;
}

/********************************************************************
 GetIgnoredDependents - Combines the current bundle's
  provider key, packages' provider keys that are being uninstalled,
  and any ignored dependencies authored for packages into a string
  list to pass to deputil.

*********************************************************************/
static HRESULT GetIgnoredDependents(
    __in const BURN_PACKAGE* pPackage,
    __in const BURN_PLAN* pPlan,
    __deref_inout STRINGDICT_HANDLE* psdIgnoredDependents
    )
{
    HRESULT hr = S_OK;
    LPWSTR sczIgnoreDependencies = NULL;

    // Create the dictionary and add the bundle provider key initially.
    hr = DictCreateStringList(psdIgnoredDependents, INITIAL_STRINGDICT_SIZE, DICT_FLAG_CASEINSENSITIVE);
    ExitOnFailure(hr, "Failed to create the string dictionary.");

    hr = DictAddKey(*psdIgnoredDependents, pPlan->wzBundleProviderKey);
    ExitOnFailure(hr, "Failed to add the bundle provider key \"%ls\" to the list of ignored dependencies.", pPlan->wzBundleProviderKey);

    // Add previously planned package providers to the dictionary.
    for (DWORD i = 0; i < pPlan->cPlannedProviders; ++i)
    {
        const DEPENDENCY* pDependency = &pPlan->rgPlannedProviders[i];

        hr = DictAddKey(*psdIgnoredDependents, pDependency->sczKey);
        ExitOnFailure(hr, "Failed to add the package provider key \"%ls\" to the list of ignored dependencies.", pDependency->sczKey);
    }

    // Get the IGNOREDEPENDENCIES property if defined.
    hr = PackageGetProperty(pPackage, DEPENDENCY_IGNOREDEPENDENCIES, &sczIgnoreDependencies);
    if (E_NOTFOUND != hr)
    {
        ExitOnFailure(hr, "Failed to get the package property: %ls", DEPENDENCY_IGNOREDEPENDENCIES);

        // TODO: this is the raw value of the property, all property values are currently formatted in a different part of planning.
        hr = DependencyAddIgnoreDependencies(*psdIgnoredDependents, sczIgnoreDependencies);
        ExitOnFailure(hr, "Failed to add the authored ignored dependencies to the cumulative list of ignored dependencies.");
    }
    else
    {
        hr = S_OK;
    }

LExit:
    ReleaseStr(sczIgnoreDependencies);

    return hr;
}

/********************************************************************
 GetProviderExists - Gets whether the provider key is registered.

*********************************************************************/
static BOOL GetProviderExists(
    __in HKEY hkRoot,
    __in_z LPCWSTR wzProviderKey
    )
{
    HRESULT hr = DepGetProviderInformation(hkRoot, wzProviderKey, NULL, NULL, NULL);
    return SUCCEEDED(hr);
}

/********************************************************************
 CalculateDependencyActionStates - Calculates the dependency execute and
  rollback actions for a package.

*********************************************************************/
static void CalculateDependencyActionStates(
    __in const BURN_PACKAGE* pPackage,
    __in const BOOTSTRAPPER_ACTION action,
    __out BURN_DEPENDENCY_ACTION* pDependencyExecuteAction,
    __out BURN_DEPENDENCY_ACTION* pDependencyRollbackAction
    )
{
    switch (action)
    {
    case BOOTSTRAPPER_ACTION_UNINSTALL:
        // Always remove the dependency when uninstalling a bundle even if the package is absent.
        *pDependencyExecuteAction = BURN_DEPENDENCY_ACTION_UNREGISTER;
        break;
    case BOOTSTRAPPER_ACTION_INSTALL: __fallthrough;
    case BOOTSTRAPPER_ACTION_CACHE:
        // Always remove the dependency during rollback when installing a bundle.
        *pDependencyRollbackAction = BURN_DEPENDENCY_ACTION_UNREGISTER;
        __fallthrough;
    case BOOTSTRAPPER_ACTION_MODIFY: __fallthrough;
    case BOOTSTRAPPER_ACTION_REPAIR:
        switch (pPackage->execute)
        {
        case BOOTSTRAPPER_ACTION_STATE_NONE:
            switch (pPackage->requested)
            {
            case BOOTSTRAPPER_REQUEST_STATE_NONE:
                // Register if a newer, compatible package is already installed.
                switch (pPackage->currentState)
                {
                case BOOTSTRAPPER_PACKAGE_STATE_OBSOLETE:
                    if (!pPackage->fPackageProviderExists)
                    {
                        break;
                    }
                    __fallthrough;
                case BOOTSTRAPPER_PACKAGE_STATE_SUPERSEDED:
                    *pDependencyExecuteAction = BURN_DEPENDENCY_ACTION_REGISTER;
                    break;
                }
                break;
            case BOOTSTRAPPER_REQUEST_STATE_PRESENT: __fallthrough;
            case BOOTSTRAPPER_REQUEST_STATE_MEND: __fallthrough;
            case BOOTSTRAPPER_REQUEST_STATE_REPAIR:
                // Register if the package is requested but already installed.
                switch (pPackage->currentState)
                {
                case BOOTSTRAPPER_PACKAGE_STATE_OBSOLETE:
                    if (!pPackage->fPackageProviderExists)
                    {
                        break;
                    }
                    __fallthrough;
                case BOOTSTRAPPER_PACKAGE_STATE_PRESENT: __fallthrough;
                case BOOTSTRAPPER_PACKAGE_STATE_SUPERSEDED:
                    *pDependencyExecuteAction = BURN_DEPENDENCY_ACTION_REGISTER;
                    break;
                }
                break;
            }
            break;
        case BOOTSTRAPPER_ACTION_STATE_UNINSTALL:
            *pDependencyExecuteAction = BURN_DEPENDENCY_ACTION_UNREGISTER;
            break;
        case BOOTSTRAPPER_ACTION_STATE_INSTALL: __fallthrough;
        case BOOTSTRAPPER_ACTION_STATE_MODIFY: __fallthrough;
        case BOOTSTRAPPER_ACTION_STATE_MEND: __fallthrough;
        case BOOTSTRAPPER_ACTION_STATE_REPAIR: __fallthrough;
        case BOOTSTRAPPER_ACTION_STATE_MINOR_UPGRADE: __fallthrough;
            *pDependencyExecuteAction = BURN_DEPENDENCY_ACTION_REGISTER;
            break;
        }
        break;
    }

    switch (*pDependencyExecuteAction)
    {
    case BURN_DEPENDENCY_ACTION_REGISTER:
        switch (pPackage->currentState)
        {
        case BOOTSTRAPPER_PACKAGE_STATE_OBSOLETE: __fallthrough;
        case BOOTSTRAPPER_PACKAGE_STATE_ABSENT: __fallthrough;
            *pDependencyRollbackAction = BURN_DEPENDENCY_ACTION_UNREGISTER;
            break;
        }
        break;
    case BURN_DEPENDENCY_ACTION_UNREGISTER:
        switch (pPackage->currentState)
        {
        case BOOTSTRAPPER_PACKAGE_STATE_PRESENT: __fallthrough;
        case BOOTSTRAPPER_PACKAGE_STATE_SUPERSEDED:
            *pDependencyRollbackAction = BURN_DEPENDENCY_ACTION_REGISTER;
            break;
        }
        break;
    }
}

/********************************************************************
 AddPackageDependencyActions - Adds the dependency execute and rollback
  actions to the plan.

*********************************************************************/
static HRESULT AddPackageDependencyActions(
    __in_opt DWORD *pdwInsertSequence,
    __in const BURN_PACKAGE* pPackage,
    __in BURN_PLAN* pPlan,
    __in const BURN_DEPENDENCY_ACTION dependencyExecuteAction,
    __in const BURN_DEPENDENCY_ACTION dependencyRollbackAction
    )
{
    HRESULT hr = S_OK;
    BURN_EXECUTE_ACTION* pAction = NULL;

    // Add the rollback plan.
    if (BURN_DEPENDENCY_ACTION_NONE != dependencyRollbackAction)
    {
        hr = PlanAppendRollbackAction(pPlan, &pAction);
        ExitOnFailure(hr, "Failed to append rollback action.");

        pAction->type = BURN_EXECUTE_ACTION_TYPE_PACKAGE_DEPENDENCY;
        pAction->packageDependency.pPackage = const_cast<BURN_PACKAGE*>(pPackage);
        pAction->packageDependency.action = dependencyRollbackAction;

        hr = StrAllocString(&pAction->packageDependency.sczBundleProviderKey, pPlan->wzBundleProviderKey, 0);
        ExitOnFailure(hr, "Failed to copy the bundle dependency provider.");

        // Put a checkpoint before the execute action so that rollback happens
        // if execute fails.
        hr = PlanExecuteCheckpoint(pPlan);
        ExitOnFailure(hr, "Failed to plan dependency checkpoint action.");
    }

    // Add the execute plan. This comes after rollback so if something goes wrong
    // rollback will try to clean up after us correctly.
    if (BURN_DEPENDENCY_ACTION_NONE != dependencyExecuteAction)
    {
        if (NULL != pdwInsertSequence)
        {
            hr = PlanInsertExecuteAction(*pdwInsertSequence, pPlan, &pAction);
            ExitOnFailure(hr, "Failed to insert execute action.");

            // Always move the sequence after this dependency action so the dependency registration
            // stays in front of the inserted actions.
            ++(*pdwInsertSequence);
        }
        else
        {
            hr = PlanAppendExecuteAction(pPlan, &pAction);
            ExitOnFailure(hr, "Failed to append execute action.");
        }

        pAction->type = BURN_EXECUTE_ACTION_TYPE_PACKAGE_DEPENDENCY;
        pAction->packageDependency.pPackage = const_cast<BURN_PACKAGE*>(pPackage);
        pAction->packageDependency.action = dependencyExecuteAction;

        hr = StrAllocString(&pAction->packageDependency.sczBundleProviderKey, pPlan->wzBundleProviderKey, 0);
        ExitOnFailure(hr, "Failed to copy the bundle dependency provider.");
    }

LExit:
    return hr;
}

static HRESULT RegisterPackageProvider(
    __in const BURN_PACKAGE* pPackage
    )
{
    HRESULT hr = S_OK;
    LPWSTR wzId = NULL;
    HKEY hkRoot = pPackage->fPerMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    if (pPackage->rgDependencyProviders)
    {
        if (BURN_PACKAGE_TYPE_MSI == pPackage->type)
        {
            wzId = pPackage->Msi.sczProductCode;
        }
        else if (BURN_PACKAGE_TYPE_MSP == pPackage->type)
        {
            wzId = pPackage->Msp.sczPatchCode;
        }

        for (DWORD i = 0; i < pPackage->cDependencyProviders; ++i)
        {
            const BURN_DEPENDENCY_PROVIDER* pProvider = &pPackage->rgDependencyProviders[i];

            if (!pProvider->fImported)
            {
                LogId(REPORT_VERBOSE, MSG_DEPENDENCY_PACKAGE_REGISTER, pProvider->sczKey, pProvider->sczVersion, pPackage->sczId);

                hr = DepRegisterDependency(hkRoot, pProvider->sczKey, pProvider->sczVersion, pProvider->sczDisplayName, wzId, 0);
                ExitOnFailure(hr, "Failed to register the package dependency provider: %ls", pProvider->sczKey);
            }
        }
    }

LExit:
    if (!pPackage->fVital)
    {
        hr = S_OK;
    }

    return hr;
}

/********************************************************************
 UnregisterPackageProvider - Removes each dependency provider
  for the package (if not imported from the package itself).

 Note: Does not check for existing dependents before removing the key.
*********************************************************************/
static void UnregisterPackageProvider(
    __in const BURN_PACKAGE* pPackage
    )
{
    HRESULT hr = S_OK;
    HKEY hkRoot = pPackage->fPerMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    if (pPackage->rgDependencyProviders)
    {
        for (DWORD i = 0; i < pPackage->cDependencyProviders; ++i)
        {
            const BURN_DEPENDENCY_PROVIDER* pProvider = &pPackage->rgDependencyProviders[i];

            if (!pProvider->fImported)
            {
                hr = DepUnregisterDependency(hkRoot, pProvider->sczKey);
                if (SUCCEEDED(hr))
                {
                    LogId(REPORT_VERBOSE, MSG_DEPENDENCY_PACKAGE_UNREGISTERED, pProvider->sczKey, pPackage->sczId);
                }
                else if (FAILED(hr) && E_FILENOTFOUND != hr)
                {
                    LogId(REPORT_VERBOSE, MSG_DEPENDENCY_PACKAGE_UNREGISTERED_FAILED, pProvider->sczKey, pPackage->sczId, hr);
                }
            }
        }
    }
}

/********************************************************************
 RegisterPackageDependency - Registers the provider key
  as a dependent of a package.

*********************************************************************/
static HRESULT RegisterPackageDependency(
    __in BOOL fPerMachine,
    __in const BURN_PACKAGE* pPackage,
    __in_z LPCWSTR wzDependentProviderKey
    )
{
    HRESULT hr = S_OK;
    HKEY hkRoot = fPerMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    // Do not register a dependency on a package in a different install context.
    if (fPerMachine != pPackage->fPerMachine)
    {
        LogId(REPORT_STANDARD, MSG_DEPENDENCY_PACKAGE_SKIP_WRONGSCOPE, pPackage->sczId, LoggingPerMachineToString(fPerMachine), LoggingPerMachineToString(pPackage->fPerMachine));
        ExitFunction1(hr = S_OK);
    }

    if (pPackage->rgDependencyProviders)
    {
        for (DWORD i = 0; i < pPackage->cDependencyProviders; ++i)
        {
            const BURN_DEPENDENCY_PROVIDER* pProvider = &pPackage->rgDependencyProviders[i];

            LogId(REPORT_VERBOSE, MSG_DEPENDENCY_PACKAGE_REGISTER_DEPENDENCY, wzDependentProviderKey, pProvider->sczKey, pPackage->sczId);

            hr = DepRegisterDependent(hkRoot, pProvider->sczKey, wzDependentProviderKey, NULL, NULL, 0);
            if (E_FILENOTFOUND != hr || pPackage->fVital)
            {
                ExitOnFailure(hr, "Failed to register the dependency on package dependency provider: %ls", pProvider->sczKey);
            }
            else
            {
                LogId(REPORT_VERBOSE, MSG_DEPENDENCY_PACKAGE_SKIP_MISSING, pProvider->sczKey, pPackage->sczId);
                hr = S_OK;
            }
        }
    }

LExit:
    return hr;
}

/********************************************************************
 UnregisterPackageDependency - Unregisters the provider key
  as a dependent of a package.

*********************************************************************/
static void UnregisterPackageDependency(
    __in BOOL fPerMachine,
    __in const BURN_PACKAGE* pPackage,
    __in_z LPCWSTR wzDependentProviderKey
    )
{
    HRESULT hr = S_OK;
    HKEY hkRoot = fPerMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    // Should be no registration to remove since we don't write keys across contexts.
    if (fPerMachine != pPackage->fPerMachine)
    {
        LogId(REPORT_STANDARD, MSG_DEPENDENCY_PACKAGE_SKIP_WRONGSCOPE, pPackage->sczId, LoggingPerMachineToString(fPerMachine), LoggingPerMachineToString(pPackage->fPerMachine));
        return;
    }

    // Loop through each package provider and remove the bundle dependency key.
    if (pPackage->rgDependencyProviders)
    {
        for (DWORD i = 0; i < pPackage->cDependencyProviders; ++i)
        {
            const BURN_DEPENDENCY_PROVIDER* pProvider = &pPackage->rgDependencyProviders[i];

            hr = DepUnregisterDependent(hkRoot, pProvider->sczKey, wzDependentProviderKey);
            if (SUCCEEDED(hr))
            {
                LogId(REPORT_VERBOSE, MSG_DEPENDENCY_PACKAGE_UNREGISTERED_DEPENDENCY, wzDependentProviderKey, pProvider->sczKey, pPackage->sczId);
            }
            else if (FAILED(hr) && E_FILENOTFOUND != hr)
            {
                LogId(REPORT_VERBOSE, MSG_DEPENDENCY_PACKAGE_UNREGISTERED_DEPENDENCY_FAILED, wzDependentProviderKey, pProvider->sczKey, pPackage->sczId, hr);
            }
        }
    }
}
