#include "interface.h"

REN_NAMESPACE_BEGIN

constexpr uint c_persistent_set = PERSISTENT_SET;
constexpr uint c_materials_slot = MATERIALS_SLOT;
#undef PERSISTENT_SET
#undef MATERIALS_SLOT

constexpr uint c_global_set = GLOBAL_SET;
constexpr uint c_global_cb_slot = GLOBAL_CB_SLOT;
constexpr uint c_matrices_slot = MATRICES_SLOT;
#undef GLOBAL_SET
#undef GLOBAL_CB_SLOT
#undef MATRICES_SLOT

constexpr uint c_model_set = MODEL_SET;
constexpr uint c_positions_slot = POSITIONS_SLOT;
constexpr uint c_colors_slot = COLORS_SLOT;
#undef MODEL_SET
#undef POSITIONS_SLOT
#undef COLORS_SLOT

constexpr uint c_push_constants_register = PUSH_CONSTANTS_REGISTER;
constexpr uint c_push_constants_space = PUSH_CONSTANTS_SPACE;
#undef PUSH_CONSTANTS_REGISTER
#undef PUSH_CONSTANTS_SPACE

REN_NAMESPACE_END
