# Cal 모듈 플러그인 설계 문서 (초안)

> 상태: 설계 합의 초안 (구현은 별도 세션에서 일괄 진행 예정)
> 1차 타겟: **Ox4**, 여유 시 **HKMC**
> 작성 기준일: 2026-06-10

---

## 0. 목적

2100R(`I:\eyeL-2100R`)에서 App 본체에 박혀 있던 캘리브레이션(cal) 로직 / 알고리즘 / UI 를
`lbsvm-core` 기반의 **런타임 로딩 DLL 모듈**로 분리한다.
모듈은 `svmdemo-ui.dll` 과 동일한 방식(lbx-intf 모듈 규약)으로 런타임에 로딩/연결한다.

- **알고리즘 1개 = DLL 모듈 1개** (UI 포함)
- 두 가지 구동 방식을 동일한 모듈/동일한 실행 진입점(`Run`)으로 지원
  1. `svmdemo-ui` 메뉴에서 패턴/알고리즘 선택 -> 모듈 로딩 -> 고유 UI 표시 (인터랙티브)
  2. 호스트가 프로그램적으로 `Run` 호출 -> 헤드리스 실행 -> var_t 결과 회신 (무인)
- **전송 수단(CAN 등)은 전적으로 호스트 책임. cal 모듈은 CAN 을 전혀 다루지 않는다.**
  cal 은 카메라 + 패턴 -> 결과만 책임지고, 명령 수신/결과 회신 배관은 호스트가 알아서 한다.

장기적으로 cal 알고리즘이 계속 추가돼도 App 본체를 건드리지 않고 모듈만 늘리는 구조를 만든다.

---

## 1. 합의된 설계 결정

| # | 항목 | 결정 | 비고 |
|---|------|------|------|
| 1 | flood/최적화 공유 커널 분리 형태 | **단일 솔루션 내 소스 공유** | 별도 static lib 분리는 보류, 같은 솔루션의 공유 소스 세트로 Ox4/HKMC 가 공유 |
| 2 | DLL 구성 | **1 DLL 통합 (헤드리스 실행 + UI)** | svmdemo-ui 의 2-export 방식을 모듈 인터페이스로 통합. UI 는 호스트가 gui_ctx 줄 때만 호출 |
| 3 | 결과 전달 | **LBX_CAMERA write-back + var_t 품질 요약** | 파라미터는 카메라 구조체에 in-place, 품질은 var_t 로 반환. 프로젝트 raw-pointer 원칙 부합 |
| 4 | 모듈 식별 | **버전 정보 기반 최소 self-id** | 거창한 디스커버리/매니페스트는 지금 안 함. 모듈은 자신을 구분할 최소 정보(이름/패턴/버전)만 보유 |
| 5 | 패턴 정의 소유 | **호스트가 var_t 주입(우선) + 모듈 json fallback** | 호스트가 패턴을 안 주면 모듈이 자체 json 을 읽어 동작 (읽기 함수 이미 존재) |
| 6 | 좌표계 canonical | **ISO8855 단일 고정 (X전/Y좌/Z상, 앞차축 원점)** | 내부는 ISO8855 하나로 통일(듀얼모드 금지). 직관/유연성은 "경계 변환 + 프레임 태깅"으로 확보(4.3). |

---

## 2. 현행 자산 인벤토리 (재사용 가능한 것들)

### 2.1 lbsvm-core 측 (신규, 그대로 활용)

- **`LBX_CAMERA`** (`lib/lbx/include/cal/lbx_camera.h:37`)
  카메라 정보 공유 표준 구조체. 이미 존재. 확장 슬롯 보유:
  - `LBX_CAMERA.tag (uintptr_t)` — "캘리브레이션용 추가 데이터 포인터" (주석 명시)
  - `LENS_PARAM.tag (uintptr_t)`, `LENS_PARAM.lens_model (fourcc "lb"/"cv")`
- **`LBX_CAMERA_ExtractImage()`** (`lib/lbx/include/cal/lbx_camera_image.h:74`)
  `cal_prepare_lv0()` 의 신규 대응. `device_image` -> `area` crop + flip 해제 -> **GREY 또는 NV12** 출력. 색 알고리즘까지 대응.
  - 보조: `LBX_CAMERA_GetStdImage()`, `LBX_CAMERA_HoldStdImage()`, `cache` 필드(zero-copy)
- **좌표 변환** (`lbx_camera.h`): `MapPhyToStdCoord`, `MapPhyToTexCoord`, `MapPhyToDevImageCoord`, `ConvertImageCoord`, `Update`, `CalcLensCalibConstants`
- **lbx-intf 모듈 시스템** (`lib/lbx/include/intf/`)
  - `LBX_MODULE_INTERFACE` (`lbx_intf_base.h:127`) — entry / host_api / to_module
  - `LBX_MODULE_INTERFACE_Open/Close` — DLL 로딩 + `mrInitialize/mrTerminate/mrQuery`
  - `LBX_HOST_API` (`lbx_intf_base.h:53`) — `to_host` 콜백 + `ext_image`(GPU)
  - 진입점 시그니처: `var_t entry(LBX_MODULE_INTERFACE* self, LbxModuleRequest request, const var_t* arguments)`
  - 버전 호환: fourcc `intf_id` + major nibble 일치 검사 (`LBX_HOST_API_IsCompatible`)
  - 메시지 시스템 `lbx_msg.h` (`LBX_MSG{sender,id}`, `set:`/`get:`/HMI)
  - (`lbx_intf_drv.h` `APDR` 등 통신 드라이버는 **호스트 영역**. cal 모듈과 무관 — 참고만.)
