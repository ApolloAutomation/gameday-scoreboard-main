"""panel_layout: picks the HUB75 panel arrangement at boot from a saved
preference, so one firmware serves a single 64x64 panel, two to four panels
side by side, and four panels in a 2x2."""

import esphome.codegen as cg
from esphome.components.hub75 import display as hub75_display
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@ApolloAutomation"]

CONF_DISPLAY_ID = "display_id"
CONF_DEFAULT_COLS = "default_cols"
CONF_MAX_COLS = "max_cols"
CONF_MAX_ROWS = "max_rows"

panel_layout_ns = cg.esphome_ns.namespace("panel_layout")
PanelLayout = panel_layout_ns.class_("PanelLayout", cg.Component)

def _default_within_max(config):
    # PanelLayout::setup() replaces a saved count above max_cols with the
    # default, so a default above it would be saved again on every boot and
    # drive more panels than set_cols() accepts.
    if config[CONF_DEFAULT_COLS] > config[CONF_MAX_COLS]:
        raise cv.Invalid(
            f"default_cols ({config[CONF_DEFAULT_COLS]}) can't be more than "
            f"max_cols ({config[CONF_MAX_COLS]})",
            path=[CONF_DEFAULT_COLS],
        )
    # The 2x2 is a chain of four panels.
    if config[CONF_MAX_ROWS] > 1 and config[CONF_MAX_COLS] < 4:
        raise cv.Invalid(
            f"max_rows: 2 (the 2x2) needs max_cols of at least 4 (got {config[CONF_MAX_COLS]})",
            path=[CONF_MAX_ROWS],
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PanelLayout),
            cv.Required(CONF_DISPLAY_ID): cv.use_id(hub75_display.HUB75Display),
            cv.Optional(CONF_DEFAULT_COLS, default=1): cv.int_range(min=1, max=8),
            # The longest chain the build drives, in panels.
            cv.Optional(CONF_MAX_COLS, default=2): cv.int_range(min=1, max=8),
            # 2 also allows the 2x2 (which needs max_cols of at least 4).
            cv.Optional(CONF_MAX_ROWS, default=1): cv.int_range(min=1, max=2),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _default_within_max,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_display(await cg.get_variable(config[CONF_DISPLAY_ID])))
    cg.add(var.set_default_cols(config[CONF_DEFAULT_COLS]))
    cg.add(var.set_max_cols(config[CONF_MAX_COLS]))
    cg.add(var.set_max_rows(config[CONF_MAX_ROWS]))
