idf_component_register(
    SRCS
        "screens/ui_Screen1.c"
        "screens/ui_Screen2.c"
        "screens/ui_Screen3.c"
        "screens/ui_Screen4.c"
        "ui.c"
        "components/ui_comp_hook.c"
        "ui_helpers.c"
        "images/ui_img_background_png.c"
        "images/ui_img_wifi_png.c"
        "images/ui_img_keypad_png.c"
        "images/ui_img_exam_png.c"
        "images/ui_img_fingerprint_png.c"
        "images/ui_img_1122079624.c"
        "images/ui_img_38905653.c"
        "images/ui_img_1045203667.c"
        "images/ui_img_settings1_png.c"
        "images/ui_img_2094406470.c"
        "images/ui_img_1534850174.c"
        "images/ui_img_chip_png.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
