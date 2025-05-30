#define NX_SERVICE_ASSUME_NON_DOMAIN
#include "service_guard.h"
#include "runtime/hosversion.h"
#include "services/ldr.h"

#define LDR_GENERATE_SERVICE_INIT(name, srvname)            \
static Service g_ldr##name##Srv;                            \
                                                            \
NX_GENERATE_SERVICE_GUARD(ldr##name);                       \
                                                            \
Result _ldr##name##Initialize(void) {                       \
    return smGetService(&g_ldr##name##Srv, "ldr:"#srvname); \
}                                                           \
                                                            \
void _ldr##name##Cleanup(void) {                            \
    serviceClose(&g_ldr##name##Srv);                        \
}                                                           \
                                                            \
Service* ldr##name##GetServiceSession(void) {               \
    return &g_ldr##name##Srv;                               \
}

LDR_GENERATE_SERVICE_INIT(Shell, shel);
LDR_GENERATE_SERVICE_INIT(Dmnt,  dmnt);
LDR_GENERATE_SERVICE_INIT(Pm,    pm);

static Result _ldrSetProgramArgumentsDeprecated(Service* srv, u64 program_id, const void *args, size_t args_size) {
    const struct {
        u32 args_size;
        u32 pad;
        u64 program_id;
    } in = { args_size, 0, program_id };

    return serviceDispatchIn(srv, 0, in,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcPointer },
        .buffers = { { args,  args_size } },
    );
}

static Result _ldrSetProgramArgumentsModern(Service* srv, u64 program_id, const void *args, size_t args_size) {
    return serviceDispatchIn(srv, 0, program_id,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcPointer },
        .buffers = { { args,  args_size } },
    );
}

static Result _ldrSetProgramArguments(Service* srv, u64 program_id, const void *args, size_t args_size) {
    if (hosversionAtLeast(11,0,0)) {
        return _ldrSetProgramArgumentsModern(srv, program_id, args, args_size);
    } else {
        return _ldrSetProgramArgumentsDeprecated(srv, program_id, args, args_size);
    }
}

static Result _ldrFlushArguments(Service* srv) {
    return serviceDispatch(srv, 1);
}

Result ldrShellSetProgramArguments(u64 program_id, const void *args, size_t args_size) {
    return _ldrSetProgramArguments(&g_ldrShellSrv, program_id, args, args_size);
}

Result ldrShellFlushArguments(void) {
    return _ldrFlushArguments(&g_ldrShellSrv);
}

Result ldrDmntSetProgramArguments(u64 program_id, const void *args, size_t args_size) {
    return _ldrSetProgramArguments(&g_ldrDmntSrv, program_id, args, args_size);
}

Result ldrDmntFlushArguments(void) {
    return _ldrFlushArguments(&g_ldrDmntSrv);
}

Result ldrDmntGetProcessModuleInfo(u64 pid, LoaderModuleInfo *out_module_infos, size_t max_out_modules, s32 *num_out) {
    return serviceDispatchInOut(&g_ldrDmntSrv, 2, pid, *num_out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcPointer },
        .buffers = { { out_module_infos, max_out_modules * sizeof(*out_module_infos) } },
    );
}

Result ldrPmCreateProcess(u64 pin_id, u32 flags, Handle reslimit_h, const LoaderProgramAttributes *attrs, Handle *out_process_h) {
    if (hosversionIsAtmosphere() || hosversionAtLeast(20,0,0)) {
        const struct {
            LoaderProgramAttributes attr;
            u16 pad;
            u32 flags;
            u64 pin_id;
        } in = { *attrs, 0, flags, pin_id };
        return serviceDispatchIn(&g_ldrPmSrv, 0, in,
            .in_num_handles = 1,
            .in_handles = { reslimit_h },
            .out_handle_attrs = { SfOutHandleAttr_HipcMove },
            .out_handles = out_process_h,
        );
    } else {
        const struct {
            u32 flags;
            u32 pad;
            u64 pin_id;
        } in = { flags, 0, pin_id };
        return serviceDispatchIn(&g_ldrPmSrv, 0, in,
            .in_num_handles = 1,
            .in_handles = { reslimit_h },
            .out_handle_attrs = { SfOutHandleAttr_HipcMove },
            .out_handles = out_process_h,
        );
    }
}

Result ldrPmGetProgramInfo(const NcmProgramLocation *loc, const LoaderProgramAttributes *attrs, LoaderProgramInfo *out_program_info) {
    if (!hosversionIsAtmosphere() && hosversionBefore(19, 0, 0))
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    if (hosversionIsAtmosphere() || hosversionAtLeast(20,0,0)) {
        const struct {
            LoaderProgramAttributes attr;
            u16 pad1;
            u32 pad2;
            NcmProgramLocation loc;
        } in = { *attrs, 0, 0, *loc };
        _Static_assert(sizeof(in) == 0x18);
        return serviceDispatchIn(&g_ldrPmSrv, 1, in,
            .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcPointer | SfBufferAttr_FixedSize },
            .buffers = { { out_program_info, sizeof(*out_program_info) } },
        );
    } else {
        return serviceDispatchIn(&g_ldrPmSrv, 1, *loc,
            .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcPointer | SfBufferAttr_FixedSize },
            .buffers = { { out_program_info, sizeof(*out_program_info) } },
        );
    }
}

Result ldrPmGetProgramInfoV1(const NcmProgramLocation *loc, LoaderProgramInfoV1 *out_program_info) {
    if (hosversionIsAtmosphere() || hosversionAtLeast(19,0,0))
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    return serviceDispatchIn(&g_ldrPmSrv, 1, *loc,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcPointer | SfBufferAttr_FixedSize },
        .buffers = { { out_program_info, sizeof(*out_program_info) } },
    );
}

Result ldrPmPinProgram(const NcmProgramLocation *loc, u64 *out_pin_id) {
    return serviceDispatchInOut(&g_ldrPmSrv, 2, *loc, *out_pin_id);
}

Result ldrPmUnpinProgram(u64 pin_id) {
    return serviceDispatchIn(&g_ldrPmSrv, 3, pin_id);
}

Result ldrPmSetEnabledProgramVerification(bool enabled) {
    if (hosversionBefore(10,0,0))
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    const u8 in = enabled != 0;
    return serviceDispatchIn(&g_ldrPmSrv, 4, in);
}
