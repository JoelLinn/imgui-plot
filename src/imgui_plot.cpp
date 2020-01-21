#include <imgui_plot.h>
#include <imgui.h>
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui_internal.h>

namespace ImGui {
// [0..1] -> [0..1]
static float rescale(float t, float min, float max, PlotConfig::Scale::Type type) {
    switch (type) {
    case PlotConfig::Scale::Linear:
        return t;
    case PlotConfig::Scale::Log10:
        return log10(ImLerp(min, max, t) / min) / log10(max / min);
    }
    return 0;
}

// [0..1] -> [0..1]
static float rescale_inv(float t, float min, float max, PlotConfig::Scale::Type type) {
    switch (type) {
    case PlotConfig::Scale::Linear:
        return t;
    case PlotConfig::Scale::Log10:
        return (pow(max/min, t) * min - min) / (max - min);
    }
    return 0;
}

// Inverse function to ImLerp
template<typename T> static inline T lerp_inv(T a, T b, T l) {
    return static_cast<T>((l - a) / (b - a));
}

static int cursor_to_idx(const ImVec2& pos, const ImRect& bb, const PlotConfig& conf, float x_min, float x_max) {
    const float t = ImClamp((pos.x - bb.Min.x) / (bb.Max.x - bb.Min.x), 0.0f, 0.9999f);
    const int v_idx = static_cast<int>(rescale_inv(t, x_min, x_max, conf.scale.type) * static_cast<double>(conf.values.count - 1) + 0.5f);
    IM_ASSERT(v_idx >= 0 && v_idx < conf.values.count);

    return v_idx;
}

// Helper to read an array index as the correct type
const float PlotConfig::Buffer::operator[](size_t i) const
{
    switch (this->type)
    {
    case PlotConfig::Buffer::Type::float32:
        return this->float32[i];
    case PlotConfig::Buffer::Type::float64:
        return static_cast<float>(this->float64[i]);
    case PlotConfig::Buffer::Type::int32:
        return static_cast<float>(this->int32[i]);
    default:
        return .0f;
    }
}

PlotStatus Plot(const char* label, const PlotConfig& conf) {
    PlotStatus status = PlotStatus::nothing;

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return status;

    const PlotConfig::Buffer* ys_list = conf.values.ys_list;
    int ys_count = conf.values.ys_count;
    const ImU32* colors = conf.values.colors;
    if (conf.values.ys.raw != nullptr) { // draw only a single plot
        ys_list = &conf.values.ys;
        ys_count = 1;
        colors = &conf.values.color;
    }

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const ImRect frame_bb(
        window->DC.CursorPos,
        window->DC.CursorPos + ImVec2(conf.frame_size.x < 0 ? window->WorkRect.GetSize().x : conf.frame_size.x, conf.frame_size.y < 0 ? window->WorkRect.GetSize().y : conf.frame_size.y));
    const ImRect total_bb = frame_bb;
    ItemSize(total_bb, style.FramePadding.y);
    if (!ItemAdd(total_bb, 0, &frame_bb))
        return status;
    const bool hovered = ItemHoverable(frame_bb, id);

    ImRect overlay_bb = frame_bb;

    RenderFrame(
        frame_bb.Min,
        frame_bb.Max,
        GetColorU32(ImGuiCol_FrameBg),
        true,
        style.FrameRounding);

    if (conf.values.count > 1) {
        float x_min;
        float x_max;
        {
            auto min = conf.values.offset;
            auto max = conf.values.offset + conf.values.count - 1;
            if (conf.values.xs.raw) {
                x_min = conf.values.xs[min];
                x_max = conf.values.xs[max];
            }
            else {
                x_min = static_cast<float>(min);
                x_max = static_cast<float>(max);
            }
        }

        struct AxisInfo {
            ImVec2 legend_largest = ImVec2(.0f, .0f);
            float tick_count = .0f;
            float tick_inc = 0.f;
            float tick_first = .0f;
        } axis_info_x, axis_info_y;

        // Precalculate basic axis parameters since the plot area depends on text widths
        if (conf.axis_x.grid_show || conf.axis_x.label_show_bl || conf.axis_x.label_show_tr) {
            switch (conf.scale.type) {
            case PlotConfig::Scale::Linear: {
                axis_info_x.tick_count = (x_max - x_min) / (conf.axis_x.tick_distance / (conf.axis_x.tick_subs + 1));
                axis_info_x.tick_inc = 1.f / axis_info_x.tick_count;
                // calculate nearest multiple of tick_distance to x_min and do inverse lerp:
                axis_info_x.tick_first = lerp_inv(x_min, x_max, (static_cast<int>(x_min / conf.axis_x.tick_distance)* conf.axis_x.tick_distance));
                axis_info_x.tick_count += conf.axis_x.tick_subs + 1;
                if (!conf.axis_x.label_show_bl && !conf.axis_x.label_show_tr) { break; }
                for (int i = 0; i <= axis_info_x.tick_count; i += conf.axis_x.tick_subs + 1) {
                    const float tick_pos = axis_info_x.tick_first + i * axis_info_x.tick_inc;
                    if (tick_pos < 0.0f) continue;
                    if (tick_pos > 1.0f) break;
                    const float x_val = ImLerp(x_min, x_max, tick_pos);
                    const char* text_end = g.TempBuffer + ImFormatString(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), conf.axis_x.label_format, x_val);
                    const ImVec2 text_size = CalcTextSize(g.TempBuffer, text_end);
                    if (text_size.x > axis_info_x.legend_largest.x) { axis_info_x.legend_largest.x = text_size.x; }
                    if (text_size.y > axis_info_x.legend_largest.y) { axis_info_x.legend_largest.y = text_size.y; }
                }
                break;
            }
            case PlotConfig::Scale::Log10: {
                if (!conf.axis_x.label_show_bl && !conf.axis_x.label_show_tr) { break; }
                for (float i = 1; i <= x_max; i *= 10)
                {
                    if (i < x_min) continue;
                    const char* text_end = g.TempBuffer + ImFormatString(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), conf.axis_x.label_format, i);
                    const ImVec2 text_size = CalcTextSize(g.TempBuffer, text_end);
                    if (text_size.x > axis_info_x.legend_largest.x) { axis_info_x.legend_largest.x = text_size.x; }
                    if (text_size.y > axis_info_x.legend_largest.y) { axis_info_x.legend_largest.y = text_size.y; }
                }
                break;
            }
            }
        }
        if (conf.axis_y.grid_show || conf.axis_y.label_show_tr || conf.axis_y.label_show_bl) {
            axis_info_y.tick_count = (conf.scale.max - conf.scale.min) / (conf.axis_y.tick_distance / (conf.axis_y.tick_subs + 1));
            axis_info_y.tick_inc = 1.f / axis_info_y.tick_count;
            axis_info_y.tick_first = lerp_inv(conf.scale.min, conf.scale.max, (static_cast<int>(conf.scale.min / conf.axis_y.tick_distance)* conf.axis_y.tick_distance));
            axis_info_y.tick_count += conf.axis_y.tick_subs + 1;
            if (conf.axis_y.label_show_bl || conf.axis_y.label_show_tr) {
                for (int i = 0; i <= axis_info_y.tick_count; i += conf.axis_y.tick_subs + 1) {
                    const float tick_pos = axis_info_y.tick_first + i * axis_info_y.tick_inc;
                    if (tick_pos < 0.0f) continue;
                    if (tick_pos > 1.0f) break;
                    // Todo make this position properly
                    const float y_val = ImLerp(conf.scale.min, conf.scale.max, tick_pos);
                    const char* text_end = g.TempBuffer + ImFormatString(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), conf.axis_y.label_format, y_val);
                    const ImVec2 text_size = CalcTextSize(g.TempBuffer, text_end);
                    if (text_size.x > axis_info_y.legend_largest.x) { axis_info_y.legend_largest.x = text_size.x; }
                    if (text_size.y > axis_info_y.legend_largest.y) { axis_info_y.legend_largest.y = text_size.y; }
                }
            }
        }

        const ImRect inner_bb(
            frame_bb.Min + style.FramePadding + ImVec2(conf.axis_y.label_show_bl ? axis_info_y.legend_largest.x : 0, conf.axis_x.label_show_tr ? axis_info_x.legend_largest.y : 0),
            frame_bb.Max - style.FramePadding - ImVec2(conf.axis_y.label_show_tr ? axis_info_y.legend_largest.x : 0, conf.axis_x.label_show_bl ? axis_info_x.legend_largest.y : 0));
        overlay_bb = inner_bb;

        if (inner_bb.GetSize().x <= 0 || inner_bb.GetSize().y <= 0) {
            return status;
        }

        int res_w;
        if (conf.skip_small_lines)
            res_w = ImMin(static_cast<int>(inner_bb.GetSize().x), static_cast<int>(conf.values.count));
        else
            res_w = conf.values.count;
        res_w -= 1;
        int item_count = conf.values.count - 1;

        // Tooltip on hover
        int v_hovered = -1;
        if (conf.tooltip.show && hovered && inner_bb.Contains(g.IO.MousePos)) {
            const int v_idx = cursor_to_idx(g.IO.MousePos, inner_bb, conf, x_min, x_max);
            const size_t data_idx = conf.values.offset + (v_idx % conf.values.count);
            const float x0 = conf.values.xs.raw ? conf.values.xs[data_idx] : v_idx;
            const float y0 = ys_list[0][data_idx]; // TODO: tooltip is only shown for the first y-value!
            SetTooltip(conf.tooltip.format, x0, y0);
            v_hovered = v_idx;
        }

        const float t_step = 1.0f / (float)res_w;
        const float inv_scale = (conf.scale.min == conf.scale.max) ?
            0.0f : (1.0f / (conf.scale.max - conf.scale.min));

        if (conf.axis_x.grid_show || conf.axis_x.label_show_bl || conf.axis_x.label_show_tr) {
            int y0 = inner_bb.Min.y;
            int y1 = inner_bb.Max.y;
            switch (conf.scale.type) {
            case PlotConfig::Scale::Linear: {
                for (int i = 0; i <= axis_info_x.tick_count; ++i) {
                    const float tick_pos = axis_info_x.tick_first + i * axis_info_x.tick_inc;
                    const bool isSub = i % (conf.axis_x.tick_subs + 1);
                    if (tick_pos < 0.0f) continue;
                    if (tick_pos > 1.0f) break;
                    int x0 = ImLerp(inner_bb.Min.x, inner_bb.Max.x, tick_pos);
                    if (conf.axis_x.grid_show) {
                        window->DrawList->AddLine(
                            ImVec2(x0, y0),
                            ImVec2(x0, y1),
                            IM_COL32(200, 200, 200, isSub ? 128 : 255));
                    }
                    if (!isSub) {
                        const float x_val = ImLerp(x_min, x_max, tick_pos);
                        const char* text_end = g.TempBuffer + ImFormatString(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), conf.axis_x.label_format, x_val);
                        const ImVec2 text_size = CalcTextSize(g.TempBuffer, text_end);
                        const float text_pos_x = ImClamp(x0 - text_size.x / 2, inner_bb.Min.x, inner_bb.Max.x - text_size.x);
                        if (conf.axis_x.label_show_bl) {
                            // bottom
                            RenderText(ImVec2(text_pos_x, y1), g.TempBuffer, text_end, false);
                        }
                        if (conf.axis_x.label_show_tr) {
                            // top
                            RenderText(ImVec2(text_pos_x, y0 - text_size.y), g.TempBuffer, text_end, false);
                        }
                    }
                }
                break;
            }
            case PlotConfig::Scale::Log10: {
                for (int start = 1; start <= x_max; start *= 10)
                {
                    for (int i = 1; i < 10; ++i) {
                        float x = start * i;
                        const bool isSub = i > 1;
                        if (x < x_min) continue;
                        if (x > x_max) break;
                        float t = log10(x / x_min) / log10(x_max / x_min);
                        int x0 = ImLerp(inner_bb.Min.x, inner_bb.Max.x, t);
                        if (conf.axis_x.grid_show) {
                            window->DrawList->AddLine(
                                ImVec2(x0, y0),
                                ImVec2(x0, y1),
                                IM_COL32(200, 200, 200, isSub ? 128 : 255));
                        }
                        if (!isSub && (conf.axis_x.label_show_bl || conf.axis_x.label_show_tr)) {
                            const char* text_end = g.TempBuffer + ImFormatString(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), conf.axis_x.label_format, x);
                            const ImVec2 text_size = CalcTextSize(g.TempBuffer, text_end);
                            const float text_pos_x = ImClamp(x0 - text_size.x / 2, inner_bb.Min.x, inner_bb.Max.x - text_size.x);
                            if (conf.axis_x.label_show_tr) {
                                RenderText(ImVec2(text_pos_x, y0 - text_size.y), g.TempBuffer, text_end, false);
                            }
                            if (conf.axis_x.label_show_bl) {
                                RenderText(ImVec2(text_pos_x, y1), g.TempBuffer, text_end, false);
                            }
                        }
                    }
                }
                break;
            }
            }
        }
        if (conf.axis_y.grid_show || conf.axis_y.label_show_tr || conf.axis_y.label_show_bl) {
            int x0 = inner_bb.Min.x;
            int x1 = inner_bb.Max.x;
            for (int i = 0; i <= axis_info_y.tick_count; ++i) {
                const float tick_pos = axis_info_y.tick_first + i * axis_info_y.tick_inc;
                const bool isSub = i % (conf.axis_y.tick_subs + 1);
                if (tick_pos < 0.0f) continue;
                if (tick_pos > 1.0f) break;
                int y0 = ImLerp(inner_bb.Max.y, inner_bb.Min.y, tick_pos);
                if (conf.axis_y.grid_show) {
                    window->DrawList->AddLine(
                        ImVec2(x0, y0),
                        ImVec2(x1, y0),
                        IM_COL32(0, 0, 0, isSub ? 16 : 64));
                }
                if (!isSub && (conf.axis_y.label_show_bl || conf.axis_y.label_show_tr)) {
                    const float y_val = ImLerp(conf.scale.min, conf.scale.max, tick_pos);
                    const char* text_end = g.TempBuffer + ImFormatString(g.TempBuffer, IM_ARRAYSIZE(g.TempBuffer), conf.axis_y.label_format, y_val);
                    const ImVec2 text_size = CalcTextSize(g.TempBuffer, text_end);
                    const float text_pos_y = ImClamp(y0 - text_size.y / 2, inner_bb.Min.y, inner_bb.Max.y - text_size.y);
                    if (conf.axis_y.label_show_bl) {
                        // left
                        RenderText(ImVec2(x0 - axis_info_y.legend_largest.x, text_pos_y), g.TempBuffer, text_end, false);
                    }
                    if (conf.axis_y.label_show_tr) {
                        // right
                        RenderText(ImVec2(x1, text_pos_y), g.TempBuffer, text_end, false);
                    }
                }
            }
        }

        const ImU32 col_hovered = GetColorU32(ImGuiCol_PlotLinesHovered);
        ImU32 col_base = GetColorU32(ImGuiCol_PlotLines);

        for (int i = 0; i < ys_count; ++i) {
            if (colors) {
                if (colors[i]) col_base = colors[i];
                else col_base = GetColorU32(ImGuiCol_PlotLines);
            }
            float v0 = ys_list[i][conf.values.offset];
            float t0 = 0.0f;
            // Point in the normalized space of our target rectangle
            ImVec2 tp0 = ImVec2(t0, 1.0f - ImSaturate((v0 - conf.scale.min) * inv_scale));

            for (int n = 0; n < res_w; n++)
            {
                const float t1 = t0 + t_step;
                const int v1_idx = (int)(t0 * item_count + 0.5f);
                IM_ASSERT(v1_idx >= 0 && v1_idx < conf.values.count);
                const float v1 = ys_list[i][conf.values.offset + (v1_idx + 1) % conf.values.count];
                const ImVec2 tp1 = ImVec2(
                    rescale(t1, x_min, x_max, conf.scale.type),
                    1.0f - ImSaturate((v1 - conf.scale.min) * inv_scale));

            // NB: Draw calls are merged together by the DrawList system. Still, we should render our batch are lower level to save a bit of CPU.
                ImVec2 pos0 = ImLerp(inner_bb.Min, inner_bb.Max, tp0);
                ImVec2 pos1 = ImLerp(inner_bb.Min, inner_bb.Max, tp1);

                if (v1_idx == v_hovered) {
                    window->DrawList->AddCircleFilled(pos0, 3, col_hovered);
                }

                window->DrawList->AddLine(
                    pos0,
                    pos1,
                    col_base,
                    conf.line_thickness);

                t0 = t1;
                tp0 = tp1;
            }
        }

        if (conf.v_lines.show) {
            for (size_t i = 0; i < conf.v_lines.count; ++i) {
                float x = lerp_inv(x_min, x_max, conf.v_lines.xs[i]);
                if (x < 0.0f || x > 1.0f) { continue; }
                const float t1 = rescale(x, x_min, x_max, conf.scale.type);
                ImVec2 pos0 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2(t1, 0.f));
                ImVec2 pos1 = ImLerp(inner_bb.Min, inner_bb.Max, ImVec2(t1, 1.f));
                window->DrawList->AddLine(pos0, pos1, IM_COL32(0xff, 0, 0, 0x88));
            }
        }

        if (conf.selection.show) {
            if (hovered) {
                if (g.IO.MouseClicked[0]) {
                    SetActiveID(id, window);
                    FocusWindow(window);

                    const int v_idx = cursor_to_idx(g.IO.MousePos, inner_bb, conf, x_min, x_max);
                    uint32_t start = conf.values.offset + (v_idx % conf.values.count);
                    uint32_t end = start;
                    if (conf.selection.sanitize_fn)
                        end = conf.selection.sanitize_fn(end - start) + start;
                    if (end < conf.values.offset + conf.values.count) {
                        *conf.selection.start = start;
                        *conf.selection.length = end - start;
                        status = PlotStatus::selection_updated;
                    }
                }
            }

            if (g.ActiveId == id) {
                if (g.IO.MouseDown[0]) {
                    const int v_idx = cursor_to_idx(g.IO.MousePos, inner_bb, conf, x_min, x_max);
                    const uint32_t start = *conf.selection.start;
                    uint32_t end = conf.values.offset + (v_idx % conf.values.count);
                    if (end > start) {
                        if (conf.selection.sanitize_fn)
                            end = conf.selection.sanitize_fn(end - start) + start;
                        if (end < conf.values.offset + conf.values.count) {
                            *conf.selection.length = end - start;
                            status = PlotStatus::selection_updated;
                        }
                    }
                } else {
                    ClearActiveID();
                }
            }
            float fSelectionStep = 1.0 / item_count;
            ImVec2 pos0 = ImLerp(inner_bb.Min, inner_bb.Max,
                ImVec2(rescale(fSelectionStep * *conf.selection.start, x_min, x_max, conf.scale.type), 0.f));
            ImVec2 pos1 = ImLerp(inner_bb.Min, inner_bb.Max,
                ImVec2(rescale(fSelectionStep * (*conf.selection.start + *conf.selection.length), x_min, x_max, conf.scale.type), 1.f));
            window->DrawList->AddRectFilled(pos0, pos1, IM_COL32(128, 128, 128, 32));
            window->DrawList->AddRect(pos0, pos1, IM_COL32(128, 128, 128, 128));
        }
    }

    // Text overlay
    if (conf.overlay_text)
        RenderTextClipped(ImVec2(overlay_bb.Min.x, overlay_bb.Min.y), overlay_bb.Max, conf.overlay_text, NULL, NULL, ImVec2(0.5f,0.0f));

    return status;
}
}
