﻿#include <Ldk/windows.h>

#include <corecrt_startup.h>
#include <vcruntime_internal.h>
#include <Ldk/ldk.h>

EXTERN_C DRIVER_INITIALIZE CrtSysDriverEntry;
EXTERN_C DRIVER_UNLOAD CrtSysDriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, CrtSysDriverEntry)
#pragma alloc_text(PAGE, CrtSysDriverUnload)
#endif

#if CRTSYS_USE_NTL_MAIN
#include "../include/ntl/driver"
#include "../include/ntl/expand_stack"
#include <memory>
std::unique_ptr<ntl::driver> this_driver;
#else
EXTERN_C DRIVER_INITIALIZE DriverEntry;
#endif

PDRIVER_UNLOAD CrtsyspDriverUnload = NULL;



//
// :-(
// TEB Tls(gs[0x58], fs[0x18])에 직접 접근하는 코드가 존재하므로 임시로 버퍼를 설정합니다.
// %ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.24.28314\include\pplwin.h Line:84
//
CHAR CrtSyspTlsBuffer[1024 * 1024];
PVOID CrtSyspTlsSlots[1024] = { &CrtSyspTlsBuffer, };

VOID
CrtSysSetTebThreadLocalStoragePointer (
    _In_ PVOID ThreadLocalStoragePointer
    )
{
#if _WIN64
    __writegsqword(0x58, (ULONG_PTR)ThreadLocalStoragePointer);
#else
    __writefsdword(0x18, (ULONG_PTR)ThreadLocalStoragePointer);
#endif
}

_Function_class_(KDEFERRED_ROUTINE)
VOID
CrtpSetTebThreadLocalStoragePointerDpcRoutine(
    _In_ struct _KDPC* Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_ PVOID ThreadLocalStoragePointer,
    _In_ PVOID RemainingCount
    )
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);

    CrtSysSetTebThreadLocalStoragePointer(ThreadLocalStoragePointer);
    InterlockedDecrement((PLONG)RemainingCount);
}

