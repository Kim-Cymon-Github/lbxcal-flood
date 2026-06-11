#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "lbx_core.h"
#include "lbx_svec.h"
#include "math/lbx_matrix.h"
#include "system/lbx_log.h"
#include "cal_kernel.h"

/*---------------------------------------------------------------------------
 * 패턴 로더 — 2023-DH-KU-SVM svm/src/lbsvm_cal.cpp 의
 * load_cal_points(:4814) / CAL_PATTERN_Load(:5061) 이식.
 *
 * 원본과의 차이:
 *   - RCM 동적배열 -> lbx SVEC (svec_length / SVEC_APPEND / SVEC_DROP)
 *   - 구 var API (var_find/var_arr_list/var_to_f32) -> 신 lbx_var
 *     (var_of_strkey 차용 포인터 / var_of_index / var_to_f32p)
 *   - 전역 anchor_fr/bk(:3686) -> CAL_ANCHORS 파라미터 (인스턴스 상태)
 *   - 전역 설정 조회 get_cal_panel_info -> 호스트 주입 panel var_t 파라미터
 *   - 구버전 marks.clip 의 앱 사이드이펙트(spec->update_cliprect,
 *     RebuildProjSurfaces)는 커널 밖 (디스플레이 도메인) — 이식하지 않음
 *---------------------------------------------------------------------------*/

void MARKER_POINT_init(MARKER_POINT* o, i32_t n)
{
    memset(o, 0, sizeof(MARKER_POINT) * n);
    for (i32_t i = 0; i < n; ++i) {
        o[i].weight = 1.0f;
        o[i].shape = MARKER_SHAPE_CROSS;
        o[i].plane = 'z';
        o[i].block_size = DEFAULT_CHESS_BLOCK_SIZE;
    }
}

void MARKER_POINT_free(MARKER_POINT* o, i32_t n)
{
    (void)o;
    (void)n;
}

void cal_pattern_init(CAL_PATTERN* t)
{
    memset(t, 0, sizeof(CAL_PATTERN));
}

void cal_pattern_free(CAL_PATTERN* t)
{
    SVEC_DROP(&t->mk);
    SVEC_DROP(&t->lines);
    t->id = NULL;
    t->base_length = 0;
}

/* OBJ 멤버를 차용 포인터로 조회. 없으면 NULL (소유권 이동 없음 — drop 금지) */
static var_t* vfind(const var_t* obj, const char* key)
{
    if (obj == NULL || !var_is_obj(*obj)) { return NULL; }
    return var_of_strkey((var_t*)obj, key, false);
}

static int f32_cmp(const void* a, const void* b)
{
    f32_t aa = *(const f32_t*)a;
    f32_t bb = *(const f32_t*)b;
    return (aa == bb) ? 0 : (aa > bb ? 1 : -1);
}

/* value 가 svec 배열 안에 (부동소수 오차 허용으로) 존재하는가 */
static bool is_in(f32_t value, const f32_t* svec_array)
{
    for (ssize_t i = (ssize_t)svec_length(svec_array) - 1; i >= 0; --i) {
        if (fabsf(svec_array[i] - value) < 1e-6f) { return true; }
    }
    return false;
}

/*---------------------------------------------------------------------------
 * var 노드(숫자 하나 또는 숫자 배열)를 f32 SVEC 로 읽어온다.
 * 반환은 실제로 읽힌 원소 수. min_length 보다 짧으면 0 패딩으로 채워 둔다.
 * node 가 NULL/비숫자면 0 (min_length>0 이면 0 패딩 배열은 만들어 둠).
 *---------------------------------------------------------------------------*/
static i32_t get_float_array(f32_t** dst, const var_t* node, i32_t min_length, bool append = false, bool sort = false)
{
    i32_t i, l;

    if (node == NULL || !var_is_arr(*node)) {
        /* 숫자 하나인 경우 */
        f32_t single_value = 0.0f;
        if (node != NULL && var_to_f32p(node, &single_value)) {
            if (!append) { SVEC_DROP(dst); }
            *SVEC_APPEND(f32_t, dst, 1) = single_value;
            return 1;
        }
        if (min_length > 0) {
            f32_t* p = SVEC_APPEND(f32_t, dst, min_length);
            memset(p, 0, sizeof(f32_t) * min_length);
        }
        return 0;
    }

    l = (i32_t)var_get_count(node);
    if (l > min_length) {
        min_length = l;
    }
    if (!append) { SVEC_DROP(dst); }
    f32_t* p = SVEC_APPEND(f32_t, dst, min_length);
    memset(p, 0, sizeof(f32_t) * min_length);
    for (i = 0; i < l; i++) {
        var_t* e = var_of_index((var_t*)node, i, false);
        p[i] = (e != NULL) ? var_to_f32_def(e, 0.0f) : 0.0f;
    }
    if (sort) {
        qsort(*dst, svec_length(*dst), sizeof(f32_t), &f32_cmp);
    }
    return l;
}