- **svmdemo-ui DLL 패턴** (`test/src/svmdemo_ui_intf.{h,cpp}`, 로딩은 `svmdemo_main.cpp:178`)
  - `svmdemo_ui_entry(intf, request, args)` + `svmdemo_process_ui(gui_context, target)`
- **var_t** (`lib/lbx/include/lbx_var.h:156`) — NaN-boxing variant. OBJ/ARR/USTR/SVEC. C++ `Var` 래퍼로 `opt["a"]["b"]=v` 가능. yaml/json 파싱(`var_yaml_` 등) 보유.

### 2.2 구버전 측 (이식 대상)

- **2100R** (`I:\eyeL-2100R\lbsvm\lbsvm_cal.cpp`): `cal_prepare_lv0`, `cal_flood`, Ox4(`cal_Ox4_v10`/`find_Ox4_markers`), `cal_camera`/`cal_lmdif`(cminpack LM), `is_checker_cross`, `SUBPIX_FINDER`, `process_calib_ui`, `CAL_RESULT{scores[8],tack_time[8],failed_chs,judgment}`
- **2023-DH-KU-SVM** — **포팅 source of truth**. 단, 경로 주의:
  - `C:\LB\projects\2023-DH-KU-SVM\svm\src\lbsvm_cal.cpp` (**10,945줄, 진짜 최신**) <- 기준
  - `C:\LB\projects\2023-DH-KU-SVM\src_\lbsvm_cal.cpp` (8,493줄, **옛 사본** — 사용 금지)
  - flood 엔진: `cal_flood_expand`(`:5539`) + `CalFloodV1`(`:7226`) + `cal_flood` 래퍼(`:7478`)
  - 패턴 로더 `load_cal_panel`/`load_cal_points`/`CAL_PATTERN_Load` — **알고리즘 독립적**
  - HKMC 변종(아래 3.4), `check_corner_type`(물리좌표 기반 코너 검증)
  - Ox4: `cal_Ox4_v10(cam, cam_num, panel_id)` 3-인자(panel_id 지원, B의 2-인자보다 개선)
  - **패턴 정의 스키마** (4.1 참조) — 신규 패턴 스키마의 기준

> **소스 of truth 요약**: Ox4 / HKMC / 공유커널 모두 `2023-DH-KU-SVM\svm\src` 기준.
> 2100R(B)은 HKMC/flood_expand 미보유(7,399줄) — A의 개선이 역반영 안 됨. A가 전 항목 최신.
> (B에 독립적 양산 패치가 있을 수 있어 최종 diff 1회 권장하나, 알고리즘 본체는 A 기준.)

---

## 3. 공유 커널 vs 알고리즘 고유 경계 (핵심)

### 3.1 분리 원칙

검토 결과 Ox4/HKMC 의 **고유 부분은 사실상 3가지로 수렴**한다.
나머지는 전부 공유 커널로 보낸다.

```
[ 공유 커널 (단일 솔루션 공유 소스) ]   <- Ox4, HKMC 공통
  A. 패턴 로더        load_cal_panel / load_cal_points   (var_t 스키마 파싱)
  B. 좌표계 매핑      coord(anchor fr/bk, origin, x_axis) -> 물리좌표 변환
  C. flood 엔진       cal_flood_expand (옵션화)
                      - Ox4 1세대 동작은 옵션 프리셋의 한 케이스로 흡수
  D. 최적화           cal_camera / cal_lmdif / cost_fn / cminpack(LM)
  E. 점/파라미터 관리  CAL_PARAMS, MARKER_POINT, Dump/LoadCamParams
  F. subpix finder    SUBPIX_FINDER

[ 알고리즘 고유 (각 DLL 모듈) ]
  (1) 초기 특징점 검출기   Ox4=타원(lbx_DetectEllipse+SelectEllipse4)
                          HKMC=패턴 예측점(load_four_markers)
  (2) 코너 검증 콜백        Ox4=is_checker_cross / HKMC=check_corner_type
                          -> CAL_FLOOD_OPT 의 함수 포인터로 주입
  (3) flood 옵션 프리셋     Ox4={subpix7x7, FAST9 th60, 12점에서 렌즈캘 on}
                          HKMC={subpix3x3, FAST9 th40, 클러스터 on, 렌즈캘 off}
  + 패턴 id, UI
```

> 모듈 = "(1)(2)(3) + 패턴id + UI" 만 들고 공유 커널을 호출하는 얇은 껍데기.

### 3.2 flood 1세대/2세대 정리 (중요)

"flood 안 쓰는 신규 알고리즘"(다른 개발자 작) 은 **이번 1차 범위 밖**이다.
1차 타겟 Ox4/HKMC 는 둘 다 flood 계열이며, 같은 엔진의 세대 차이다.

