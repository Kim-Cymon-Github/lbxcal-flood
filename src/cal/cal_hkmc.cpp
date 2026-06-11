#include "cal_module.h"

/*---------------------------------------------------------------------------
 * cal-hkmc — HKMC SRV 패턴 캘리브레이션 모듈.
 *
 * Phase 0: 공유 entry + 더미 Run 골격만.
 * Phase 3 포팅 타겟 = cal_HKMC_v11 (SRV/필리핀, 인식률 최종판). 연결할 고유 요소:
 *   - 초기 특징점 검출기: 패턴 예측점 (load_four_markers)
 *   - 코너 검증 콜백: check_corner_type (물리좌표 사분면 밝기)
 *   - flood 옵션 프리셋: subpix 3x3, FAST9 th=20, px_dist 15, 2단계 rough/strict
 *   - VN 변종의 GENERATIVE_FITTING 은 옵션 보강책으로 별도 보존
 *---------------------------------------------------------------------------*/
static const CAL_MODULE_DESC HKMC_DESC = {
    "cal-hkmc",
    "HKMC SRV checker calibration module (flood kernel)",
    "HKMC_SRV_B",
};

extern "C" CAL_MODULE_EXPORT var_t LBX_API cal_hkmc_entry(LBX_MODULE_INTERFACE* intf, LbxModuleRequest request, const var_t* arguments)
{
    return cal_module_entry_common(intf, request, arguments, &HKMC_DESC);
}