i32_t load_cal_points(CAL_PATTERN* result, const var_t* node)
{
    i32_t type = 0, n = 0, i, iv = 0, id, start_index;
    i16_t* p_i = NULL;
    f32_t* v = NULL;
    MARKER_POINT** points = &(result->mk);
    MARKER_POINT* p;
    i16_t** lines = &(result->lines);

    if (node == NULL || !var_is_obj(*node)) {
        return 0;
    }

    start_index = (i32_t)svec_length(*points);
    id = start_index;

    f32_t* w = NULL;  /* weight 저장 */
    get_float_array(&w, vfind(node, "w"), 0);

    f32_t* blksz = NULL;  /* block_size 저장 */
    get_float_array(&blksz, vfind(node, "block_size"), 0);

    /* shape: 문자열("+r O..") 또는 숫자(enum). 미지정은 CROSS */
    const char* shape_str = NULL;
    i32_t shape_len = 0;
    char shape_def[2] = { (char)MARKER_SHAPE_CROSS, 0 };
    {
        var_t* node_shape = vfind(node, "shape");
        const char* s = (node_shape != NULL) ? var_get_str(node_shape) : NULL;
        if (s != NULL) {
            shape_str = s;
            shape_len = (i32_t)strlen(s);
        } else {
            i32_t si = 0;
            if (node_shape != NULL && var_to_i32p(node_shape, &si)) {
                shape_def[0] = (char)si;
            }
            shape_str = shape_def;
            shape_len = 1;
        }
    }

    /* type 1: "xywh" 또는 "origin"+"size": x,y 는 좌상단, CW 회전 순서로 4점 생성
     * type 2: "rect": left, top, right, bottom (보통 top 이 bottom 보다 큰 수)
     * type 3: "pt2s": x,y 쌍 나열. lines 는 지정 순서대로 연결
     * type 4: "pt3s": x,y,z 나열. pt2s 와 유사
     * type 5: "x","y","z" + "ex_x","ex_y","ex_z": 그리드 방식, lines 는 그리드 연결 */

    if (get_float_array(&v, vfind(node, "origin"), 2) > 0) {
        if (get_float_array(&v, vfind(node, "size"), 2, true) > 0) {
            type = 1;
        }
    }
    if (type == 0 && get_float_array(&v, vfind(node, "xywh"), 4) > 0) {
        type = 1;
    }
    if (type == 0 && get_float_array(&v, vfind(node, "rect"), 4) > 0) {
        type = 2;
    }
    if (type == 0 && get_float_array(&v, vfind(node, "pt2s"), 2) > 0) {
        type = 3;
    }
    if (type == 0 && get_float_array(&v, vfind(node, "pt3s"), 3) > 0) {
        type = 4;
    }

    if (type == 0) {
        i32_t ix, iy, iz, sx_n, sy_n, sz_n, sxy_n, ii;
        f32_t *sx = NULL, *sy = NULL, *sz = NULL, *ex = NULL, *ey = NULL, *ez = NULL;
        i16_t* ids = NULL;

        /* 미지정 축은 0 하나가 기본값이 되도록 min_length 1 + 정렬해서 읽는다 */
        get_float_array(&sx, vfind(node, "x"), 1, false, true);
        get_float_array(&sy, vfind(node, "y"), 1, false, true);
        get_float_array(&sz, vfind(node, "z"), 1, false, true);
        get_float_array(&ex, vfind(node, "ex_x"), 0);
        get_float_array(&ey, vfind(node, "ex_y"), 0);
        get_float_array(&ez, vfind(node, "ex_z"), 0);

        sx_n = (i32_t)svec_length(sx);
        sy_n = (i32_t)svec_length(sy);
        sz_n = (i32_t)svec_length(sz);
        sxy_n = sx_n * sy_n;

        /* 연결관계까지 따지기 위해 먼저 전체 그리드 포인트에 대해 제외 여부를 마킹한다 */
        SVEC_SET_LENGTH(i16_t, &ids, (size_t)sx_n * sy_n * sz_n);

        for (iz = 0; iz < sz_n; ++iz) {
            bool exclude = true;
            if (ez) {
                exclude = is_in(sz[iz], ez);
            }
            for (iy = 0; iy < sy_n; ++iy) {
                if (ey) {
                    exclude = exclude && is_in(sy[iy], ey);
                }
                ii = iz * sxy_n + iy * sx_n;
                for (ix = 0; ix < sx_n; ++ix, ++ii) {
                    if (exclude && is_in(sx[ix], ex)) {
                        ids[ii] = -1;
                        continue;
                    }
                    p = SVEC_APPEND(MARKER_POINT, points, 1);
                    MARKER_POINT_init(p, 1);
                    p->phy = vec3_f32_(sx[ix], sy[iy], sz[iz]);
                    ids[ii] = (i16_t)(id + n); /* 추가될 체커의 번호를 기록 */
                    ++n;
                }
            }
        }

        for (iz = 0; iz < sz_n; ++iz) {
            for (iy = 0; iy < sy_n; ++iy) {
                ii = iz * sxy_n + iy * sx_n;
                for (ix = 0; ix < sx_n; ++ix, ++ii) {
                    i32_t i0, i1 = ids[ii];
                    if (i1 == -1) {
                        continue;
                    }
                    if (ix > 0 && (i0 = ids[ii - 1]) != -1) { /* 가로선 연결 */
                        p_i = SVEC_APPEND(i16_t, lines, 2);
                        p_i[0] = (i16_t)i0;
                        p_i[1] = (i16_t)i1;
                    }
                    if (iy > 0 && (i0 = ids[ii - sx_n]) != -1) { /* 세로선 연결 */
                        p_i = SVEC_APPEND(i16_t, lines, 2);
                        p_i[0] = (i16_t)i0;
                        p_i[1] = (i16_t)i1;
                    }
                    if (iz > 0 && (i0 = ids[ii - sxy_n]) != -1) { /* 깊이선 연결 */
                        p_i = SVEC_APPEND(i16_t, lines, 2);
                        p_i[0] = (i16_t)i0;
                        p_i[1] = (i16_t)i1;
                    }
                }
            }
        }

        SVEC_DROP(&sx);
        SVEC_DROP(&sy);
        SVEC_DROP(&sz);
        SVEC_DROP(&ex);
        SVEC_DROP(&ey);
        SVEC_DROP(&ez);
        SVEC_DROP(&ids);

    } else {
        n = 4;
        /* 사각 혹은 단순 점 배열 기반 */
        switch (type) {
        case 1: /* xywh 형태이므로 rect 형태로 변환해야 함. break 는 쓰지 말 것 */
            v[2] = v[0] + v[2]; /* right 는 left 에 width 를 더해서 구함 */
            v[3] = v[1] - v[3]; /* bottom 은 top 에서 height 를 빼서 구함 (top 쪽이 큰 수) */
            /* fall through */
        case 2: { /* rect */
            p = SVEC_APPEND(MARKER_POINT, points, 4);
            MARKER_POINT_init(p, 4);
            p[0].phy = vec3_f32_(v[0], v[1], 0.0f);
            p[1].phy = vec3_f32_(v[2], v[1], 0.0f);
            p[2].phy = vec3_f32_(v[2], v[3], 0.0f);
            p[3].phy = vec3_f32_(v[0], v[3], 0.0f);
            break;
        }
        case 3: {
            n = (i32_t)svec_length(v) / 2;
            p = SVEC_APPEND(MARKER_POINT, points, n);
            MARKER_POINT_init(p, n);
            for (i = 0, iv = 0; i < n; ++i, iv += 2) {
                p[i].phy = vec3_f32_(v[iv], v[iv + 1], 0.0f);
            }
            break;
        }
        case 4: {
            n = (i32_t)svec_length(v) / 3;
            p = SVEC_APPEND(MARKER_POINT, points, n);
            MARKER_POINT_init(p, n);
            for (i = 0, iv = 0; i < n; ++i, iv += 3) {
                p[i].phy = vec3_f32_(v[iv], v[iv + 1], v[iv + 2]);
            }
            break;
        }
        default:
            n = 0;
            break;
        }

        if (lines && n > 0) {
            /* 점 간의 관계를 표시하기 위해 라인 인덱스를 구성 (닫힌 형태) */
            p_i = SVEC_APPEND(i16_t, lines, (size_t)n * 2);
            for (i = 0, iv = 0; i < n; ++i, iv += 2) {
                p_i[iv] = (i16_t)(id + i);
                p_i[iv + 1] = (i16_t)(id + i + 1);
            }
            p_i[2 * n - 1] = (i16_t)id; /* 맨 마지막은 첫 시작점과 연결함 */
        }
    }

    p = *points + start_index;
    if ((size_t)n <= svec_length(w)) {
        for (i = 0; i < n; ++i) {
            p[i].weight = w[i];
        }
    } else {
        f32_t w0 = (w == NULL ? 1.0f : w[0]);
        for (i = 0; i < n; ++i) {
            p[i].weight = w0;
        }
    }
    /* Unit size */
    if ((size_t)n <= svec_length(blksz)) {
        for (i = 0; i < n; ++i) {
            p[i].block_size = blksz[i];
        }
    } else {
        f32_t blksz0 = (blksz == NULL ? DEFAULT_CHESS_BLOCK_SIZE : blksz[0]);
        for (i = 0; i < n; ++i) {
            p[i].block_size = blksz0;
        }
    }
    /* Shapes */
    if (n <= shape_len) {
        for (i = 0; i < n; ++i) {
            p[i].shape = (u8_t)shape_str[i];
        }
    } else {
        u8_t shape0 = (u8_t)shape_str[0];
        for (i = 0; i < n; ++i) {
            p[i].shape = shape0;
        }
    }

    SVEC_DROP(&w);
    SVEC_DROP(&blksz);
    SVEC_DROP(&v);
    return n;
}