| 항목 | cal_flood (Ox4/2100R) | cal_flood_expand (HKMC/2023) |
|------|-----------------------|------------------------------|
| 코너 검증 | `is_checker_cross()` 이미지만 | `check_corner_type()` 물리좌표 사분면 밝기 |
| 검출 순서 | 거리순 순차 추가 | 클러스터 우선 -> 다중 후보 최적 선택 |
| 예측점 | 없음 | `use_prediction_as_a_corner` 강제 추가 |
| subpix/threshold | 7x7 / FAST9=60 (하드코드) | 3x3 / FAST9=40 (`CAL_FLOOD_OPT` 로 옵션화) |
| 렌즈캘 | 12점에서 활성 | 기본 비활성 (POS/ANGLE 만) |

**결론: 공유 커널의 flood 엔진은 2세대 `cal_flood_expand` 를 기준으로 잡고,
Ox4 의 1세대 동작은 옵션 프리셋의 특수 케이스로 표현한다.**

### 3.3 구버전 -> 신규 매핑

| 구버전 (2100R/2023) | 신규 (lbsvm-core) | 메모 |
|---------------------|-------------------|------|
| `cal_prepare_lv0()` (Gray) | `LBX_CAMERA_ExtractImage()` (GREY/NV12) | 색 대응 |
| `cam->area` crop+flip | 동일 (`area` flip 자동 해제) | 그대로 |
| `cal->lv0` 버퍼 소유 | `LBX_CAMERA.cache` | zero-copy 개선 |
| `TLBCamera` | `LBX_CAMERA` | tag 슬롯에 cal 작업 상태 부착 |
| `CAL_RESULT{scores[8],...}` | var_t 품질 요약 (5.3) | 직렬화 |
| `cal_panel` JS 리터럴(js_eval) | var_t 문서 (yaml/json) | 1:1 매핑 |

### 3.4 HKMC 변종 계보 (포팅 타겟 확정)

메뉴(`svm\src\lbsvm_mastercal.cpp:39`)에 `라벨##패널id` 형태로 변종이 박혀 있다:
`"HKMC VN##HKMC"`, `"HKMC SRV##HKMC_SRV_B"`, `"PM HKMC SRV##HKMC_SRV_B"`.

| 변종 | 함수 | 패널 id | 핵심 차이 |
|------|------|---------|-----------|
| HKMC VN (최초) | `CalHKMC_v10` (`svm\src\lbsvm_cal.cpp:10402`) | `HKMC` | GENERATIVE_FITTING(가상점 6x15=90, weight 0.05)로 fitting 보강 |
| **HKMC SRV / 필리핀 (최종·인식 최고)** | **`cal_HKMC_v11` (`:10349`)** | `HKMC_SRV_B` | fast.th=20(반사광 강화), px_dist 15, **2단계 정밀화**(rough segs4 -> strict px5/segs10), 8점+ 시 CX/K1/K2 렌즈캘 |
| HKMC2 | `cal_HKMC2_v11` (`:10579`) | — | 패턴매칭 기반(별개 계열) |

- `.bak` 에 `HKMC PHP##HKMC_PHP_A` 흔적 -> 현재 `HKMC_SRV_B` 로 개명. 필리핀 버전이 서비스캘로 승격된 것이 마지막.
- **HKMC 포팅 1순위 = `cal_HKMC_v11`(SRV/필리핀)**. VN 의 GENERATIVE_FITTING 은 옵션 보강책으로 별도 보존.
- 인식률 우위(필리핀>베트남) 요인: 낮은 FAST9 threshold + 좁은 초기 탐색 + 2단계 rough/strict.

---

## 4. 패턴 정의 스키마

2023-DH-KU-SVM 의 `cal_panel` 스키마를 var_t(OBJ/ARR) 문서로 그대로 옮긴다.
패턴 로더(`load_cal_panel`/`load_cal_points`)는 이미 알고리즘 독립적이므로 공유 커널로 이동한다.

### 4.1 스키마 (var_t/json)

```jsonc
"cal_panel": {
  "<PANEL_ID>": {                 // "Ox4", "HKMC", "CBoard" ...
    "base": [width, height, offset],   // 물리 치수(mm). offset=후방 음영
    "search": {                        // (옵션) 카메라별 영상 검색 영역(정규화 0~1)
      "rear":  [L,T,R,B], "front": [...], "left": [...], "right": [...]
    },
    "parts": {                         // 카메라별 정의
      "<CAM_ID>": [                    // "rear"/"front"/"left"/"right"
        {
          "coord": {                   // 지역 좌표계
            "anchor": "fr" | "bk",     // 전방/후방 기준점
            "origin": [x, y],          // 앵커로부터 오프셋(mm)
            "x_axis": [dx, dy]         // X축 방향(자동 정규화), Y축은 90도 회전 자동
          },
          "marks": {                   // (옵션) QR 등 마커 영역
            "size": [w,h], "origin": [x,y], "clip": [x,y], "excl": [L,T,R,B]
          },
          "checkers": [                // 체커/점 정의 (5가지 포맷)
            { "x": [...], "y": [...], "ex_x": [..], "ex_y": [..], "w": 1.0, "shape": 1 },
            { "pt2s": [x0,y0,x1,y1,...], "w": [..], "shape": 2 },
            { "pt3s": [x0,y0,z0,...],   "w": 1.0 }
          ]
        }
      ]
    }
  }
}
```

점 정의 5종: `origin+size`/`xywh`, `rect`, `pt2s`(2D), `pt3s`(3D), `x`+`y`+`ex_*`(그리드).
그리드는 인접점 **라인 연결 자동 생성**(`CAL_PATTERN.lines`).

### 4.2 자료구조 (공유 커널)

