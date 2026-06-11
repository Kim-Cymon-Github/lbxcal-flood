# cal-flood Linux Makefile
#
# 플러그인 .so 2종 + 테스트 호스트를 한 Makefile 에서 빌드.
#
# 산출물 레이아웃:
#   $(OUT_PATH)/cal-ox4.so       — 배포: Ox4 캘 모듈 (dlopen 플러그인, "lib" prefix 없음)
#   $(OUT_PATH)/cal-hkmc.so      — 배포: HKMC SRV 캘 모듈
#   $(BUILD_OUT)/cal-host        — 테스트: 최소 호스트 (파일입력 -> LBX_CAMERA -> Run)
#   $(BUILD_OUT)/*.{o,d}         — 휘발성 중간 산출물
#
# 핵심:
#   - 휘발성 산출물(중간 파일·테스트 바이너리·tar.gz) 은 모두 $(BUILD_OUT) 한 곳.
#     -> make clean = rm -rf build/linux/$(PLATFORM_ID).
#   - 배포 .so 만 $(OUT_PATH) 로 떨어뜨림 (외부 참조 안 바뀜).
#   - PLATFORM_ID 별 격리. 같은 트리에서 여러 타겟 교차 컴파일해도 충돌 없음.
#   - 헤더 의존성 추적 (-MMD -MP) — .h 만 바꿔도 재컴파일 됨.
#   - 공유 커널(cal_module 등) 오브젝트를 두 .so 가 함께 링크 (단일 솔루션 소스 공유).
#
# 사용:
#   make                # ox4 + hkmc + cal-host
#   make ox4 / hkmc     # 모듈 하나만
#   make test           # cal-host 만
#   make run            # cal-host 실행 (rpath + LD_LIBRARY_PATH 안전망)
#   make clean

PLATFORM_ID ?= x64
STRIP ?= strip

#=============================================================================
# 소스/오브젝트 목록 (basename. BUILD_OUT prefix 는 아래에서 일괄 적용)
#
# dllmain.cpp 는 Windows 전용 (DllMain + #pragma comment) — Linux 빌드 제외.
# COMMON_NAMES 가 공유 커널: Phase 1 에서 cal_kernel / cal_pattern 등이 추가된다.
#=============================================================================
COMMON_NAMES = \
    cal_module \
    cal_pattern

OX4_NAMES  = cal_ox4
HKMC_NAMES = cal_hkmc

TEST_NAMES = cal_host_main

#=============================================================================
# 경로
#=============================================================================
BUILD_OUT = $(PLATFORM_ID)

OX4_TARGET  = cal-ox4.so
HKMC_TARGET = cal-hkmc.so
OUT_PATH    = ../../lib/lbx/lib/linux/$(PLATFORM_ID)

TEST_TARGET = cal-host

COMMON_OBJS = $(addprefix $(BUILD_OUT)/,$(addsuffix .o,$(COMMON_NAMES)))
OX4_OBJS    = $(addprefix $(BUILD_OUT)/,$(addsuffix .o,$(OX4_NAMES)))
HKMC_OBJS   = $(addprefix $(BUILD_OUT)/,$(addsuffix .o,$(HKMC_NAMES)))
TEST_OBJS   = $(addprefix $(BUILD_OUT)/,$(addsuffix .o,$(TEST_NAMES)))

# 패턴 룰이 소스를 찾을 디렉토리 목록. 위에 적힌 순서대로 검색.
# basename 충돌 없을 때만 안전 — 현재 *_NAMES 는 전부 unique.
vpath %.cpp ../../src/cal
vpath %.cpp ../../test/src

INC= \
    -I../../src/ \
    -I../../lib/lbx/include/ \

DEFS= \
    -DLINUX \
    -DENABLE_LOG \
    -D_LARGEFILE64_SOURCE \
    -D_FILE_OFFSET_BITS=64 \

LDFLAGS = -L$(OUT_PATH) -lrt -lm -lpthread -llbx-core -llbx-intf -llbx-cal

# 헤더 의존성 자동 추적용 플래그.
# -MMD: 시스템 헤더 제외, 프로젝트 헤더만 .d 로 기록 (빌드 그래프 가벼움)
# -MP : 헤더 삭제 시 "no rule to make target" 에러 방지용 phony 룰 자동 추가
DEPFLAGS := -MMD -MP

CFLAGS+=$(INC) $(DEFS) -Wall -Wstrict-aliasing=0 $(DEPFLAGS)

