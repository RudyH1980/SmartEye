idf_component_register(
    SRCS
        "src/portal_ui.cpp"
        "fonts/portal_font_hero_112.c"
        "fonts/portal_font_super_44.c"
        "fonts/portal_font_title_30.c"
        "fonts/portal_font_small_22.c"
        "fonts/portal_font_icons_40.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lvgl__lvgl
)
