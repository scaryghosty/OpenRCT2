/*****************************************************************************
 * Copyright (c) 2014-2024 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../UiStringIds.h"

#include <openrct2-ui/interface/Graph.h>
#include <openrct2/Date.h>
#include <openrct2/localisation/Formatter.h>
#include <openrct2/localisation/Formatting.h>
#include <openrct2/localisation/Localisation.Date.h>
#include <openrct2/world/Park.h>

namespace OpenRCT2::Graph
{
    constexpr int32_t kDashLength = 2;

    template<typename T>
    static void DrawYLabels(
        DrawPixelInfo& dpi, const ScreenRect& internalBounds, const T min, const T max, const int32_t numYLabels,
        const int32_t yLabelStepPx, const ColourWithFlags lineCol, const FmtString& fmt)
    {
        T curLabel = max;
        int32_t curScreenPos = internalBounds.GetTop() - 5;
        const T yLabelStep = (max - min) / (numYLabels - 1);
        for (int32_t i = 0; i < numYLabels; i++)
        {
            char buffer[64]{};
            FormatStringToBuffer(buffer, sizeof(buffer), fmt, curLabel);
            DrawText(dpi, { internalBounds.GetLeft(), curScreenPos }, { FontStyle::Small, TextAlignment::RIGHT }, buffer);
            GfxFillRectInset(
                dpi, { { internalBounds.GetLeft(), curScreenPos + 5 }, { internalBounds.GetRight(), curScreenPos + 5 } },
                lineCol, INSET_RECT_FLAG_BORDER_INSET);
            curScreenPos += yLabelStepPx;
            curLabel -= yLabelStep;
        }
    }

    template<typename T, T TkNoValue>
    static void DrawMonths(DrawPixelInfo& dpi, const T* series, int32_t count, const ScreenRect& bounds, const int32_t xStep)
    {
        auto& date = GetDate();
        int32_t currentMonth = date.GetMonth();
        int32_t currentDay = date.GetMonthTicks();
        int32_t yearOver32 = (currentMonth * 4) + (currentDay >> 14) - 31;
        auto screenCoords = bounds.Point1;
        for (int32_t i = count - 1; i >= 0; i--)
        {
            if (series[i] != TkNoValue && yearOver32 % 4 == 0)
            {
                // Draw month text
                auto ft = Formatter();
                ft.Add<StringId>(DateGameShortMonthNames[DateGetMonth((yearOver32 / 4) + MONTH_COUNT)]);
                DrawTextBasic(
                    dpi, screenCoords - ScreenCoordsXY{ 0, 14 }, STR_GRAPH_LABEL, ft,
                    { FontStyle::Small, TextAlignment::CENTRE });

                // Draw month mark
                GfxFillRect(
                    dpi, { screenCoords - ScreenCoordsXY{ 0, 4 }, screenCoords - ScreenCoordsXY{ 0, 1 } }, PALETTE_INDEX_10);
            }

            yearOver32 = (yearOver32 + 1) % 32;
            screenCoords.x += xStep;
        }
    }

    template<typename T>
    static void DrawHoveredValue(
        DrawPixelInfo& dpi, const T value, const int32_t hoverIdx, const ScreenRect& bounds, const int32_t xStep,
        const T minValue, const T maxValue, const_utf8string text, ColourWithFlags textCol)
    {
        const int32_t screenRange = bounds.GetHeight();
        const int32_t valueRange = maxValue - minValue;

        const int32_t yPosition = bounds.GetBottom() - ((value - minValue) * screenRange) / valueRange;
        ScreenCoordsXY coords = { bounds.GetRight() - hoverIdx * xStep, yPosition };

        GfxDrawDashedLine(
            dpi,
            {
                { coords.x, bounds.GetTop() },
                { coords.x, bounds.GetBottom() },
            },
            kDashLength, PALETTE_INDEX_10);
        GfxDrawDashedLine(dpi, { { bounds.GetLeft(), coords.y }, coords }, kDashLength, PALETTE_INDEX_10);

        Formatter ft;
        ft.Add<money64>(value);
        DrawText(dpi, coords - ScreenCoordsXY{ 0, 16 }, { textCol, TextAlignment::CENTRE }, text);

        GfxFillRect(dpi, { { coords - ScreenCoordsXY{ 2, 2 } }, coords + ScreenCoordsXY{ 2, 2 } }, PALETTE_INDEX_10);
        GfxFillRect(dpi, { { coords - ScreenCoordsXY{ 1, 1 } }, { coords + ScreenCoordsXY{ 1, 1 } } }, PALETTE_INDEX_21);
    }

    template<typename T, T TkNoValue, bool TbackgroundLine>
    static void DrawLine(
        DrawPixelInfo& dpi, const T* series, const int32_t count, const ScreenRect& bounds, const int32_t xStep,
        const T minValue, const T maxValue)
    {
        const int32_t screenRange = bounds.GetHeight();
        const int32_t valueRange = maxValue - minValue;

        ScreenCoordsXY lastCoords;
        bool lastCoordsValid = false;
        ScreenCoordsXY coords = bounds.Point1;
        for (int32_t i = count - 1; i >= 0; i--)
        {
            auto value = series[i];
            if (value != TkNoValue)
            {
                coords.y = bounds.GetBottom() - ((value - minValue) * screenRange) / valueRange;

                if constexpr (TbackgroundLine)
                {
                    if (lastCoordsValid)
                    {
                        auto leftTop1 = lastCoords + ScreenCoordsXY{ 1, 1 };
                        auto rightBottom1 = coords + ScreenCoordsXY{ 1, 1 };
                        auto leftTop2 = lastCoords + ScreenCoordsXY{ 0, 1 };
                        auto rightBottom2 = coords + ScreenCoordsXY{ 0, 1 };
                        GfxDrawLine(dpi, { leftTop1, rightBottom1 }, PALETTE_INDEX_10);
                        GfxDrawLine(dpi, { leftTop2, rightBottom2 }, PALETTE_INDEX_10);
                    }
                    if (i == 0)
                    {
                        GfxFillRect(dpi, { coords, coords + ScreenCoordsXY{ 2, 2 } }, PALETTE_INDEX_10);
                    }
                }
                else
                {
                    if (lastCoordsValid)
                    {
                        auto leftTop = lastCoords;
                        auto rightBottom = coords;
                        GfxDrawLine(dpi, { leftTop, rightBottom }, PALETTE_INDEX_21);
                    }
                    if (i == 0)
                    {
                        GfxFillRect(
                            dpi, { coords - ScreenCoordsXY{ 1, 1 }, coords + ScreenCoordsXY{ 1, 1 } }, PALETTE_INDEX_21);
                    }
                }

                lastCoords = coords;
                lastCoordsValid = true;
            }
            coords.x += xStep;
        }
    }

    void DrawFinanceGraph(DrawPixelInfo& dpi, const GraphProperties<money64>& p)
    {
        const FmtString fmt("{BLACK}{CURRENCY2DP} -");
        DrawYLabels<money64>(dpi, p.internalBounds, p.min, p.max, p.numYLabels, p.yLabelStepPx, p.lineCol, fmt);
        DrawMonths<money64, kMoney64Undefined>(dpi, p.series, p.numPoints, p.internalBounds, p.xStepPx);
        DrawLine<money64, kMoney64Undefined, true>(dpi, p.series, p.numPoints, p.internalBounds, p.xStepPx, p.min, p.max);
        DrawLine<money64, kMoney64Undefined, false>(dpi, p.series, p.numPoints, p.internalBounds, p.xStepPx, p.min, p.max);
        if (p.hoverIdx >= 0 && p.hoverIdx < p.numPoints)
        {
            const money64 value = p.series[p.hoverIdx];
            if (value != kMoney64Undefined)
            {
                char buffer[64]{};
                FormatStringToBuffer(buffer, sizeof(buffer), "{CURRENCY2DP}", value);
                DrawHoveredValue<money64>(
                    dpi, value, p.hoverIdx, p.internalBounds, p.xStepPx, p.min, p.max, buffer,
                    p.lineCol.withFlag(ColourFlag::withOutline, true));
            }
        }
    }

    void DrawRatingGraph(DrawPixelInfo& dpi, const GraphProperties<uint8_t>& p)
    {
        constexpr uint8_t noValue = ParkRatingHistoryUndefined;
        const FmtString fmt("{BLACK}{COMMA32} -");
        // Since the park rating rating history is divided by 4, we have to fudge the max number here.
        DrawYLabels<uint16_t>(dpi, p.internalBounds, p.min, 1000, p.numYLabels, p.yLabelStepPx, p.lineCol, fmt);
        DrawMonths<uint8_t, noValue>(dpi, p.series, p.numPoints, p.internalBounds, p.xStepPx);
        DrawLine<uint8_t, noValue, true>(dpi, p.series, p.numPoints, p.internalBounds, p.xStepPx, p.min, p.max);
        DrawLine<uint8_t, noValue, false>(dpi, p.series, p.numPoints, p.internalBounds, p.xStepPx, p.min, p.max);
    }

    void DrawGuestGraph(DrawPixelInfo& dpi, const GraphProperties<uint32_t>& p)
    {
        constexpr uint32_t noValue = GuestsInParkHistoryUndefined;
        const FmtString fmt("{BLACK}{COMMA32} -");
        DrawYLabels<uint32_t>(dpi, p.internalBounds, p.min, p.max, p.numYLabels, p.yLabelStepPx, p.lineCol, fmt);
        DrawMonths<uint32_t, noValue>(dpi, p.series, p.numPoints, p.internalBounds, p.xStepPx);
        DrawLine<uint32_t, noValue, true>(dpi, p.series, p.numPoints, p.internalBounds, p.xStepPx, p.min, p.max);
        DrawLine<uint32_t, noValue, false>(dpi, p.series, p.numPoints, p.internalBounds, p.xStepPx, p.min, p.max);
    }
} // namespace OpenRCT2::Graph
