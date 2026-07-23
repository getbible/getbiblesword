# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.25)

project(getBibleSwordConsumer LANGUAGES C)

find_package(getBibleSword CONFIG REQUIRED)

add_executable(getbiblesword_consumer main.c)
target_compile_features(getbiblesword_consumer PRIVATE c_std_11)
target_link_libraries(getbiblesword_consumer PRIVATE getBibleSword::getBibleSword)
