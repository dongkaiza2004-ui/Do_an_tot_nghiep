cmake_minimum_required(VERSION 3.16)

# ĐặtEXTRA_COMPONENT_DIRS TRƯỚC dòng include project.cmake
set(EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(project_face_cam)