# 테스트 실행파일은 build/linux/$P/, 배포 .so 는 lib/lbx/lib/linux/$P/.
# 둘 사이 상대 깊이는 3단계 (linux->build->repo-root->lib/lbx/lib/linux/$P).
# rpath 박아넣어서 LD_LIBRARY_PATH 없이도 ./cal-host 실행 가능.
# cal-ox4.so / cal-hkmc.so 자체는 dlopen 으로 적재되므로 같은 OUT_PATH 안에서 찾힘.
TEST_RPATH := '$$ORIGIN/../../../lib/lbx/lib/linux/$(PLATFORM_ID)'

# x64 한정 옵션
# - ASan(+LSan): lbx-core 가 x64 에서 -fsanitize=address 로 빌드되므로 그걸
#   link/dlopen 하는 우리 .so 와 테스트도 ASan 일관 빌드 필수. 아니면 ASan
#   런타임이 메인 실행파일 최상위에 안 와서 "runtime does not come first" 거부.
# - strip: ASan 백트레이스에 심볼이 필요하므로 no-op(true)으로 대체.
ifeq ($(PLATFORM_ID),x64)
    SANITIZER_FLAGS := -fsanitize=address -fno-omit-frame-pointer -g
    CFLAGS  += $(SANITIZER_FLAGS)
    LDFLAGS += $(SANITIZER_FLAGS)
    LDFLAGS += -Wl,-rpath,$(TEST_RPATH)
    STRIP := true
endif


all: $(OX4_TARGET) $(HKMC_TARGET) $(TEST_TARGET)

ox4: $(OX4_TARGET)
$(OX4_TARGET): $(COMMON_OBJS) $(OX4_OBJS) | $(OUT_PATH)
	$(CXX) -shared -Wl,-soname,$(OX4_TARGET) -o $(BUILD_OUT)/$(OX4_TARGET) $(COMMON_OBJS) $(OX4_OBJS) $(LDFLAGS)
	$(STRIP) $(BUILD_OUT)/$(OX4_TARGET)
	cd $(BUILD_OUT) && tar -czvf $(OX4_TARGET).tar.gz $(OX4_TARGET)
	mv $(BUILD_OUT)/$(OX4_TARGET) $(OUT_PATH)

hkmc: $(HKMC_TARGET)
$(HKMC_TARGET): $(COMMON_OBJS) $(HKMC_OBJS) | $(OUT_PATH)
	$(CXX) -shared -Wl,-soname,$(HKMC_TARGET) -o $(BUILD_OUT)/$(HKMC_TARGET) $(COMMON_OBJS) $(HKMC_OBJS) $(LDFLAGS)
	$(STRIP) $(BUILD_OUT)/$(HKMC_TARGET)
	cd $(BUILD_OUT) && tar -czvf $(HKMC_TARGET).tar.gz $(HKMC_TARGET)
	mv $(BUILD_OUT)/$(HKMC_TARGET) $(OUT_PATH)

test: $(TEST_TARGET)
$(TEST_TARGET): $(TEST_OBJS) | $(BUILD_OUT)
	$(CXX) $(LDFLAGS) -o $(BUILD_OUT)/$(TEST_TARGET) $(TEST_OBJS) -ldl
	$(STRIP) $(BUILD_OUT)/$(TEST_TARGET)
	cd $(BUILD_OUT) && tar -czvf $(TEST_TARGET).tar.gz $(TEST_TARGET)

run: $(OX4_TARGET) $(HKMC_TARGET) $(TEST_TARGET)
#	rpath 로도 .so 찾도록 박혀있지만, 안전망으로 LD_LIBRARY_PATH 같이 지정
	LD_LIBRARY_PATH=$(OUT_PATH):$$LD_LIBRARY_PATH ./$(BUILD_OUT)/$(TEST_TARGET)

#=============================================================================
# 패턴 룰 — 플러그인 / 테스트 모두 .cpp + -fPIC. vpath 가 소스 위치를 찾아준다.
#=============================================================================
$(BUILD_OUT)/%.o: %.cpp | $(BUILD_OUT)
	$(CXX) $(CFLAGS) -fPIC -c $< -o $@

$(BUILD_OUT) $(OUT_PATH):
	mkdir -p $@

clean:
	rm -rf $(BUILD_OUT)
	rm -f $(OUT_PATH)/$(OX4_TARGET)
	rm -f $(OUT_PATH)/$(HKMC_TARGET)

testc:
	rm -f $(TEST_OBJS)
	rm -f $(BUILD_OUT)/$(TEST_TARGET)

#=============================================================================
# 헤더 의존성 끼워넣기 — 첫 빌드엔 .d 없으니 -include 로 침묵 처리.
#=============================================================================
-include $(COMMON_OBJS:.o=.d)
-include $(OX4_OBJS:.o=.d)
-include $(HKMC_OBJS:.o=.d)
-include $(TEST_OBJS:.o=.d)
