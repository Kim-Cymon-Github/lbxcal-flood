#include "cal_module.h"

/*---------------------------------------------------------------------------
 * cal-ox4 — Ox4 패턴 (타원 4점 마커) 캘리브레이션 모듈.
 *
 * Phase 0: 공유 entry + 더미 Run 골격만.
 * Phase 2 에서 연결할 고유 요소:
 *   - 초기 특징점 검출기: lbx_DetectEllipse + SelectEllipse4
 *   - 코너 검증 콜백: is_checker_cross
 *   - flood 옵션 프리셋: subpix 7x7, FAST9 th=60, 12점에서 렌즈캘 on
 *---------------------------------------------------------------------------*/
static const CAL_MODULE_DESC OX4_DESC = {
    "cal-ox4",
    "Ox4 four-ellipse calibration module (flood kernel)",
    "Ox4",
};

extern "C" CAL_MODULE_EXPORT var_t LBX_API cal_ox4_entry(LBX_MODULE_INTERFACE* intf, LbxModuleRequest request, const var_t* arguments)
{
    return cal_module_entry_common(intf, request, arguments, &OX4_DESC);
}
