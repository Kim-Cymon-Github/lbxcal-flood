#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "lbx_core.h"
#include "system/lbx_log.h"
#include "cal_module.h"
#include "cal_kernel.h"
#include "../version.txt"

/* entry 가 self(&intf)를 LBX_CAL_MODULE* 로 up-cast 하는 first-member 관용구 보증 */
LBX_CT_ASSERT(offsetof(LBX_CAL_MODULE, intf) == 0, cal_module_intf_first);

#define CAL_STR_HELPER(x) #x
#define CAL_STR(x) CAL_STR_HELPER(x)
#define CAL_MODULE_VERSION_STR \
    CAL_STR(VERSION_MAJOR) "." CAL_STR(VERSION_MINOR) "." CAL_STR(VERSION_PATCH)

/* OBJ 멤버 차용 조회 (소유권 이동 없음) */
static var_t* vprop(const var_t* obj, const char* key)
{
    if (obj == NULL || !var_is_obj(*obj)) { return NULL; }
    return var_of_strkey((var_t*)obj, key, false);
}

/*---------------------------------------------------------------------------
 * params 에서 이 모듈의 패턴 문서를 찾는다.
 *   params["panel_id"] (옵션, 기본 desc->pattern_id) -> params["cal_panel"][id]
 * 반환은 차용 포인터 (없으면 NULL).
 *---------------------------------------------------------------------------*/
static var_t* find_panel_doc(const CAL_MODULE_DESC* desc, const var_t* params)
{
    var_t* v_id = vprop(params, "panel_id");
    const char* panel_id = (v_id != NULL) ? var_get_str(v_id) : NULL;
    if (panel_id == NULL) { panel_id = desc->pattern_id; }
    return vprop(vprop(params, "cal_panel"), panel_id);
}

/*---------------------------------------------------------------------------
 * Run — Phase 0/1 진행 중 구현.
 *
 * 아직 검출/최적화 본체는 없지만, params 로 패턴 문서가 주입되면 공유 커널의
 * 패턴 로더(CAL_PATTERN_Load)를 실제로 태워 카메라별 마커 수를 보고한다 ->
 * "호스트 주입 var_t -> 커널 파싱 -> 차량좌표 변환" 경로의 E2E 검증.
 * 카메라 파라미터 write-back 은 아직 없음 (flood/최적화 이식 시).
 *---------------------------------------------------------------------------*/