```c
typedef u8_t MARKER_SHAPE;            // UNKNOWN=0, CROSS=1, CORNER=2, POINT=3

typedef struct {
    vec3_f32     phy;     // 물리좌표(차량기준 mm)
    vec2_f32     img;     // 이미지좌표(px)
    MARKER_SHAPE shape;   // 검출기 선택 힌트
    u8_t         _reserved;
    u16_t        weight;  // x100 fixed point
} MARKER_POINT;

typedef struct {
    MARKER_POINT* mk;     // 점 배열 (RCM 동적)
    i16_t*        lines;  // 점 연결관계 (2개씩 쌍)
} CAL_PATTERN;
```

### 4.3 물리좌표 규약 (ISO8855 — 신규 lbsvm-core 기준, 구버전과 다름!)

신규 lbsvm-core 는 **이미 ISO8855 채택**(`src/lbsvm_vehicle.h:6-20`, `COORD_ISO8855=0` 이 SDK 기본):
- **+X = 전방, +Y = 좌, +Z = 상** (right-hand).
- **원점 = 앞바퀴 차축 중심의 바닥면.** ISO8855 표준은 CG지만, OE 보정장이 앞바퀴를 고정장치로 센터링하므로 앞차축 기준 채택(주석 명시).
- **효과: 앞차축 원점이라 차종이 바뀌어도 패턴 상대좌표 불변.** (구버전 뒷범퍼 원점은 차 길이/휠베이스가 바뀌면 매번 패턴 재정의가 필요했음 — 이 문제를 해소.)

구버전(2100R/KU) 패턴은 **구 좌표계로 작성됨** (`COORD_ENU`: +X 우, +Y 전, +Z 상, **뒷범퍼 중앙 원점** = EYEL1 버전). 따라서 패턴 점은 ENU(rear) -> ISO8855(front-axle) 변환 필요:
- 축/부호: (X_old=우, Y_old=전) -> (X_new=전=Y_old, Y_new=좌=-X_old)
- 원점 이동: 뒷범퍼 -> 앞차축 = (리어오버행 + 휠베이스) 만큼 전방 이동. 차량 dims(`VEH_UNIT.length/wheelbase/rear_overhang`) 필요.

`coord.anchor`(`fr`/`bk`) + `origin` + `x_axis` 가 이 변환을 표현하는 수단. 공유 커널 좌표 매핑은 `LBX_COORD_SYSTEM`(`lbsvm_vehicle.h:264`: ISO8855/ENU/NED)을 인지해 처리한다.

> **정확한 패턴 좌표는 CAD 또는 사용자 제공 자료로 확정 (별도 작업, 지금 급하지 않음).**
> 임시로는 KU 설정 데이터의 차 길이/리어오버행/휠베이스로 역산 가능.

#### 좌표 정책 (결정 6 — 확정)

- **내부 canonical = ISO8855 단일.** 알고리즘 코드/내부 자료구조는 프레임을 절대 분기하지 않는다. **내부 듀얼모드 금지.**
- **직관/유연성은 경계에서만 해결**한다:
  - 패턴 정의는 **출처 프레임을 선언**(예: `"coord_system": "ENU"`, 기본 ISO8855)하고, 로더가 **로드 시 1회 ISO8855 로 변환**. -> author 는 편한 프레임으로 작성 가능.
  - UI/표시/외부 출력은 **표시 단계에서 보는 쪽 프레임으로 변환**(디버그용 토글 가능).
- **저장물(cal 결과 / 카메라 파라미터)에 프레임 태그 1필드를 박는다.** -> 미래에 canonical 을 바꿔도 옛 데이터를 태그 보고 변환 가능. 락인 방지의 핵심.
- 변환 도구는 기존 `LBX_COORD_SYSTEM`(`lbsvm_vehicle.h:264`)을 **경계 변환 유틸**로 사용(전역 모드 아님).
- canonical 로 ISO8855 를 고른 이유: OEM/차량동역학/센서퓨전이 전부 ISO8855 -> 네이티브 interop. 축 직관 불편은 경계 변환으로 흡수.

### 4.4 패턴 소유/공급 (결정 5)

- **우선**: 호스트가 `mrInitialize` 또는 Run 인자의 var_t 로 패턴 문서 주입.
- **fallback**: 호스트가 안 주면 모듈이 자체 json 파일을 읽어 동작
  (var_t yaml/json 읽기 함수 재사용).
- 효과: 차종/라인 변경 시 재컴파일 없이 패턴 문서 교체만으로 대응.

---

## 5. 모듈 인터페이스 규약

### 5.1 식별 (결정 4 — 최소 self-id)

```c
// 확정 (lbx_intf_cal.h, lbx 번들 0.2.59+) — 레포 관례(LBX_GUI_MODULE_ID 등)에 맞춘 이름
#define LBX_CAL_MODULE_ID      fourcc_('C','A','L','M')
#define LBX_CAL_MODULE_VERSION LBX_MAKE_VERSION(0, 1, 0, 0)
```

- 모듈은 자신을 구분할 최소 정보만 보유: 모듈명, 지원 패턴 id, 버전.
- `mrQuery` 응답(var_t)에 `{ name, pattern, version, has_ui }` 정도로 노출.
- 별도 디스커버리/매니페스트 시스템은 지금 만들지 않는다.

### 5.2 생명주기 / vtable (1 DLL 통합 — 결정 2)

