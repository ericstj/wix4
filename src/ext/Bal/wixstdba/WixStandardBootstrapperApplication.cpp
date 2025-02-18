// Copyright (c) .NET Foundation and contributors. All rights reserved. Licensed under the Microsoft Reciprocal License. See LICENSE.TXT file in the project root for full license information.

#include "precomp.h"
#include "BalBaseBootstrapperApplicationProc.h"
#include "BalBaseBootstrapperApplication.h"

static const LPCWSTR WIXBUNDLE_VARIABLE_ELEVATED = L"WixBundleElevated";

static const LPCWSTR WIXSTDBA_WINDOW_CLASS = L"WixStdBA";

static const LPCWSTR WIXSTDBA_VARIABLE_INSTALL_FOLDER = L"InstallFolder";
static const LPCWSTR WIXSTDBA_VARIABLE_LAUNCH_TARGET_PATH = L"LaunchTarget";
static const LPCWSTR WIXSTDBA_VARIABLE_LAUNCH_TARGET_ELEVATED_ID = L"LaunchTargetElevatedId";
static const LPCWSTR WIXSTDBA_VARIABLE_LAUNCH_ARGUMENTS = L"LaunchArguments";
static const LPCWSTR WIXSTDBA_VARIABLE_LAUNCH_HIDDEN = L"LaunchHidden";
static const LPCWSTR WIXSTDBA_VARIABLE_LAUNCH_WORK_FOLDER = L"LaunchWorkingFolder";

static const DWORD WIXSTDBA_ACQUIRE_PERCENTAGE = 30;

static const LPCWSTR WIXSTDBA_VARIABLE_BUNDLE_FILE_VERSION = L"WixBundleFileVersion";
static const LPCWSTR WIXSTDBA_VARIABLE_LANGUAGE_ID = L"WixStdBALanguageId";
static const LPCWSTR WIXSTDBA_VARIABLE_RESTART_REQUIRED = L"WixStdBARestartRequired";
static const LPCWSTR WIXSTDBA_VARIABLE_SHOW_VERSION = L"WixStdBAShowVersion";
static const LPCWSTR WIXSTDBA_VARIABLE_SUPPRESS_OPTIONS_UI = L"WixStdBASuppressOptionsUI";

enum WIXSTDBA_STATE
{
    WIXSTDBA_STATE_INITIALIZING,
    WIXSTDBA_STATE_INITIALIZED,
    WIXSTDBA_STATE_HELP,
    WIXSTDBA_STATE_DETECTING,
    WIXSTDBA_STATE_DETECTED,
    WIXSTDBA_STATE_PLANNING,
    WIXSTDBA_STATE_PLANNED,
    WIXSTDBA_STATE_APPLYING,
    WIXSTDBA_STATE_CACHING,
    WIXSTDBA_STATE_CACHED,
    WIXSTDBA_STATE_EXECUTING,
    WIXSTDBA_STATE_EXECUTED,
    WIXSTDBA_STATE_APPLIED,
    WIXSTDBA_STATE_FAILED,
};

enum WM_WIXSTDBA
{
    WM_WIXSTDBA_SHOW_HELP = WM_APP + 100,
    WM_WIXSTDBA_DETECT_PACKAGES,
    WM_WIXSTDBA_PLAN_PACKAGES,
    WM_WIXSTDBA_APPLY_PACKAGES,
    WM_WIXSTDBA_CHANGE_STATE,
    WM_WIXSTDBA_SHOW_FAILURE,
};

// This enum must be kept in the same order as the vrgwzPageNames array.
enum WIXSTDBA_PAGE
{
    WIXSTDBA_PAGE_LOADING,
    WIXSTDBA_PAGE_HELP,
    WIXSTDBA_PAGE_INSTALL,
    WIXSTDBA_PAGE_MODIFY,
    WIXSTDBA_PAGE_PROGRESS,
    WIXSTDBA_PAGE_PROGRESS_PASSIVE,
    WIXSTDBA_PAGE_SUCCESS,
    WIXSTDBA_PAGE_FAILURE,
    COUNT_WIXSTDBA_PAGE,
};

// This array must be kept in the same order as the WIXSTDBA_PAGE enum.
static LPCWSTR vrgwzPageNames[] = {
    L"Loading",
    L"Help",
    L"Install",
    L"Modify",
    L"Progress",
    L"ProgressPassive",
    L"Success",
    L"Failure",
};

enum WIXSTDBA_CONTROL
{
    // Welcome page
    WIXSTDBA_CONTROL_INSTALL_BUTTON = THEME_FIRST_ASSIGN_CONTROL_ID,
    WIXSTDBA_CONTROL_EULA_RICHEDIT,
    WIXSTDBA_CONTROL_EULA_LINK,
    WIXSTDBA_CONTROL_EULA_ACCEPT_CHECKBOX,

    // Modify page
    WIXSTDBA_CONTROL_REPAIR_BUTTON,
    WIXSTDBA_CONTROL_UNINSTALL_BUTTON,

    // Progress page
    WIXSTDBA_CONTROL_CACHE_PROGRESS_PACKAGE_TEXT,
    WIXSTDBA_CONTROL_CACHE_PROGRESS_BAR,
    WIXSTDBA_CONTROL_CACHE_PROGRESS_TEXT,

    WIXSTDBA_CONTROL_EXECUTE_PROGRESS_PACKAGE_TEXT,
    WIXSTDBA_CONTROL_EXECUTE_PROGRESS_BAR,
    WIXSTDBA_CONTROL_EXECUTE_PROGRESS_TEXT,
    WIXSTDBA_CONTROL_EXECUTE_PROGRESS_ACTIONDATA_TEXT,

    WIXSTDBA_CONTROL_OVERALL_PROGRESS_PACKAGE_TEXT,
    WIXSTDBA_CONTROL_OVERALL_PROGRESS_BAR,
    WIXSTDBA_CONTROL_OVERALL_CALCULATED_PROGRESS_BAR,
    WIXSTDBA_CONTROL_OVERALL_PROGRESS_TEXT,

    WIXSTDBA_CONTROL_PROGRESS_CANCEL_BUTTON,

    // Success page
    WIXSTDBA_CONTROL_LAUNCH_BUTTON,
    WIXSTDBA_CONTROL_SUCCESS_RESTART_BUTTON,

    // Failure page
    WIXSTDBA_CONTROL_FAILURE_LOGFILE_LINK,
    WIXSTDBA_CONTROL_FAILURE_MESSAGE_TEXT,
    WIXSTDBA_CONTROL_FAILURE_RESTART_BUTTON,
};

static THEME_ASSIGN_CONTROL_ID vrgInitControls[] = {
    { WIXSTDBA_CONTROL_INSTALL_BUTTON, L"InstallButton" },
    { WIXSTDBA_CONTROL_EULA_RICHEDIT, L"EulaRichedit" },
    { WIXSTDBA_CONTROL_EULA_LINK, L"EulaHyperlink" },
    { WIXSTDBA_CONTROL_EULA_ACCEPT_CHECKBOX, L"EulaAcceptCheckbox" },

    { WIXSTDBA_CONTROL_REPAIR_BUTTON, L"RepairButton" },
    { WIXSTDBA_CONTROL_UNINSTALL_BUTTON, L"UninstallButton" },

    { WIXSTDBA_CONTROL_CACHE_PROGRESS_PACKAGE_TEXT, L"CacheProgressPackageText" },
    { WIXSTDBA_CONTROL_CACHE_PROGRESS_BAR, L"CacheProgressbar" },
    { WIXSTDBA_CONTROL_CACHE_PROGRESS_TEXT, L"CacheProgressText" },
    { WIXSTDBA_CONTROL_EXECUTE_PROGRESS_PACKAGE_TEXT, L"ExecuteProgressPackageText" },
    { WIXSTDBA_CONTROL_EXECUTE_PROGRESS_BAR, L"ExecuteProgressbar" },
    { WIXSTDBA_CONTROL_EXECUTE_PROGRESS_TEXT, L"ExecuteProgressText" },
    { WIXSTDBA_CONTROL_EXECUTE_PROGRESS_ACTIONDATA_TEXT, L"ExecuteProgressActionDataText"},
    { WIXSTDBA_CONTROL_OVERALL_PROGRESS_PACKAGE_TEXT, L"OverallProgressPackageText" },
    { WIXSTDBA_CONTROL_OVERALL_PROGRESS_BAR, L"OverallProgressbar" },
    { WIXSTDBA_CONTROL_OVERALL_CALCULATED_PROGRESS_BAR, L"OverallCalculatedProgressbar" },
    { WIXSTDBA_CONTROL_OVERALL_PROGRESS_TEXT, L"OverallProgressText" },
    { WIXSTDBA_CONTROL_PROGRESS_CANCEL_BUTTON, L"ProgressCancelButton" },

    { WIXSTDBA_CONTROL_LAUNCH_BUTTON, L"LaunchButton" },
    { WIXSTDBA_CONTROL_SUCCESS_RESTART_BUTTON, L"SuccessRestartButton" },

    { WIXSTDBA_CONTROL_FAILURE_LOGFILE_LINK, L"FailureLogFileLink" },
    { WIXSTDBA_CONTROL_FAILURE_MESSAGE_TEXT, L"FailureMessageText" },
    { WIXSTDBA_CONTROL_FAILURE_RESTART_BUTTON, L"FailureRestartButton" },
};

typedef struct _WIXSTDBA_PACKAGE_INFO
{
    LPWSTR sczPackageId;
    BOOL fWasAlreadyInstalled;
    BOOL fPlannedToBeInstalled;
    BOOL fSuccessfullyInstalled;
} WIXSTDBA_PACKAGE_INFO;


static HRESULT DAPI EvaluateVariableConditionCallback(
    __in_z LPCWSTR wzCondition,
    __out BOOL* pf,
    __in_opt LPVOID pvContext
    );
static HRESULT DAPI FormatVariableStringCallback(
    __in_z LPCWSTR wzFormat,
    __inout LPWSTR* psczOut,
    __in_opt LPVOID pvContext
    );
static HRESULT DAPI GetVariableNumericCallback(
    __in_z LPCWSTR wzVariable,
    __out LONGLONG* pllValue,
    __in_opt LPVOID pvContext
    );
static HRESULT DAPI SetVariableNumericCallback(
    __in_z LPCWSTR wzVariable,
    __in LONGLONG llValue,
    __in_opt LPVOID pvContext
    );
static HRESULT DAPI GetVariableStringCallback(
    __in_z LPCWSTR wzVariable,
    __inout LPWSTR* psczValue,
    __in_opt LPVOID pvContext
    );
static HRESULT DAPI SetVariableStringCallback(
    __in_z LPCWSTR wzVariable,
    __in_z_opt LPCWSTR wzValue,
    __in BOOL fFormatted,
    __in_opt LPVOID pvContext
    );
static LPCSTR LoggingBoolToString(
    __in BOOL f
    );
static LPCSTR LoggingRequestStateToString(
    __in BOOTSTRAPPER_REQUEST_STATE requestState
    );
static LPCSTR LoggingMsiFeatureStateToString(
    __in BOOTSTRAPPER_FEATURE_STATE featureState
    );


class CWixStandardBootstrapperApplication : public CBalBaseBootstrapperApplication
{
public: // IBootstrapperApplication
    virtual STDMETHODIMP OnStartup()
    {
        HRESULT hr = S_OK;
        DWORD dwUIThreadId = 0;

        // create UI thread
        m_hUiThread = ::CreateThread(NULL, 0, UiThreadProc, this, 0, &dwUIThreadId);
        if (!m_hUiThread)
        {
            BalExitWithLastError(hr, "Failed to create UI thread.");
        }

    LExit:
        return hr;
    }


    virtual STDMETHODIMP OnShutdown(
        __inout BOOTSTRAPPER_SHUTDOWN_ACTION* pAction
        )
    {
        HRESULT hr = S_OK;

        // wait for UI thread to terminate
        if (m_hUiThread)
        {
            ::WaitForSingleObject(m_hUiThread, INFINITE);
            ReleaseHandle(m_hUiThread);
        }

        // If a restart was required.
        if (m_fRestartRequired)
        {
            if (m_fShouldRestart && m_fAllowRestart)
            {
                *pAction = BOOTSTRAPPER_SHUTDOWN_ACTION_RESTART;
            }

            if (m_fPrereq)
            {
                BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, BOOTSTRAPPER_SHUTDOWN_ACTION_RESTART == *pAction
                    ? "The prerequisites scheduled a restart. The bootstrapper application will be reloaded after the computer is restarted."
                    : "A restart is required by the prerequisites but the user delayed it. The bootstrapper application will be reloaded after the computer is restarted.");
            }
        }
        else if (m_fPrereqInstalled)
        {
            BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "The prerequisites were successfully installed. The bootstrapper application will be reloaded.");
            *pAction = BOOTSTRAPPER_SHUTDOWN_ACTION_RELOAD_BOOTSTRAPPER;
        }
        else if (m_fPrereqAlreadyInstalled)
        {
            BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "The prerequisites were already installed. The bootstrapper application will not be reloaded to prevent an infinite loop.");
        }
        else if (m_fPrereq)
        {
            BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "The prerequisites were not successfully installed, error: 0x%x. The bootstrapper application will be not reloaded.", m_hrFinal);
        }

        return hr;
    }


    virtual STDMETHODIMP OnDetectRelatedBundle(
        __in LPCWSTR wzBundleId,
        __in BOOTSTRAPPER_RELATION_TYPE relationType,
        __in LPCWSTR wzBundleTag,
        __in BOOL fPerMachine,
        __in LPCWSTR wzVersion,
        __in BOOTSTRAPPER_RELATED_OPERATION operation,
        __in BOOL fMissingFromCache,
        __inout BOOL* pfCancel
        )
    {
        BAL_INFO_PACKAGE* pPackage = NULL;

        if (!fMissingFromCache)
        {
            if (SUCCEEDED(BalInfoAddRelatedBundleAsPackage(&m_Bundle.packages, wzBundleId, relationType, fPerMachine, &pPackage)))
            {
                InitializePackageInfoForPackage(pPackage);
            }

            // If we're not doing a prerequisite install, remember when our bundle would cause a downgrade.
            if (!m_fPrereq && BOOTSTRAPPER_RELATED_OPERATION_DOWNGRADE == operation)
            {
                BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "A newer version (v%ls) of this product is installed.", wzVersion);
                m_fDowngrading = TRUE;
            }
        }

        return CBalBaseBootstrapperApplication::OnDetectRelatedBundle(wzBundleId, relationType, wzBundleTag, fPerMachine, wzVersion, operation, fMissingFromCache, pfCancel);
    }


    virtual STDMETHODIMP OnDetectPackageComplete(
        __in LPCWSTR wzPackageId,
        __in HRESULT /*hrStatus*/,
        __in BOOTSTRAPPER_PACKAGE_STATE state
        )
    {
        WIXSTDBA_PACKAGE_INFO* pPackageInfo = NULL;
        BAL_INFO_PACKAGE* pPackage = NULL;

        if (BOOTSTRAPPER_PACKAGE_STATE_PRESENT == state &&
            SUCCEEDED(GetPackageInfo(wzPackageId, &pPackageInfo, &pPackage)) &&
            pPackageInfo)
        {
            // If the package is already installed, remember that.
            pPackageInfo->fWasAlreadyInstalled = TRUE;
        }

        return S_OK;
    }


    virtual STDMETHODIMP OnDetectComplete(
        __in HRESULT hrStatus,
        __in BOOL /*fEligibleForCleanup*/
        )
    {
        HRESULT hr = S_OK;

        // If we're not interacting with the user or we're doing a layout or we're resuming just after a force restart
        // then automatically start planning.
        BOOL fSkipToPlan = SUCCEEDED(hrStatus) &&
                           (BOOTSTRAPPER_DISPLAY_FULL > m_command.display ||
                            BOOTSTRAPPER_ACTION_LAYOUT == m_command.action ||
                            BOOTSTRAPPER_RESUME_TYPE_REBOOT == m_command.resumeType);

        // If we're requiring user input (which currently means Install, Repair, or Uninstall)
        // or if we're skipping to an action that modifies machine state
        // then evaluate conditions.
        BOOL fEvaluateConditions = SUCCEEDED(hrStatus) &&
            (!fSkipToPlan || BOOTSTRAPPER_ACTION_LAYOUT < m_command.action && BOOTSTRAPPER_ACTION_UPDATE_REPLACE > m_command.action);

        if (fEvaluateConditions)
        {
            hrStatus = EvaluateConditions();
        }

        if (FAILED(hrStatus))
        {
            fSkipToPlan = FALSE;
        }
        else
        {
            if (m_fPrereq)
            {
                m_fPrereqAlreadyInstalled = TRUE;

                // At this point we have to assume that all prerequisite packages need to be installed, so set to false if any of them aren't installed.
                for (DWORD i = 0; i < m_Bundle.packages.cPackages; ++i)
                {
                    BAL_INFO_PACKAGE* pPackage = &m_Bundle.packages.rgPackages[i];
                    WIXSTDBA_PACKAGE_INFO* pPackageInfo = reinterpret_cast<WIXSTDBA_PACKAGE_INFO*>(pPackage->pvCustomData);
                    if (pPackage->fPrereqPackage && pPackageInfo && !pPackageInfo->fWasAlreadyInstalled)
                    {
                        m_fPrereqAlreadyInstalled = FALSE;
                        break;
                    }
                }
            }
            else if (m_fDowngrading && BOOTSTRAPPER_ACTION_UNINSTALL < m_command.action)
            {
                if (m_fSuppressDowngradeFailure)
                {
                    BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "Downgrade failure has been suppressed; exiting bundle.");

                    hr = S_OK;
                    SetState(WIXSTDBA_STATE_APPLIED, hr);
                    ExitFunction();
                }
                else
                {
                    // If we are going to apply a downgrade, bail.
                    hr = HRESULT_FROM_WIN32(ERROR_PRODUCT_VERSION);
                    BalExitOnFailure(hr, "Cannot install a product when a newer version is installed.");
                }
            }
        }

        SetState(WIXSTDBA_STATE_DETECTED, hrStatus);

        if (fSkipToPlan)
        {
            ::PostMessageW(m_hWnd, WM_WIXSTDBA_PLAN_PACKAGES, 0, m_command.action);
        }

    LExit:
        return hr;
    }


    virtual STDMETHODIMP OnPlanRelatedBundle(
        __in_z LPCWSTR wzBundleId,
        __in BOOTSTRAPPER_REQUEST_STATE recommendedState,
        __inout_z BOOTSTRAPPER_REQUEST_STATE* pRequestedState,
        __inout BOOL* pfCancel
        )
    {
        // If we're only installing prerequisites, do not touch related bundles.
        if (m_fPrereq)
        {
            *pRequestedState = BOOTSTRAPPER_REQUEST_STATE_NONE;
        }

        return CBalBaseBootstrapperApplication::OnPlanRelatedBundle(wzBundleId, recommendedState, pRequestedState, pfCancel);
    }


    virtual STDMETHODIMP OnPlanPackageBegin(
        __in_z LPCWSTR wzPackageId,
        __in BOOTSTRAPPER_PACKAGE_STATE state,
        __in BOOL fCached,
        __in BOOTSTRAPPER_PACKAGE_CONDITION_RESULT installCondition,
        __in BOOTSTRAPPER_REQUEST_STATE recommendedState,
        __in BOOTSTRAPPER_CACHE_TYPE recommendedCacheType,
        __inout BOOTSTRAPPER_REQUEST_STATE* pRequestState,
        __inout BOOTSTRAPPER_CACHE_TYPE* pRequestedCacheType,
        __inout BOOL* pfCancel
        )
    {
        HRESULT hr = S_OK;
        WIXSTDBA_PACKAGE_INFO* pPackageInfo = NULL;
        BAL_INFO_PACKAGE* pPackage = NULL;

        // If we're planning to install prerequisites, install them. The prerequisites need to be installed
        // in all cases (even uninstall!) so the BA can load next.
        if (m_fPrereq)
        {
            // Only install prerequisite packages, and check the InstallCondition on them.
            BOOL fInstall = FALSE;
            hr = GetPackageInfo(wzPackageId, &pPackageInfo, &pPackage);
            if (SUCCEEDED(hr) && pPackage->fPrereqPackage && pPackageInfo)
            {
                pPackageInfo->fPlannedToBeInstalled = fInstall = BOOTSTRAPPER_PACKAGE_CONDITION_FALSE != installCondition;
            }

            if (fInstall)
            {
                *pRequestState = BOOTSTRAPPER_REQUEST_STATE_PRESENT;
            }
            else
            {
                *pRequestState = BOOTSTRAPPER_REQUEST_STATE_NONE;
            }

            // Don't force cache packages while installing prerequisites.
            if (BOOTSTRAPPER_CACHE_TYPE_FORCE == *pRequestedCacheType)
            {
                *pRequestedCacheType = BOOTSTRAPPER_CACHE_TYPE_KEEP;
            }
        }
        else if (m_sczAfterForcedRestartPackage) // after force restart, skip packages until after the package that caused the restart.
        {
            // After restart we need to finish the dependency registration for our package so allow the package
            // to go present.
            if (CSTR_EQUAL == ::CompareStringW(LOCALE_NEUTRAL, 0, wzPackageId, -1, m_sczAfterForcedRestartPackage, -1))
            {
                // Do not allow a repair because that could put us in a perpetual restart loop.
                if (BOOTSTRAPPER_REQUEST_STATE_REPAIR == *pRequestState)
                {
                    *pRequestState = BOOTSTRAPPER_REQUEST_STATE_PRESENT;
                }

                ReleaseNullStr(m_sczAfterForcedRestartPackage); // no more skipping now.
            }
            else // not the matching package, so skip it.
            {
                BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "Skipping package: %ls, after restart because it was applied before the restart.", wzPackageId);

                *pRequestState = BOOTSTRAPPER_REQUEST_STATE_NONE;
            }
        }

        return CBalBaseBootstrapperApplication::OnPlanPackageBegin(wzPackageId, state, fCached, installCondition, recommendedState, recommendedCacheType, pRequestState, pRequestedCacheType, pfCancel);
    }


    virtual STDMETHODIMP OnPlanMsiPackage(
        __in_z LPCWSTR wzPackageId,
        __in BOOL fExecute,
        __in BOOTSTRAPPER_ACTION_STATE action,
        __inout BOOL* pfCancel,
        __inout BURN_MSI_PROPERTY* pActionMsiProperty,
        __inout INSTALLUILEVEL* pUiLevel,
        __inout BOOL* pfDisableExternalUiHandler
        )
    {
        HRESULT hr = S_OK;
        WIXSTDBA_PACKAGE_INFO* pPackageInfo = NULL;
        BAL_INFO_PACKAGE* pPackage = NULL;
        BOOL fShowInternalUI = FALSE;
        INSTALLUILEVEL uiLevel = INSTALLUILEVEL_NOCHANGE;

        switch (m_command.display)
        {
        case BOOTSTRAPPER_DISPLAY_FULL:
            uiLevel = INSTALLUILEVEL_FULL;
            break;

        case BOOTSTRAPPER_DISPLAY_PASSIVE:
            uiLevel = INSTALLUILEVEL_REDUCED;
            break;
        }

        if (INSTALLUILEVEL_NOCHANGE != uiLevel)
        {
            hr = GetPackageInfo(wzPackageId, &pPackageInfo, &pPackage);
            if (SUCCEEDED(hr) && pPackage->sczDisplayInternalUICondition)
            {
                hr = BalEvaluateCondition(pPackage->sczDisplayInternalUICondition, &fShowInternalUI);
                BalExitOnFailure(hr, "Failed to evaluate condition for package '%ls': %ls", wzPackageId, pPackage->sczDisplayInternalUICondition);

                if (fShowInternalUI)
                {
                    *pUiLevel = uiLevel;
                }
            }
        }

    LExit:
        return __super::OnPlanMsiPackage(wzPackageId, fExecute, action, pfCancel, pActionMsiProperty, pUiLevel, pfDisableExternalUiHandler);
    }


    virtual STDMETHODIMP OnPlanComplete(
        __in HRESULT hrStatus
        )
    {
        HRESULT hr = S_OK;

        if (m_fPrereq)
        {
            m_fPrereqAlreadyInstalled = TRUE;

            // Now that we've planned the packages, we can focus on the prerequisite packages that are supposed to be installed.
            for (DWORD i = 0; i < m_Bundle.packages.cPackages; ++i)
            {
                BAL_INFO_PACKAGE* pPackage = &m_Bundle.packages.rgPackages[i];
                WIXSTDBA_PACKAGE_INFO* pPackageInfo = reinterpret_cast<WIXSTDBA_PACKAGE_INFO*>(pPackage->pvCustomData);
                if (pPackage->fPrereqPackage && pPackageInfo && !pPackageInfo->fWasAlreadyInstalled && pPackageInfo->fPlannedToBeInstalled)
                {
                    m_fPrereqAlreadyInstalled = FALSE;
                    break;
                }
            }
        }

        SetState(WIXSTDBA_STATE_PLANNED, hrStatus);

        if (SUCCEEDED(hrStatus))
        {
            ::PostMessageW(m_hWnd, WM_WIXSTDBA_APPLY_PACKAGES, 0, 0);
        }

        m_fStartedExecution = FALSE;
        m_dwCalculatedCacheProgress = 0;
        m_dwCalculatedExecuteProgress = 0;

        return hr;
    }


    virtual STDMETHODIMP OnPauseAutomaticUpdatesBegin(
        )
    {
        HRESULT hr = S_OK;
        LOC_STRING* pLocString = NULL;
        LPWSTR sczFormattedString = NULL;
        LPCWSTR wz = NULL;

        hr = __super::OnPauseAutomaticUpdatesBegin();

        LocGetString(m_pWixLoc, L"#(loc.PauseAutomaticUpdatesMessage)", &pLocString);

        if (pLocString)
        {
            BalFormatString(pLocString->wzText, &sczFormattedString);
        }

        wz = sczFormattedString ? sczFormattedString : L"Pausing Windows automatic updates";

        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_OVERALL_PROGRESS_PACKAGE_TEXT, wz);

        ReleaseStr(sczFormattedString);
        return hr;
    }


    virtual STDMETHODIMP OnSystemRestorePointBegin(
        )
    {
        HRESULT hr = S_OK;
        LOC_STRING* pLocString = NULL;
        LPWSTR sczFormattedString = NULL;
        LPCWSTR wz = NULL;

        hr = __super::OnSystemRestorePointBegin();

        LocGetString(m_pWixLoc, L"#(loc.SystemRestorePointMessage)", &pLocString);

        if (pLocString)
        {
            BalFormatString(pLocString->wzText, &sczFormattedString);
        }

        wz = sczFormattedString ? sczFormattedString : L"Creating system restore point";

        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_OVERALL_PROGRESS_PACKAGE_TEXT, wz);

        ReleaseStr(sczFormattedString);
        return hr;
    }


    virtual STDMETHODIMP OnCachePackageBegin(
        __in_z LPCWSTR wzPackageId,
        __in DWORD cCachePayloads,
        __in DWORD64 dw64PackageCacheSize,
        __inout BOOL* pfCancel
        )
    {
        if (wzPackageId && *wzPackageId)
        {
            BAL_INFO_PACKAGE* pPackage = NULL;
            HRESULT hr = BalInfoFindPackageById(&m_Bundle.packages, wzPackageId, &pPackage);
            LPCWSTR wz = (SUCCEEDED(hr) && pPackage->sczDisplayName) ? pPackage->sczDisplayName : wzPackageId;

            ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_CACHE_PROGRESS_PACKAGE_TEXT, wz);

            // If something started executing, leave it in the overall progress text.
            if (!m_fStartedExecution)
            {
                ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_OVERALL_PROGRESS_PACKAGE_TEXT, wz);
            }
        }

        return __super::OnCachePackageBegin(wzPackageId, cCachePayloads, dw64PackageCacheSize, pfCancel);
    }


    virtual STDMETHODIMP OnCacheAcquireProgress(
        __in_z LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in DWORD64 dw64Progress,
        __in DWORD64 dw64Total,
        __in DWORD dwOverallPercentage,
        __inout BOOL* pfCancel
        )
    {
#ifdef DEBUG
        BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "WIXSTDBA: OnCacheAcquireProgress() - container/package: %ls, payload: %ls, progress: %I64u, total: %I64u, overall progress: %u%%", wzPackageOrContainerId, wzPayloadId, dw64Progress, dw64Total, dwOverallPercentage);
