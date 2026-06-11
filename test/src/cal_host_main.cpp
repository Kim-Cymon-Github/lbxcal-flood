/*---------------------------------------------------------------------------
 * cal-host — cal 모듈 최소 테스트 호스트 (Phase 0: 헤드리스 전용).
 *
 * cal DLL 을 cal-flood 레포 안에서 독립 구동/검증한다. SVM 렌더 파이프라인
 * 없음. cal 모듈 계약(lbx_intf_cal.h)의 호스트측만 최소 구현:
 *   1. LBX_HOST_API 준비 (ext_image 없음 — 코어 cal 은 CPU 만으로 동작)
 *   2. LBX_CAMERA[] 어댑터 (Phase 0 은 device_image 없이 4채널 골격만.
 *      파일입력(test_png) 연결은 커널 이식과 함께 Phase 1 에서)
 *   3. 모듈 로딩 -> mrQuery 자가식별 -> Run -> GetResult -> Close
 *   4. 결과 var_t 를 json 으로 출력하고 PASS/FAIL 판정 (CI 회귀용 exit code)
 *
 * 같은 cal DLL 이 무수정으로 cal-host(dev) 와 svmdemo-ui/App(production)
 * 양쪽에 투입된다 — 이 harness 는 호스트 계약의 두 번째 소비자다.
 *
 * 사용:
 *   cal-host [모듈명]          # 기본 cal-ox4. 예: cal-host cal-hkmc
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>

#include "lbx_core.h"
#include "system/lbx_log.h"
#include "intf/lbx_intf_cal.h"
#include "cal/lbx_camera.h"

#if defined(_WIN32)
#   pragma comment (lib, "lbx-core")
#   pragma comment (lib, "lbx-intf")
#   pragma comment (lib, "lbx-cal")
#endif

/* 모듈 -> 호스트 메시지 수신부. Phase 0 은 도착 확인만 한다. */
static i32_t LBX_API host_on_message(void* sender, void* msg)
{
    (void)sender;
    (void)msg;
    return LBX_NO_ERROR;
}

static void print_json(const char* tag, const var_t* v)
{
    UString s = var_to_json(v, 2);
    printf("[%s]\n%s\n", tag, s.c_str());
}

int main(int argc, char** argv)
{
    const char* mod_name = (argc > 1) ? argv[1] : "cal-ox4";
    i32_t pass = 1;

    /* entry 함수명 유도: "cal-ox4" -> "cal_ox4_entry" */
    char entry_name[64];
    {
        size_t n = strlen(mod_name);
        if (n > 40) { n = 40; }
        for (size_t i = 0; i < n; ++i) {
            entry_name[i] = (mod_name[i] == '-') ? '_' : mod_name[i];
        }
        entry_name[n] = '\0';
        strcat(entry_name, "_entry");
    }

    LBX_HOST_API host_api;
    LBX_HOST_API_Init(&host_api, &host_on_message, NULL);

    LBX_CAL_MODULE mod;
    LBX_CAL_MODULE_Init(&mod, &host_api);

    printf("cal-host: opening %s (%s)\n", mod_name, entry_name);
    {
        var_t r = LBX_MODULE_INTERFACE_Open(&mod.intf, mod_name, entry_name, NULL);
        print_json("open", &r);
        var_drop(&r);
        /* 실패 판정은 var 코드가 아니라 슬롯으로 한다 — Open 실패 시 entry 가
         * NULL 이고, mrInitialize 실패 시 vtable(Run)이 비어 있다.
         * (에러 var 에 var_get_i32 를 쓰면 0(LBX_NO_ERROR)으로 오판할 수 있음) */
        if (mod.intf.entry == NULL || mod.Run == NULL) {
            printf("cal-host: FAIL - cannot open module %s\n", mod_name);
            return 1;
        }
    }

    /* 자가식별 (설계 결정 4 — 최소 self-id) */
    {
        var_t q = mod.intf.entry(&mod.intf, mrQuery, NULL);
        print_json("query", &q);
        var_drop(&q);
    }

    /* LBX_CAMERA[] 어댑터 — 4채널 골격. id 로 패턴 part 매핑이 계약이므로
     * id 는 실차 표준명을 사용한다. */
    LBX_CAMERA cams[4];
    static const char* CAM_IDS[4] = { "front", "rear", "left", "right" };
    for (i32_t i = 0; i < 4; ++i) {
        LBX_CAMERA_Init(&cams[i], CAM_IDS[i], i);
    }

    /* 패턴 문서 주입 (설계 결정 5 — 호스트 var_t 주입 우선).
     * front 만 3x2 그리드(6점)를 정의해 패턴 로더 경로를 검증한다. */
    var_t params = var_json_(
        "{\"cal_panel\":{\"Ox4\":{\"front\":[{"
        "\"coord\":{\"anchor\":\"fr\",\"origin\":[0,1000],\"x_axis\":[1,0]},"
        "\"checkers\":[{\"x\":[0,500,1000],\"y\":[0,500],\"w\":1.0}]"
        "}]},"
        "\"HKMC_SRV_B\":{\"front\":[{"
        "\"coord\":{\"anchor\":\"bk\",\"origin\":[0,-500],\"x_axis\":[1,0]},"
        "\"checkers\":[{\"x\":[0,250,500,750],\"y\":[0,250],\"w\":1.0}]"
        "}]}},"
        "\"anchors\":{\"fr\":[0,3000],\"bk\":[0,-1000]}}");

    /* 헤드리스 Run -> 결과 검증 */
    if (mod.Run == NULL) {
        printf("cal-host: FAIL - Run slot is NULL after mrInitialize\n");
        pass = 0;
    } else {
        var_t res = mod.Run(&mod, cams, 4, &params);
        print_json("run", &res);
        {
            VarRef rr(&res);
            bool ok = rr[LBX_CAL_RESULT_OK];
            ssize_t cam_n = rr[LBX_CAL_RESULT_CAMERAS].Count();
            i32_t front_mk = rr[LBX_CAL_RESULT_CAMERAS][0]["markers"];
            if (!ok || cam_n != 4) {
                printf("cal-host: FAIL - ok=%d cameras=%d\n", (int)ok, (int)cam_n);
                pass = 0;
            }
            /* front 패턴: Ox4=3x2 그리드 6점, HKMC=4x2 그리드 8점 */
            if (front_mk != 6 && front_mk != 8) {
                printf("cal-host: FAIL - front markers=%d (expected 6 or 8)\n", (int)front_mk);
                pass = 0;
            }
        }
        var_drop(&res);

        /* GetResult 재조회가 같은 문서를 돌려주는지 */
        if (mod.GetResult != NULL) {
            var_t res2 = mod.GetResult(&mod);
            VarRef rr2(&res2);
            if (!(bool)rr2[LBX_CAL_RESULT_OK]) {
                printf("cal-host: FAIL - GetResult mismatch\n");
                pass = 0;
            }
            var_drop(&res2);
        }
        if (mod.Reset != NULL) {
            mod.Reset(&mod);
        }
    }

    var_drop(&params);
    {
        var_t r = LBX_MODULE_INTERFACE_Close(&mod.intf, NULL);
        var_drop(&r);
    }
    for (i32_t i = 0; i < 4; ++i) {
        LBX_CAMERA_Free(&cams[i]);
    }

    printf("cal-host: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