lbx-intf 규약을 그대로 따른다. 진입점에서 `mrInitialize` 시 vtable 을 채운다.

```c
typedef struct LBX_CAL_MODULE {
    LBX_MODULE_INTERFACE intf;     // 표준 모듈 헤더 (entry/host_api/to_module). 반드시 첫 멤버.
    void* impl;                    // 모듈 인스턴스 상태 (mrInitialize 시 alloc, mrTerminate 시 free)

    // --- 헤드리스 실행 (항상 제공) ---
    // cams: 호스트 소유 LBX_CAMERA[] (device_image 보유). 모듈이 cam->id 로 패턴 part(rear/front/left/right) 매핑.
    // params: var_t OBJ (패턴 문서, base_length, 옵션 등). NULL 이면 모듈 fallback.
    // 반환: var_t 품질 요약(5.3). 카메라 파라미터(pos/lens)는 cams 에 in-place write-back.
    // 동기 호출(블로킹). LM 최적화로 수백 ms~초 소요 가능 -> UI 스레드에서 호출 시 그 프레임 멈춤(v1 허용).
    var_t (LBX_API* Run)(LBX_CAL_MODULE* self, LBX_CAMERA* cams, i32_t cam_count, const var_t* params);
    var_t (LBX_API* GetResult)(LBX_CAL_MODULE* self);   // 마지막 결과 재조회
    void  (LBX_API* Reset)(LBX_CAL_MODULE* self);

    // --- UI (옵션: 호스트가 gui_ctx 제공할 때만) ---
    // gui_context: ImGuiContext*. target: 호스트가 넘기는 cams+상태 번들(검출점 오버레이/품질 표시용).
    // 카메라 프레임/오버레이를 텍스처로 그리려면 host_api->ext_image 사용(없으면 UI 비주얼 일부 제한).
    i32_t (LBX_API* RenderUI)(LBX_CAL_MODULE* self, void* gui_context, void* target);
} LBX_CAL_MODULE;

// 진입점 (DLL export). 호스트가 LBX_CAL_MODULE 를 할당하고 &mod.intf 로 Open 호출 ->
// entry 가 self(=&intf, 첫 멤버)를 LBX_CAL_MODULE* 로 up-cast 해 vtable(Run/RenderUI/...) 채움.
// LBX_CAL_MODULE 타입은 호스트/모듈 공유 헤더에 정의(예: lbx_intf_cal.h).
LBX_CAL_EXPORT var_t LBX_API cal_<algo>_entry(
    LBX_MODULE_INTERFACE* self, LbxModuleRequest request, const var_t* arguments);
```

- 헤드리스(호스트 프로그램 호출)와 인터랙티브(UI) 가 **동일한 `Run`** 을 공유.
- UI 는 `RenderUI` 로 분리되어 gui_context 없으면 호출 안 됨 -> 헤드리스 배포 시 ImGui/GPU 경로 미진입.
- **핵심 코어 cal 은 CPU(device_image)만으로 동작 -> 헤드리스는 GPU/ext_image 불필요(CI 친화).** ext_image 는 UI 비주얼 전용.
- **포팅 주의(중요): 구버전은 `anchor_fr`/`anchor_bk`(`:3686-3687`), `calopt`, `LB_CAL_V0` 등을 파일 스코프 전역으로 둠. DLL 이식 시 이들을 `impl` 인스턴스 상태로 옮긴다.** 전역 유지 시 단일 로드는 되지만 재진입/다중 인스턴스를 막고 smell.

### 5.3 결과 var_t 스키마 (결정 3 — 품질 요약)

`CAL_RESULT` 를 var_t 로 직렬화. 파라미터 본체는 `LBX_CAMERA` write-back.

```jsonc
{
  "ok": true,                  // 전체 성공 여부
  "judgment": 0,               // 종합 판정 코드
  "failed_mask": 0,            // 실패 카메라 비트마스크
  "cameras": [
    { "no": 0, "id": "rear",  "ok": true,  "score": 0.31, "time_ms": 142.0 },
    { "no": 1, "id": "front", "ok": false, "score": 3.20, "time_ms": 167.0 }
  ]
}
```

- 성공 기준: `0 < score < OPT_SUCCESS_SCORE(2.0)` (기존 규약 유지).
- 진행도/중간상태가 필요하면 `to_host` 메시지(`cal:` 류 id)로 별도 통지 — 1차에서는 보류 가능.

---

## 6. 두 트리거 경로

### 6.1 인터랙티브 (svmdemo-ui)

```
svmdemo-ui 메뉴(지원 패턴 목록에서 선택)
  -> LBX_MODULE_INTERFACE_Open("cal-<algo>", "cal_<algo>_entry", params)
  -> mrInitialize (vtable 채움)
  -> 매 프레임 RenderUI(gui_ctx, target)  // 고유 UI
  -> 사용자가 실행 -> Run(cams, n, params) -> 결과 표시
```

### 6.2 무인 (호스트 프로그램 호출 — 전송수단 무관)

```
호스트가 cal 명령 트리거 (전송수단=CAN 등은 호스트 책임, cal 무관)
  -> 패턴 id 로 모듈 선택, LBX_MODULE_INTERFACE_Open(...)
  -> Run(cams, n, params)   // gui_ctx 없음 -> UI 미진입
  -> var_t 결과 (성공/실패 + 카메라별 품질) 반환
  -> LBX_MODULE_INTERFACE_Close()
  -> (호스트가 결과를 알아서 회신/저장)
```