#endif

        UpdateCacheProgress(dwOverallPercentage);

        return __super::OnCacheAcquireProgress(wzPackageOrContainerId, wzPayloadId, dw64Progress, dw64Total, dwOverallPercentage, pfCancel);
    }


    virtual STDMETHODIMP OnCacheContainerOrPayloadVerifyProgress(
        __in_z LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in DWORD64 dw64Progress,
        __in DWORD64 dw64Total,
        __in DWORD dwOverallPercentage,
        __inout BOOL* pfCancel
        )
    {
#ifdef DEBUG
        BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "WIXSTDBA: OnCacheContainerOrPayloadVerifyProgress() - container/package: %ls, payload: %ls, progress: %I64u, total: %I64u, overall progress: %u%%", wzPackageOrContainerId, wzPayloadId, dw64Progress, dw64Total, dwOverallPercentage);
#endif

        UpdateCacheProgress(dwOverallPercentage);

        return __super::OnCacheContainerOrPayloadVerifyProgress(wzPackageOrContainerId, wzPayloadId, dw64Progress, dw64Total, dwOverallPercentage, pfCancel);
    }


    virtual STDMETHODIMP OnCachePayloadExtractProgress(
        __in_z LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in DWORD64 dw64Progress,
        __in DWORD64 dw64Total,
        __in DWORD dwOverallPercentage,
        __inout BOOL* pfCancel
        )
    {
#ifdef DEBUG
        BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "WIXSTDBA: OnCachePayloadExtractProgress() - container/package: %ls, payload: %ls, progress: %I64u, total: %I64u, overall progress: %u%%", wzPackageOrContainerId, wzPayloadId, dw64Progress, dw64Total, dwOverallPercentage);
#endif

        UpdateCacheProgress(dwOverallPercentage);

        return __super::OnCachePayloadExtractProgress(wzPackageOrContainerId, wzPayloadId, dw64Progress, dw64Total, dwOverallPercentage, pfCancel);
    }


    virtual STDMETHODIMP OnCacheVerifyProgress(
        __in_z LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in DWORD64 dw64Progress,
        __in DWORD64 dw64Total,
        __in DWORD dwOverallPercentage,
        __in BOOTSTRAPPER_CACHE_VERIFY_STEP verifyStep,
        __inout BOOL* pfCancel
        )
    {
#ifdef DEBUG
        BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "WIXSTDBA: OnCacheVerifyProgress() - container/package: %ls, payload: %ls, progress: %I64u, total: %I64u, overall progress: %u%%, step: %u", wzPackageOrContainerId, wzPayloadId, dw64Progress, dw64Total, dwOverallPercentage, verifyStep);
#endif

        UpdateCacheProgress(dwOverallPercentage);

        return __super::OnCacheVerifyProgress(wzPackageOrContainerId, wzPayloadId, dw64Progress, dw64Total, dwOverallPercentage, verifyStep, pfCancel);
    }


    virtual STDMETHODIMP OnCacheAcquireComplete(
        __in_z LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in HRESULT hrStatus,
        __in BOOTSTRAPPER_CACHEACQUIRECOMPLETE_ACTION recommendation,
        __inout BOOTSTRAPPER_CACHEACQUIRECOMPLETE_ACTION* pAction
        )
    {
        SetProgressState(hrStatus);
        return __super::OnCacheAcquireComplete(wzPackageOrContainerId, wzPayloadId, hrStatus, recommendation, pAction);
    }


    virtual STDMETHODIMP OnCacheContainerOrPayloadVerifyComplete(
        __in_z LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in HRESULT hrStatus
        )
    {
        SetProgressState(hrStatus);
        return __super::OnCacheContainerOrPayloadVerifyComplete(wzPackageOrContainerId, wzPayloadId, hrStatus);
    }


    virtual STDMETHODIMP OnCachePayloadExtractComplete(
        __in_z LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in HRESULT hrStatus
        )
    {
        SetProgressState(hrStatus);
        return __super::OnCachePayloadExtractComplete(wzPackageOrContainerId, wzPayloadId, hrStatus);
    }


    virtual STDMETHODIMP OnCacheVerifyComplete(
        __in_z LPCWSTR wzPackageId,
        __in_z LPCWSTR wzPayloadId,
        __in HRESULT hrStatus,
        __in BOOTSTRAPPER_CACHEVERIFYCOMPLETE_ACTION recommendation,
        __inout BOOTSTRAPPER_CACHEVERIFYCOMPLETE_ACTION* pAction
        )
    {
        SetProgressState(hrStatus);
        return __super::OnCacheVerifyComplete(wzPackageId, wzPayloadId, hrStatus, recommendation, pAction);
    }


    virtual STDMETHODIMP OnCacheComplete(
        __in HRESULT hrStatus
        )
    {
        UpdateCacheProgress(SUCCEEDED(hrStatus) ? 100 : 0);
        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_CACHE_PROGRESS_PACKAGE_TEXT, L"");
        SetState(WIXSTDBA_STATE_CACHED, S_OK); // we always return success here and let OnApplyComplete() deal with the error.
        return __super::OnCacheComplete(hrStatus);
    }


    virtual STDMETHODIMP OnError(
        __in BOOTSTRAPPER_ERROR_TYPE errorType,
        __in LPCWSTR wzPackageId,
        __in DWORD dwCode,
        __in_z LPCWSTR wzError,
        __in DWORD dwUIHint,
        __in DWORD /*cData*/,
        __in_ecount_z_opt(cData) LPCWSTR* /*rgwzData*/,
        __in int /*nRecommendation*/,
        __inout int* pResult
        )
    {
        HRESULT hr = S_OK;
        int nResult = *pResult;
        LPWSTR sczError = NULL;

        if (BOOTSTRAPPER_DISPLAY_EMBEDDED == m_command.display)
        {
            hr = m_pEngine->SendEmbeddedError(dwCode, wzError, dwUIHint, &nResult);
            if (FAILED(hr))
            {
                nResult = IDERROR;
            }
        }
        else if (BOOTSTRAPPER_DISPLAY_FULL == m_command.display)
        {
            // If this is an authentication failure, let the engine try to handle it for us.
            if (BOOTSTRAPPER_ERROR_TYPE_HTTP_AUTH_SERVER == errorType || BOOTSTRAPPER_ERROR_TYPE_HTTP_AUTH_PROXY == errorType)
            {
                nResult = IDTRYAGAIN;
            }
            else // show a generic error message box.
            {
                BalRetryErrorOccurred(wzPackageId, dwCode);

                if (!m_fShowingInternalUiThisPackage)
                {
                    // If no error message was provided, use the error code to try and get an error message.
                    if (!wzError || !*wzError || BOOTSTRAPPER_ERROR_TYPE_WINDOWS_INSTALLER != errorType)
                    {
                        hr = StrAllocFromError(&sczError, dwCode, NULL);
                        if (FAILED(hr) || !sczError || !*sczError)
                        {
                            // special case for ERROR_FAIL_NOACTION_REBOOT: use loc string for Windows XP
                            if (ERROR_FAIL_NOACTION_REBOOT == dwCode)
                            {
                                LOC_STRING* pLocString = NULL;
                                hr = LocGetString(m_pWixLoc, L"#(loc.ErrorFailNoActionReboot)", &pLocString);
                                if (SUCCEEDED(hr))
                                {
                                    StrAllocString(&sczError, pLocString->wzText, 0);
                                }
                                else
                                {
                                    StrAllocFormatted(&sczError, L"0x%x", dwCode);
                                }
                            }
                            else
                            {
                                StrAllocFormatted(&sczError, L"0x%x", dwCode);
                            }
                        }
                        hr = S_OK;
                    }

                    nResult = ::MessageBoxW(m_hWnd, sczError ? sczError : wzError, m_pTheme->sczCaption, dwUIHint);
                }
            }

            SetProgressState(HRESULT_FROM_WIN32(dwCode));
        }
        else // just take note of the error code and let things continue.
        {
            BalRetryErrorOccurred(wzPackageId, dwCode);
        }

        ReleaseStr(sczError);
        *pResult = nResult;
        return hr;
    }


    virtual STDMETHODIMP OnExecuteMsiMessage(
        __in_z LPCWSTR wzPackageId,
        __in INSTALLMESSAGE messageType,
        __in DWORD dwUIHint,
        __in_z LPCWSTR wzMessage,
        __in DWORD cData,
        __in_ecount_z_opt(cData) LPCWSTR* rgwzData,
        __in int nRecommendation,
        __inout int* pResult
        )
    {
#ifdef DEBUG
        BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "WIXSTDBA: OnExecuteMsiMessage() - package: %ls, message: %ls", wzPackageId, wzMessage);
#endif
        if (BOOTSTRAPPER_DISPLAY_FULL == m_command.display && (INSTALLMESSAGE_WARNING == messageType || INSTALLMESSAGE_USER == messageType))
        {
            if (!m_fShowingInternalUiThisPackage)
            {
                int nResult = ::MessageBoxW(m_hWnd, wzMessage, m_pTheme->sczCaption, dwUIHint);
                return nResult;
            }
        }

        if (INSTALLMESSAGE_ACTIONSTART == messageType)
        {
            ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_EXECUTE_PROGRESS_ACTIONDATA_TEXT, wzMessage);
        }

        return __super::OnExecuteMsiMessage(wzPackageId, messageType, dwUIHint, wzMessage, cData, rgwzData, nRecommendation, pResult);
    }


    virtual STDMETHODIMP OnProgress(
        __in DWORD dwProgressPercentage,
        __in DWORD dwOverallProgressPercentage,
        __inout BOOL* pfCancel
        )
    {
        WCHAR wzProgress[5] = { };

#ifdef DEBUG
        BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "WIXSTDBA: OnProgress() - progress: %u%%, overall progress: %u%%", dwProgressPercentage, dwOverallProgressPercentage);
#endif

        ::StringCchPrintfW(wzProgress, countof(wzProgress), L"%u%%", dwOverallProgressPercentage);
        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_OVERALL_PROGRESS_TEXT, wzProgress);

        ThemeSetProgressControl(m_pTheme, WIXSTDBA_CONTROL_OVERALL_PROGRESS_BAR, dwOverallProgressPercentage);
        SetTaskbarButtonProgress(dwOverallProgressPercentage);

        return __super::OnProgress(dwProgressPercentage, dwOverallProgressPercentage, pfCancel);
    }


    virtual STDMETHODIMP OnExecutePackageBegin(
        __in_z LPCWSTR wzPackageId,
        __in BOOL fExecute,
        __in BOOTSTRAPPER_ACTION_STATE action,
        __in INSTALLUILEVEL uiLevel,
        __in BOOL fDisableExternalUiHandler,
        __inout BOOL* pfCancel
        )
    {
        HRESULT hr = S_OK;
        LPWSTR sczFormattedString = NULL;
        BOOL fShowingInternalUiThisPackage = FALSE;

        m_fStartedExecution = TRUE;

        if (wzPackageId && *wzPackageId)
        {
            BAL_INFO_PACKAGE* pPackage = NULL;
            BalInfoFindPackageById(&m_Bundle.packages, wzPackageId, &pPackage);

            LPCWSTR wz = wzPackageId;
            if (pPackage)
            {
                LOC_STRING* pLocString = NULL;

                switch (pPackage->type)
                {
                case BAL_INFO_PACKAGE_TYPE_BUNDLE_ADDON:
                    LocGetString(m_pWixLoc, L"#(loc.ExecuteAddonRelatedBundleMessage)", &pLocString);
                    break;

                case BAL_INFO_PACKAGE_TYPE_BUNDLE_PATCH:
                    LocGetString(m_pWixLoc, L"#(loc.ExecutePatchRelatedBundleMessage)", &pLocString);
                    break;

                case BAL_INFO_PACKAGE_TYPE_BUNDLE_UPGRADE:
                    LocGetString(m_pWixLoc, L"#(loc.ExecuteUpgradeRelatedBundleMessage)", &pLocString);
                    break;
                }

                if (pLocString)
                {
                    // If the wix developer is showing a hidden variable in the UI, then obviously they don't care about keeping it safe
                    // so don't go down the rabbit hole of making sure that this is securely freed.
                    BalFormatString(pLocString->wzText, &sczFormattedString);
                }

                wz = sczFormattedString ? sczFormattedString : pPackage->sczDisplayName ? pPackage->sczDisplayName : wzPackageId;
            }

            fShowingInternalUiThisPackage = INSTALLUILEVEL_NONE != (INSTALLUILEVEL_NONE & uiLevel);

            ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_EXECUTE_PROGRESS_PACKAGE_TEXT, wz);
            ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_OVERALL_PROGRESS_PACKAGE_TEXT, wz);
        }

        ::EnterCriticalSection(&m_csShowingInternalUiThisPackage);
        m_fShowingInternalUiThisPackage = fShowingInternalUiThisPackage;
        hr = __super::OnExecutePackageBegin(wzPackageId, fExecute, action, uiLevel, fDisableExternalUiHandler, pfCancel);
        ::LeaveCriticalSection(&m_csShowingInternalUiThisPackage);

        ReleaseStr(sczFormattedString);
        return hr;
    }


    virtual STDMETHODIMP OnExecuteProgress(
        __in_z LPCWSTR wzPackageId,
        __in DWORD dwProgressPercentage,
        __in DWORD dwOverallProgressPercentage,
        __inout BOOL* pfCancel
        )
    {
        WCHAR wzProgress[5] = { };

#ifdef DEBUG
        BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "WIXSTDBA: OnExecuteProgress() - package: %ls, progress: %u%%, overall progress: %u%%", wzPackageId, dwProgressPercentage, dwOverallProgressPercentage);
