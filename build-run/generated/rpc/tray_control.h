

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Tue Jan 19 06:14:07 2038
 */
/* Compiler settings for C:/Users/golov/Desktop/ZIVPO/ZIVPO_Practice/trayapp/src/rpc/tray_control.idl:
    Oicf, W1, Zp8, env=Win64 (32b run), target_arch=AMD64 8.01.0628 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */


#ifndef __tray_control_h__
#define __tray_control_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifndef DECLSPEC_XFGVIRT
#if defined(_CONTROL_FLOW_GUARD_XFG)
#define DECLSPEC_XFGVIRT(base, func) __declspec(xfg_virtual(base, func))
#else
#define DECLSPEC_XFGVIRT(base, func)
#endif
#endif

/* Forward Declarations */ 

#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __TrayControl_INTERFACE_DEFINED__
#define __TrayControl_INTERFACE_DEFINED__

/* interface TrayControl */
/* [version][uuid] */ 

typedef struct TrayAuthState
    {
    int authenticated;
    /* [string] */ wchar_t *username;
    } 	TrayAuthState;

typedef struct TrayLicenseState
    {
    int licensed;
    int blocked;
    hyper expiresAtUnix;
    } 	TrayLicenseState;

typedef struct TrayAvDatabaseInfo
    {
    hyper releaseDateUnix;
    unsigned long recordCount;
    } 	TrayAvDatabaseInfo;

typedef struct TrayScanReport
    {
    int malicious;
    unsigned long scannedFiles;
    unsigned long infectedFiles;
    /* [string] */ wchar_t *summary;
    } 	TrayScanReport;

void TrayStopService( 
    /* [in] */ handle_t binding);

unsigned long TrayGetAuthState( 
    /* [in] */ handle_t binding,
    /* [out] */ TrayAuthState *state);

unsigned long TrayLogin( 
    /* [in] */ handle_t binding,
    /* [string][in] */ wchar_t *username,
    /* [string][in] */ wchar_t *password,
    /* [out] */ TrayAuthState *state);

void TrayLogout( 
    /* [in] */ handle_t binding);

unsigned long TrayGetLicenseState( 
    /* [in] */ handle_t binding,
    /* [out] */ TrayLicenseState *state);

unsigned long TrayActivateProduct( 
    /* [in] */ handle_t binding,
    /* [string][in] */ wchar_t *licenseKey,
    /* [out] */ TrayLicenseState *state);

unsigned long TrayEnsureAntivirusAvailable( 
    /* [in] */ handle_t binding);

unsigned long TrayGetAvDatabaseInfo( 
    /* [in] */ handle_t binding,
    /* [out] */ TrayAvDatabaseInfo *info);

unsigned long TrayScanFile( 
    /* [in] */ handle_t binding,
    /* [string][in] */ wchar_t *path,
    /* [out] */ TrayScanReport *report);

unsigned long TrayScanDirectory( 
    /* [in] */ handle_t binding,
    /* [string][in] */ wchar_t *path,
    /* [out] */ TrayScanReport *report);

unsigned long TrayScanFixedDrives( 
    /* [in] */ handle_t binding,
    /* [out] */ TrayScanReport *report);

unsigned long TrayConfigureSchedule( 
    /* [in] */ handle_t binding,
    /* [in] */ unsigned long intervalMinutes);

unsigned long TrayGetScheduledScanReport( 
    /* [in] */ handle_t binding,
    /* [out] */ TrayScanReport *report);

unsigned long TrayAddMonitorDirectory( 
    /* [in] */ handle_t binding,
    /* [string][in] */ wchar_t *path);

unsigned long TrayGetMonitorScanReport( 
    /* [in] */ handle_t binding,
    /* [out] */ TrayScanReport *report);



extern RPC_IF_HANDLE TrayControl_v1_0_c_ifspec;
extern RPC_IF_HANDLE TrayControl_v1_0_s_ifspec;
#endif /* __TrayControl_INTERFACE_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