NTSTATUS
CrtSysInitializeTebThreadLocalStoragePointer(
    VOID
    )
{
    KdBreakPoint();

    LONG remainingCount = KeNumberProcessors - 1;
    if (remainingCount == 0) {
        CrtSysSetTebThreadLocalStoragePointer(&CrtSyspTlsSlots);
        return STATUS_SUCCESS;
    }

    PKDPC dpcs = (PKDPC)ExAllocatePoolWithTag( NonPagedPool,
                                               sizeof(KDPC) * ((SIZE_T)KeNumberProcessors - 1),
                                               'pmeT' );
    if (dpcs == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KIRQL oldIrql = KeRaiseIrqlToDpcLevel();
    CCHAR currentProcessor = (CCHAR)KeGetCurrentProcessorNumber();
    PKDPC dpc = dpcs;
    for (CCHAR i = 0; i < KeNumberProcessors; i++) {
        if (i == currentProcessor) {
            continue;
        }
        KeInitializeDpc(dpc, CrtpSetTebThreadLocalStoragePointerDpcRoutine, NULL);
        KeSetTargetProcessorDpc(dpc, i);
        KeInsertQueueDpc(dpc, &CrtSyspTlsSlots, &remainingCount);
        dpc++;
    }
    CrtSysSetTebThreadLocalStoragePointer(&CrtSyspTlsSlots);
    while (InterlockedCompareExchange(&remainingCount, 0, 0) != 0) {
        YieldProcessor();
    }
    KeLowerIrql(oldIrql);

    ExFreePoolWithTag( dpcs,
                       'pmeT' );

    return STATUS_SUCCESS;
}



#if UCXXRT
extern "C" int  __cdecl _do_onexit();
extern "C" int  __cdecl _do_quick_onexit();

extern "C" void __cdecl __initialize_memory();
extern "C" void __cdecl __acrt_initialize_new_handler(_In_opt_ void* encoded_null);
#else
#include <vcstartup_internal.h>
extern "C" extern _onexit_table_t __acrt_atexit_table;
extern "C" extern _onexit_table_t __acrt_at_quick_exit_table;
#endif



EXTERN_C
NTSTATUS
CrtSysDriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;

    PAGED_CODE();
#if CRTSYS_USE_NTL_MAIN
    std::unique_ptr<ntl::driver> driver = std::make_unique<ntl::driver>(DriverObject);
    if (!driver) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
#endif
    status = CrtSysInitializeTebThreadLocalStoragePointer();
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = LdkInitialize( DriverObject,
                            RegistryPath,
                            0 );
    if (!NT_SUCCESS(status)) {
        return status;
    }

#if UCXXRT
    // do feature initializions
    __isa_available_init();

    // do memory initializions
    __initialize_memory();

    // do pointer initializions
    void* const encoded_null = __crt_fast_encode_pointer(nullptr);
    __acrt_initialize_new_handler(encoded_null);

    // do C initializions
    if (_initterm_e(__xi_a, __xi_z) != 0)
        return 255;

    // do C++ initializions
    _initterm(__xc_a, __xc_z);
#else
    if (!__scrt_initialize_onexit_tables(__scrt_module_type::exe)) {
       KdBreakPoint();
       LdkTerminate();
       return STATUS_UNSUCCESSFUL;
    }

    if (!__scrt_initialize_crt(__scrt_module_type::exe)) {
       KdBreakPoint();
       LdkTerminate();
       return STATUS_UNSUCCESSFUL;
    }

    _initialize_onexit_table(&__acrt_atexit_table);
    // _initialize_onexit_table(&__acrt_at_quick_exit_table);

    // Do C initialization:
    if (_initterm_e(__xi_a, __xi_z) != 0)
    {
       KdBreakPoint();
       __vcrt_uninitialize(false);
       LdkTerminate();
       return STATUS_UNSUCCESSFUL;
    }

    // Do C++ initialization:
    _initterm(__xc_a, __xc_z);

    //// If this module has any dynamically initialized __declspec(thread)
    //// variables, then we invoke their initialization for the primary thread
    //// used to start the process:
    //_tls_callback_type const* const tls_init_callback = __scrt_get_dyn_tls_init_callback();
    //if (*tls_init_callback != nullptr && __scrt_is_nonwritable_in_current_image(tls_init_callback))
    //{
    //   (*tls_init_callback)(nullptr, DLL_THREAD_ATTACH, nullptr);
    //}

    //// If this module has any thread-local destructors, register the
    //// callback function with the Unified CRT to run on exit.
    //_tls_callback_type const* const tls_dtor_callback = __scrt_get_dyn_tls_dtor_callback();
    //if (*tls_dtor_callback != nullptr && __scrt_is_nonwritable_in_current_image(tls_dtor_callback))
    //{
    //   _register_thread_local_exe_atexit_callback(*tls_dtor_callback);
    //}
#endif

#if CRTSYS_USE_NTL_MAIN
    ntl::status s = ntl::expand_stack( ntl::main,
                                       std::ref(*driver.get()),
                                       std::wstring(RegistryPath->Buffer) );
    if (!s.is_ok()) {
        CrtSysDriverUnload( DriverObject );
        return s;
    }
    this_driver = std::move(driver);
#else
    status = DriverEntry( DriverObject,
                          RegistryPath );
    if (!NT_SUCCESS(status)) {
        CrtSysDriverUnload( DriverObject );
        return status;
    }
    CrtsyspDriverUnload = DriverObject->DriverUnload;
#endif
    DriverObject->DriverUnload = CrtSysDriverUnload;
    return status;
}

#if CRTSYS_USE_NTL_MAIN
namespace ntl {
class driver_unload_invoker {
public:
  static void unload(driver &driver) { driver.unload(); }
};
} // namespace ntl
#endif

EXTERN_C
VOID
CrtSysDriverUnload (
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    PAGED_CODE();
#if CRTSYS_USE_NTL_MAIN
    UNREFERENCED_PARAMETER(DriverObject);
    if (this_driver) {
        ntl::driver_unload_invoker::unload( *this_driver.get() );
    }
#else
    if (CrtsyspDriverUnload) {
        CrtsyspDriverUnload( DriverObject );
    }
#endif
#if UCXXRT
    // do exit() of atexit()
    _do_onexit();

    // do pre terminations
    _initterm(__xp_a, __xp_z);

    // do terminations
    _initterm(__xt_a, __xt_z);
#else
    _execute_onexit_table(&__acrt_atexit_table);
    // _execute_onexit_table(&__acrt_at_quick_exit_table);

    _initterm(__xp_a, __xp_z);
    _initterm(__xt_a, __xt_z);

    __scrt_uninitialize_crt(true, false);
#endif
    LdkTerminate();
}