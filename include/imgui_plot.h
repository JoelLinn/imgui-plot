#pragma once
#include <cstdint>
#include <imgui.h>

namespace ImGui {
// Use this structure to pass the plot data and settings into the Plot function
struct PlotConfig {
    // Helper struct to avoid templating overhead
    struct Buffer {
        enum class Type {
            float32,
            float64,
            int32
        } type = Type::float32;
        union {
            const void* raw = nullptr;
            const float* float32;
            const double* float64;
            const int32_t* int32;
        };

        Buffer() : type(Type::float32), raw(nullptr) {}
        Buffer(const float* data) : type(Type::float32), float32(data) {}
        Buffer(const double* data) : type(Type::float64), float64(data) {}
        Buffer(const int32_t* data) : type(Type::int32), int32(data) {}
        const float operator[] (size_t) const;
        inline bool IsNullptr() const { return raw == nullptr; }
    };
    struct Values {
        // if necessary, you can provide x-axis values
        Buffer xs;
        // array of y values. If null, use ys_list (below)
        Buffer ys;
        // the number of values in each array
        size_t count;
        // Plot color. If 0, use ImGuiCol_PlotLines.
        ImU32 color = 0;

        // in case you need to draw multiple plots at once, use this instead of ys
        const Buffer* ys_list = nullptr;
        // the number of plots to draw
        size_t ys_count = 0;
        // colors for each plot
        const ImU32* colors = nullptr;
    } values;
    struct Scale {
        // Minimum plot value
        float min;
        // Maximum plot value
        float max;
        enum class Type {
            Linear,
            Log10,
        };
        // How to scale the x-axis
        Type type = Type::Linear;
    } scale;
    struct Tooltip {
        bool show = false;
        const char* format = "%g: %8.4g";
    } tooltip;
    struct Axis {
        bool grid_show = false;
        bool label_show_bl = false; // show labels bottom or left respectively
        bool label_show_tr = false; // show labels top or right respectively
        float tick_distance = 100; // at which intervals to draw the grid or label
        unsigned int tick_subs = 9; // how many subticks between each tick
        const char* label_format = "%g";
    } axis_x, axis_y;
    struct Selection {
        bool show = false;
        size_t* start = nullptr;
        size_t* length = nullptr;
        // "Sanitize" function. Give it selection length, and it will return
        // the "allowed" length. Useful for FFT, where selection must be
        // of power of two
        size_t(*sanitize_fn)(size_t) = nullptr;
    } selection;
    struct VerticalLines {
        bool show = false;
        Buffer xs; // at which x values to draw the lines
        size_t count = 0;
    } v_lines;
    // Set size to -1 to fill parent window
    ImVec2 frame_size = ImVec2(-1.f, 0.f);
    float line_thickness = 1.f;
    // Can cause aliasing effects if enabled.
    bool skip_small_lines = true;
    const char* overlay_text = nullptr;
};

enum class PlotStatus {
    nothing,
    selection_updated,
};

IMGUI_API PlotStatus Plot(const char* label, const PlotConfig& conf);
}