static var_t LBX_API cal_module_run(LBX_CAL_MODULE* self, LBX_CAMERA* cams, i32_t cam_count, const var_t* params)
{
    CAL_MODULE_IMPL* impl = (CAL_MODULE_IMPL*)self->impl;

    if (impl == NULL) {
        return var_err_(LBX_ERROR_NOT_INITIALIZED, "cal module is not initialized");
    }

    /* 앵커 오프셋: params["anchors"]={"fr":[x,y],"bk":[x,y]} (옵션, 기본 0) */
    CAL_ANCHORS anchors = { { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    {
        var_t* v_anchors = vprop(params, "anchors");
        var_t* v_fr = vprop(v_anchors, "fr");
        var_t* v_bk = vprop(v_anchors, "bk");
        if (v_fr != NULL && var_is_arr(*v_fr) && var_get_count(v_fr) >= 2) {
            var_to_f32p(var_of_index(v_fr, 0, false), &anchors.fr.x);
            var_to_f32p(var_of_index(v_fr, 1, false), &anchors.fr.y);
        }
        if (v_bk != NULL && var_is_arr(*v_bk) && var_get_count(v_bk) >= 2) {
            var_to_f32p(var_of_index(v_bk, 0, false), &anchors.bk.x);
            var_to_f32p(var_of_index(v_bk, 1, false), &anchors.bk.y);
        }
    }

    var_t* panel = find_panel_doc(impl->desc, params);

    Variant res;
    res[LBX_CAL_RESULT_OK]           = true;
    res[LBX_CAL_RESULT_JUDGMENT]     = (i32_t)0;
    res[LBX_CAL_RESULT_FAILED_MASK]  = (i32_t)0;
    res[LBX_CAL_RESULT_COORD_SYSTEM] = "ISO8855";
    res["dummy"] = true;  /* 검출/최적화 본체 이식 전 스캐폴딩 표식 */
    res[LBX_CAL_RESULT_CAMERAS] = VAR_ARR;  /* cam_count==0 이어도 빈 배열은 존재하도록 */
    if (cams != NULL) {
        for (i32_t i = 0; i < cam_count; ++i) {
            i32_t mk_count = 0;
            if (panel != NULL) {
                CAL_PATTERN pat;
                cal_pattern_init(&pat);
                mk_count = CAL_PATTERN_Load(&pat, panel, cams[i].id, &anchors);
                cal_pattern_free(&pat);
            }
            res[LBX_CAL_RESULT_CAMERAS][i]["no"]      = (i32_t)cams[i].no;
            res[LBX_CAL_RESULT_CAMERAS][i]["id"]      = cams[i].id;
            res[LBX_CAL_RESULT_CAMERAS][i]["ok"]      = true;
            res[LBX_CAL_RESULT_CAMERAS][i]["markers"] = mk_count;
            res[LBX_CAL_RESULT_CAMERAS][i]["score"]   = 0.0f;
            res[LBX_CAL_RESULT_CAMERAS][i]["time_ms"] = 0.0f;
        }
    }

    var_drop(&impl->last_result);
    impl->last_result = res.Share();
    return res.Share();
}

static var_t LBX_API cal_module_get_result(LBX_CAL_MODULE* self)
{
    CAL_MODULE_IMPL* impl = (CAL_MODULE_IMPL*)self->impl;
    if (impl == NULL) { return VAR_NULL; }
    return var_share(impl->last_result);
}

static void LBX_API cal_module_reset(LBX_CAL_MODULE* self)
{
    CAL_MODULE_IMPL* impl = (CAL_MODULE_IMPL*)self->impl;
    if (impl != NULL) {
        var_drop(&impl->last_result);
        impl->last_result = VAR_NULL;
    }
}

/*---------------------------------------------------------------------------
 * 공유 entry 본체.
 *
 * 각 알고리즘 TU 의 cal_<algo>_entry 는 자기 DESC 를 물려서 이 함수를 부르는
 * 한 줄 wrapper 다. 모든 가변 상태는 impl 인스턴스에 둔다 — 구버전처럼
 * 파일 스코프 전역(anchor_fr/bk, calopt 류)을 만들지 말 것 (재진입/다중
 * 인스턴스 차단 + DLL 모듈에서 smell).
 *---------------------------------------------------------------------------*/
var_t cal_module_entry_common(LBX_MODULE_INTERFACE* intf, LbxModuleRequest request,
                              const var_t* arguments, const CAL_MODULE_DESC* desc)
{
    LBX_CAL_MODULE* mod = (LBX_CAL_MODULE*)intf;
    (void)arguments;

    if (intf == NULL || desc == NULL) {
        return var_i32_(LBX_ERROR_INVALID_VALUE);
    }
    if (intf->intf_id != LBX_CAL_MODULE_ID) {
        Err_("Invalid interface id " FOURCC_VFMT, FOURCC_VARG(intf->intf_id));
        return var_i32_(LBX_ERROR_INVALID_VALUE);
    }
    if (intf->intf_version != LBX_CAL_MODULE_VERSION) {
        Err_("Version mismatch: required " LBX_VERSION_VFMT ", module " LBX_VERSION_VFMT,
                LBX_VERSION_VARG(intf->intf_version), LBX_VERSION_VARG(LBX_CAL_MODULE_VERSION));
        return var_i32_(LBX_ERROR_INVALID_VERSION);
    }

    switch (request) {
    case mrInitialize: {
        if (!LBX_HOST_API_IsCompatible(intf->host_api)) {
            Err_("host_api major version mismatch (got 0x%x, need 0x%x)",
                 intf->host_api ? intf->host_api->intf_version : 0u,
                 LBX_HOST_API_VERSION);
            return var_i32_(LBX_ERROR_INVALID_VERSION);
        }
        CAL_MODULE_IMPL* impl = (CAL_MODULE_IMPL*)calloc(1, sizeof(CAL_MODULE_IMPL));
        if (impl == NULL) {
            return var_i32_(LBX_ERROR_OUT_OF_MEMORY);
        }
        impl->desc        = desc;
        impl->last_result = VAR_NULL;

        intf->module_name        = desc->name;
        intf->module_description = desc->description;
        intf->module_version     = LBX_MAKE_VERSION(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, BUILD_NUMBER);

        mod->impl      = impl;
        mod->Run       = &cal_module_run;
        mod->GetResult = &cal_module_get_result;
        mod->Reset     = &cal_module_reset;
        mod->RenderUI  = NULL;  /* UI 는 Phase 2 (process_calib_ui 이식) 에서 연결 */
        break;
    }
    case mrTerminate: {
        CAL_MODULE_IMPL* impl = (CAL_MODULE_IMPL*)mod->impl;
        if (impl != NULL) {
            var_drop(&impl->last_result);
            free(impl);
        }
        mod->impl      = NULL;
        mod->Run       = NULL;
        mod->GetResult = NULL;
        mod->Reset     = NULL;
        mod->RenderUI  = NULL;
        break;
    }
    case mrQuery: {
        /* 최소 self-id (설계 결정 4) — 디스커버리/매니페스트 시스템은 안 만든다 */
        Variant q;
        q["name"]    = desc->name;
        q["pattern"] = desc->pattern_id;
        q["version"] = CAL_MODULE_VERSION_STR;
        q["has_ui"]  = false;  /* RenderUI 연결 시 true 로 */
        return q.Share();
    }
    default:
        return var_err_(LBX_ERROR_INVALID_ENUM, "Invalid request");
    }
    return var_i32_(LBX_NO_ERROR);
}
