#ifndef cal_kernelH
#define cal_kernelH
//---------------------------------------------------------------------------

#include "lbx_core.h"
#include "lbx_var.h"

#ifdef __cplusplus
extern "C" {
#endif //#ifdef __cplusplus

/*
 * Shared calibration kernel types (flood family).
 *
 * Ported from 2023-DH-KU-SVM svm/src/lbsvm_cal.{h,cpp} (source of truth).
 * Old RCM dynamic arrays are replaced by lbx SVEC (svec_length /
 * SVEC_APPEND / SVEC_DROP). All physical coordinates are in the
 * canonical vehicle frame (ISO8855, front-axle origin) in millimeters;
 * conversion from a pattern's authoring frame happens at load time.
 */

/** Edge length (mm) of the black square that forms a checker cross point. */
#define DEFAULT_CHESS_BLOCK_SIZE 100

typedef u8_t MARKER_SHAPE;
#define MARKER_SHAPE_UNKNOWN 0
#define MARKER_SHAPE_CROSS   1  /**< Chessboard cross. */
#define MARKER_SHAPE_CORNER  2  /**< Plain corner. */
#define MARKER_SHAPE_POINT   3  /**< Circular dot. */

/**
 * @brief Detailed corner classification beyond MARKER_SHAPE.
 *
 * Values are printable characters on purpose (debug dumps read directly).
 * Quadrant numbering follows screen-space quadrants of the pattern model.
 */
typedef enum {
    ctUnknown   = ' ',
    ct1         = '1',  /**< Quadrant 1 black: corner pattern. */
    ct2         = '2',  /**< Quadrant 2 black: corner pattern. */
    ct3         = '3',  /**< Quadrant 3 black: corner pattern. */
    ct4         = '4',  /**< Quadrant 4 black: corner pattern. */
    ct13        = '<',  /**< Quadrants 1+3 black: cross pattern. */
    ct24        = '>',  /**< Quadrants 2+4 black: cross pattern. */
    ctCross     = '+',  /**< Cross pattern, orientation agnostic. */
    ctCorner    = 'r',  /**< Corner pattern, orientation agnostic. */
    ctPoint     = 'O',  /**< Circular point. */
    ctNotCorner = '-',  /**< Rejected: not a corner. */
    ctVariance  = 'V',  /**< Rejected: corner but variance too high. */
    ctMarkerLT  = 'a',
    ctMarkerRT  = 'b',
    ctMarkerRB  = 'c',
    ctMarkerLB  = 'd',
} TCornerType;

/**
 * @brief One calibration marker point.
 *
 * phy is authored in the pattern's local frame and converted to the
 * canonical vehicle frame by CAL_PATTERN_Load. img is filled by the
 * detection pipeline.
 */
typedef struct {
    vec3_f32     phy;        /**< Physical coordinate (vehicle frame, mm). */
    vec2_f32     img;        /**< Image coordinate (px). */
    MARKER_SHAPE shape;      /**< Detector hint (cross / corner / point). */
    u8_t         plane;      /**< Normal axis of the marker plane: 'x' = yz, 'y' = xz, 'z' = xy. */
    u16_t        _reserved;
    f32_t        block_size; /**< Edge length (mm) of the block forming the cross. */
    f32_t        weight;     /**< Optimization weight (1.0 default). */
} MARKER_POINT;

extern void MARKER_POINT_init(MARKER_POINT* o, i32_t n LBX_DEFAULT(1));
extern void MARKER_POINT_free(MARKER_POINT* o, i32_t n LBX_DEFAULT(1));

/**
 * @brief A loaded calibration panel pattern: marker points + connectivity.
 *
 * mk and lines are SVEC arrays (query length with svec_length()).
 * lines stores index pairs (2 entries per edge) into mk.
 */
typedef struct {
    const char*   id;
    MARKER_POINT* mk;          /**< SVEC of marker points. */
    i16_t*        lines;       /**< SVEC of index pairs describing edges. */
    i32_t         base_length;
} CAL_PATTERN;

extern void cal_pattern_init(CAL_PATTERN* t);
extern void cal_pattern_free(CAL_PATTERN* t);

/**
 * @brief Anchor offsets referenced by a pattern's "coord.anchor" field.
 *
 * "fr" / "bk" anchor names in the pattern document add these offsets to
 * the record origin. Old code kept them as file-scope globals
 * (anchor_fr / anchor_bk); they are instance state now -- the module
 * derives them from vehicle dims or the params document.
 */
typedef struct {
    vec2_f32 fr;  /**< Front anchor offset (vehicle frame, mm). */
    vec2_f32 bk;  /**< Rear anchor offset (vehicle frame, mm). */
} CAL_ANCHORS;

/**
 * @brief Parse one point-group node ("marks" entry or one "checkers" item)
 *        and append the markers (and connectivity lines) to @p result.
 *
 * Accepted node formats (auto-detected): origin+size / xywh / rect /
 * pt2s / pt3s / grid (x, y, z with ex_x / ex_y / ex_z exclusions).
 * Optional fields: w (weights), block_size, shape ('+', 'r', 'O' or int).
 *
 * @return Number of markers appended.
 */
extern i32_t load_cal_points(CAL_PATTERN* result, const var_t* node);

/**
 * @brief Load a calibration pattern for one camera from a panel document.
 *
 * @param self     Destination pattern. Existing mk array is replaced.
 * @param panel    Panel document node, i.e. cal_panel["<PANEL_ID>"]
 *                 (host-injected var_t; an optional "parts" wrapper is
 *                 accepted). NULL or non-object returns 0.
 * @param cam_id   Camera key inside the panel ("front" / "rear" / ...).
 * @param anchors  Anchor offsets for "fr" / "bk". NULL = zero offsets.
 * @return Total marker count in @p self after loading.
 *
 * @note Physical coords are transformed record-local -> vehicle frame
 *       here (coord.origin + coord.x_axis + anchor). The old in-app
 *       "clip" side effect (display clip rect update) is intentionally
 *       not part of the kernel.
 */
extern i32_t CAL_PATTERN_Load(CAL_PATTERN* self, const var_t* panel, const char* cam_id, const CAL_ANCHORS* anchors);

#ifdef __cplusplus
}
#endif //#ifdef __cplusplus

#endif //#ifndef cal_kernelH