---

## 7. 저장소/솔루션/빌드 구성 (결정 1 — 단일 레포·솔루션 소스 공유)

### 7.0 워크스페이스 위치 (lit 멀티레포)

`L:/` 는 `lit` 워크스페이스 루트이고 **각 모듈이 독립 git 레포**다(`.lit` 의 `[[project]]` 로 빌드 멤버십 정의).
cal 모듈은 `drv/`(plat-*/avio-* 로더블 DLL)와 **동종**이므로, **`cal/` 그룹 폴더를 `drv/` 형제로** 둔다.

- **레포 위치**: `L:/cal/cal-flood` (그룹 `cal/`, 레포명 `cal-flood`, 하이픈 관례)
  - 이름 의미: **flood 커널을 공유하는 cal 알고리즘 묶음**. 미래 비-flood 알고리즘은 `cal/` 의 별도 레포로.
- **`.lit` 등록**: `[[project]] path = "cal/cal-flood"` 추가.
- lbx 라이브러리는 다른 모듈과 동일하게 `lib/lbx` 번들로 소비(release.bundle 규약).

### 7.1 단일 레포 · 다중 DLL (lbx-gfx 패턴 미러)

`lbx-gfx`(레포 1개 -> `lbx-gfxgl`/`lbx-gfxvk` 2 DLL)를 **그대로 따른다**. 레포당 DLL 1개에 집착하지 않고,
**얼마나 가까운지(cohesion)** 로 묶는다 — Ox4/HKMC 는 flood 커널을 통째 공유하므로 한 레포가 맞다.

```
cal/cal-flood/
  src/cal/
    cal_kernel.*           // 공통: flood 엔진(cal_flood_expand+CalFloodV1), 최적화(cal_camera/lmdif/cost), subpix
    cal_pattern.*          // 공통: 패턴 로더(load_cal_panel/points), 좌표 매핑(coord/anchor), 좌표계 변환
    (공개 인터페이스 LBX_CAL_MODULE 는 lbx-intf 에 정의 — 7.3. cal-flood 는 include 만)
    cal_ox4.*              // 고유: 타원 검출 + is_checker_cross + flood 옵션 프리셋 + cal_ox4_entry
    cal_hkmc.*             // 고유: 패턴 예측점 + check_corner_type + 프리셋 + cal_hkmc_entry
    dllmain.*  version.txt  // (lbx-gfx 처럼)
  build/vs/cal-flood.sln
    cal-ox4         (DLL)   // 공통 src + cal_ox4.* 컴파일/링크
    cal-hkmc        (DLL)   // 공통 src + cal_hkmc.* 컴파일/링크
    cal-host        (EXE)   // 7.2 테스트 호스트 (두 DLL 공용)
    cal-ox4-test / cal-hkmc-test (옵션, lbx-gfx 의 *-test 대응)
  build/linux/Makefile
  lib/ test/ doc/ README.md RELEASE-NOTES.md
```

- 공통 커널은 **공유 소스 세트**로 각 DLL 프로젝트가 함께 컴파일(별도 static lib 분리는 보류; lbx-gfx 도 src 공유 방식).
  - 백엔드 분기 파일 접미사 관례(`_gles`/`_vk`)에 대응해 알고리즘 고유 파일은 `_ox4`/`_hkmc` 접미사.
- 각 DLL 링크: `lbx-cal`(ExtractImage/좌표변환), `lbx-intf`, var_t, cminpack, lbx 이미지 커널(FAST9/ellipse/checker).
- 인코딩: 모든 c/cpp/h 는 **UTF-8 BOM**. 타입: raw C 타입 금지, lbx 고정폭 타입(`i32_t` 등).

### 7.3 모듈 인터페이스 헤더 위치 = `lbx-intf`

`LBX_CAL_MODULE` vtable / `LBX_CAL_INTF_ID` / 결과 var_t 스키마 상수는 **`lbx-intf` 레포**에
`lbx_intf_cal.h` 로 둔다 — 기존 `lbx_intf_gui.h`/`lbx_intf_avio.h`/`lbx_intf_plat.h`/`lbx_intf_drv.h`
(로더블 모듈 타입별 인터페이스 정의)와 같은 자리. **lbx-intf = 계약 정의, cal-flood = 구현**.
- 호스트(svmdemo-ui / cal-host / App)와 모듈(cal-flood) 모두 `lib/lbx` 번들의 이 헤더를 include.
- 즉 1차 작업은 `lbx-intf` 레포에 헤더 1개 추가를 포함한다(작은 추가, 기존 패턴 그대로).

### 7.4 테스트 호스트 `cal-host` (전용 최소 EXE — 결정)

cal DLL 을 **cal-flood 레포 안에서 독립 구동/검증**하는 최소 호스트 EXE(`cal-flood.sln` 의 한 프로젝트). SVM 렌더 파이프라인 없음.
cal 모듈 계약의 **호스트측만 최소 구현**한다 (svmdemo 의 무거운 데모 흐름과 분리).

