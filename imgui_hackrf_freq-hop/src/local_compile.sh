#!/usr/bin/bash

g++ -std=c++11 -I../imgui -I../backends -g -Wall -Wformat `pkg-config --cflags glfw3` -c -o gui_replay.o gui_replay.cpp && g++ gui_replay.o ../imgui/imgui*.cpp ../backends/imgui_impl_glfw.cpp ../backends/imgui_impl_opengl2.cpp \
    -I../imgui -I../backends `pkg-config --libs glfw3` -lGL -ldl -lpthread -lhackrf -o gui_replay

echo "done"