/* 순수하게 물리 좌표만을 읽어온다 */
i32_t CAL_PATTERN_Load(CAL_PATTERN* self, const var_t* panel, const char* cam_id, const CAL_ANCHORS* anchors)
{
    i32_t i, rec_cnt = 0;
    f32_t* tmp_array = NULL;
    vec2_f32 o = { 0.0f, 0.0f }, x_axis = { 1.0f, 0.0f };

    if (panel == NULL || !var_is_obj(*panel) || cam_id == NULL) {
        return 0;
    }

    Log_("Loading cal pattern for %s", cam_id);

    /* 예전엔 parts 노드 없이 바로 정의했음 — 양쪽 다 허용 */
    var_t* v_parts = vfind(panel, "parts");
    if (v_parts != NULL && var_is_obj(*v_parts)) {
        panel = v_parts;
    }

    /* 카메라별 패턴 레코드 배열 */
    var_t* v_cal = vfind(panel, cam_id);
    if (v_cal != NULL && var_is_arr(*v_cal)) {
        rec_cnt = (i32_t)var_get_count(v_cal);

        SVEC_DROP(&self->mk);
        SVEC_DROP(&self->lines);

        for (i = 0; i < rec_cnt; i++) {
            var_t* v_rec = var_of_index(v_cal, i, false);
            if (v_rec == NULL) { continue; }

            mat4_t L = mat4_(vec4_x_axis(), vec4_y_axis(), vec4_z_axis(), vec4_w_axis());

            /* 지역 좌표계 정보 */
            var_t* v_node = vfind(v_rec, "coord");
            if (v_node != NULL && var_is_obj(*v_node)) {
                var_t* v_anchor = vfind(v_node, "anchor");
                const char* anchor = (v_anchor != NULL) ? var_get_str(v_anchor) : NULL;

                if (get_float_array(&tmp_array, vfind(v_node, "origin"), 2) > 0) {
                    o = *(vec2_f32*)tmp_array;
                }
                if (get_float_array(&tmp_array, vfind(v_node, "x_axis"), 2) > 0) {
                    x_axis = *(vec2_f32*)tmp_array;
                }
                /* 수동으로 x_axis 를 설정한 경우를 대비해 normalize */
                f32_t inv_of_len_x_axis = 1.0f / sqrtf(vec2_f32_sqlen(x_axis));
                x_axis.x *= inv_of_len_x_axis;
                x_axis.y *= inv_of_len_x_axis;

                if (anchors != NULL && anchor != NULL) {
                    if (strcmp(anchor, "fr") == 0) {
                        o.x += anchors->fr.x;
                        o.y += anchors->fr.y;
                    } else if (strcmp(anchor, "bk") == 0) {
                        o.x += anchors->bk.x;
                        o.y += anchors->bk.y;
                    }
                }

                L = mat4_(vec4_(x_axis.x, x_axis.y, 0.0f, 0.0f),
                          vec4_(0.0f - x_axis.y, x_axis.x, 0.0f, 0.0f),
                          vec4_(0.0f, 0.0f, 1.0f, 0.0f),
                          vec4_(o.x, o.y, 0.0f, 1.0f));
            }

            i32_t n = (i32_t)svec_length(self->mk);

            /* 마커 정보. 구버전의 marks.clip 사이드이펙트(디스플레이 클립)는 커널 밖 */
            v_node = vfind(v_rec, "marks");
            if (v_node != NULL && var_is_obj(*v_node)) {
                load_cal_points(self, v_node);
            }

            /* 체커 정보 */
            v_node = vfind(v_rec, "checkers");
            if (v_node != NULL && var_is_arr(*v_node)) {
                i32_t item_count = (i32_t)var_get_count(v_node);
                for (i32_t it = 0; it < item_count; ++it) {
                    load_cal_points(self, var_of_index(v_node, it, false));
                }
            }

            /* 레코드 지역좌표 -> 차량(월드) 좌표 변환 */
            i32_t total = (i32_t)svec_length(self->mk);
            if (total > n) {
                mat4_mul_pnt3_ns(L, &((self->mk + n)->phy), (size_t)(total - n), sizeof(MARKER_POINT));
            }
        }
    }
    SVEC_DROP(&tmp_array);
    return (i32_t)svec_length(self->mk);
}
