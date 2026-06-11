# cal-flood

차량 SVM/ADAS 캘리브레이션 알고리즘을 **런타임 로딩 DLL 플러그인**으로 제공하는 모듈 묶음.
flood 기반 캘 알고리즘(Ox4, HKMC)이 공용 커널 소스를 한 저장소에서 공유한다
(1 레포 -> N DLL, `lbx-gfx` 패턴).

## 산출물

| DLL | 알고리즘 | 패널 id |
|---|---|---|
| `cal-ox4` | Ox4 — 타원 마커 검출 + 체스보드 flood | `Ox4` |
| `cal-hkmc` | HKMC — 패턴 예측점 + `check_corner_type` flood | `HKMC`, `HKMC_SRV_B` |

각 DLL 은 `lbx-intf` 모듈 규약(`cal_<algo>_entry` 진입점)을 따르며 호스트가 런타임에 로딩한다.
헤드리스 실행(`Run`)과 옵션 UI(`RenderUI`)를 한 DLL 에 통합. 모듈은 카메라 + 패턴 -> 결과
(카메라 파라미터 write-back + var_t 품질 요약)만 책임진다.

## 구조

```
src/                 dllmain.cpp, version.txt
  cal/               공통 커널 + 고유 모듈
                       cal_module.{h,cpp}  — 모듈 entry 플러밍(LBX_CAL_MODULE vtable, impl 인스턴스 상태)
                       cal_kernel.h        — 커널 타입(MARKER_POINT/CAL_PATTERN/CAL_ANCHORS, SVEC 기반)
                       cal_pattern.cpp     — 패턴 로더(load_cal_points/CAL_PATTERN_Load, var_t)
                       cal_ox4.cpp / cal_hkmc.cpp — 알고리즘 고유(DESC + entry)
test/src/            cal_host_main.cpp — cal-host 테스트 호스트
build/vs/            cal-flood.sln -> cal-ox4(DLL), cal-hkmc(DLL), cal-host(EXE)
build/linux/         Makefile (플러그인 .so 2종 + cal-host)
lib/                 lbx(서브모듈), imgui(서브모듈, Cal UI), cminpack(서브모듈, LM 최적화)
```

## 빌드

- Windows: `build/vs/cal-flood.sln`
- Linux: `build/linux/Makefile`
- lit 워크스페이스: `.lit` 에 `[[project]] path = "cal/cal-flood"` 등록 후 `lit build`

## 의존

- `lib/lbx` — `lbx-core`(SVEC/수학), `lbx-cal`(LBX_CAMERA/ExtractImage/좌표변환), `lbx-intf`(모듈 규약 `lbx_intf_cal.h`), var_t
- `lib/imgui` — Cal UI (RenderUI 이식 시 사용)
- `lib/cminpack` — Levenberg-Marquardt(`lmdif`) 최적화 (v1.3.11, float 빌드로 커널에 편입 예정)
- 이미지 커널(FAST9/subpix/is_checker_cross/PCM)은 신 lbx 번들에 없어 구 lbx 에서 `src/cal/` 로 벤더링 예정 (안정화 후 lbx-core 승격 검토)

## 상태

**Phase 0 완료 + Phase 1 진행 중.** 모듈 골격(entry/vtable/mrQuery)과 패턴 로더가 동작하며,
cal-host 로 양 모듈 E2E PASS (Windows x64 + Linux x64/ASan). `Run` 의 검출/최적화 본체는
아직 더미 — flood 엔진/최적화/이미지 커널 이식이 남아 있다.

설계·범위·소스 of truth·HKMC 변종 등 전체 설계는
[Cal 모듈 플러그인 설계 문서](../../lbsvm-core/doc/Cal_Module_Plugin_Design.md) 참조.
