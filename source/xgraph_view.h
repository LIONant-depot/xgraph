#ifndef XGRAPH_VIEW_H
#define XGRAPH_VIEW_H
#pragma once

// xgraph's canvas frame (ImGui; the layout and routes headers stay ImGui-free): the view a graph is drawn in - pan (right drag; a right
// click without a drag is reported, for a menu), wheel zoom anchored under the cursor, Home resets - its grid, and the routed wires drawn
// crisp (on pixel centres, crossings broken), lit per route (selection, hover, a trace) over the dimmed rest. A domain draws its steps
// with ToScreen / Len / Text; this owns no document. The material stack's canvas look (xmaterial_stack_canvas.h), shared.
#include "xgraph_routes.h"
#include "dependencies/imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace xgraph::view
{
    struct style
    {
        ImU32 m_Canvas  = IM_COL32( 59,  59,  67, 255);
        ImU32 m_Grid    = IM_COL32(120, 120, 120,  40);    // faded by zoom squared
        float m_GridStep = 32.0f;                           // world units
        float m_Dim = 0.55f, m_DimTrace = 0.15f;            // unlit routes' alpha (a trace dims the rest further)
        float m_WireW = 1.5f, m_LitW = 2.5f;                // screen px
        float m_ZoomMin = 0.5f, m_ZoomMax = 1.5f;
        float m_Pick = 5.0f;                                // a click this close to a route picks it, screen px
    };

    inline ImU32 Alpha(ImU32 C, float a) noexcept { return (C & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(a, 0.0f, 1.0f) * ((C >> IM_COL32_A_SHIFT) & 0xFF)) << IM_COL32_A_SHIFT); }

    struct view
    {
        ImVec2  m_Origin{}, m_Pan{}, m_Max{};
        float   m_Zoom = 1.0f;
        bool    m_bPanning = false;
        float   m_PanTravel = 0;

        ImVec2 ToScreen(float x, float y) const noexcept { return { m_Origin.x + m_Pan.x + x * m_Zoom, m_Origin.y + m_Pan.y + y * m_Zoom }; }
        ImVec2 ToWorld(ImVec2 p) const noexcept { return { (p.x - m_Origin.x - m_Pan.x) / m_Zoom, (p.y - m_Origin.y - m_Pan.y) / m_Zoom }; }
        float  Len(float l) const noexcept { return l * m_Zoom; }
        static float Snap(float v) noexcept { return std::floor(v) + 0.5f; }       // a thin line on a pixel centre reads crisp

        // The frame's input, at the start of a canvas child (the cursor at its top left). True: a right click with no drag (open the menu
        // of what is under the mouse)
        bool Input(const style& S, ImVec2 Avail) noexcept
        {
            m_Origin = ImGui::GetCursorScreenPos();
            m_Max    = { m_Origin.x + Avail.x, m_Origin.y + Avail.y };
            auto& IO = ImGui::GetIO();
            const bool bInside = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(m_Origin, m_Max);
            if (bInside && IO.MouseWheel != 0)
            {
                const ImVec2 W = ToWorld(IO.MousePos);
                m_Zoom = std::clamp(std::round((m_Zoom + (IO.MouseWheel > 0 ? 0.1f : -0.1f)) * 10.0f) / 10.0f, S.m_ZoomMin, S.m_ZoomMax);
                m_Pan  = { IO.MousePos.x - m_Origin.x - W.x * m_Zoom, IO.MousePos.y - m_Origin.y - W.y * m_Zoom };
            }
            if (bInside && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { m_bPanning = true; m_PanTravel = 0; }
            bool bClick = false;
            if (m_bPanning)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) { m_Pan.x += IO.MouseDelta.x; m_Pan.y += IO.MouseDelta.y; m_PanTravel += std::abs(IO.MouseDelta.x) + std::abs(IO.MouseDelta.y); }
                else { m_bPanning = false; bClick = m_PanTravel < 3; }
            }
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Home, false)) { m_Zoom = 1; m_Pan = {}; }
            return bClick;
        }

        void DrawBackground(ImDrawList* D, const style& S) const noexcept
        {
            D->AddRectFilled(m_Origin, m_Max, S.m_Canvas);
            const ImU32 Grid = Alpha(S.m_Grid, std::min(1.0f, m_Zoom * m_Zoom));
            const ImVec2 W0 = ToWorld(m_Origin), W1 = ToWorld(m_Max);
            for (int i = static_cast<int>(std::floor(W0.x / S.m_GridStep)); i <= static_cast<int>(std::ceil(W1.x / S.m_GridStep)); ++i)
            { const float x = Snap(ToScreen(i * S.m_GridStep, 0).x); D->AddLine({ x, m_Origin.y }, { x, m_Max.y }, Grid); }
            for (int i = static_cast<int>(std::floor(W0.y / S.m_GridStep)); i <= static_cast<int>(std::ceil(W1.y / S.m_GridStep)); ++i)
            { const float y = Snap(ToScreen(0, i * S.m_GridStep).y); D->AddLine({ m_Origin.x, y }, { m_Max.x, y }, Grid); }
        }

        // An axis aligned world segment in screen space: on a pixel centre, the ends pushed out half the width so corners close
        void Line(ImDrawList* D, float x0, float y0, float x1, float y1, ImU32 C, float Th) const noexcept
        {
            ImVec2 a = ToScreen(std::min(x0, x1), std::min(y0, y1)), b = ToScreen(std::max(x0, x1), std::max(y0, y1));
            const float e = Th * 0.5f;
            if (y0 == y1) { a.x -= e; b.x += e; a.y = b.y = Snap(a.y); }
            else          { a.y -= e; b.y += e; a.x = b.x = Snap(a.x); }
            D->AddLine(a, b, C, Th);
        }

        // Every wire (merged per source, crossings broken) dimmed, then the lit routes over them: Lit[edge] 0 = not lit, else its alpha;
        // SourceColor[source] (routes::result::m_Sources order)
        void DrawRoutes(ImDrawList* D, const style& S, const routes::result& R, const std::vector<ImU32>& SourceColor, const std::vector<float>& Lit, bool bTrace = false) const noexcept
        {
            const float Dim = bTrace ? S.m_DimTrace : S.m_Dim;
            for (auto& W : R.m_Wires) Line(D, W.m_X0, W.m_Y0, W.m_X1, W.m_Y1, Alpha(SourceColor[W.m_Source], Dim), S.m_WireW);
            for (auto& J : R.m_Junctions) D->AddCircleFilled(ToScreen(J.m_X, J.m_Y), 2.5f, Alpha(SourceColor[J.m_Source], Dim));
            std::vector<float> Levels(Lit.begin(), Lit.end());
            std::sort(Levels.begin(), Levels.end()); Levels.erase(std::unique(Levels.begin(), Levels.end()), Levels.end());
            for (const float Level : Levels)                                            // brightest on top
            {
                if (Level <= 0) continue;
                for (int e = 0; e < static_cast<int>(Lit.size()); ++e)
                {
                    if (Lit[e] != Level || R.m_Routes[e].m_Points.empty()) continue;
                    const ImU32 C = Alpha(SourceColor[R.m_RouteSource[e]], Level);
                    const auto& P = R.m_Routes[e].m_Points;
                    const float Th = Level >= 1.0f ? S.m_LitW : S.m_WireW + 0.5f;
                    for (std::size_t k = 0; k + 1 < P.size(); ++k)          // (along its source's wires, so the crossing gaps stay)
                    {
                        const auto s = routes::Seg(P[k].m_X, P[k].m_Y, P[k + 1].m_X, P[k + 1].m_Y, 0);
                        const bool bH = s.m_Y0 == s.m_Y1;
                        for (const auto& W : R.m_Wires)
                        {
                            if (W.m_Source != R.m_RouteSource[e] || bH != (W.m_Y0 == W.m_Y1)) continue;
                            if (bH  && W.m_Y0 == s.m_Y0) if (const float a = std::max(W.m_X0, s.m_X0), b = std::min(W.m_X1, s.m_X1); a < b) Line(D, a, s.m_Y0, b, s.m_Y0, C, Th);
                            if (!bH && W.m_X0 == s.m_X0) if (const float a = std::max(W.m_Y0, s.m_Y0), b = std::min(W.m_Y1, s.m_Y1); a < b) Line(D, s.m_X0, a, s.m_X0, b, C, Th);
                        }
                    }
                    for (auto& J : R.m_Junctions)
                        if (J.m_Source == R.m_RouteSource[e] && routes::OnPolyline(P, J.m_X, J.m_Y)) D->AddCircleFilled(ToScreen(J.m_X, J.m_Y), 3.5f, C);
                }
            }
        }

        // The route under the mouse: the nearest drawn segment within the style's pick distance (-1: none)
        int RouteAt(const style& S, const routes::result& R, ImVec2 Mouse) const noexcept
        {
            int Best = -1;
            float BestD = S.m_Pick;
            for (int e = 0; e < static_cast<int>(R.m_Routes.size()); ++e)
            {
                const auto& P = R.m_Routes[e].m_Points;
                for (std::size_t k = 0; k + 1 < P.size(); ++k)
                {
                    const ImVec2 a = ToScreen(P[k].m_X, P[k].m_Y), b = ToScreen(P[k + 1].m_X, P[k + 1].m_Y);
                    const float dx = std::max({ std::min(a.x, b.x) - Mouse.x, 0.0f, Mouse.x - std::max(a.x, b.x) });
                    const float dy = std::max({ std::min(a.y, b.y) - Mouse.y, 0.0f, Mouse.y - std::max(a.y, b.y) });
                    if (const float d = std::max(dx, dy); d < BestD) { BestD = d; Best = e; }
                }
            }
            return Best;
        }
    };
}

#endif // XGRAPH_VIEW_H