책임:
1. **프레임 공급** — **파일입력 우선**(정지영상, `test/src/test_png.h` 재사용 -> **재현 가능 회귀**), AVIO 라이브는 옵션.
2. **`LBX_CAMERA[]` 어댑터** — 로드한 `LBX_IMAGE` 를 `cam->device_image` 에 연결하고 `id`/`area`/`lens` 기본값 세팅. (현재 svmdemo 는 `vbuf[]` 만 다루고 LBX_CAMERA 를 안 만듦 -> 이 어댑터가 빠진 유일한 조각.)
3. **패턴 var_t 주입** (또는 모듈 fallback 경로 검증).
4. **모듈 로딩** — `LBX_MODULE_INTERFACE_Open("cal-<algo>", "cal_<algo>_entry", ...)`.
5. **두 모드**:
   - **헤드리스 콘솔**: `Run` -> var_t 결과 출력/assert. CI 회귀용 (GPU 불필요).
   - **GUI**: `lbx-gui` ImGui context + 매 프레임 `RenderUI` + `Run` 트리거 + 결과/검출점 표시. 비주얼 확인용.
   - 단일 바이너리 + `--headless` 플래그로 통합 가능.

재사용/원칙:
- `svmdemo_main.cpp` 의 AVIO/gui/`open_module` bringup 과 `test_png` 를 참고/복사. **공유 lib 로 추상화하지 않는다**(과한 추상화 회피, CLAUDE.md ethos). dev harness 이므로 최소 bringup 복사 허용.
- **같은 cal DLL 이 무수정으로** cal-host(dev) 와 나중의 svmdemo-ui/App(production) 양쪽에 투입된다 — harness 는 중복이 아니라 호스트 계약의 두 번째 소비자.

---

## 8. 구현 범위 / 단계 (별도 세션)

### Phase 0 — 레포 스캐폴딩 + 인터페이스 헤더 + 테스트 호스트 (선행) — **완료 (2026-06-11)**
- [x] `cal/cal-flood` 레포 생성(castproj 로 lbx-gfx 복제) — 그래픽 코드/`glfw` 서브모듈 제거, `lib/lbx`+`lib/imgui` 유지
- [x] 프로젝트 리네임: `cal-floodgl`/`cal-floodvk` -> `cal-ox4`/`cal-hkmc` (GUID 보존, sln/vcxproj/rc/filters/Makefile), README/RELEASE-NOTES/CLAUDE.md 정리
- [x] `.lit` 에 `[[project]] path="cal/cal-flood"` 등록
- [x] `lbx-intf` 레포에 `lbx_intf_cal.h` 추가 + `lbx_intf_cal.c`(Init) — lbx 번들 0.2.59 build 664 로 배포 완료.
      ID 는 레포 관례에 맞춰 **`LBX_CAL_MODULE_ID` = fourcc 'CALM'** / `LBX_CAL_MODULE_VERSION` 0.1.0 으로 확정 (§5.1 의 `LBX_CAL_INTF_ID` 예시명 대체).
      결과 스키마에 **`coord_system` 프레임 태그** 키 추가(§4.3 락인 방지 정책 반영).
- [x] `cal-ox4`/`cal-hkmc` vcxproj + Makefile 재구성 완료. `src/cal/cal_module.{h,cpp}`(공유 entry 플러밍) + `cal_ox4.cpp`/`cal_hkmc.cpp`(DESC+entry 한 줄 wrapper).
      Makefile 은 avio-file 섀시(플러그인 .so, lib prefix 없음, OUT_PATH=lbx 번들, ASan x64) 기반 재작성.
- [x] `cal-host` EXE (`test/src/cal_host_main.cpp`): `LBX_CAMERA[4]` 어댑터(front/rear/left/right) -> 모듈 로딩 -> mrQuery -> 헤드리스 `Run` -> PASS/FAIL exit code.
      파일입력(test_png) 연결은 커널이 실제 픽셀을 쓰는 Phase 1 에서.
- [x] 더미 cal 모듈 E2E 검증 — Windows x64 Debug/Release + Linux x64(ASan) 양쪽, ox4/hkmc 둘 다 PASS.
      주의: Linux 에서 플러그인 .so dlopen 은 bare-name 검색이라 exe rpath 가 아닌 **LD_LIBRARY_PATH**(또는 평탄 배포 디렉토리)로 찾는다 — `make run` 이 그 안전망.
- [ ] GUI 모드(lbx-gui + RenderUI) 골격 — Phase 2 의 `process_calib_ui` 이식과 함께 진행하기로 변경

### Phase 1 — 공유 커널 이식 (진행 중 — 2026-06-11 슬라이스 1 완료)
- [x] 타입 정의 1차: `MARKER_POINT`/`CAL_PATTERN`/`TCornerType`/`CAL_ANCHORS` -> `src/cal/cal_kernel.h` (RCM -> **SVEC** 대체, lbx 타입).
      `CAL_PARAMS`/`CAL_FLOOD_OPT`/`POINT_CLUSTER` 는 최적화/flood 슬라이스에서.
- [x] 패턴 로더 이식: `load_cal_points`(원본 :4814) + `CAL_PATTERN_Load`(:5061) -> `src/cal/cal_pattern.cpp`.
      구 var API -> 신 lbx_var(`var_of_strkey` 차용 / `var_of_index` / `var_to_f32p`). 5종 포맷(xywh/rect/pt2s/pt3s/grid+ex) 전부.
      구버전 `marks.clip` 의 앱 사이드이펙트(spec cliprect)는 커널 밖으로 배제.
      **cal-host E2E 검증**: 호스트 var_t 주입 -> 그리드 생성 -> anchor 오프셋 -> 월드변환, Ox4 front 6점/HKMC front 8점 (Win+Linux ASan PASS).