#endif

        ::StringCchPrintfW(wzProgress, countof(wzProgress), L"%u%%", dwOverallProgressPercentage);
        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_EXECUTE_PROGRESS_TEXT, wzProgress);

        ThemeSetProgressControl(m_pTheme, WIXSTDBA_CONTROL_EXECUTE_PROGRESS_BAR, dwOverallProgressPercentage);

        m_dwCalculatedExecuteProgress = dwOverallProgressPercentage * (100 - WIXSTDBA_ACQUIRE_PERCENTAGE) / 100;
        ThemeSetProgressControl(m_pTheme, WIXSTDBA_CONTROL_OVERALL_CALCULATED_PROGRESS_BAR, m_dwCalculatedCacheProgress + m_dwCalculatedExecuteProgress);

        SetTaskbarButtonProgress(m_dwCalculatedCacheProgress + m_dwCalculatedExecuteProgress);

        return __super::OnExecuteProgress(wzPackageId, dwProgressPercentage, dwOverallProgressPercentage, pfCancel);
    }


    virtual STDMETHODIMP OnExecutePackageComplete(
        __in_z LPCWSTR wzPackageId,
        __in HRESULT hrStatus,
        __in BOOTSTRAPPER_APPLY_RESTART restart,
        __in BOOTSTRAPPER_EXECUTEPACKAGECOMPLETE_ACTION recommendation,
        __inout BOOTSTRAPPER_EXECUTEPACKAGECOMPLETE_ACTION* pAction
        )
    {
        HRESULT hr = S_OK;
        SetProgressState(hrStatus);

        hr = __super::OnExecutePackageComplete(wzPackageId, hrStatus, restart, recommendation, pAction);

        WIXSTDBA_PACKAGE_INFO* pPackageInfo = NULL;
        BAL_INFO_PACKAGE* pPackage;
        HRESULT hrPrereq = GetPackageInfo(wzPackageId, &pPackageInfo, &pPackage);
        if (SUCCEEDED(hrPrereq) && pPackageInfo)
        {
            pPackageInfo->fSuccessfullyInstalled = SUCCEEDED(hrStatus);

            // If the prerequisite required a restart (any restart) then do an immediate
            // restart to ensure that the bundle will get launched again post reboot.
            if (m_fPrereq && pPackage->fPrereqPackage && BOOTSTRAPPER_APPLY_RESTART_NONE != restart)
            {
                *pAction = BOOTSTRAPPER_EXECUTEPACKAGECOMPLETE_ACTION_RESTART;
            }
        }

        return hr;
    }


    virtual STDMETHODIMP OnExecuteComplete(
        __in HRESULT hrStatus
        )
    {
        HRESULT hr = S_OK;

        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_EXECUTE_PROGRESS_PACKAGE_TEXT, L"");
        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_EXECUTE_PROGRESS_ACTIONDATA_TEXT, L"");
        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_OVERALL_PROGRESS_PACKAGE_TEXT, L"");
        ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_PROGRESS_CANCEL_BUTTON, FALSE); // no more cancel.
        m_fShowingInternalUiThisPackage = FALSE;

        SetState(WIXSTDBA_STATE_EXECUTED, S_OK); // we always return success here and let OnApplyComplete() deal with the error.
        SetProgressState(hrStatus);

        return hr;
    }


    virtual STDMETHODIMP OnCacheAcquireResolving(
        __in_z_opt LPCWSTR wzPackageOrContainerId,
        __in_z_opt LPCWSTR wzPayloadId,
        __in_z LPCWSTR* rgSearchPaths,
        __in DWORD /*cSearchPaths*/,
        __in BOOL /*fFoundLocal*/,
        __in DWORD dwRecommendedSearchPath,
        __in_z_opt LPCWSTR /*wzDownloadUrl*/,
        __in_z_opt LPCWSTR /*wzPayloadContainerId*/,
        __in BOOTSTRAPPER_CACHE_RESOLVE_OPERATION /*recommendation*/,
        __inout DWORD* /*pdwChosenSearchPath*/,
        __inout BOOTSTRAPPER_CACHE_RESOLVE_OPERATION* pAction,
        __inout BOOL* pfCancel
        )
    {
        HRESULT hr = S_OK;

        if (BOOTSTRAPPER_CACHE_RESOLVE_NONE == *pAction && BOOTSTRAPPER_DISPLAY_FULL == m_command.display) // prompt to change the source location.
        {
            OPENFILENAMEW ofn = { };
            WCHAR wzFile[MAX_PATH] = { };

            ::StringCchCopyW(wzFile, countof(wzFile), rgSearchPaths[dwRecommendedSearchPath]);

            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = m_hWnd;
            ofn.lpstrFile = wzFile;
            ofn.nMaxFile = countof(wzFile);
            ofn.lpstrFilter = L"All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            ofn.lpstrTitle = m_pTheme->sczCaption;

            if (::GetOpenFileNameW(&ofn))
            {
                hr = m_pEngine->SetLocalSource(wzPackageOrContainerId, wzPayloadId, ofn.lpstrFile);
                *pAction = BOOTSTRAPPER_CACHE_RESOLVE_RETRY;
            }
            else
            {
                *pfCancel = TRUE;
            }
        }
        // else there's nothing more we can do in non-interactive mode

        *pfCancel |= CheckCanceled();
        return hr;
    }


    virtual STDMETHODIMP OnApplyComplete(
        __in HRESULT hrStatus,
        __in BOOTSTRAPPER_APPLY_RESTART restart,
        __in BOOTSTRAPPER_APPLYCOMPLETE_ACTION recommendation,
        __inout BOOTSTRAPPER_APPLYCOMPLETE_ACTION* pAction
        )
    {
        HRESULT hr = S_OK;

        __super::OnApplyComplete(hrStatus, restart, recommendation, pAction);

        m_restartResult = restart; // remember the restart result so we return the correct error code no matter what the user chooses to do in the UI.
        m_fRestartRequired = BOOTSTRAPPER_APPLY_RESTART_NONE != restart;
        BalSetStringVariable(WIXSTDBA_VARIABLE_RESTART_REQUIRED, m_fRestartRequired ? L"1" : NULL, FALSE);

        m_fShouldRestart = m_fRestartRequired && BAL_INFO_RESTART_NEVER < m_BalInfoCommand.restart;

        // Automatically restart if we're not displaying a UI or the command line said to always allow restarts.
        m_fAllowRestart = m_fShouldRestart && (BOOTSTRAPPER_DISPLAY_FULL > m_command.display || BAL_INFO_RESTART_PROMPT < m_BalInfoCommand.restart);

        if (m_fPrereq)
        {
            m_fPrereqInstalled = TRUE;
            BOOL fInstalledAPackage = FALSE;

            for (DWORD i = 0; i < m_Bundle.packages.cPackages; ++i)
            {
                BAL_INFO_PACKAGE* pPackage = &m_Bundle.packages.rgPackages[i];
                WIXSTDBA_PACKAGE_INFO* pPackageInfo = reinterpret_cast<WIXSTDBA_PACKAGE_INFO*>(pPackage->pvCustomData);
                if (pPackage->fPrereqPackage && pPackageInfo && pPackageInfo->fPlannedToBeInstalled && !pPackageInfo->fWasAlreadyInstalled)
                {
                    if (pPackageInfo->fSuccessfullyInstalled)
                    {
                        fInstalledAPackage = TRUE;
                    }
                    else
                    {
                        m_fPrereqInstalled = FALSE;
                        break;
                    }
                }
            }

            m_fPrereqInstalled = m_fPrereqInstalled && fInstalledAPackage;
        }

        // If we are showing UI, wait a beat before moving to the final screen.
        if (BOOTSTRAPPER_DISPLAY_NONE < m_command.display)
        {
            ::Sleep(250);
        }

        SetState(WIXSTDBA_STATE_APPLIED, hrStatus);
        SetTaskbarButtonProgress(100); // show full progress bar, green, yellow, or red

        *pAction = BOOTSTRAPPER_APPLYCOMPLETE_ACTION_NONE;

        return hr;
    }

    virtual STDMETHODIMP OnLaunchApprovedExeComplete(
        __in HRESULT hrStatus,
        __in DWORD /*processId*/
        )
    {
        HRESULT hr = S_OK;

        if (HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) == hrStatus)
        {
            //try with ShelExec next time
            OnClickLaunchButton();
        }
        else
        {
            ::PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
        }

        return hr;
    }

    virtual STDMETHODIMP_(void) BAProcFallback(
        __in BOOTSTRAPPER_APPLICATION_MESSAGE message,
        __in const LPVOID pvArgs,
        __inout LPVOID pvResults,
        __inout HRESULT* phr,
        __in_opt LPVOID /*pvContext*/
        )
    {
        if (!m_pfnBAFunctionsProc || FAILED(*phr))
        {
            return;
        }

        // Always log before and after so we don't get blamed when BAFunctions changes something.
        switch (message)
        {
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTBEGIN:
            OnDetectBeginFallback(reinterpret_cast<BA_ONDETECTBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTCOMPLETE:
            OnDetectCompleteFallback(reinterpret_cast<BA_ONDETECTCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANBEGIN:
            OnPlanBeginFallback(reinterpret_cast<BA_ONPLANBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANCOMPLETE:
            OnPlanCompleteFallback(reinterpret_cast<BA_ONPLANCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONSTARTUP: // BAFunctions is loaded during this event on a separate thread so it's not possible to forward it.
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONSHUTDOWN:
            OnShutdownFallback(reinterpret_cast<BA_ONSHUTDOWN_ARGS*>(pvArgs), reinterpret_cast<BA_ONSHUTDOWN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONSYSTEMSHUTDOWN:
            OnSystemShutdownFallback(reinterpret_cast<BA_ONSYSTEMSHUTDOWN_ARGS*>(pvArgs), reinterpret_cast<BA_ONSYSTEMSHUTDOWN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTFORWARDCOMPATIBLEBUNDLE:
            OnDetectForwardCompatibleBundleFallback(reinterpret_cast<BA_ONDETECTFORWARDCOMPATIBLEBUNDLE_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTFORWARDCOMPATIBLEBUNDLE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTUPDATEBEGIN:
            OnDetectUpdateBeginFallback(reinterpret_cast<BA_ONDETECTUPDATEBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTUPDATEBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTUPDATE:
            OnDetectUpdateFallback(reinterpret_cast<BA_ONDETECTUPDATE_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTUPDATE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTUPDATECOMPLETE:
            OnDetectUpdateCompleteFallback(reinterpret_cast<BA_ONDETECTUPDATECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTUPDATECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTRELATEDBUNDLE:
            OnDetectRelatedBundleFallback(reinterpret_cast<BA_ONDETECTRELATEDBUNDLE_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTRELATEDBUNDLE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTPACKAGEBEGIN:
            OnDetectPackageBeginFallback(reinterpret_cast<BA_ONDETECTPACKAGEBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTPACKAGEBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTRELATEDMSIPACKAGE:
            OnDetectRelatedMsiPackageFallback(reinterpret_cast<BA_ONDETECTRELATEDMSIPACKAGE_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTRELATEDMSIPACKAGE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTPATCHTARGET:
            OnDetectPatchTargetFallback(reinterpret_cast<BA_ONDETECTPATCHTARGET_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTPATCHTARGET_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTMSIFEATURE:
            OnDetectMsiFeatureFallback(reinterpret_cast<BA_ONDETECTMSIFEATURE_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTMSIFEATURE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONDETECTPACKAGECOMPLETE:
            OnDetectPackageCompleteFallback(reinterpret_cast<BA_ONDETECTPACKAGECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONDETECTPACKAGECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANRELATEDBUNDLE:
            OnPlanRelatedBundleFallback(reinterpret_cast<BA_ONPLANRELATEDBUNDLE_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANRELATEDBUNDLE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANPACKAGEBEGIN:
            OnPlanPackageBeginFallback(reinterpret_cast<BA_ONPLANPACKAGEBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANPACKAGEBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANPATCHTARGET:
            OnPlanPatchTargetFallback(reinterpret_cast<BA_ONPLANPATCHTARGET_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANPATCHTARGET_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANMSIFEATURE:
            OnPlanMsiFeatureFallback(reinterpret_cast<BA_ONPLANMSIFEATURE_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANMSIFEATURE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANPACKAGECOMPLETE:
            OnPlanPackageCompleteFallback(reinterpret_cast<BA_ONPLANPACKAGECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANPACKAGECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONAPPLYBEGIN:
            OnApplyBeginFallback(reinterpret_cast<BA_ONAPPLYBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONAPPLYBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONELEVATEBEGIN:
            OnElevateBeginFallback(reinterpret_cast<BA_ONELEVATEBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONELEVATEBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONELEVATECOMPLETE:
            OnElevateCompleteFallback(reinterpret_cast<BA_ONELEVATECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONELEVATECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPROGRESS:
            OnProgressFallback(reinterpret_cast<BA_ONPROGRESS_ARGS*>(pvArgs), reinterpret_cast<BA_ONPROGRESS_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONERROR:
            OnErrorFallback(reinterpret_cast<BA_ONERROR_ARGS*>(pvArgs), reinterpret_cast<BA_ONERROR_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONREGISTERBEGIN:
            OnRegisterBeginFallback(reinterpret_cast<BA_ONREGISTERBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONREGISTERBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONREGISTERCOMPLETE:
            OnRegisterCompleteFallback(reinterpret_cast<BA_ONREGISTERCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONREGISTERCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEBEGIN:
            OnCacheBeginFallback(reinterpret_cast<BA_ONCACHEBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEPACKAGEBEGIN:
            OnCachePackageBeginFallback(reinterpret_cast<BA_ONCACHEPACKAGEBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEPACKAGEBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEACQUIREBEGIN:
            OnCacheAcquireBeginFallback(reinterpret_cast<BA_ONCACHEACQUIREBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEACQUIREBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEACQUIREPROGRESS:
            OnCacheAcquireProgressFallback(reinterpret_cast<BA_ONCACHEACQUIREPROGRESS_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEACQUIREPROGRESS_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEACQUIRERESOLVING:
            OnCacheAcquireResolvingFallback(reinterpret_cast<BA_ONCACHEACQUIRERESOLVING_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEACQUIRERESOLVING_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEACQUIRECOMPLETE:
            OnCacheAcquireCompleteFallback(reinterpret_cast<BA_ONCACHEACQUIRECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEACQUIRECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEVERIFYBEGIN:
            OnCacheVerifyBeginFallback(reinterpret_cast<BA_ONCACHEVERIFYBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEVERIFYBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEVERIFYCOMPLETE:
            OnCacheVerifyCompleteFallback(reinterpret_cast<BA_ONCACHEVERIFYCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEVERIFYCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEPACKAGECOMPLETE:
            OnCachePackageCompleteFallback(reinterpret_cast<BA_ONCACHEPACKAGECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEPACKAGECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHECOMPLETE:
            OnCacheCompleteFallback(reinterpret_cast<BA_ONCACHECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONEXECUTEBEGIN:
            OnExecuteBeginFallback(reinterpret_cast<BA_ONEXECUTEBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONEXECUTEBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONEXECUTEPACKAGEBEGIN:
            OnExecutePackageBeginFallback(reinterpret_cast<BA_ONEXECUTEPACKAGEBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONEXECUTEPACKAGEBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONEXECUTEPATCHTARGET:
            OnExecutePatchTargetFallback(reinterpret_cast<BA_ONEXECUTEPATCHTARGET_ARGS*>(pvArgs), reinterpret_cast<BA_ONEXECUTEPATCHTARGET_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONEXECUTEPROGRESS:
            OnExecuteProgressFallback(reinterpret_cast<BA_ONEXECUTEPROGRESS_ARGS*>(pvArgs), reinterpret_cast<BA_ONEXECUTEPROGRESS_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONEXECUTEMSIMESSAGE:
            OnExecuteMsiMessageFallback(reinterpret_cast<BA_ONEXECUTEMSIMESSAGE_ARGS*>(pvArgs), reinterpret_cast<BA_ONEXECUTEMSIMESSAGE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONEXECUTEFILESINUSE:
            OnExecuteFilesInUseFallback(reinterpret_cast<BA_ONEXECUTEFILESINUSE_ARGS*>(pvArgs), reinterpret_cast<BA_ONEXECUTEFILESINUSE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONEXECUTEPACKAGECOMPLETE:
            OnExecutePackageCompleteFallback(reinterpret_cast<BA_ONEXECUTEPACKAGECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONEXECUTEPACKAGECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONEXECUTECOMPLETE:
            OnExecuteCompleteFallback(reinterpret_cast<BA_ONEXECUTECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONEXECUTECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONUNREGISTERBEGIN:
            OnUnregisterBeginFallback(reinterpret_cast<BA_ONUNREGISTERBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONUNREGISTERBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONUNREGISTERCOMPLETE:
            OnUnregisterCompleteFallback(reinterpret_cast<BA_ONUNREGISTERCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONUNREGISTERCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONAPPLYCOMPLETE:
            OnApplyCompleteFallback(reinterpret_cast<BA_ONAPPLYCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONAPPLYCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONLAUNCHAPPROVEDEXEBEGIN:
            OnLaunchApprovedExeBeginFallback(reinterpret_cast<BA_ONLAUNCHAPPROVEDEXEBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONLAUNCHAPPROVEDEXEBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONLAUNCHAPPROVEDEXECOMPLETE:
            OnLaunchApprovedExeCompleteFallback(reinterpret_cast<BA_ONLAUNCHAPPROVEDEXECOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONLAUNCHAPPROVEDEXECOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANMSIPACKAGE:
            OnPlanMsiPackageFallback(reinterpret_cast<BA_ONPLANMSIPACKAGE_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANMSIPACKAGE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONBEGINMSITRANSACTIONBEGIN:
            OnBeginMsiTransactionBeginFallback(reinterpret_cast<BA_ONBEGINMSITRANSACTIONBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONBEGINMSITRANSACTIONBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONBEGINMSITRANSACTIONCOMPLETE:
            OnBeginMsiTransactionCompleteFallback(reinterpret_cast<BA_ONBEGINMSITRANSACTIONCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONBEGINMSITRANSACTIONCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCOMMITMSITRANSACTIONBEGIN:
            OnCommitMsiTransactionBeginFallback(reinterpret_cast<BA_ONCOMMITMSITRANSACTIONBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONCOMMITMSITRANSACTIONBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCOMMITMSITRANSACTIONCOMPLETE:
            OnCommitMsiTransactionCompleteFallback(reinterpret_cast<BA_ONCOMMITMSITRANSACTIONCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONCOMMITMSITRANSACTIONCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONROLLBACKMSITRANSACTIONBEGIN:
            OnRollbackMsiTransactionBeginFallback(reinterpret_cast<BA_ONROLLBACKMSITRANSACTIONBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONROLLBACKMSITRANSACTIONBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONROLLBACKMSITRANSACTIONCOMPLETE:
            OnRollbackMsiTransactionCompleteFallback(reinterpret_cast<BA_ONROLLBACKMSITRANSACTIONCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONROLLBACKMSITRANSACTIONCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPAUSEAUTOMATICUPDATESBEGIN:
            OnPauseAutomaticUpdatesBeginFallback(reinterpret_cast<BA_ONPAUSEAUTOMATICUPDATESBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONPAUSEAUTOMATICUPDATESBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPAUSEAUTOMATICUPDATESCOMPLETE:
            OnPauseAutomaticUpdatesCompleteFallback(reinterpret_cast<BA_ONPAUSEAUTOMATICUPDATESCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONPAUSEAUTOMATICUPDATESCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONSYSTEMRESTOREPOINTBEGIN:
            OnSystemRestorePointBeginFallback(reinterpret_cast<BA_ONSYSTEMRESTOREPOINTBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONSYSTEMRESTOREPOINTBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONSYSTEMRESTOREPOINTCOMPLETE:
            OnSystemRestorePointCompleteFallback(reinterpret_cast<BA_ONSYSTEMRESTOREPOINTCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONSYSTEMRESTOREPOINTCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANNEDPACKAGE:
            OnPlannedPackageFallback(reinterpret_cast<BA_ONPLANNEDPACKAGE_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANNEDPACKAGE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEVERIFYPROGRESS:
            OnCacheVerifyProgressFallback(reinterpret_cast<BA_ONCACHEVERIFYPROGRESS_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEVERIFYPROGRESS_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHECONTAINERORPAYLOADVERIFYBEGIN:
            OnCacheContainerOrPayloadVerifyBeginFallback(reinterpret_cast<BA_ONCACHECONTAINERORPAYLOADVERIFYBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHECONTAINERORPAYLOADVERIFYBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHECONTAINERORPAYLOADVERIFYCOMPLETE:
            OnCacheContainerOrPayloadVerifyCompleteFallback(reinterpret_cast<BA_ONCACHECONTAINERORPAYLOADVERIFYCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHECONTAINERORPAYLOADVERIFYCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHECONTAINERORPAYLOADVERIFYPROGRESS:
            OnCacheContainerOrPayloadVerifyProgressFallback(reinterpret_cast<BA_ONCACHECONTAINERORPAYLOADVERIFYPROGRESS_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHECONTAINERORPAYLOADVERIFYPROGRESS_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEPAYLOADEXTRACTBEGIN:
            OnCachePayloadExtractBeginFallback(reinterpret_cast<BA_ONCACHEPAYLOADEXTRACTBEGIN_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEPAYLOADEXTRACTBEGIN_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEPAYLOADEXTRACTCOMPLETE:
            OnCachePayloadExtractCompleteFallback(reinterpret_cast<BA_ONCACHEPAYLOADEXTRACTCOMPLETE_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEPAYLOADEXTRACTCOMPLETE_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONCACHEPAYLOADEXTRACTPROGRESS:
            OnCachePayloadExtractProgressFallback(reinterpret_cast<BA_ONCACHEPAYLOADEXTRACTPROGRESS_ARGS*>(pvArgs), reinterpret_cast<BA_ONCACHEPAYLOADEXTRACTPROGRESS_RESULTS*>(pvResults));
            break;
        case BOOTSTRAPPER_APPLICATION_MESSAGE_ONPLANROLLBACKBOUNDARY:
            OnPlanRollbackBoundaryFallback(reinterpret_cast<BA_ONPLANROLLBACKBOUNDARY_ARGS*>(pvArgs), reinterpret_cast<BA_ONPLANROLLBACKBOUNDARY_RESULTS*>(pvResults));
            break;
        default:
#ifdef DEBUG
            BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "WIXSTDBA: Forwarding unknown BA message: %d", message);
#endif
            m_pfnBAFunctionsProc((BA_FUNCTIONS_MESSAGE)message, pvArgs, pvResults, m_pvBAFunctionsProcContext);
            break;
        }
    }


private: // privates
    void OnDetectBeginFallback(
        __in BA_ONDETECTBEGIN_ARGS* pArgs,
        __inout BA_ONDETECTBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectCompleteFallback(
        __in BA_ONDETECTCOMPLETE_ARGS* pArgs,
        __inout BA_ONDETECTCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnPlanBeginFallback(
        __in BA_ONPLANBEGIN_ARGS* pArgs,
        __inout BA_ONPLANBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnPlanCompleteFallback(
        __in BA_ONPLANCOMPLETE_ARGS* pArgs,
        __inout BA_ONPLANCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnShutdownFallback(
        __in BA_ONSHUTDOWN_ARGS* pArgs,
        __inout BA_ONSHUTDOWN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONSHUTDOWN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnSystemShutdownFallback(
        __in BA_ONSYSTEMSHUTDOWN_ARGS* pArgs,
        __inout BA_ONSYSTEMSHUTDOWN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONSYSTEMSHUTDOWN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectForwardCompatibleBundleFallback(
        __in BA_ONDETECTFORWARDCOMPATIBLEBUNDLE_ARGS* pArgs,
        __inout BA_ONDETECTFORWARDCOMPATIBLEBUNDLE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTFORWARDCOMPATIBLEBUNDLE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectUpdateBeginFallback(
        __in BA_ONDETECTUPDATEBEGIN_ARGS* pArgs,
        __inout BA_ONDETECTUPDATEBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTUPDATEBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectUpdateFallback(
        __in BA_ONDETECTUPDATE_ARGS* pArgs,
        __inout BA_ONDETECTUPDATE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTUPDATE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectUpdateCompleteFallback(
        __in BA_ONDETECTUPDATECOMPLETE_ARGS* pArgs,
        __inout BA_ONDETECTUPDATECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTUPDATECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectRelatedBundleFallback(
        __in BA_ONDETECTRELATEDBUNDLE_ARGS* pArgs,
        __inout BA_ONDETECTRELATEDBUNDLE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTRELATEDBUNDLE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectPackageBeginFallback(
        __in BA_ONDETECTPACKAGEBEGIN_ARGS* pArgs,
        __inout BA_ONDETECTPACKAGEBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTPACKAGEBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectRelatedMsiPackageFallback(
        __in BA_ONDETECTRELATEDMSIPACKAGE_ARGS* pArgs,
        __inout BA_ONDETECTRELATEDMSIPACKAGE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTRELATEDMSIPACKAGE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectPatchTargetFallback(
        __in BA_ONDETECTPATCHTARGET_ARGS* pArgs,
        __inout BA_ONDETECTPATCHTARGET_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTPATCHTARGET, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectMsiFeatureFallback(
        __in BA_ONDETECTMSIFEATURE_ARGS* pArgs,
        __inout BA_ONDETECTMSIFEATURE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTMSIFEATURE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnDetectPackageCompleteFallback(
        __in BA_ONDETECTPACKAGECOMPLETE_ARGS* pArgs,
        __inout BA_ONDETECTPACKAGECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONDETECTPACKAGECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnPlanRelatedBundleFallback(
        __in BA_ONPLANRELATEDBUNDLE_ARGS* pArgs,
        __inout BA_ONPLANRELATEDBUNDLE_RESULTS* pResults
        )
    {
        BOOTSTRAPPER_REQUEST_STATE requestedState = pResults->requestedState;
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANRELATEDBUNDLE, pArgs, pResults, m_pvBAFunctionsProcContext);
        BalLogId(BOOTSTRAPPER_LOG_LEVEL_STANDARD, MSG_WIXSTDBA_PLANNED_RELATED_BUNDLE, m_hModule, pArgs->wzBundleId, LoggingRequestStateToString(requestedState), LoggingRequestStateToString(pResults->requestedState));
    }

    void OnPlanPackageBeginFallback(
        __in BA_ONPLANPACKAGEBEGIN_ARGS* pArgs,
        __inout BA_ONPLANPACKAGEBEGIN_RESULTS* pResults
        )
    {
        BOOTSTRAPPER_REQUEST_STATE requestedState = pResults->requestedState;
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANPACKAGEBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
        BalLogId(BOOTSTRAPPER_LOG_LEVEL_STANDARD, MSG_WIXSTDBA_PLANNED_PACKAGE, m_hModule, pArgs->wzPackageId, LoggingRequestStateToString(requestedState), LoggingRequestStateToString(pResults->requestedState));
    }

    void OnPlanPatchTargetFallback(
        __in BA_ONPLANPATCHTARGET_ARGS* pArgs,
        __inout BA_ONPLANPATCHTARGET_RESULTS* pResults
        )
    {
        BOOTSTRAPPER_REQUEST_STATE requestedState = pResults->requestedState;
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANPATCHTARGET, pArgs, pResults, m_pvBAFunctionsProcContext);
        BalLogId(BOOTSTRAPPER_LOG_LEVEL_STANDARD, MSG_WIXSTDBA_PLANNED_TARGET_MSI_PACKAGE, m_hModule, pArgs->wzPackageId, pArgs->wzProductCode, LoggingRequestStateToString(requestedState), LoggingRequestStateToString(pResults->requestedState));
    }

    void OnPlanMsiFeatureFallback(
        __in BA_ONPLANMSIFEATURE_ARGS* pArgs,
        __inout BA_ONPLANMSIFEATURE_RESULTS* pResults
        )
    {
        BOOTSTRAPPER_FEATURE_STATE requestedState = pResults->requestedState;
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANMSIFEATURE, pArgs, pResults, m_pvBAFunctionsProcContext);
        BalLogId(BOOTSTRAPPER_LOG_LEVEL_STANDARD, MSG_WIXSTDBA_PLANNED_MSI_FEATURE, m_hModule, pArgs->wzPackageId, pArgs->wzFeatureId, LoggingMsiFeatureStateToString(requestedState), LoggingMsiFeatureStateToString(pResults->requestedState));
    }

    void OnPlanPackageCompleteFallback(
        __in BA_ONPLANPACKAGECOMPLETE_ARGS* pArgs,
        __inout BA_ONPLANPACKAGECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANPACKAGECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnPlannedPackageFallback(
        __in BA_ONPLANNEDPACKAGE_ARGS* pArgs,
        __inout BA_ONPLANNEDPACKAGE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANNEDPACKAGE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnApplyBeginFallback(
        __in BA_ONAPPLYBEGIN_ARGS* pArgs,
        __inout BA_ONAPPLYBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONAPPLYBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnElevateBeginFallback(
        __in BA_ONELEVATEBEGIN_ARGS* pArgs,
        __inout BA_ONELEVATEBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONELEVATEBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnElevateCompleteFallback(
        __in BA_ONELEVATECOMPLETE_ARGS* pArgs,
        __inout BA_ONELEVATECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONELEVATECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnProgressFallback(
        __in BA_ONPROGRESS_ARGS* pArgs,
        __inout BA_ONPROGRESS_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPROGRESS, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnErrorFallback(
        __in BA_ONERROR_ARGS* pArgs,
        __inout BA_ONERROR_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONERROR, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnRegisterBeginFallback(
        __in BA_ONREGISTERBEGIN_ARGS* pArgs,
        __inout BA_ONREGISTERBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONREGISTERBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnRegisterCompleteFallback(
        __in BA_ONREGISTERCOMPLETE_ARGS* pArgs,
        __inout BA_ONREGISTERCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONREGISTERCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheBeginFallback(
        __in BA_ONCACHEBEGIN_ARGS* pArgs,
        __inout BA_ONCACHEBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCachePackageBeginFallback(
        __in BA_ONCACHEPACKAGEBEGIN_ARGS* pArgs,
        __inout BA_ONCACHEPACKAGEBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEPACKAGEBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheAcquireBeginFallback(
        __in BA_ONCACHEACQUIREBEGIN_ARGS* pArgs,
        __inout BA_ONCACHEACQUIREBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEACQUIREBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheAcquireProgressFallback(
        __in BA_ONCACHEACQUIREPROGRESS_ARGS* pArgs,
        __inout BA_ONCACHEACQUIREPROGRESS_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEACQUIREPROGRESS, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheAcquireResolvingFallback(
        __in BA_ONCACHEACQUIRERESOLVING_ARGS* pArgs,
        __inout BA_ONCACHEACQUIRERESOLVING_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEACQUIRERESOLVING, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheAcquireCompleteFallback(
        __in BA_ONCACHEACQUIRECOMPLETE_ARGS* pArgs,
        __inout BA_ONCACHEACQUIRECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEACQUIRECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheVerifyBeginFallback(
        __in BA_ONCACHEVERIFYBEGIN_ARGS* pArgs,
        __inout BA_ONCACHEVERIFYBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEVERIFYBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheVerifyCompleteFallback(
        __in BA_ONCACHEVERIFYCOMPLETE_ARGS* pArgs,
        __inout BA_ONCACHEVERIFYCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEVERIFYCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCachePackageCompleteFallback(
        __in BA_ONCACHEPACKAGECOMPLETE_ARGS* pArgs,
        __inout BA_ONCACHEPACKAGECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEPACKAGECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheCompleteFallback(
        __in BA_ONCACHECOMPLETE_ARGS* pArgs,
        __inout BA_ONCACHECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnExecuteBeginFallback(
        __in BA_ONEXECUTEBEGIN_ARGS* pArgs,
        __inout BA_ONEXECUTEBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONEXECUTEBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnExecutePackageBeginFallback(
        __in BA_ONEXECUTEPACKAGEBEGIN_ARGS* pArgs,
        __inout BA_ONEXECUTEPACKAGEBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONEXECUTEPACKAGEBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnExecutePatchTargetFallback(
        __in BA_ONEXECUTEPATCHTARGET_ARGS* pArgs,
        __inout BA_ONEXECUTEPATCHTARGET_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONEXECUTEPATCHTARGET, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnExecuteProgressFallback(
        __in BA_ONEXECUTEPROGRESS_ARGS* pArgs,
        __inout BA_ONEXECUTEPROGRESS_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONEXECUTEPROGRESS, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnExecuteMsiMessageFallback(
        __in BA_ONEXECUTEMSIMESSAGE_ARGS* pArgs,
        __inout BA_ONEXECUTEMSIMESSAGE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONEXECUTEMSIMESSAGE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnExecuteFilesInUseFallback(
        __in BA_ONEXECUTEFILESINUSE_ARGS* pArgs,
        __inout BA_ONEXECUTEFILESINUSE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONEXECUTEFILESINUSE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnExecutePackageCompleteFallback(
        __in BA_ONEXECUTEPACKAGECOMPLETE_ARGS* pArgs,
        __inout BA_ONEXECUTEPACKAGECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONEXECUTEPACKAGECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnExecuteCompleteFallback(
        __in BA_ONEXECUTECOMPLETE_ARGS* pArgs,
        __inout BA_ONEXECUTECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONEXECUTECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnUnregisterBeginFallback(
        __in BA_ONUNREGISTERBEGIN_ARGS* pArgs,
        __inout BA_ONUNREGISTERBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONUNREGISTERBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnUnregisterCompleteFallback(
        __in BA_ONUNREGISTERCOMPLETE_ARGS* pArgs,
        __inout BA_ONUNREGISTERCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONUNREGISTERCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnApplyCompleteFallback(
        __in BA_ONAPPLYCOMPLETE_ARGS* pArgs,
        __inout BA_ONAPPLYCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONAPPLYCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnLaunchApprovedExeBeginFallback(
        __in BA_ONLAUNCHAPPROVEDEXEBEGIN_ARGS* pArgs,
        __inout BA_ONLAUNCHAPPROVEDEXEBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONLAUNCHAPPROVEDEXEBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnLaunchApprovedExeCompleteFallback(
        __in BA_ONLAUNCHAPPROVEDEXECOMPLETE_ARGS* pArgs,
        __inout BA_ONLAUNCHAPPROVEDEXECOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONLAUNCHAPPROVEDEXECOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnPlanMsiPackageFallback(
        __in BA_ONPLANMSIPACKAGE_ARGS* pArgs,
        __inout BA_ONPLANMSIPACKAGE_RESULTS* pResults
        )
    {
        BURN_MSI_PROPERTY actionMsiProperty = pResults->actionMsiProperty;
        INSTALLUILEVEL uiLevel = pResults->uiLevel;
        BOOL fDisableExternalUiHandler = pResults->fDisableExternalUiHandler;
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANMSIPACKAGE, pArgs, pResults, m_pvBAFunctionsProcContext);
        BalLogId(BOOTSTRAPPER_LOG_LEVEL_STANDARD, MSG_WIXSTDBA_PLANNED_MSI_PACKAGE, m_hModule, pArgs->wzPackageId, actionMsiProperty, uiLevel, fDisableExternalUiHandler ? "yes" : "no", pResults->actionMsiProperty, pResults->uiLevel, pResults->fDisableExternalUiHandler ? "yes" : "no");
    }

    void OnBeginMsiTransactionBeginFallback(
        __in BA_ONBEGINMSITRANSACTIONBEGIN_ARGS* pArgs,
        __inout BA_ONBEGINMSITRANSACTIONBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONBEGINMSITRANSACTIONBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnBeginMsiTransactionCompleteFallback(
        __in BA_ONBEGINMSITRANSACTIONCOMPLETE_ARGS* pArgs,
        __inout BA_ONBEGINMSITRANSACTIONCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONBEGINMSITRANSACTIONCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCommitMsiTransactionBeginFallback(
        __in BA_ONCOMMITMSITRANSACTIONBEGIN_ARGS* pArgs,
        __inout BA_ONCOMMITMSITRANSACTIONBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCOMMITMSITRANSACTIONBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCommitMsiTransactionCompleteFallback(
        __in BA_ONCOMMITMSITRANSACTIONCOMPLETE_ARGS* pArgs,
        __inout BA_ONCOMMITMSITRANSACTIONCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCOMMITMSITRANSACTIONCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnRollbackMsiTransactionBeginFallback(
        __in BA_ONROLLBACKMSITRANSACTIONBEGIN_ARGS* pArgs,
        __inout BA_ONROLLBACKMSITRANSACTIONBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONROLLBACKMSITRANSACTIONBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnRollbackMsiTransactionCompleteFallback(
        __in BA_ONROLLBACKMSITRANSACTIONCOMPLETE_ARGS* pArgs,
        __inout BA_ONROLLBACKMSITRANSACTIONCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONROLLBACKMSITRANSACTIONCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnPauseAutomaticUpdatesBeginFallback(
        __in BA_ONPAUSEAUTOMATICUPDATESBEGIN_ARGS* pArgs,
        __inout BA_ONPAUSEAUTOMATICUPDATESBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPAUSEAUTOMATICUPDATESBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnPauseAutomaticUpdatesCompleteFallback(
        __in BA_ONPAUSEAUTOMATICUPDATESCOMPLETE_ARGS* pArgs,
        __inout BA_ONPAUSEAUTOMATICUPDATESCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPAUSEAUTOMATICUPDATESCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnSystemRestorePointBeginFallback(
        __in BA_ONSYSTEMRESTOREPOINTBEGIN_ARGS* pArgs,
        __inout BA_ONSYSTEMRESTOREPOINTBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONSYSTEMRESTOREPOINTBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnSystemRestorePointCompleteFallback(
        __in BA_ONSYSTEMRESTOREPOINTCOMPLETE_ARGS* pArgs,
        __inout BA_ONSYSTEMRESTOREPOINTCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONSYSTEMRESTOREPOINTCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnPlanForwardCompatibleBundleFallback(
        __in BA_ONPLANFORWARDCOMPATIBLEBUNDLE_ARGS* pArgs,
        __inout BA_ONPLANFORWARDCOMPATIBLEBUNDLE_RESULTS* pResults
        )
    {
        BOOL fIgnoreBundle = pResults->fIgnoreBundle;
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANFORWARDCOMPATIBLEBUNDLE, pArgs, pResults, m_pvBAFunctionsProcContext);
        BalLogId(BOOTSTRAPPER_LOG_LEVEL_STANDARD, MSG_WIXSTDBA_PLANNED_FORWARD_COMPATIBLE_BUNDLE, m_hModule, pArgs->wzBundleId, fIgnoreBundle ? "ignore" : "enable", pResults->fIgnoreBundle ? "ignore" : "enable");
    }

    void OnCacheVerifyProgressFallback(
        __in BA_ONCACHEVERIFYPROGRESS_ARGS* pArgs,
        __inout BA_ONCACHEVERIFYPROGRESS_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEVERIFYPROGRESS, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheContainerOrPayloadVerifyBeginFallback(
        __in BA_ONCACHECONTAINERORPAYLOADVERIFYBEGIN_ARGS* pArgs,
        __inout BA_ONCACHECONTAINERORPAYLOADVERIFYBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHECONTAINERORPAYLOADVERIFYBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheContainerOrPayloadVerifyCompleteFallback(
        __in BA_ONCACHECONTAINERORPAYLOADVERIFYCOMPLETE_ARGS* pArgs,
        __inout BA_ONCACHECONTAINERORPAYLOADVERIFYCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHECONTAINERORPAYLOADVERIFYCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCacheContainerOrPayloadVerifyProgressFallback(
        __in BA_ONCACHECONTAINERORPAYLOADVERIFYPROGRESS_ARGS* pArgs,
        __inout BA_ONCACHECONTAINERORPAYLOADVERIFYPROGRESS_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHECONTAINERORPAYLOADVERIFYPROGRESS, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCachePayloadExtractBeginFallback(
        __in BA_ONCACHEPAYLOADEXTRACTBEGIN_ARGS* pArgs,
        __inout BA_ONCACHEPAYLOADEXTRACTBEGIN_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEPAYLOADEXTRACTBEGIN, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCachePayloadExtractCompleteFallback(
        __in BA_ONCACHEPAYLOADEXTRACTCOMPLETE_ARGS* pArgs,
        __inout BA_ONCACHEPAYLOADEXTRACTCOMPLETE_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEPAYLOADEXTRACTCOMPLETE, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnCachePayloadExtractProgressFallback(
        __in BA_ONCACHEPAYLOADEXTRACTPROGRESS_ARGS* pArgs,
        __inout BA_ONCACHEPAYLOADEXTRACTPROGRESS_RESULTS* pResults
        )
    {
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONCACHEPAYLOADEXTRACTPROGRESS, pArgs, pResults, m_pvBAFunctionsProcContext);
    }

    void OnPlanRollbackBoundaryFallback(
        __in BA_ONPLANROLLBACKBOUNDARY_ARGS* pArgs,
        __inout BA_ONPLANROLLBACKBOUNDARY_RESULTS* pResults
        )
    {
        BOOL fTransaction = pResults->fTransaction;
        m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONPLANROLLBACKBOUNDARY, pArgs, pResults, m_pvBAFunctionsProcContext);
        BalLogId(BOOTSTRAPPER_LOG_LEVEL_STANDARD, MSG_WIXSTDBA_PLANNED_ROLLBACK_BOUNDARY, m_hModule, pArgs->wzRollbackBoundaryId, LoggingBoolToString(fTransaction), LoggingBoolToString(pResults->fTransaction));
    }


public: //CBalBaseBootstrapperApplication
    virtual STDMETHODIMP Initialize(
        __in const BOOTSTRAPPER_CREATE_ARGS* pCreateArgs
        )
    {
        HRESULT hr = S_OK;
        LONGLONG llInstalled = 0;

        hr = __super::Initialize(pCreateArgs);
        BalExitOnFailure(hr, "CBalBaseBootstrapperApplication initialization failed.");

        memcpy_s(&m_command, sizeof(m_command), pCreateArgs->pCommand, sizeof(BOOTSTRAPPER_COMMAND));
        memcpy_s(&m_createArgs, sizeof(m_createArgs), pCreateArgs, sizeof(BOOTSTRAPPER_CREATE_ARGS));
        m_createArgs.pCommand = &m_command;

        if (m_fPrereq)
        {
            // Pre-req BA should only show help or do an install (to launch the Managed BA which can then do the right action).
            if (BOOTSTRAPPER_ACTION_HELP != m_command.action)
            {
                m_command.action = BOOTSTRAPPER_ACTION_INSTALL;
            }
        }
        else // maybe modify the action state if the bundle is or is not already installed.
        {
            hr = BalGetNumericVariable(L"WixBundleInstalled", &llInstalled);
            if (SUCCEEDED(hr) && BOOTSTRAPPER_RESUME_TYPE_REBOOT != m_command.resumeType && llInstalled && BOOTSTRAPPER_ACTION_INSTALL == m_command.action)
            {
                m_command.action = BOOTSTRAPPER_ACTION_MODIFY;
            }
            else if (!llInstalled && (BOOTSTRAPPER_ACTION_MODIFY == m_command.action || BOOTSTRAPPER_ACTION_REPAIR == m_command.action))
            {
                m_command.action = BOOTSTRAPPER_ACTION_INSTALL;
            }
        }

        // When resuming from restart doing some install-like operation, try to find the package that forced the
        // restart. We'll use this information during planning.
        if (BOOTSTRAPPER_RESUME_TYPE_REBOOT == m_command.resumeType && BOOTSTRAPPER_ACTION_UNINSTALL < m_command.action)
        {
            // Ensure the forced restart package variable is null when it is an empty string.
            hr = BalGetStringVariable(L"WixBundleForcedRestartPackage", &m_sczAfterForcedRestartPackage);
            if (FAILED(hr) || !m_sczAfterForcedRestartPackage || !*m_sczAfterForcedRestartPackage)
            {
                ReleaseNullStr(m_sczAfterForcedRestartPackage);
            }
        }

        hr = S_OK;

    LExit:
        return hr;
    }

private:
    //
    // UiThreadProc - entrypoint for UI thread.
    //
    static DWORD WINAPI UiThreadProc(
        __in LPVOID pvContext
        )
    {
        HRESULT hr = S_OK;
        CWixStandardBootstrapperApplication* pThis = (CWixStandardBootstrapperApplication*)pvContext;
        BOOL fComInitialized = FALSE;
        BOOL fRet = FALSE;
        MSG msg = { };

        // Initialize COM and theme.
        hr = ::CoInitialize(NULL);
        BalExitOnFailure(hr, "Failed to initialize COM.");
        fComInitialized = TRUE;

        hr = ThemeInitialize(pThis->m_hModule);
        BalExitOnFailure(hr, "Failed to initialize theme manager.");

        hr = pThis->InitializeData();
        BalExitOnFailure(hr, "Failed to initialize data in bootstrapper application.");

        // Create main window.
        pThis->InitializeTaskbarButton();
        hr = pThis->CreateMainWindow();
        BalExitOnFailure(hr, "Failed to create main window.");

        if (FAILED(pThis->m_hrFinal))
        {
            pThis->SetState(WIXSTDBA_STATE_FAILED, hr);
            ::PostMessageW(pThis->m_hWnd, WM_WIXSTDBA_SHOW_FAILURE, 0, 0);
        }
        else
        {
            // Okay, we're ready for packages now.
            pThis->SetState(WIXSTDBA_STATE_INITIALIZED, hr);
            ::PostMessageW(pThis->m_hWnd, BOOTSTRAPPER_ACTION_HELP == pThis->m_command.action ? WM_WIXSTDBA_SHOW_HELP : WM_WIXSTDBA_DETECT_PACKAGES, 0, 0);
        }

        // message pump
        while (0 != (fRet = ::GetMessageW(&msg, NULL, 0, 0)))
        {
            if (-1 == fRet)
            {
                hr = E_UNEXPECTED;
                BalExitOnFailure(hr, "Unexpected return value from message pump.");
            }
            else if (!ThemeHandleKeyboardMessage(pThis->m_pTheme, msg.hwnd, &msg))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }

        // Succeeded thus far, check to see if anything went wrong while actually
        // executing changes.
        if (FAILED(pThis->m_hrFinal))
        {
            hr = pThis->m_hrFinal;
        }
        else if (pThis->CheckCanceled())
        {
            hr = HRESULT_FROM_WIN32(ERROR_INSTALL_USEREXIT);
        }

    LExit:
        // destroy main window
        pThis->DestroyMainWindow();
        pThis->UninitializeTaskbarButton();

        // initiate engine shutdown
        DWORD dwQuit = HRESULT_CODE(hr);
        if (BOOTSTRAPPER_APPLY_RESTART_INITIATED == pThis->m_restartResult)
        {
            dwQuit = ERROR_SUCCESS_REBOOT_INITIATED;
        }
        else if (BOOTSTRAPPER_APPLY_RESTART_REQUIRED == pThis->m_restartResult)
        {
            dwQuit = ERROR_SUCCESS_REBOOT_REQUIRED;
        }
        pThis->m_pEngine->Quit(dwQuit);

        ReleaseTheme(pThis->m_pTheme);
        ThemeUninitialize();

        // uninitialize COM
        if (fComInitialized)
        {
            ::CoUninitialize();
        }

        return hr;
    }


    //
    // InitializeData - initializes all the package and prerequisite information.
    //
    HRESULT InitializeData()
    {
        HRESULT hr = S_OK;
        LPWSTR sczModulePath = NULL;
        IXMLDOMDocument* pixdManifest = NULL;

        hr = BalManifestLoad(m_hModule, &pixdManifest);
        BalExitOnFailure(hr, "Failed to load bootstrapper application manifest.");

        hr = BalInfoParseFromXml(&m_Bundle, pixdManifest);
        BalExitOnFailure(hr, "Failed to load bundle information.");

        hr = ProcessCommandLine(&m_sczLanguage);
        ExitOnFailure(hr, "Unknown commandline parameters.");

        hr = PathRelativeToModule(&sczModulePath, NULL, m_hModule);
        BalExitOnFailure(hr, "Failed to get module path.");

        hr = LoadLocalization(sczModulePath, m_sczLanguage);
        ExitOnFailure(hr, "Failed to load localization.");

        hr = LoadTheme(sczModulePath, m_sczLanguage);
        ExitOnFailure(hr, "Failed to load theme.");

        hr = BalConditionsParseFromXml(&m_Conditions, pixdManifest, m_pWixLoc);
        BalExitOnFailure(hr, "Failed to load conditions from XML.");

        hr = LoadBAFunctions(pixdManifest);
        BalExitOnFailure(hr, "Failed to load bootstrapper functions.");

        GetBundleFileVersion();
        // don't fail if we couldn't get the version info; best-effort only

        hr = InitializePackageInfo();
        BalExitOnFailure(hr, "Failed to initialize wixstdba package information.");

        if (m_fPrereq)
        {
            hr = InitializePrerequisiteInformation();
            BalExitOnFailure(hr, "Failed to initialize prerequisite information.");
        }
        else
        {
            hr = ParseBootstrapperApplicationDataFromXml(pixdManifest);
            BalExitOnFailure(hr, "Failed to read bootstrapper application data.");
        }

        if (BOOTSTRAPPER_ACTION_CACHE == m_plannedAction)
        {
            if (m_fSupportCacheOnly)
            {
                // Doesn't make sense to prompt the user if cache only is requested.
                if (BOOTSTRAPPER_DISPLAY_PASSIVE < m_command.display)
                {
                    m_command.display = BOOTSTRAPPER_DISPLAY_PASSIVE;
                }

                m_command.action = BOOTSTRAPPER_ACTION_CACHE;
            }
            else
            {
                BalLog(BOOTSTRAPPER_LOG_LEVEL_ERROR, "Ignoring attempt to only cache a bundle that does not explicitly support it.");
                m_plannedAction = BOOTSTRAPPER_ACTION_UNKNOWN;
            }
        }

    LExit:
        ReleaseObject(pixdManifest);
        ReleaseStr(sczModulePath);

        return hr;
    }


    //
    // ProcessCommandLine - process the provided command line arguments.
    //
    HRESULT ProcessCommandLine(
        __inout LPWSTR* psczLanguage
        )
    {
        HRESULT hr = S_OK;
        int argc = 0;
        LPWSTR* argv = NULL;
        BOOL fUnknownArg = FALSE;

        argc = m_BalInfoCommand.cUnknownArgs;
        argv = m_BalInfoCommand.rgUnknownArgs;

        if (argc)
        {
            for (int i = 0; i < argc; ++i)
            {
                fUnknownArg = FALSE;

                if (argv[i][0] == L'-' || argv[i][0] == L'/')
                {
                    if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, &argv[i][1], -1, L"lang", -1))
                    {
                        if (i + 1 >= argc)
                        {
                            hr = E_INVALIDARG;
                            BalExitOnFailure(hr, "Must specify a language.");
                        }

                        ++i;

                        hr = StrAllocString(psczLanguage, &argv[i][0], 0);
                        BalExitOnFailure(hr, "Failed to copy language.");
                    }
                    else if (CSTR_EQUAL == ::CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, &argv[i][1], -1, L"cache", -1))
                    {
                        m_plannedAction = BOOTSTRAPPER_ACTION_CACHE;
                    }
                    else
                    {
                        fUnknownArg = TRUE;
                    }
                }
                else
                {
                    fUnknownArg = TRUE;
                }

                if (fUnknownArg)
                {
                    BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "Ignoring unknown argument: %ls", argv[i]);
                }
            }
        }

        hr = BalSetOverridableVariablesFromEngine(&m_Bundle.overridableVariables, &m_BalInfoCommand, m_pEngine);
        BalExitOnFailure(hr, "Failed to set overridable variables from the command line.");

    LExit:
        return hr;
    }

    HRESULT LoadLocalization(
        __in_z LPCWSTR wzModulePath,
        __in_z_opt LPCWSTR wzLanguage
        )
    {
        HRESULT hr = S_OK;
        LPWSTR sczLocPath = NULL;
        LPWSTR sczFormatted = NULL;
        LPCWSTR wzLocFileName = m_fPrereq ? L"mbapreq.wxl" : L"thm.wxl";

        // Find and load .wxl file.
        hr = LocProbeForFile(wzModulePath, wzLocFileName, wzLanguage, &sczLocPath);
        BalExitOnFailure(hr, "Failed to probe for loc file: %ls in path: %ls", wzLocFileName, wzModulePath);

        hr = LocLoadFromFile(sczLocPath, &m_pWixLoc);
        BalExitOnFailure(hr, "Failed to load loc file from path: %ls", sczLocPath);

        // Set WixStdBALanguageId to .wxl language id.
        if (WIX_LOCALIZATION_LANGUAGE_NOT_SET != m_pWixLoc->dwLangId)
        {
            ::SetThreadLocale(m_pWixLoc->dwLangId);

            hr = m_pEngine->SetVariableNumeric(WIXSTDBA_VARIABLE_LANGUAGE_ID, m_pWixLoc->dwLangId);
            BalExitOnFailure(hr, "Failed to set WixStdBALanguageId variable.");
        }

        // Load ConfirmCancelMessage.
        hr = StrAllocString(&m_sczConfirmCloseMessage, L"#(loc.ConfirmCancelMessage)", 0);
        ExitOnFailure(hr, "Failed to initialize confirm message loc identifier.");

        hr = LocLocalizeString(m_pWixLoc, &m_sczConfirmCloseMessage);
        BalExitOnFailure(hr, "Failed to localize confirm close message: %ls", m_sczConfirmCloseMessage);

        hr = BalFormatString(m_sczConfirmCloseMessage, &sczFormatted);
        if (SUCCEEDED(hr))
        {
            ReleaseStr(m_sczConfirmCloseMessage);
            m_sczConfirmCloseMessage = sczFormatted;
            sczFormatted = NULL;
        }

    LExit:
        ReleaseStr(sczFormatted);
        ReleaseStr(sczLocPath);

        return hr;
    }


    HRESULT LoadTheme(
        __in_z LPCWSTR wzModulePath,
        __in_z_opt LPCWSTR wzLanguage
        )
    {
        HRESULT hr = S_OK;
        LPWSTR sczThemePath = NULL;
        LPCWSTR wzThemeFileName = m_fPrereq ? L"mbapreq.thm" : L"thm.xml";

        hr = LocProbeForFile(wzModulePath, wzThemeFileName, wzLanguage, &sczThemePath);
        BalExitOnFailure(hr, "Failed to probe for theme file: %ls in path: %ls", wzThemeFileName, wzModulePath);

        hr = ThemeLoadFromFile(sczThemePath, &m_pTheme);
        BalExitOnFailure(hr, "Failed to load theme from path: %ls", sczThemePath);

        hr = ThemeRegisterVariableCallbacks(m_pTheme, EvaluateVariableConditionCallback, FormatVariableStringCallback, GetVariableNumericCallback, SetVariableNumericCallback, GetVariableStringCallback, SetVariableStringCallback, NULL);
        BalExitOnFailure(hr, "Failed to register variable theme callbacks.");

        hr = ThemeLocalize(m_pTheme, m_pWixLoc);
        BalExitOnFailure(hr, "Failed to localize theme: %ls", sczThemePath);

    LExit:
        ReleaseStr(sczThemePath);

        return hr;
    }


    HRESULT InitializePackageInfo()
    {
        HRESULT hr = S_OK;
        BAL_INFO_PACKAGE* pPackage = NULL;

        for (DWORD i = 0; i < m_Bundle.packages.cPackages; ++i)
        {
            pPackage = &m_Bundle.packages.rgPackages[i];

            hr = InitializePackageInfoForPackage(pPackage);
            BalExitOnFailure(hr, "Failed to initialize wixstdba package info for package: %ls.", pPackage->sczId);
        }

    LExit:
        return hr;
    }


    HRESULT InitializePackageInfoForPackage(
        __in BAL_INFO_PACKAGE* pPackage
        )
    {
        HRESULT hr = S_OK;

        pPackage->pvCustomData = MemAlloc(sizeof(WIXSTDBA_PACKAGE_INFO), TRUE);
        BalExitOnNull(pPackage->pvCustomData, hr, E_OUTOFMEMORY, "Failed to allocate memory for wixstdba package info.");

    LExit:
        return hr;
    }


    HRESULT InitializePrerequisiteInformation()
    {
        HRESULT hr = S_OK;
        BAL_INFO_PACKAGE* pPackage = NULL;

        for (DWORD i = 0; i < m_Bundle.packages.cPackages; ++i)
        {
            pPackage = &m_Bundle.packages.rgPackages[i];
            if (!pPackage->fPrereqPackage)
            {
                continue;
            }

            if (pPackage->sczPrereqLicenseFile)
            {
                if (m_sczLicenseFile)
                {
                    hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    BalExitOnFailure(hr, "More than one license file specified in prerequisite info.");
                }

                hr = StrAllocString(&m_sczLicenseFile, pPackage->sczPrereqLicenseFile, 0);
                BalExitOnFailure(hr, "Failed to copy license file location from prereq package.");
            }

            if (pPackage->sczPrereqLicenseUrl)
            {
                if (m_sczLicenseUrl)
                {
                    hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    BalExitOnFailure(hr, "More than one license URL specified in prerequisite info.");
                }

                hr = StrAllocString(&m_sczLicenseUrl, pPackage->sczPrereqLicenseUrl, 0);
                BalExitOnFailure(hr, "Failed to copy license URL from prereq package.");
            }
        }

    LExit:
        return hr;
    }


    HRESULT ParseBootstrapperApplicationDataFromXml(
        __in IXMLDOMDocument* pixdManifest
        )
    {
        HRESULT hr = S_OK;
        IXMLDOMNode* pNode = NULL;
        DWORD dwBool = 0;

        hr = XmlSelectSingleNode(pixdManifest, L"/BootstrapperApplicationData/WixStdbaInformation", &pNode);
        if (S_FALSE == hr)
        {
            hr = E_INVALIDARG;
        }
        BalExitOnFailure(hr, "BootstrapperApplication.xml manifest is missing wixstdba information.");

        hr = XmlGetAttributeEx(pNode, L"LicenseFile", &m_sczLicenseFile);
        if (E_NOTFOUND == hr)
        {
            hr = S_OK;
        }
        BalExitOnFailure(hr, "Failed to get license file.");

        hr = XmlGetAttributeEx(pNode, L"LicenseUrl", &m_sczLicenseUrl);
        if (E_NOTFOUND == hr)
        {
            hr = S_OK;
        }
        BalExitOnFailure(hr, "Failed to get license URL.");

        ReleaseObject(pNode);

        hr = XmlSelectSingleNode(pixdManifest, L"/BootstrapperApplicationData/WixStdbaOptions", &pNode);
        if (S_FALSE == hr)
        {
            ExitFunction1(hr = S_OK);
        }
        BalExitOnFailure(hr, "Failed to read wixstdba options from BootstrapperApplication.xml manifest.");

        hr = XmlGetAttributeNumber(pNode, L"SuppressOptionsUI", &dwBool);
        if (S_FALSE == hr)
        {
            hr = S_OK;
        }
        else if (SUCCEEDED(hr) && dwBool)
        {
            hr = BalSetNumericVariable(WIXSTDBA_VARIABLE_SUPPRESS_OPTIONS_UI, 1);
            BalExitOnFailure(hr, "Failed to set '%ls' variable.", WIXSTDBA_VARIABLE_SUPPRESS_OPTIONS_UI);
        }
        BalExitOnFailure(hr, "Failed to get SuppressOptionsUI value.");

        dwBool = 0;
        hr = XmlGetAttributeNumber(pNode, L"SuppressDowngradeFailure", &dwBool);
        if (S_FALSE == hr)
        {
            hr = S_OK;
        }
        else if (SUCCEEDED(hr))
        {
            m_fSuppressDowngradeFailure = 0 < dwBool;
        }
        BalExitOnFailure(hr, "Failed to get SuppressDowngradeFailure value.");

        dwBool = 0;
        hr = XmlGetAttributeNumber(pNode, L"SuppressRepair", &dwBool);
        if (S_FALSE == hr)
        {
            hr = S_OK;
        }
        else if (SUCCEEDED(hr))
        {
            m_fSuppressRepair = 0 < dwBool;
        }
        BalExitOnFailure(hr, "Failed to get SuppressRepair value.");

        hr = XmlGetAttributeNumber(pNode, L"ShowVersion", &dwBool);
        if (S_FALSE == hr)
        {
            hr = S_OK;
        }
        else if (SUCCEEDED(hr) && dwBool)
        {
            hr = BalSetNumericVariable(WIXSTDBA_VARIABLE_SHOW_VERSION, 1);
            BalExitOnFailure(hr, "Failed to set '%ls' variable.", WIXSTDBA_VARIABLE_SHOW_VERSION);
        }
        BalExitOnFailure(hr, "Failed to get ShowVersion value.");

        hr = XmlGetAttributeNumber(pNode, L"SupportCacheOnly", &dwBool);
        if (S_FALSE == hr)
        {
            hr = S_OK;
        }
        else if (SUCCEEDED(hr))
        {
            m_fSupportCacheOnly = 0 < dwBool;
        }
        BalExitOnFailure(hr, "Failed to get SupportCacheOnly value.");

    LExit:
        ReleaseObject(pNode);
        return hr;
    }

    HRESULT GetPackageInfo(
        __in_z LPCWSTR wzPackageId,
        __out WIXSTDBA_PACKAGE_INFO** ppPackageInfo,
        __out BAL_INFO_PACKAGE** ppPackage
        )
    {
        HRESULT hr = E_NOTFOUND;
        WIXSTDBA_PACKAGE_INFO* pPackageInfo = NULL;
        BAL_INFO_PACKAGE* pPackage = NULL;

        Assert(wzPackageId && *wzPackageId);
        Assert(ppPackage);
        Assert(ppPackageInfo);

        hr = BalInfoFindPackageById(&m_Bundle.packages, wzPackageId, &pPackage);
        if (E_NOTFOUND != hr)
        {
            ExitOnFailure(hr, "Failed trying to find the requested package.");

            pPackageInfo = reinterpret_cast<WIXSTDBA_PACKAGE_INFO*>(pPackage->pvCustomData);
        }

        *ppPackageInfo = pPackageInfo;
        *ppPackage = pPackage;

    LExit:
        return hr;
    }


    //
    // Get the file version of the bootstrapper and record in bootstrapper log file
    //
    HRESULT GetBundleFileVersion()
    {
        HRESULT hr = S_OK;
        ULARGE_INTEGER uliVersion = { };
        LPWSTR sczCurrentPath = NULL;
        VERUTIL_VERSION* pVersion = NULL;

        hr = PathForCurrentProcess(&sczCurrentPath, NULL);
        BalExitOnFailure(hr, "Failed to get bundle path.");

        hr = FileVersion(sczCurrentPath, &uliVersion.HighPart, &uliVersion.LowPart);
        BalExitOnFailure(hr, "Failed to get bundle file version.");

        hr = VerVersionFromQword(uliVersion.QuadPart, &pVersion);
        BalExitOnFailure(hr, "Failed to create bundle file version.");

        hr = m_pEngine->SetVariableVersion(WIXSTDBA_VARIABLE_BUNDLE_FILE_VERSION, pVersion->sczVersion);
        BalExitOnFailure(hr, "Failed to set WixBundleFileVersion variable.");

    LExit:
        ReleaseVerutilVersion(pVersion);
        ReleaseStr(sczCurrentPath);

        return hr;
    }


    //
    // CreateMainWindow - creates the main install window.
    //
    HRESULT CreateMainWindow()
    {
        HRESULT hr = S_OK;
        HICON hIcon = reinterpret_cast<HICON>(m_pTheme->hIcon);
        WNDCLASSW wc = { };
        DWORD dwWindowStyle = 0;
        int x = CW_USEDEFAULT;
        int y = CW_USEDEFAULT;
        POINT ptCursor = { };

        // If the theme did not provide an icon, try using the icon from the bundle engine.
        if (!hIcon)
        {
            HMODULE hBootstrapperEngine = ::GetModuleHandleW(NULL);
            if (hBootstrapperEngine)
            {
                hIcon = ::LoadIconW(hBootstrapperEngine, MAKEINTRESOURCEW(1));
            }
        }

        // Register the window class and create the window.
        wc.lpfnWndProc = CWixStandardBootstrapperApplication::WndProc;
        wc.hInstance = m_hModule;
        wc.hIcon = hIcon;
        wc.hCursor = ::LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = m_pTheme->rgFonts[m_pTheme->dwFontId].hBackground;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = WIXSTDBA_WINDOW_CLASS;
        if (!::RegisterClassW(&wc))
        {
            ExitWithLastError(hr, "Failed to register window.");
        }

        m_fRegistered = TRUE;

        // Calculate the window style based on the theme style and command display value.
        dwWindowStyle = m_pTheme->dwStyle;
        if (BOOTSTRAPPER_DISPLAY_NONE >= m_command.display)
        {
            dwWindowStyle &= ~WS_VISIBLE;
        }

        // Don't show the window if there is a splash screen (it will be made visible when the splash screen is hidden)
        if (::IsWindow(m_command.hwndSplashScreen))
        {
            dwWindowStyle &= ~WS_VISIBLE;
        }

        // Center the window on the monitor with the mouse.
        if (::GetCursorPos(&ptCursor))
        {
            x = ptCursor.x;
            y = ptCursor.y;
        }

        hr = ThemeCreateParentWindow(m_pTheme, 0, wc.lpszClassName, m_pTheme->sczCaption, dwWindowStyle, x, y, HWND_DESKTOP, m_hModule, this, THEME_WINDOW_INITIAL_POSITION_CENTER_MONITOR_FROM_COORDINATES, &m_hWnd);
        ExitOnFailure(hr, "Failed to create window.");

        hr = S_OK;

    LExit:
        return hr;
    }


    //
    // InitializeTaskbarButton - initializes taskbar button for progress.
    //
    void InitializeTaskbarButton()
    {
        HRESULT hr = S_OK;

        hr = ::CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_ALL, __uuidof(ITaskbarList3), reinterpret_cast<LPVOID*>(&m_pTaskbarList));
        if (REGDB_E_CLASSNOTREG == hr) // not supported before Windows 7
        {
            ExitFunction1(hr = S_OK);
        }
        BalExitOnFailure(hr, "Failed to create ITaskbarList3. Continuing.");

        m_uTaskbarButtonCreatedMessage = ::RegisterWindowMessageW(L"TaskbarButtonCreated");
        BalExitOnNullWithLastError(m_uTaskbarButtonCreatedMessage, hr, "Failed to get TaskbarButtonCreated message. Continuing.");

    LExit:
        return;
    }

    //
    // DestroyMainWindow - clean up all the window registration.
    //
    void DestroyMainWindow()
    {
        if (::IsWindow(m_hWnd))
        {
            ::DestroyWindow(m_hWnd);
            m_hWnd = NULL;
            m_fTaskbarButtonOK = FALSE;
        }

        if (m_fRegistered)
        {
            ::UnregisterClassW(WIXSTDBA_WINDOW_CLASS, m_hModule);
            m_fRegistered = FALSE;
        }
    }


    //
    // UninitializeTaskbarButton - clean up the taskbar registration.
    //
    void UninitializeTaskbarButton()
    {
        m_fTaskbarButtonOK = FALSE;
        ReleaseNullObject(m_pTaskbarList);
    }


    static LRESULT CallDefaultWndProc(
        __in CWixStandardBootstrapperApplication* pBA,
        __in HWND hWnd,
        __in UINT uMsg,
        __in WPARAM wParam,
        __in LPARAM lParam
        )
    {
        LRESULT lres = NULL;
        THEME* pTheme = NULL;
        HRESULT hr = S_OK;
        BA_FUNCTIONS_WNDPROC_ARGS wndProcArgs = { };
        BA_FUNCTIONS_WNDPROC_RESULTS wndProcResults = { };

        if (pBA)
        {
            pTheme = pBA->m_pTheme;

            if (pBA->m_pfnBAFunctionsProc)
            {
                wndProcArgs.cbSize = sizeof(wndProcArgs);
                wndProcArgs.pTheme = pTheme;
                wndProcArgs.hWnd = hWnd;
                wndProcArgs.uMsg = uMsg;
                wndProcArgs.wParam = wParam;
                wndProcArgs.lParam = lParam;
                wndProcResults.cbSize = sizeof(wndProcResults);

                hr = pBA->m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_WNDPROC, &wndProcArgs, &wndProcResults, pBA->m_pvBAFunctionsProcContext);
                if (E_NOTIMPL != hr)
                {
                    lres = wndProcResults.lres;
                    ExitFunction();
                }
            }
        }

        lres = ThemeDefWindowProc(pTheme, hWnd, uMsg, wParam, lParam);

    LExit:
        return lres;
    }

    //
    // WndProc - standard windows message handler.
    //
    static LRESULT CALLBACK WndProc(
        __in HWND hWnd,
        __in UINT uMsg,
        __in WPARAM wParam,
        __in LPARAM lParam
        )
    {
#pragma warning(suppress:4312)
        CWixStandardBootstrapperApplication* pBA = reinterpret_cast<CWixStandardBootstrapperApplication*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        BOOL fCancel = FALSE;

        switch (uMsg)
        {
        case WM_NCCREATE:
        {
            LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            pBA = reinterpret_cast<CWixStandardBootstrapperApplication*>(lpcs->lpCreateParams);
#pragma warning(suppress:4244)
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pBA));
        }
        break;

        case WM_NCDESTROY:
        {
            LRESULT lres = CallDefaultWndProc(pBA, hWnd, uMsg, wParam, lParam);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
            ::PostQuitMessage(0);
            return lres;
        }

        case WM_CREATE:
            if (!pBA->OnCreate(hWnd))
            {
                return -1;
            }
            break;

        case WM_QUERYENDSESSION:
            fCancel = true;
            pBA->OnSystemShutdown(static_cast<DWORD>(lParam), &fCancel);
            return !fCancel;

        case WM_CLOSE:
            // If the user chose not to close, do *not* let the default window proc handle the message.
            if (!pBA->OnClose())
            {
                return 0;
            }
            break;

        case WM_WIXSTDBA_SHOW_HELP:
            pBA->OnShowHelp();
            return 0;

        case WM_WIXSTDBA_DETECT_PACKAGES:
            pBA->OnDetect();
            return 0;

        case WM_WIXSTDBA_PLAN_PACKAGES:
            pBA->OnPlan(static_cast<BOOTSTRAPPER_ACTION>(lParam));
            return 0;

        case WM_WIXSTDBA_APPLY_PACKAGES:
            pBA->OnApply();
            return 0;

        case WM_WIXSTDBA_CHANGE_STATE:
            pBA->OnChangeState(static_cast<WIXSTDBA_STATE>(lParam));
            return 0;

        case WM_WIXSTDBA_SHOW_FAILURE:
            pBA->OnShowFailure();
            return 0;

        case WM_COMMAND:
            switch (HIWORD(wParam))
            {
            case BN_CLICKED:
                switch (LOWORD(wParam))
                {
                case WIXSTDBA_CONTROL_EULA_ACCEPT_CHECKBOX:
                    pBA->OnClickAcceptCheckbox();
                    return 0;

                case WIXSTDBA_CONTROL_INSTALL_BUTTON:
                    pBA->OnClickInstallButton();
                    return 0;

                case WIXSTDBA_CONTROL_REPAIR_BUTTON:
                    pBA->OnClickRepairButton();
                    return 0;

                case WIXSTDBA_CONTROL_UNINSTALL_BUTTON:
                    pBA->OnClickUninstallButton();
                    return 0;

                case WIXSTDBA_CONTROL_LAUNCH_BUTTON:
                    pBA->OnClickLaunchButton();
                    return 0;

                case WIXSTDBA_CONTROL_SUCCESS_RESTART_BUTTON: __fallthrough;
                case WIXSTDBA_CONTROL_FAILURE_RESTART_BUTTON:
                    pBA->OnClickRestartButton();
                    return 0;

                case WIXSTDBA_CONTROL_PROGRESS_CANCEL_BUTTON:
                    pBA->OnClickCloseButton();
                    return 0;
                }
                break;
            }
            break;

        case WM_NOTIFY:
            if (lParam)
            {
                LPNMHDR pnmhdr = reinterpret_cast<LPNMHDR>(lParam);
                switch (pnmhdr->code)
                {
                case NM_CLICK: __fallthrough;
                case NM_RETURN:
                    switch (static_cast<DWORD>(pnmhdr->idFrom))
                    {
                    case WIXSTDBA_CONTROL_EULA_LINK:
                        pBA->OnClickEulaLink();
                        return 1;
                    case WIXSTDBA_CONTROL_FAILURE_LOGFILE_LINK:
                        pBA->OnClickLogFileLink();
                        return 1;
                    }
                }
            }
            break;
        }

        if (pBA && pBA->m_pTaskbarList && uMsg == pBA->m_uTaskbarButtonCreatedMessage)
        {
            pBA->m_fTaskbarButtonOK = TRUE;
            return 0;
        }

        return CallDefaultWndProc(pBA, hWnd, uMsg, wParam, lParam);
    }


    //
    // OnCreate - finishes loading the theme.
    //
    BOOL OnCreate(
        __in HWND /*hWnd*/
        )
    {
        HRESULT hr = S_OK;
        LPWSTR sczLicenseFormatted = NULL;
        LPWSTR sczLicensePath = NULL;
        LPWSTR sczLicenseDirectory = NULL;
        LPWSTR sczLicenseFilename = NULL;
        BA_FUNCTIONS_ONTHEMELOADED_ARGS themeLoadedArgs = { };
        BA_FUNCTIONS_ONTHEMELOADED_RESULTS themeLoadedResults = { };

        hr = ThemeLoadControls(m_pTheme, vrgInitControls, countof(vrgInitControls));
        BalExitOnFailure(hr, "Failed to load theme controls.");

        C_ASSERT(COUNT_WIXSTDBA_PAGE == countof(vrgwzPageNames));
        C_ASSERT(countof(m_rgdwPageIds) == countof(vrgwzPageNames));

        ThemeGetPageIds(m_pTheme, vrgwzPageNames, m_rgdwPageIds, countof(m_rgdwPageIds));

        // Load the RTF EULA control with text if the control exists.
        if (ThemeControlExists(m_pTheme, WIXSTDBA_CONTROL_EULA_RICHEDIT))
        {
            hr = (m_sczLicenseFile && *m_sczLicenseFile) ? S_OK : E_INVALIDDATA;
            if (SUCCEEDED(hr))
            {
                hr = StrAllocString(&sczLicenseFormatted, m_sczLicenseFile, 0);
                if (SUCCEEDED(hr))
                {
                    hr = LocLocalizeString(m_pWixLoc, &sczLicenseFormatted);
                    if (SUCCEEDED(hr))
                    {
                        // Assume there is no hidden variables to be formatted
                        // so don't worry about securely freeing it.
                        hr = BalFormatString(sczLicenseFormatted, &sczLicenseFormatted);
                        if (SUCCEEDED(hr))
                        {
                            hr = PathRelativeToModule(&sczLicensePath, sczLicenseFormatted, m_hModule);
                            if (SUCCEEDED(hr))
                            {
                                hr = PathGetDirectory(sczLicensePath, &sczLicenseDirectory);
                                if (SUCCEEDED(hr))
                                {
                                    hr = StrAllocString(&sczLicenseFilename, PathFile(sczLicenseFormatted), 0);
                                    if (SUCCEEDED(hr))
                                    {
                                        hr = LocProbeForFile(sczLicenseDirectory, sczLicenseFilename, m_sczLanguage, &sczLicensePath);
                                        if (SUCCEEDED(hr))
                                        {
                                            hr = ThemeLoadRichEditFromFile(m_pTheme, WIXSTDBA_CONTROL_EULA_RICHEDIT, sczLicensePath, m_hModule);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (FAILED(hr))
            {
                BalLog(BOOTSTRAPPER_LOG_LEVEL_ERROR, "Failed to load file into license richedit control from path '%ls' manifest value: %ls", sczLicensePath, m_sczLicenseFile);
                hr = S_OK;
            }
        }

        if (m_pfnBAFunctionsProc)
        {
            themeLoadedArgs.cbSize = sizeof(themeLoadedArgs);
            themeLoadedArgs.pTheme = m_pTheme;
            themeLoadedArgs.pWixLoc = m_pWixLoc;
            themeLoadedResults.cbSize = sizeof(themeLoadedResults);
            hr = m_pfnBAFunctionsProc(BA_FUNCTIONS_MESSAGE_ONTHEMELOADED, &themeLoadedArgs, &themeLoadedResults, m_pvBAFunctionsProcContext);
            BalExitOnFailure(hr, "BAFunctions OnThemeLoaded failed.");
        }

    LExit:
        ReleaseStr(sczLicenseFilename);
        ReleaseStr(sczLicenseDirectory);
        ReleaseStr(sczLicensePath);
        ReleaseStr(sczLicenseFormatted);

        return SUCCEEDED(hr);
    }


    //
    // OnShowFailure - display the failure page.
    //
    void OnShowFailure()
    {
        SetState(WIXSTDBA_STATE_FAILED, S_OK);

        // If the UI should be visible, display it now and hide the splash screen
        if (BOOTSTRAPPER_DISPLAY_NONE < m_command.display)
        {
            ::ShowWindow(m_pTheme->hwndParent, SW_SHOW);
        }

        m_pEngine->CloseSplashScreen();
    }


    //
    // OnShowHelp - display the help page.
    //
    void OnShowHelp()
    {
        SetState(WIXSTDBA_STATE_HELP, S_OK);

        // If the UI should be visible, display it now and hide the splash screen
        if (BOOTSTRAPPER_DISPLAY_NONE < m_command.display)
        {
            ::ShowWindow(m_pTheme->hwndParent, SW_SHOW);
        }

        m_pEngine->CloseSplashScreen();
    }


    //
    // OnDetect - start the processing of packages.
    //
    void OnDetect()
    {
        HRESULT hr = S_OK;

        SetState(WIXSTDBA_STATE_DETECTING, hr);

        // If the UI should be visible, display it now and hide the splash screen
        if (BOOTSTRAPPER_DISPLAY_NONE < m_command.display)
        {
            ::ShowWindow(m_pTheme->hwndParent, SW_SHOW);
        }

        m_pEngine->CloseSplashScreen();

        // Tell the core we're ready for the packages to be processed now.
        hr = m_pEngine->Detect();
        BalExitOnFailure(hr, "Failed to start detecting chain.");

    LExit:
        if (FAILED(hr))
        {
            SetState(WIXSTDBA_STATE_DETECTING, hr);
        }
    }


    //
    // OnPlan - plan the detected changes.
    //
    void OnPlan(
        __in BOOTSTRAPPER_ACTION action
        )
    {
        HRESULT hr = S_OK;

        m_plannedAction = action;

        SetState(WIXSTDBA_STATE_PLANNING, hr);

        hr = m_pEngine->Plan(action);
        BalExitOnFailure(hr, "Failed to start planning packages.");

    LExit:
        if (FAILED(hr))
        {
            SetState(WIXSTDBA_STATE_PLANNING, hr);
        }
    }


    //
    // OnApply - apply the packages.
    //
    void OnApply()
    {
        HRESULT hr = S_OK;

        SetState(WIXSTDBA_STATE_APPLYING, hr);
        SetProgressState(hr);
        SetTaskbarButtonProgress(0);

        hr = m_pEngine->Apply(m_hWnd);
        BalExitOnFailure(hr, "Failed to start applying packages.");

        ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_PROGRESS_CANCEL_BUTTON, TRUE); // ensure the cancel button is enabled before starting.

    LExit:
        if (FAILED(hr))
        {
            SetState(WIXSTDBA_STATE_APPLYING, hr);
        }
    }


    //
    // OnChangeState - change state.
    //
    void OnChangeState(
        __in WIXSTDBA_STATE state
        )
    {
        WIXSTDBA_STATE stateOld = m_state;
        DWORD dwOldPageId = 0;
        DWORD dwNewPageId = 0;
        LPWSTR sczText = NULL;
        LPWSTR sczUnformattedText = NULL;
        LPWSTR sczControlState = NULL;
        LPWSTR sczControlName = NULL;

        m_state = state;

        // If our install is at the end (success or failure) and we're not showing full UI or
        // we successfully installed the prerequisite(s) and either no restart is required or can automatically restart
        // then exit.
        if ((WIXSTDBA_STATE_APPLIED <= m_state && BOOTSTRAPPER_DISPLAY_FULL > m_command.display) ||
            (WIXSTDBA_STATE_APPLIED == m_state && m_fPrereq && (!m_fRestartRequired || m_fShouldRestart && m_fAllowRestart)))
        {
            // Quietly exit.
            ::PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
        }
        else // try to change the pages.
        {
            DeterminePageId(stateOld, &dwOldPageId);
            DeterminePageId(m_state, &dwNewPageId);

            if (dwOldPageId != dwNewPageId)
            {
                // Enable disable controls per-page.
                if (m_rgdwPageIds[WIXSTDBA_PAGE_INSTALL] == dwNewPageId) // on the "Install" page, ensure the install button is enabled/disabled correctly.
                {
                    LONGLONG llElevated = 0;
                    if (m_Bundle.fPerMachine)
                    {
                        BalGetNumericVariable(WIXBUNDLE_VARIABLE_ELEVATED, &llElevated);
                    }
                    ThemeControlElevates(m_pTheme, WIXSTDBA_CONTROL_INSTALL_BUTTON, (m_Bundle.fPerMachine && !llElevated));

                    // If the EULA control exists, show it only if a license URL is provided as well.
                    if (ThemeControlExists(m_pTheme, WIXSTDBA_CONTROL_EULA_LINK))
                    {
                        BOOL fEulaLink = (m_sczLicenseUrl && *m_sczLicenseUrl);
                        ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_EULA_LINK, fEulaLink);
                        ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_EULA_ACCEPT_CHECKBOX, fEulaLink);
                    }

                    BOOL fAcceptedLicense = !ThemeControlExists(m_pTheme, WIXSTDBA_CONTROL_EULA_ACCEPT_CHECKBOX) || !ThemeControlEnabled(m_pTheme, WIXSTDBA_CONTROL_EULA_ACCEPT_CHECKBOX) || ThemeIsControlChecked(m_pTheme, WIXSTDBA_CONTROL_EULA_ACCEPT_CHECKBOX);
                    ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_INSTALL_BUTTON, fAcceptedLicense);
                }
                else if (m_rgdwPageIds[WIXSTDBA_PAGE_MODIFY] == dwNewPageId)
                {
                    ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_REPAIR_BUTTON, !m_fSuppressRepair);
                }
                else if (m_rgdwPageIds[WIXSTDBA_PAGE_SUCCESS] == dwNewPageId) // on the "Success" page, check if the restart or launch button should be enabled.
                {
                    BOOL fEnableRestartButton = FALSE;
                    BOOL fLaunchTargetExists = FALSE;
                    if (m_fShouldRestart)
                    {
                        if (BAL_INFO_RESTART_PROMPT == m_BalInfoCommand.restart)
                        {
                            fEnableRestartButton = TRUE;
                        }
                    }
                    else if (ThemeControlExists(m_pTheme, WIXSTDBA_CONTROL_LAUNCH_BUTTON))
                    {
                        fLaunchTargetExists = BalVariableExists(WIXSTDBA_VARIABLE_LAUNCH_TARGET_PATH);
                    }

                    ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_LAUNCH_BUTTON, fLaunchTargetExists && BOOTSTRAPPER_ACTION_UNINSTALL < m_plannedAction);
                    ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_SUCCESS_RESTART_BUTTON, fEnableRestartButton);
                }
                else if (m_rgdwPageIds[WIXSTDBA_PAGE_FAILURE] == dwNewPageId) // on the "Failure" page, show error message and check if the restart button should be enabled.
                {
                    BOOL fShowLogLink = (m_Bundle.sczLogVariable && *m_Bundle.sczLogVariable); // if there is a log file variable then we'll assume the log file exists.
                    BOOL fShowErrorMessage = FALSE;
                    BOOL fEnableRestartButton = FALSE;

                    if (FAILED(m_hrFinal))
                    {
                        // If we know the failure message, use that.
                        if (m_sczFailedMessage && *m_sczFailedMessage)
                        {
                            StrAllocString(&sczUnformattedText, m_sczFailedMessage, 0);
                        }
                        else if (E_MBAHOST_NET452_ON_WIN7RTM == m_hrFinal)
                        {
                            HRESULT hr = StrAllocString(&sczUnformattedText, L"#(loc.NET452WIN7RTMErrorMessage)", 0);
                            if (FAILED(hr))
                            {
                                BalLogError(hr, "Failed to initialize NET452WIN7RTMErrorMessage loc identifier.");
                            }
                            else
                            {
                                hr = LocLocalizeString(m_pWixLoc, &sczUnformattedText);
                                if (FAILED(hr))
                                {
                                    BalLogError(hr, "Failed to localize NET452WIN7RTMErrorMessage: %ls", sczUnformattedText);
                                    ReleaseNullStr(sczUnformattedText);
                                }
                            }
                        }
                        else if (E_DNCHOST_SCD_RUNTIME_FAILURE == m_hrFinal)
                        {
                            HRESULT hr = StrAllocString(&sczUnformattedText, L"#(loc.SCDRUNTIMEFAILUREErrorMessage)", 0);
                            if (FAILED(hr))
                            {
                                BalLogError(hr, "Failed to initialize SCDRUNTIMEFAILUREErrorMessage loc identifier.");
                            }
                            else
                            {
                                hr = LocLocalizeString(m_pWixLoc, &sczUnformattedText);
                                if (FAILED(hr))
                                {
                                    BalLogError(hr, "Failed to localize SCDRUNTIMEFAILUREErrorMessage: %ls", sczUnformattedText);
                                    ReleaseNullStr(sczUnformattedText);
                                }
                            }
                        }
                        else // try to get the error message from the error code.
                        {
                            StrAllocFromError(&sczUnformattedText, m_hrFinal, NULL);
                            if (!sczUnformattedText || !*sczUnformattedText)
                            {
                                StrAllocFromError(&sczUnformattedText, E_FAIL, NULL);
                            }
                        }

                        if (E_WIXSTDBA_CONDITION_FAILED == m_hrFinal)
                        {
                            if (sczUnformattedText)
                            {
                                StrAllocString(&sczText, sczUnformattedText, 0);
                            }
                        }
                        else if (E_MBAHOST_NET452_ON_WIN7RTM == m_hrFinal)
                        {
                            if (sczUnformattedText)
                            {
                                BalFormatString(sczUnformattedText, &sczText);
                            }
                        }
                        else if (E_DNCHOST_SCD_RUNTIME_FAILURE == m_hrFinal)
                        {
                            if (sczUnformattedText)
                            {
                                BalFormatString(sczUnformattedText, &sczText);
                            }
                        }
                        else
                        {
                            StrAllocFormatted(&sczText, L"0x%08x - %ls", m_hrFinal, sczUnformattedText);
                        }

                        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_FAILURE_MESSAGE_TEXT, sczText);
                        fShowErrorMessage = TRUE;
                    }

                    if (m_fShouldRestart)
                    {
                        if (BAL_INFO_RESTART_PROMPT == m_BalInfoCommand.restart)
                        {
                            fEnableRestartButton = TRUE;
                        }
                    }

                    ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_FAILURE_LOGFILE_LINK, fShowLogLink);
                    ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_FAILURE_MESSAGE_TEXT, fShowErrorMessage);
                    ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_FAILURE_RESTART_BUTTON, fEnableRestartButton);
                }

                HRESULT hr = ThemeShowPage(m_pTheme, dwOldPageId, SW_HIDE);
                if (FAILED(hr))
                {
                    BalLogError(hr, "Failed to hide page: %u", dwOldPageId);
                }

                hr = ThemeShowPage(m_pTheme, dwNewPageId, SW_SHOW);
                if (FAILED(hr))
                {
                    BalLogError(hr, "Failed to show page: %u", dwOldPageId);
                }

                // On the install page set the focus to the install button or the next enabled control if install is disabled.
                if (m_rgdwPageIds[WIXSTDBA_PAGE_INSTALL] == dwNewPageId)
                {
                    ThemeSetFocus(m_pTheme, WIXSTDBA_CONTROL_INSTALL_BUTTON);
                }
            }
        }

        ReleaseStr(sczText);
        ReleaseStr(sczUnformattedText);
        ReleaseStr(sczControlState);
        ReleaseStr(sczControlName);
    }


    //
    // OnClose - called when the window is trying to be closed.
    //
    BOOL OnClose()
    {
        BOOL fClose = FALSE;
        BOOL fCancel = FALSE;

        // If we've already succeeded or failed or showing the help page, just close (prompts are annoying if the bootstrapper is done).
        if (WIXSTDBA_STATE_APPLIED <= m_state || WIXSTDBA_STATE_HELP == m_state)
        {
            fClose = TRUE;
        }
        else // prompt the user or force the cancel if there is no UI.
        {
            ::EnterCriticalSection(&m_csShowingInternalUiThisPackage);
            fClose = PromptCancel(
                m_hWnd,
                BOOTSTRAPPER_DISPLAY_FULL != m_command.display || m_fShowingInternalUiThisPackage,
                m_sczConfirmCloseMessage ? m_sczConfirmCloseMessage : L"Are you sure you want to cancel?",
                m_pTheme->sczCaption);
            ::LeaveCriticalSection(&m_csShowingInternalUiThisPackage);

            fCancel = fClose;
        }

        // If we're doing progress then we never close, we just cancel to let rollback occur.
        if (WIXSTDBA_STATE_APPLYING <= m_state && WIXSTDBA_STATE_APPLIED > m_state)
        {
            // If we canceled, disable cancel button since clicking it again is silly.
            if (fClose)
            {
                ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_PROGRESS_CANCEL_BUTTON, FALSE);
            }

            fClose = FALSE;
        }

        if (fClose)
        {
            DWORD dwCurrentPageId = 0;
            DeterminePageId(m_state, &dwCurrentPageId);

            // Hide the current page to let thmutil do its thing with variables.
            ThemeShowPageEx(m_pTheme, dwCurrentPageId, SW_HIDE, fCancel ? THEME_SHOW_PAGE_REASON_CANCEL : THEME_SHOW_PAGE_REASON_DEFAULT);
        }

        return fClose;
    }


    //
    // OnClickAcceptCheckbox - allow the install to continue.
    //
    void OnClickAcceptCheckbox()
    {
        BOOL fAcceptedLicense = ThemeIsControlChecked(m_pTheme, WIXSTDBA_CONTROL_EULA_ACCEPT_CHECKBOX);
        ThemeControlEnable(m_pTheme, WIXSTDBA_CONTROL_INSTALL_BUTTON, fAcceptedLicense);
    }


    //
    // OnClickInstallButton - start the install by planning the packages.
    //
    void OnClickInstallButton()
    {
        this->OnPlan(BOOTSTRAPPER_ACTION_INSTALL);
    }


    //
    // OnClickRepairButton - start the repair.
    //
    void OnClickRepairButton()
    {
        this->OnPlan(BOOTSTRAPPER_ACTION_REPAIR);
    }


    //
    // OnClickUninstallButton - start the uninstall.
    //
    void OnClickUninstallButton()
    {
        this->OnPlan(BOOTSTRAPPER_ACTION_UNINSTALL);
    }


    //
    // OnClickCloseButton - close the application.
    //
    void OnClickCloseButton()
    {
        ::SendMessageW(m_hWnd, WM_CLOSE, 0, 0);
    }


    //
    // OnClickEulaLink - show the end user license agreement.
    //
    void OnClickEulaLink()
    {
        HRESULT hr = S_OK;
        LPWSTR sczLicenseUrl = NULL;
        LPWSTR sczLicensePath = NULL;
        LPWSTR sczLicenseDirectory = NULL;
        LPWSTR sczLicenseFilename = NULL;
        URI_PROTOCOL protocol = URI_PROTOCOL_UNKNOWN;

        hr = StrAllocString(&sczLicenseUrl, m_sczLicenseUrl, 0);
        BalExitOnFailure(hr, "Failed to copy license URL: %ls", m_sczLicenseUrl);

        hr = LocLocalizeString(m_pWixLoc, &sczLicenseUrl);
        BalExitOnFailure(hr, "Failed to localize license URL: %ls", m_sczLicenseUrl);

        // Assume there is no hidden variables to be formatted
        // so don't worry about securely freeing it.
        hr = BalFormatString(sczLicenseUrl, &sczLicenseUrl);
        BalExitOnFailure(hr, "Failed to get formatted license URL: %ls", m_sczLicenseUrl);

        hr = UriProtocol(sczLicenseUrl, &protocol);
        if (FAILED(hr) || URI_PROTOCOL_UNKNOWN == protocol)
        {
            // Probe for localized license file
            hr = PathRelativeToModule(&sczLicensePath, sczLicenseUrl, m_hModule);
            if (SUCCEEDED(hr))
            {
                hr = PathGetDirectory(sczLicensePath, &sczLicenseDirectory);
                if (SUCCEEDED(hr))
                {
                    hr = LocProbeForFile(sczLicenseDirectory, PathFile(sczLicenseUrl), m_sczLanguage, &sczLicensePath);
                }
            }
        }

        hr = ShelExecUnelevated(sczLicensePath ? sczLicensePath : sczLicenseUrl, NULL, L"open", NULL, SW_SHOWDEFAULT);
        BalExitOnFailure(hr, "Failed to launch URL to EULA.");

    LExit:
        ReleaseStr(sczLicensePath);
        ReleaseStr(sczLicenseUrl);
        ReleaseStr(sczLicenseDirectory);
        ReleaseStr(sczLicenseFilename);
    }


    //
    // OnClickLaunchButton - launch the app from the success page.
    //
    void OnClickLaunchButton()
    {
        HRESULT hr = S_OK;
        LPWSTR sczUnformattedLaunchTarget = NULL;
        LPWSTR sczLaunchTarget = NULL;
        LPWSTR sczLaunchTargetElevatedId = NULL;
        LPWSTR sczUnformattedArguments = NULL;
        LPWSTR sczArguments = NULL;
        LPWSTR sczUnformattedLaunchFolder = NULL;
        LPWSTR sczLaunchFolder = NULL;
        int nCmdShow = SW_SHOWNORMAL;

        hr = BalGetStringVariable(WIXSTDBA_VARIABLE_LAUNCH_TARGET_PATH, &sczUnformattedLaunchTarget);
        BalExitOnFailure(hr, "Failed to get launch target variable '%ls'.", WIXSTDBA_VARIABLE_LAUNCH_TARGET_PATH);

        hr = BalFormatString(sczUnformattedLaunchTarget, &sczLaunchTarget);
        BalExitOnFailure(hr, "Failed to format launch target variable: %ls", sczUnformattedLaunchTarget);

        if (BalVariableExists(WIXSTDBA_VARIABLE_LAUNCH_TARGET_ELEVATED_ID))
        {
            hr = BalGetStringVariable(WIXSTDBA_VARIABLE_LAUNCH_TARGET_ELEVATED_ID, &sczLaunchTargetElevatedId);
            BalExitOnFailure(hr, "Failed to get launch target elevated id '%ls'.", WIXSTDBA_VARIABLE_LAUNCH_TARGET_ELEVATED_ID);
        }

        if (BalVariableExists(WIXSTDBA_VARIABLE_LAUNCH_ARGUMENTS))
        {
            hr = BalGetStringVariable(WIXSTDBA_VARIABLE_LAUNCH_ARGUMENTS, &sczUnformattedArguments);
            BalExitOnFailure(hr, "Failed to get launch arguments '%ls'.", WIXSTDBA_VARIABLE_LAUNCH_ARGUMENTS);
        }

        if (BalVariableExists(WIXSTDBA_VARIABLE_LAUNCH_HIDDEN))
        {
            nCmdShow = SW_HIDE;
        }

        if (BalVariableExists(WIXSTDBA_VARIABLE_LAUNCH_WORK_FOLDER))
        {
            hr = BalGetStringVariable(WIXSTDBA_VARIABLE_LAUNCH_WORK_FOLDER, &sczUnformattedLaunchFolder);
            BalExitOnFailure(hr, "Failed to get launch working directory variable '%ls'.", WIXSTDBA_VARIABLE_LAUNCH_WORK_FOLDER);
        }

        if (sczLaunchTargetElevatedId && !m_fTriedToLaunchElevated)
        {
            m_fTriedToLaunchElevated = TRUE;
            hr = m_pEngine->LaunchApprovedExe(m_hWnd, sczLaunchTargetElevatedId, sczUnformattedArguments, 0);
            if (FAILED(hr))
            {
                BalLogError(hr, "Failed to launch elevated target: %ls", sczLaunchTargetElevatedId);

                //try with ShelExec next time
                OnClickLaunchButton();
            }
        }
        else
        {
            if (sczUnformattedArguments)
            {
                hr = BalFormatString(sczUnformattedArguments, &sczArguments);
                BalExitOnFailure(hr, "Failed to format launch arguments variable: %ls", sczUnformattedArguments);
            }

            if (sczUnformattedLaunchFolder)
            {
                hr = BalFormatString(sczUnformattedLaunchFolder, &sczLaunchFolder);
                BalExitOnFailure(hr, "Failed to format launch working directory variable: %ls", sczUnformattedLaunchFolder);
            }

            hr = ShelExec(sczLaunchTarget, sczArguments, L"open", sczLaunchFolder, nCmdShow, m_hWnd, NULL);
            BalExitOnFailure(hr, "Failed to launch target: %ls", sczLaunchTarget);

            ::PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
        }

    LExit:
        StrSecureZeroFreeString(sczLaunchFolder);
        ReleaseStr(sczUnformattedLaunchFolder);
        StrSecureZeroFreeString(sczArguments);
        ReleaseStr(sczUnformattedArguments);
        ReleaseStr(sczLaunchTargetElevatedId);
        StrSecureZeroFreeString(sczLaunchTarget);
        ReleaseStr(sczUnformattedLaunchTarget);
    }


    //
    // OnClickRestartButton - allows the restart and closes the app.
    //
    void OnClickRestartButton()
    {
        AssertSz(m_fRestartRequired, "Restart must be requested to be able to click on the restart button.");

        m_fAllowRestart = TRUE;
        ::SendMessageW(m_hWnd, WM_CLOSE, 0, 0);
    }


    //
    // OnClickLogFileLink - show the log file.
    //
    void OnClickLogFileLink()
    {
        HRESULT hr = S_OK;
        LPWSTR sczLogFile = NULL;

        hr = BalGetStringVariable(m_Bundle.sczLogVariable, &sczLogFile);
        BalExitOnFailure(hr, "Failed to get log file variable '%ls'.", m_Bundle.sczLogVariable);

        hr = ShelExecUnelevated(L"notepad.exe", sczLogFile, L"open", NULL, SW_SHOWDEFAULT);
        BalExitOnFailure(hr, "Failed to open log file target: %ls", sczLogFile);

    LExit:
        ReleaseStr(sczLogFile);
    }


    //
    // SetState
    //
    void SetState(
        __in WIXSTDBA_STATE state,
        __in HRESULT hrStatus
        )
    {
        if (FAILED(hrStatus))
        {
            m_hrFinal = hrStatus;
        }

        if (FAILED(m_hrFinal))
        {
            state = WIXSTDBA_STATE_FAILED;
        }

        if (m_state < state)
        {
            ::PostMessageW(m_hWnd, WM_WIXSTDBA_CHANGE_STATE, 0, state);
        }
    }


    void DeterminePageId(
        __in WIXSTDBA_STATE state,
        __out DWORD* pdwPageId
        )
    {
        if (BOOTSTRAPPER_DISPLAY_PASSIVE == m_command.display)
        {
            switch (state)
            {
            case WIXSTDBA_STATE_INITIALIZED:
                *pdwPageId = BOOTSTRAPPER_ACTION_HELP == m_command.action ? m_rgdwPageIds[WIXSTDBA_PAGE_HELP] : m_rgdwPageIds[WIXSTDBA_PAGE_LOADING];
                break;

            case WIXSTDBA_STATE_HELP:
                *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_HELP];
                break;

            case WIXSTDBA_STATE_DETECTING:
                *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_LOADING] ? m_rgdwPageIds[WIXSTDBA_PAGE_LOADING] : m_rgdwPageIds[WIXSTDBA_PAGE_PROGRESS_PASSIVE] ? m_rgdwPageIds[WIXSTDBA_PAGE_PROGRESS_PASSIVE] : m_rgdwPageIds[WIXSTDBA_PAGE_PROGRESS];
                break;

            case WIXSTDBA_STATE_DETECTED: __fallthrough;
            case WIXSTDBA_STATE_PLANNING: __fallthrough;
            case WIXSTDBA_STATE_PLANNED: __fallthrough;
            case WIXSTDBA_STATE_APPLYING: __fallthrough;
            case WIXSTDBA_STATE_CACHING: __fallthrough;
            case WIXSTDBA_STATE_CACHED: __fallthrough;
            case WIXSTDBA_STATE_EXECUTING: __fallthrough;
            case WIXSTDBA_STATE_EXECUTED:
                *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_PROGRESS_PASSIVE] ? m_rgdwPageIds[WIXSTDBA_PAGE_PROGRESS_PASSIVE] : m_rgdwPageIds[WIXSTDBA_PAGE_PROGRESS];
                break;

            default:
                *pdwPageId = 0;
                break;
            }
        }
        else if (BOOTSTRAPPER_DISPLAY_FULL == m_command.display)
        {
            switch (state)
            {
            case WIXSTDBA_STATE_INITIALIZING:
                *pdwPageId = 0;
                break;

            case WIXSTDBA_STATE_INITIALIZED:
                *pdwPageId = BOOTSTRAPPER_ACTION_HELP == m_command.action ? m_rgdwPageIds[WIXSTDBA_PAGE_HELP] : m_rgdwPageIds[WIXSTDBA_PAGE_LOADING];
                break;

            case WIXSTDBA_STATE_HELP:
                *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_HELP];
                break;

            case WIXSTDBA_STATE_DETECTING:
                *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_LOADING];
                break;

            case WIXSTDBA_STATE_DETECTED:
                switch (m_command.action)
                {
                case BOOTSTRAPPER_ACTION_INSTALL:
                    *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_INSTALL];
                    break;

                case BOOTSTRAPPER_ACTION_MODIFY: __fallthrough;
                case BOOTSTRAPPER_ACTION_REPAIR: __fallthrough;
                case BOOTSTRAPPER_ACTION_UNINSTALL:
                    *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_MODIFY];
                    break;
                }
                break;

            case WIXSTDBA_STATE_PLANNING: __fallthrough;
            case WIXSTDBA_STATE_PLANNED: __fallthrough;
            case WIXSTDBA_STATE_APPLYING: __fallthrough;
            case WIXSTDBA_STATE_CACHING: __fallthrough;
            case WIXSTDBA_STATE_CACHED: __fallthrough;
            case WIXSTDBA_STATE_EXECUTING: __fallthrough;
            case WIXSTDBA_STATE_EXECUTED:
                *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_PROGRESS];
                break;

            case WIXSTDBA_STATE_APPLIED:
                *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_SUCCESS];
                break;

            case WIXSTDBA_STATE_FAILED:
                *pdwPageId = m_rgdwPageIds[WIXSTDBA_PAGE_FAILURE];
                break;
            }
        }
    }


    HRESULT EvaluateConditions()
    {
        HRESULT hr = S_OK;
        BOOL fResult = FALSE;

        for (DWORD i = 0; i < m_Conditions.cConditions; ++i)
        {
            BAL_CONDITION* pCondition = m_Conditions.rgConditions + i;

            hr = BalConditionEvaluate(pCondition, m_pEngine, &fResult, &m_sczFailedMessage);
            BalExitOnFailure(hr, "Failed to evaluate condition.");

            if (!fResult)
            {
                hr = E_WIXSTDBA_CONDITION_FAILED;
                BalExitOnFailure(hr, "%ls", m_sczFailedMessage);
            }
        }

        ReleaseNullStrSecure(m_sczFailedMessage);

    LExit:
        return hr;
    }

    void UpdateCacheProgress(
        __in DWORD dwOverallPercentage
        )
    {
        WCHAR wzProgress[5] = { };

        ::StringCchPrintfW(wzProgress, countof(wzProgress), L"%u%%", dwOverallPercentage);
        ThemeSetTextControl(m_pTheme, WIXSTDBA_CONTROL_CACHE_PROGRESS_TEXT, wzProgress);

        ThemeSetProgressControl(m_pTheme, WIXSTDBA_CONTROL_CACHE_PROGRESS_BAR, dwOverallPercentage);

        m_dwCalculatedCacheProgress = dwOverallPercentage * WIXSTDBA_ACQUIRE_PERCENTAGE / 100;
        ThemeSetProgressControl(m_pTheme, WIXSTDBA_CONTROL_OVERALL_CALCULATED_PROGRESS_BAR, m_dwCalculatedCacheProgress + m_dwCalculatedExecuteProgress);

        SetTaskbarButtonProgress(m_dwCalculatedCacheProgress + m_dwCalculatedExecuteProgress);
    }


    void SetTaskbarButtonProgress(
        __in DWORD dwOverallPercentage
        )
    {
        HRESULT hr = S_OK;

        if (m_fTaskbarButtonOK)
        {
            hr = m_pTaskbarList->SetProgressValue(m_hWnd, dwOverallPercentage, 100UL);
            BalExitOnFailure(hr, "Failed to set taskbar button progress to: %d%%.", dwOverallPercentage);
        }

    LExit:
        return;
    }


    void SetTaskbarButtonState(
        __in TBPFLAG tbpFlags
        )
    {
        HRESULT hr = S_OK;

        if (m_fTaskbarButtonOK)
        {
            hr = m_pTaskbarList->SetProgressState(m_hWnd, tbpFlags);
            BalExitOnFailure(hr, "Failed to set taskbar button state.", tbpFlags);
        }

    LExit:
        return;
    }


    void SetProgressState(
        __in HRESULT hrStatus
        )
    {
        TBPFLAG flag = TBPF_NORMAL;

        if (IsCanceled() || HRESULT_FROM_WIN32(ERROR_INSTALL_USEREXIT) == hrStatus)
        {
            flag = TBPF_PAUSED;
        }
        else if (IsRollingBack() || FAILED(hrStatus))
        {
            flag = TBPF_ERROR;
        }

        SetTaskbarButtonState(flag);
    }


    HRESULT LoadBAFunctions(
        __in IXMLDOMDocument* pixdManifest
        )
    {
        HRESULT hr = S_OK;
        IXMLDOMNode* pBAFunctionsNode = NULL;
        LPWSTR sczBafName = NULL;
        LPWSTR sczBafPath = NULL;
        BA_FUNCTIONS_CREATE_ARGS bafCreateArgs = { };
        BA_FUNCTIONS_CREATE_RESULTS bafCreateResults = { };

        hr = XmlSelectSingleNode(pixdManifest, L"/BootstrapperApplicationData/WixBalBAFunctions", &pBAFunctionsNode);
        BalExitOnFailure(hr, "Failed to read WixBalBAFunctions node from BootstrapperApplicationData.xml.");

        if (S_FALSE == hr)
        {
            ExitFunction();
        }

        hr = XmlGetAttributeEx(pBAFunctionsNode, L"FilePath", &sczBafName);
        BalExitOnFailure(hr, "Failed to get BAFunctions FilePath.");

        hr = PathRelativeToModule(&sczBafPath, sczBafName, m_hModule);
        BalExitOnFailure(hr, "Failed to get path to BAFunctions DLL.");

        BalLog(BOOTSTRAPPER_LOG_LEVEL_STANDARD, "WIXSTDBA: LoadBAFunctions() - BAFunctions DLL %ls", sczBafPath);

        m_hBAFModule = ::LoadLibraryExW(sczBafPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        BalExitOnNullWithLastError(m_hBAFModule, hr, "WIXSTDBA: LoadBAFunctions() - Failed to load DLL %ls", sczBafPath);

        PFN_BA_FUNCTIONS_CREATE pfnBAFunctionsCreate = reinterpret_cast<PFN_BA_FUNCTIONS_CREATE>(::GetProcAddress(m_hBAFModule, "BAFunctionsCreate"));
        BalExitOnNullWithLastError(pfnBAFunctionsCreate, hr, "Failed to get BAFunctionsCreate entry-point from: %ls", sczBafPath);

        bafCreateArgs.cbSize = sizeof(bafCreateArgs);
        bafCreateArgs.qwBAFunctionsAPIVersion = MAKEQWORDVERSION(0, 0, 0, 2); // TODO: need to decide whether to keep this, and if so when to update it.
        bafCreateArgs.pBootstrapperCreateArgs = &m_createArgs;

        bafCreateResults.cbSize = sizeof(bafCreateResults);

        hr = pfnBAFunctionsCreate(&bafCreateArgs, &bafCreateResults);
        BalExitOnFailure(hr, "Failed to create BAFunctions.");

        m_pfnBAFunctionsProc = bafCreateResults.pfnBAFunctionsProc;
        m_pvBAFunctionsProcContext = bafCreateResults.pvBAFunctionsProcContext;

    LExit:
        if (m_hBAFModule && !m_pfnBAFunctionsProc)
        {
            ::FreeLibrary(m_hBAFModule);
            m_hBAFModule = NULL;
        }
        ReleaseStr(sczBafPath);
        ReleaseStr(sczBafName);
        ReleaseObject(pBAFunctionsNode);

        return hr;
    }


public:
    //
    // Constructor - initialize member variables.
    //
    CWixStandardBootstrapperApplication(
        __in HMODULE hModule,
        __in BOOL fPrereq,
        __in HRESULT hrHostInitialization,
        __in IBootstrapperEngine* pEngine
    ) : CBalBaseBootstrapperApplication(pEngine, 3, 3000)
    {
        m_hModule = hModule;
        m_command = { };
        m_createArgs = { };

        m_plannedAction = BOOTSTRAPPER_ACTION_UNKNOWN;

        m_sczAfterForcedRestartPackage = NULL;

        m_pWixLoc = NULL;
        m_Bundle = { };
        m_Conditions = { };
        m_sczConfirmCloseMessage = NULL;
        m_sczFailedMessage = NULL;

        m_sczLanguage = NULL;
        m_pTheme = NULL;
        memset(m_rgdwPageIds, 0, sizeof(m_rgdwPageIds));
        m_hUiThread = NULL;
        m_fRegistered = FALSE;
        m_hWnd = NULL;

        m_state = WIXSTDBA_STATE_INITIALIZING;
        m_hrFinal = hrHostInitialization;

        m_fDowngrading = FALSE;
        m_restartResult = BOOTSTRAPPER_APPLY_RESTART_NONE;
        m_fRestartRequired = FALSE;
        m_fShouldRestart = FALSE;
        m_fAllowRestart = FALSE;

        m_sczLicenseFile = NULL;
        m_sczLicenseUrl = NULL;
        m_fSuppressDowngradeFailure = FALSE;
        m_fSuppressRepair = FALSE;
        m_fSupportCacheOnly = FALSE;

        m_pTaskbarList = NULL;
        m_uTaskbarButtonCreatedMessage = UINT_MAX;
        m_fTaskbarButtonOK = FALSE;
        ::InitializeCriticalSection(&m_csShowingInternalUiThisPackage);
        m_fShowingInternalUiThisPackage = FALSE;
        m_fTriedToLaunchElevated = FALSE;

        m_fPrereq = fPrereq;
        m_fPrereqInstalled = FALSE;
        m_fPrereqAlreadyInstalled = FALSE;

        pEngine->AddRef();
        m_pEngine = pEngine;

        m_hBAFModule = NULL;
        m_pfnBAFunctionsProc = NULL;
        m_pvBAFunctionsProcContext = NULL;
    }


    //
    // Destructor - release member variables.
    //
    ~CWixStandardBootstrapperApplication()
    {
        AssertSz(!::IsWindow(m_hWnd), "Window should have been destroyed before destructor.");
        AssertSz(!m_pTaskbarList, "Taskbar should have been released before destructor.");
        AssertSz(!m_pTheme, "Theme should have been released before destructor.");

        for (DWORD i = 0; i < m_Bundle.packages.cPackages; ++i)
        {
            ReleaseMem(m_Bundle.packages.rgPackages[i].pvCustomData);
        }

        ::DeleteCriticalSection(&m_csShowingInternalUiThisPackage);
        ReleaseStr(m_sczFailedMessage);
        ReleaseStr(m_sczConfirmCloseMessage);
        BalConditionsUninitialize(&m_Conditions);
        BalInfoUninitialize(&m_Bundle);
        LocFree(m_pWixLoc);

        ReleaseStr(m_sczLanguage);
        ReleaseStr(m_sczLicenseFile);
        ReleaseStr(m_sczLicenseUrl);
        ReleaseStr(m_sczAfterForcedRestartPackage);
        ReleaseNullObject(m_pEngine);

        if (m_hBAFModule)
        {
            PFN_BA_FUNCTIONS_DESTROY pfnBAFunctionsDestroy = reinterpret_cast<PFN_BA_FUNCTIONS_DESTROY>(::GetProcAddress(m_hBAFModule, "BAFunctionsDestroy"));
            if (pfnBAFunctionsDestroy)
            {
                pfnBAFunctionsDestroy();
            }

            ::FreeLibrary(m_hBAFModule);
            m_hBAFModule = NULL;
        }
    }

private:
    HMODULE m_hModule;
    BOOTSTRAPPER_CREATE_ARGS m_createArgs;
    BOOTSTRAPPER_COMMAND m_command;
    IBootstrapperEngine* m_pEngine;
    BOOTSTRAPPER_ACTION m_plannedAction;

    LPWSTR m_sczAfterForcedRestartPackage;

    WIX_LOCALIZATION* m_pWixLoc;
    BAL_INFO_BUNDLE m_Bundle;
    BAL_CONDITIONS m_Conditions;
    LPWSTR m_sczFailedMessage;
    LPWSTR m_sczConfirmCloseMessage;

    LPWSTR m_sczLanguage;
    THEME* m_pTheme;
    DWORD m_rgdwPageIds[countof(vrgwzPageNames)];
    HANDLE m_hUiThread;
    BOOL m_fRegistered;
    HWND m_hWnd;

    WIXSTDBA_STATE m_state;
    HRESULT m_hrFinal;

    BOOL m_fStartedExecution;
    DWORD m_dwCalculatedCacheProgress;
    DWORD m_dwCalculatedExecuteProgress;

    BOOL m_fDowngrading;
    BOOTSTRAPPER_APPLY_RESTART m_restartResult;
    BOOL m_fRestartRequired;
    BOOL m_fShouldRestart;
    BOOL m_fAllowRestart;

    LPWSTR m_sczLicenseFile;
    LPWSTR m_sczLicenseUrl;
    BOOL m_fSuppressDowngradeFailure;
    BOOL m_fSuppressRepair;
    BOOL m_fSupportCacheOnly;

    BOOL m_fPrereq;
    BOOL m_fPrereqInstalled;
    BOOL m_fPrereqAlreadyInstalled;

    ITaskbarList3* m_pTaskbarList;
    UINT m_uTaskbarButtonCreatedMessage;
    BOOL m_fTaskbarButtonOK;
    CRITICAL_SECTION m_csShowingInternalUiThisPackage;
    BOOL m_fShowingInternalUiThisPackage;
    BOOL m_fTriedToLaunchElevated;

    HMODULE m_hBAFModule;
    PFN_BA_FUNCTIONS_PROC m_pfnBAFunctionsProc;
    LPVOID m_pvBAFunctionsProcContext;
};


//
// CreateBootstrapperApplication - creates a new IBootstrapperApplication object.
//
HRESULT CreateBootstrapperApplication(
    __in HMODULE hModule,
    __in BOOL fPrereq,
    __in HRESULT hrHostInitialization,
    __in IBootstrapperEngine* pEngine,
    __in const BOOTSTRAPPER_CREATE_ARGS* pArgs,
    __inout BOOTSTRAPPER_CREATE_RESULTS* pResults,
    __out IBootstrapperApplication** ppApplication
    )
{
    HRESULT hr = S_OK;
    CWixStandardBootstrapperApplication* pApplication = NULL;

    if (BOOTSTRAPPER_DISPLAY_UNKNOWN == pArgs->pCommand->display)
    {
        BalExitOnFailure(hr = E_INVALIDARG, "Engine requested Unknown display type.");
    }

    pApplication = new CWixStandardBootstrapperApplication(hModule, fPrereq, hrHostInitialization, pEngine);
    BalExitOnNull(pApplication, hr, E_OUTOFMEMORY, "Failed to create new standard bootstrapper application object.");

    hr = pApplication->Initialize(pArgs);
    ExitOnFailure(hr, "CWixStandardBootstrapperApplication initialization failed.");

    pResults->pfnBootstrapperApplicationProc = BalBaseBootstrapperApplicationProc;
    pResults->pvBootstrapperApplicationProcContext = pApplication;
    *ppApplication = pApplication;
    pApplication = NULL;

LExit:
    ReleaseObject(pApplication);
    return hr;
}


static HRESULT DAPI EvaluateVariableConditionCallback(
    __in_z LPCWSTR wzCondition,
    __out BOOL* pf,
    __in_opt LPVOID /*pvContext*/
    )
{
    return BalEvaluateCondition(wzCondition, pf);
}


static HRESULT DAPI FormatVariableStringCallback(
    __in_z LPCWSTR wzFormat,
    __inout LPWSTR* psczOut,
    __in_opt LPVOID /*pvContext*/
    )
{
    return BalFormatString(wzFormat, psczOut);
}


static HRESULT DAPI GetVariableNumericCallback(
    __in_z LPCWSTR wzVariable,
    __out LONGLONG* pllValue,
    __in_opt LPVOID /*pvContext*/
    )
{
    return BalGetNumericVariable(wzVariable, pllValue);
}


static HRESULT DAPI SetVariableNumericCallback(
    __in_z LPCWSTR wzVariable,
    __in LONGLONG llValue,
    __in_opt LPVOID /*pvContext*/
    )
{
    return BalSetNumericVariable(wzVariable, llValue);
}


static HRESULT DAPI GetVariableStringCallback(
    __in_z LPCWSTR wzVariable,
    __inout LPWSTR* psczValue,
    __in_opt LPVOID /*pvContext*/
    )
{
    return BalGetStringVariable(wzVariable, psczValue);
}


static HRESULT DAPI SetVariableStringCallback(
    __in_z LPCWSTR wzVariable,
    __in_z_opt LPCWSTR wzValue,
    __in BOOL fFormatted,
    __in_opt LPVOID /*pvContext*/
    )
{
    return BalSetStringVariable(wzVariable, wzValue, fFormatted);
}

static LPCSTR LoggingBoolToString(
    __in BOOL f
    )
{
    if (f)
    {
        return "Yes";
    }

    return "No";
}

static LPCSTR LoggingRequestStateToString(
    __in BOOTSTRAPPER_REQUEST_STATE requestState
    )
{
    switch (requestState)
    {
    case BOOTSTRAPPER_REQUEST_STATE_NONE:
        return "None";
    case BOOTSTRAPPER_REQUEST_STATE_FORCE_ABSENT:
        return "ForceAbsent";
    case BOOTSTRAPPER_REQUEST_STATE_ABSENT:
        return "Absent";
    case BOOTSTRAPPER_REQUEST_STATE_CACHE:
        return "Cache";
    case BOOTSTRAPPER_REQUEST_STATE_PRESENT:
        return "Present";
    case BOOTSTRAPPER_REQUEST_STATE_REPAIR:
        return "Repair";
    default:
        return "Invalid";
    }
}

static LPCSTR LoggingMsiFeatureStateToString(
    __in BOOTSTRAPPER_FEATURE_STATE featureState
    )
{
    switch (featureState)
    {
    case BOOTSTRAPPER_FEATURE_STATE_UNKNOWN:
        return "Unknown";
    case BOOTSTRAPPER_FEATURE_STATE_ABSENT:
        return "Absent";
    case BOOTSTRAPPER_FEATURE_STATE_ADVERTISED:
        return "Advertised";
    case BOOTSTRAPPER_FEATURE_STATE_LOCAL:
        return "Local";
    case BOOTSTRAPPER_FEATURE_STATE_SOURCE:
        return "Source";
    default:
        return "Invalid";
    }
}
