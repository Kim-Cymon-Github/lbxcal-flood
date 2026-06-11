## cal-flood v0.1.0 (unreleased)

- 저장소 부트스트랩 — `lbx-gfx` 구조(1 레포 -> N DLL)에서 복제.
- 그래픽 코드 전부 제거(device/texture/mesh/draw/shader, gl/vk 백엔드 소스), `glfw` 서브모듈 제거.
- 프로젝트 골격 리네임: `cal-floodgl`/`cal-floodvk` -> **`cal-ox4`/`cal-hkmc`** (GUID 보존).
- Phase 0 완료: `lbx_intf_cal.h` 계약(lbx 번들 0.2.59+, `LBX_CAL_MODULE_ID='CALM'`) 기반 모듈 골격
  (`src/cal/cal_module.{h,cpp}` 공유 entry + `cal_ox4.cpp`/`cal_hkmc.cpp`) + `cal-host` 테스트 EXE.
  Windows x64 Debug/Release + Linux x64(ASan) 빌드, 더미 Run E2E PASS.
- 캘 커널/알고리즘 본체는 미이식 (Phase 1+) — Run 은 아직 고정 더미 결과.

### Dependencies (서브모듈)
- lib/lbx: main
- lib/imgui: (docking)
- lib/cminpack: v1.3.11 (devernay/cminpack, LM 최적화 `lmdif`)
