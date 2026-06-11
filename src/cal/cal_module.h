#ifndef cal_moduleH
#define cal_moduleH
//---------------------------------------------------------------------------

#include "intf/lbx_intf_cal.h"

#ifdef _WIN32
#   ifdef CAL_MODULE_DLL
#       define CAL_MODULE_EXPORT __declspec(dllexport)
#   else
#       define CAL_MODULE_EXPORT __declspec(dllimport)
#   endif
#else
#   define CAL_MODULE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif //#ifdef __cplusplus

/**
 * @brief Static identity and policy of one cal algorithm module.
 *
 * One instance per algorithm DLL, defined at file scope in the
 * algorithm's own translation unit (cal_ox4.cpp / cal_hkmc.cpp).
 * The shared entry machinery reads it; it never changes at runtime.
 *
 * Grows with the porting phases: detector callbacks and the flood
 * option preset are added here when the shared kernel lands.
 */
typedef struct {
    const char* name;         /**< Module name, e.g. "cal-ox4". */
    const char* description;  /**< One-line module description. */
    const char* pattern_id;   /**< Panel/pattern id this module solves, e.g. "Ox4". */
} CAL_MODULE_DESC;

/**
 * @brief Per-instance module state, stored in LBX_CAL_MODULE.impl.
 *
 * Allocated at mrInitialize, freed at mrTerminate. All mutable state
 * lives here -- no file-scope globals (multi-instance / re-entry safe).
 */
typedef struct {
    const CAL_MODULE_DESC* desc;
    var_t last_result;        /**< Result of the last Run, kept for GetResult. */
} CAL_MODULE_IMPL;

/**
 * @brief Shared entry machinery used by every cal_<algo>_entry.
 *
 * Validates intf id/version, then services mrInitialize (allocate impl,
 * fill the LBX_CAL_MODULE vtable), mrTerminate (free impl) and mrQuery
 * (self-id document).
 */
var_t cal_module_entry_common(LBX_MODULE_INTERFACE* intf, LbxModuleRequest request,
                              const var_t* arguments, const CAL_MODULE_DESC* desc);

#ifdef __cplusplus
}
#endif //#ifdef __cplusplus

#endif //#ifndef cal_moduleH