- [x] 좌표계 매핑(coord/anchor) 이식: 전역 `anchor_fr/bk`(:3686) -> `CAL_ANCHORS` 파라미터(인스턴스 상태). `params["anchors"]={"fr":[..],"bk":[..]}` 주입 지원.
- [ ] 최적화 `cal_camera`(:1419)/`cal_lmdif`(:1355 V2)/`cost_fn`(:1183 cal_fcn V2) + Dump/LoadCamParams(:668-743) 이식 — **cminpack 벤더링 필요** (`C:\LB\projects\2023-DH-KU-SVM\lib\cminpack` -> cal-flood `lib/cminpack` 서브셋: lmdif+qrfac 계열, float 빌드). TLBCamera 필드 -> LBX_CAMERA 매핑(pos.coord->pos.x/y/z, lens.crv->lens.k[4], scale.y aspect 대응 확인 필요).
- [ ] flood 엔진 이식: `cal_flood_expand`(:5539) + `CalFloodV1`(:7226) + `cal_flood_find_marker_candidates`(:5328) + `check_corner_type`(:3102, PCM_PATTERN 포함), 검증 콜백 주입 가능하게
- [ ] `SUBPIX_FINDER`/FAST9 등 이미지 커널 — **신규 lbx 번들에 없음(확인됨)**. 구 lbx 에서 벤더링 필요: `2023-DH-KU-SVM\lbx\image\lbx_fast_9.{c,h}` + `lbx_corner.{c,h}`(is_checker_cross/FindCornerSubPix). 우선 cal-flood `src/cal/` 에 두고, 재사용 확정 시 lbx-core 승격.

### Phase 2 — Ox4 모듈
- [ ] 타원 검출기(`lbx_DetectEllipse`/`SelectEllipse4`) 연결
- [ ] Ox4 검증 콜백(`is_checker_cross`) + flood 옵션 프리셋
- [ ] `cal_ox4_entry` + Run/GetResult/Reset + RenderUI(`process_calib_ui` 이식)
- [ ] svmdemo-ui 로딩/메뉴 연동
- [ ] 결과 var_t 직렬화 + 카메라 write-back 검증

### Phase 3 — HKMC 모듈 (여유 시)
- [ ] 포팅 타겟 = `cal_HKMC_v11`(SRV/필리핀, `:10349`), 패널 id `HKMC_SRV_B`
- [ ] 패턴 예측점 검출(`load_four_markers`) 연결
- [ ] HKMC 검증 콜백(`check_corner_type`) + flood 옵션 프리셋(fast.th=20, px_dist 15, 2단계 rough/strict)
- [ ] VN 변종의 GENERATIVE_FITTING(`CalHKMC_v10`, `:10402`)은 옵션 보강책으로 별도 보존
- [ ] `cal_hkmc_entry` + UI

### Phase 4 — 패턴 좌표 ISO8855 정합 (후속, 자료 확보 후)
- [ ] 구 KU 패턴(ENU/rear) -> ISO8855/front-axle 변환 정의
- [ ] CAD/사용자 자료로 정확 좌표 확정, 또는 차량 dims(length/wheelbase/rear_overhang)로 역산
- [ ] (CAN 등 전송 디스패치는 호스트 영역 — cal 범위 밖)

---

## 9. 보류 / 비범위

- **CAN / 통신 배관 — 전적으로 호스트 영역. cal 모듈 범위 밖 (cal 은 CAN 무관).**
- **정확한 패턴 좌표 확정 (ENU/rear -> ISO8855/front-axle 변환 실측치)** — CAD 또는 사용자 자료 대기. 급하지 않음. 임시 역산은 KU dims 활용.
- "flood 미사용" 제3 알고리즘(타 개발자 작) 통합 — 1차 범위 밖.
- 정식 디스커버리/매니페스트 시스템 — 최소 self-id 로 대체(결정 4).
- 진행도/중간상태 스트리밍 표준 메시지 — 필요 시 후속.
- 공유 커널의 정식 static lib 승격 — 안정화 후 검토.
- `LBX_MESH`/gfx 추상화 등 — 본 작업과 무관, CLAUDE.md 의 별도 로드맵.

---

## 10. 미해결/확인 필요 항목

- ~~`LBX_CAL_INTF_ID` fourcc 최종 확정~~ -> **확정: `LBX_CAL_MODULE_ID` = 'CALM' (Phase 0 에서 배포됨)**.
- 호스트 -> 모듈 카메라 전달 시 `device_image` 라이프타임/소유권 규약 명문화.
- cminpack 의 신규 솔루션 내 위치(이미 lib 에 있는지 / 별도 포함 필요한지) 확인.
- HKMC 패턴 정의 파일 실물 위치/내용 — `svm\bin\lbsvm0000_new_chess.txt`(JS 리터럴, `cal_panel`) 확인됨. 단 패널 id `HKMC`(VN)와 `HKMC_SRV_B`(SRV/필리핀) 항목의 실제 체커 배치/base 치수 차이를 별도 세션에서 발췌 확정 필요. (`src_` 사본 말고 `svm\src`/`svm\bin` 기준으로 확인.)
- 색 알고리즘이 필요로 하는 입력 포맷(GREY vs NV12) 모듈별 선언 방식.
