/*****************************************************************************
 * Copyright (c) 2014-2024 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <openrct2-ui/interface/LandTool.h>
#include <openrct2-ui/interface/Viewport.h>
#include <openrct2-ui/interface/ViewportInteraction.h>
#include <openrct2-ui/interface/ViewportQuery.h>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/windows/Window.h>
#include <openrct2/Cheats.h>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/Input.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/actions/LandSetRightsAction.h>
#include <openrct2/actions/MapChangeSizeAction.h>
#include <openrct2/actions/PeepSpawnPlaceAction.h>
#include <openrct2/actions/SurfaceSetStyleAction.h>
#include <openrct2/audio/audio.h>
#include <openrct2/entity/EntityList.h>
#include <openrct2/entity/EntityRegistry.h>
#include <openrct2/entity/Staff.h>
#include <openrct2/object/TerrainSurfaceObject.h>
#include <openrct2/ride/RideData.h>
#include <openrct2/ride/Track.h>
#include <openrct2/ride/TrainManager.h>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/world/Footpath.h>
#include <openrct2/world/Scenery.h>
#include <openrct2/world/Surface.h>
#include <openrct2/world/tile_element/Slope.h>
#include <vector>

namespace OpenRCT2::Ui::Windows
{
    static constexpr uint16_t MapColour2(uint8_t colourA, uint8_t colourB)
    {
        return (colourA << 8) | colourB;
    }
    static constexpr uint16_t MapColour(uint8_t colour)
    {
        return MapColour2(colour, colour);
    }
    static constexpr uint16_t MapColourUnowned(uint16_t colour)
    {
        return MapColour2((colour & 0xFF00) >> 8, PALETTE_INDEX_10);
    }
    static int32_t getTechnicalMapSize()
    {
        // Take non-square maps into account
        return std::max(GetGameState().MapSize.x, GetGameState().MapSize.y) - 2;
    }
    static int32_t getTechnicalMapSizeBig()
    {
        return getTechnicalMapSize() * kCoordsXYStep;
    }
    static int32_t getMaxTileStartXY()
    {
        return getTechnicalMapSizeBig() - kCoordsXYStep;
    }
    static int32_t getMiniMapWidth()
    {
        return getTechnicalMapSize() * 2;
    }

    static constexpr StringId WINDOW_TITLE = STR_MAP_LABEL;
    static constexpr int32_t WH = 259;
    static constexpr int32_t WW = 245;

    static constexpr uint16_t kReservedHSpace = 6;
    static constexpr uint16_t kReservedTopSpace = 46;
    static constexpr uint16_t kScenarioEditorReservedSpace = 72;
    static constexpr uint16_t kRidesTabReservedSpace = 4 * kListRowHeight + 4;
    static constexpr uint16_t kDefaultReservedSpace = 14;

    static int32_t getMapOffset(int16_t width)
    {
        return (width - getMiniMapWidth() - kReservedHSpace - SCROLLBAR_SIZE) / 2;
    }

    // Some functions manipulate coordinates on the map. These are the coordinates of the pixels in the
    // minimap. In order to distinguish those from actual coordinates, we use a separate name.
    using MapCoordsXY = TileCoordsXY;

    enum
    {
        PAGE_PEEPS,
        PAGE_RIDES
    };

    enum WindowMapWidgetIdx : WidgetIndex
    {
        WIDX_BACKGROUND,
        WIDX_TITLE,
        WIDX_CLOSE,
        WIDX_RESIZE,
        WIDX_PEOPLE_TAB,
        WIDX_RIDES_TAB,
        WIDX_MAP,
        WIDX_MAP_SIZE_SPINNER_Y,
        WIDX_MAP_SIZE_SPINNER_Y_UP,
        WIDX_MAP_SIZE_SPINNER_Y_DOWN,
        WIDX_MAP_SIZE_LINK,
        WIDX_MAP_SIZE_SPINNER_X,
        WIDX_MAP_SIZE_SPINNER_X_UP,
        WIDX_MAP_SIZE_SPINNER_X_DOWN,
        WIDX_SET_LAND_RIGHTS,
        WIDX_BUILD_PARK_ENTRANCE,
        WIDX_PEOPLE_STARTING_POSITION,
        WIDX_LAND_TOOL,
        WIDX_LAND_TOOL_SMALLER,
        WIDX_LAND_TOOL_LARGER,
        WIDX_LAND_OWNED_CHECKBOX,
        WIDX_CONSTRUCTION_RIGHTS_OWNED_CHECKBOX,
        WIDX_LAND_SALE_CHECKBOX,
        WIDX_CONSTRUCTION_RIGHTS_SALE_CHECKBOX,
        WIDX_MAP_GENERATOR
    };

    // clang-format off
static Widget window_map_widgets[] = {
    WINDOW_SHIM(WINDOW_TITLE, WW, WH),
    MakeWidget        ({  0,  43}, {245, 215}, WindowWidgetType::Resize,    WindowColour::Secondary                                                                                  ),
    MakeRemapWidget   ({  3,  17}, { 31,  27}, WindowWidgetType::ColourBtn, WindowColour::Secondary, SPR_TAB,                         STR_SHOW_PEOPLE_ON_MAP_TIP                     ),
    MakeRemapWidget   ({ 34,  17}, { 31,  27}, WindowWidgetType::ColourBtn, WindowColour::Secondary, SPR_TAB,                         STR_SHOW_RIDES_STALLS_ON_MAP_TIP               ),
    MakeWidget        ({  3,  46}, {239, 180}, WindowWidgetType::Scroll,    WindowColour::Secondary, SCROLL_BOTH                                                                     ),
    MakeSpinnerWidgets({102, 229}, { 50,  12}, WindowWidgetType::Spinner,   WindowColour::Secondary, STR_COMMA16                                                                     ), // NB: 3 widgets
    MakeWidget        ({153, 230}, { 20,  12}, WindowWidgetType::FlatBtn,   WindowColour::Secondary, ImageId(SPR_G2_LINK_CHAIN),               STR_MAINTAIN_SQUARE_MAP_TOOLTIP                ),
    MakeSpinnerWidgets({174, 229}, { 50,  12}, WindowWidgetType::Spinner,   WindowColour::Secondary, STR_POP16_COMMA16                                                               ), // NB: 3 widgets
    MakeWidget        ({  4,   1}, { 24,  24}, WindowWidgetType::FlatBtn,   WindowColour::Secondary, ImageId(SPR_BUY_LAND_RIGHTS),             STR_SELECT_PARK_OWNED_LAND_TIP                 ),
    MakeWidget        ({  4,   1}, { 24,  24}, WindowWidgetType::FlatBtn,   WindowColour::Secondary, ImageId(SPR_PARK_ENTRANCE),               STR_BUILD_PARK_ENTRANCE_TIP                    ),
    MakeWidget        ({ 28,   1}, { 24,  24}, WindowWidgetType::FlatBtn,   WindowColour::Secondary, 0xFFFFFFFF,                      STR_SET_STARTING_POSITIONS_TIP                 ),
    MakeWidget        ({  4,  17}, { 44,  32}, WindowWidgetType::ImgBtn,    WindowColour::Secondary, ImageId(SPR_LAND_TOOL_SIZE_0)                                                            ),
    MakeRemapWidget   ({  5,  18}, { 16,  16}, WindowWidgetType::TrnBtn,    WindowColour::Secondary, SPR_LAND_TOOL_DECREASE,          STR_ADJUST_SMALLER_LAND_TIP                    ),
    MakeRemapWidget   ({ 31,  32}, { 16,  16}, WindowWidgetType::TrnBtn,    WindowColour::Secondary, SPR_LAND_TOOL_INCREASE,          STR_ADJUST_LARGER_LAND_TIP                     ),
    MakeWidget        ({ 58, 197}, {184,  12}, WindowWidgetType::Checkbox,  WindowColour::Secondary, STR_LAND_OWNED,                  STR_SET_LAND_TO_BE_OWNED_TIP                   ),
    MakeWidget        ({ 58, 197}, {184,  12}, WindowWidgetType::Checkbox,  WindowColour::Secondary, STR_CONSTRUCTION_RIGHTS_OWNED,   STR_SET_CONSTRUCTION_RIGHTS_TO_BE_OWNED_TIP    ),
    MakeWidget        ({ 58, 197}, {184,  12}, WindowWidgetType::Checkbox,  WindowColour::Secondary, STR_LAND_SALE,                   STR_SET_LAND_TO_BE_AVAILABLE_TIP               ),
    MakeWidget        ({ 58, 197}, {174,  12}, WindowWidgetType::Checkbox,  WindowColour::Secondary, STR_CONSTRUCTION_RIGHTS_SALE,    STR_SET_CONSTRUCTION_RIGHTS_TO_BE_AVAILABLE_TIP),
    MakeWidget        ({110, 189}, {131,  14}, WindowWidgetType::Button,    WindowColour::Secondary, STR_MAPGEN_WINDOW_TITLE,         STR_MAP_GENERATOR_TIP                          ),
    kWidgetsEnd,
};
    // clang-format on

    // These represent a coefficient for the map size to be multiplied
    // Used in transforming viewport view coordinates to minimap coordinates
    // rct2: 0x00981BBC (analogous)
    static constexpr ScreenCoordsXY MiniMapOffsetFactors[] = {
        { 1, 0 },
        { 2, 1 },
        { 1, 2 },
        { 0, 1 },
    };

    static constexpr StringId MapLabels[] = {
        STR_MAP_RIDE,       STR_MAP_FOOD_STALL, STR_MAP_DRINK_STALL,  STR_MAP_SOUVENIR_STALL,
        STR_MAP_INFO_KIOSK, STR_MAP_FIRST_AID,  STR_MAP_CASH_MACHINE, STR_MAP_TOILET,
    };

    static constexpr uint16_t RideKeyColours[] = {
        MapColour(PALETTE_INDEX_61),  // COLOUR_KEY_RIDE
        MapColour(PALETTE_INDEX_42),  // COLOUR_KEY_FOOD
        MapColour(PALETTE_INDEX_20),  // COLOUR_KEY_DRINK
        MapColour(PALETTE_INDEX_209), // COLOUR_KEY_SOUVENIR
        MapColour(PALETTE_INDEX_136), // COLOUR_KEY_KIOSK
        MapColour(PALETTE_INDEX_102), // COLOUR_KEY_FIRST_AID
        MapColour(PALETTE_INDEX_55),  // COLOUR_KEY_CASH_MACHINE
        MapColour(PALETTE_INDEX_161), // COLOUR_KEY_TOILETS
    };

    static constexpr uint8_t DefaultPeepMapColour = PALETTE_INDEX_20;
    static constexpr uint8_t GuestMapColour = PALETTE_INDEX_172;
    static constexpr uint8_t GuestMapColourAlternate = PALETTE_INDEX_21;
    static constexpr uint8_t StaffMapColour = PALETTE_INDEX_138;
    static constexpr uint8_t StaffMapColourAlternate = PALETTE_INDEX_10;

    static constexpr uint16_t WaterColour = MapColour(PALETTE_INDEX_195);

    static constexpr uint16_t ElementTypeMaskColour[] = {
        0xFFFF, // TILE_ELEMENT_TYPE_SURFACE
        0x0000, // TILE_ELEMENT_TYPE_PATH
        0x00FF, // TILE_ELEMENT_TYPE_TRACK
        0xFF00, // TILE_ELEMENT_TYPE_SMALL_SCENERY
        0x0000, // TILE_ELEMENT_TYPE_ENTRANCE
        0xFFFF, // TILE_ELEMENT_TYPE_WALL
        0x0000, // TILE_ELEMENT_TYPE_LARGE_SCENERY
        0xFFFF, // TILE_ELEMENT_TYPE_BANNER
    };

    static constexpr uint16_t ElementTypeAddColour[] = {
        MapColour(PALETTE_INDEX_0),                     // TILE_ELEMENT_TYPE_SURFACE
        MapColour(PALETTE_INDEX_17),                    // TILE_ELEMENT_TYPE_PATH
        MapColour2(PALETTE_INDEX_183, PALETTE_INDEX_0), // TILE_ELEMENT_TYPE_TRACK
        MapColour2(PALETTE_INDEX_0, PALETTE_INDEX_99),  // TILE_ELEMENT_TYPE_SMALL_SCENERY
        MapColour(PALETTE_INDEX_186),                   // TILE_ELEMENT_TYPE_ENTRANCE
        MapColour(PALETTE_INDEX_0),                     // TILE_ELEMENT_TYPE_WALL
        MapColour(PALETTE_INDEX_99),                    // TILE_ELEMENT_TYPE_LARGE_SCENERY
        MapColour(PALETTE_INDEX_0),                     // TILE_ELEMENT_TYPE_BANNER
    };

    namespace MapFlashingFlags
    {
        constexpr uint16_t FlashGuests = (1 << 1);
        constexpr uint16_t FlashStaff = (1 << 3);
        constexpr uint16_t SwitchColour = (1 << 15); // Every couple ticks the colour switches
    }                                                // namespace MapFlashingFlags

    class MapWindow final : public Window
    {
        uint8_t _rotation;
        uint8_t _activeTool;
        uint32_t _currentLine;
        uint16_t _landRightsToolSize;
        int32_t _firstColumnWidth;
        std::vector<uint8_t> _mapImageData;
        bool _mapWidthAndHeightLinked{ true };
        bool _recalculateScrollbars = false;
        enum class ResizeDirection
        {
            Both,
            X,
            Y,
        } _resizeDirection{ ResizeDirection::Both };
        uint16_t _flashingFlags = 0;

    public:
        void OnOpen() override
        {
            widgets = window_map_widgets;

            hold_down_widgets = (1uLL << WIDX_MAP_SIZE_SPINNER_Y_UP) | (1uLL << WIDX_MAP_SIZE_SPINNER_Y_DOWN)
                | (1uLL << WIDX_MAP_SIZE_SPINNER_X_UP) | (1uLL << WIDX_MAP_SIZE_SPINNER_X_DOWN)
                | (1uLL << WIDX_LAND_TOOL_LARGER) | (1uLL << WIDX_LAND_TOOL_SMALLER);

            flags |= WF_RESIZABLE;
            min_width = WW;
            min_height = WH;

            SetInitialWindowDimensions();
            ResetMaxWindowDimensions();
            ResizeMiniMap();
            InitScrollWidgets();
            CalculateTextLayout();

            _rotation = GetCurrentRotation();

            InitMap();
            gWindowSceneryRotation = 0;
            CentreMapOnViewPoint();
            FootpathSelectDefault();

            auto& gameState = GetGameState();
            _mapWidthAndHeightLinked = gameState.MapSize.x == gameState.MapSize.y;

            // Reset land rights tool size
            _landRightsToolSize = 1;
        }

        void OnClose() override
        {
            _mapImageData.clear();
            _mapImageData.shrink_to_fit();

            if (isToolActive(classification, number))
            {
                ToolCancel();
            }
        }

        void OnMouseUp(WidgetIndex widgetIndex) override
        {
            switch (widgetIndex)
            {
                case WIDX_CLOSE:
                    Close();
                    break;
                case WIDX_SET_LAND_RIGHTS:
                    Invalidate();
                    if (ToolSet(*this, widgetIndex, Tool::UpArrow))
                        break;
                    _activeTool = 2;
                    // Prevent mountain tool size.
                    _landRightsToolSize = std::max<uint16_t>(kLandToolMinimumSize, _landRightsToolSize);
                    ShowGridlines();
                    ShowLandRights();
                    ShowConstructionRights();
                    break;
                case WIDX_LAND_OWNED_CHECKBOX:
                    _activeTool ^= 2;

                    if (_activeTool & 2)
                        _activeTool &= 0xF2;

                    Invalidate();
                    break;
                case WIDX_LAND_SALE_CHECKBOX:
                    _activeTool ^= 8;

                    if (_activeTool & 8)
                        _activeTool &= 0xF8;

                    Invalidate();
                    break;
                case WIDX_CONSTRUCTION_RIGHTS_OWNED_CHECKBOX:
                    _activeTool ^= 1;

                    if (_activeTool & 1)
                        _activeTool &= 0xF1;

                    Invalidate();
                    break;
                case WIDX_CONSTRUCTION_RIGHTS_SALE_CHECKBOX:
                    _activeTool ^= 4;

                    if (_activeTool & 4)
                        _activeTool &= 0xF4;

                    Invalidate();
                    break;
                case WIDX_BUILD_PARK_ENTRANCE:
                {
                    if (!WindowFindByClass(WindowClass::EditorParkEntrance))
                        ContextOpenWindow(WindowClass::EditorParkEntrance);
                    else
                        WindowCloseByClass(WindowClass::EditorParkEntrance);
                    break;
                }
                case WIDX_PEOPLE_STARTING_POSITION:
                    if (ToolSet(*this, widgetIndex, Tool::UpArrow))
                        break;

                    ShowGridlines();
                    ShowLandRights();
                    ShowConstructionRights();
                    break;
                case WIDX_LAND_TOOL:
                    InputLandSize();
                    break;
                case WIDX_MAP_SIZE_SPINNER_Y:
                case WIDX_MAP_SIZE_SPINNER_X:
                    InputMapSize(widgetIndex);
                    break;
                case WIDX_MAP_SIZE_LINK:
                    _mapWidthAndHeightLinked = !_mapWidthAndHeightLinked;
                    break;
                case WIDX_MAP_GENERATOR:
                    ContextOpenWindow(WindowClass::Mapgen);
                    break;
                default:
                    if (widgetIndex >= WIDX_PEOPLE_TAB && widgetIndex <= WIDX_RIDES_TAB)
                    {
                        widgetIndex -= WIDX_PEOPLE_TAB;
                        if (widgetIndex == selected_tab)
                            break;

                        selected_tab = widgetIndex;
                        list_information_type = 0;
                        _recalculateScrollbars = true;
                        ResetMaxWindowDimensions();
                    }
            }
        }

        void OnMouseDown(WidgetIndex widgetIndex) override
        {
            switch (widgetIndex)
            {
                case WIDX_MAP_SIZE_SPINNER_Y_UP:
                    _resizeDirection = ResizeDirection::Y;
                    IncreaseMapSize();
                    break;
                case WIDX_MAP_SIZE_SPINNER_Y_DOWN:
                    _resizeDirection = ResizeDirection::Y;
                    DecreaseMapSize();
                    break;
                case WIDX_MAP_SIZE_SPINNER_X_UP:
                    _resizeDirection = ResizeDirection::X;
                    IncreaseMapSize();
                    break;
                case WIDX_MAP_SIZE_SPINNER_X_DOWN:
                    _resizeDirection = ResizeDirection::X;
                    DecreaseMapSize();
                    break;
                case WIDX_LAND_TOOL_SMALLER:
                    // Decrement land rights tool size
                    _landRightsToolSize = std::max<uint16_t>(kLandToolMinimumSize, _landRightsToolSize - 1);

                    Invalidate();
                    break;
                case WIDX_LAND_TOOL_LARGER:
                    // Increment land rights tool size
                    _landRightsToolSize = std::min<uint16_t>(kLandToolMaximumSize, _landRightsToolSize + 1);

                    Invalidate();
                    break;
            }
        }

        void OnUpdate() override
        {
            // the flickering frequency is reduced by 4, compared to the original
            // it was done due to inability to reproduce original frequency
            // and decision that the original one looks too fast
            if (gCurrentRealTimeTicks % 4 == 0)
                _flashingFlags ^= MapFlashingFlags::SwitchColour;

            // Handle guest map flashing
            _flashingFlags &= ~MapFlashingFlags::FlashGuests;
            if (WindowFindByClass(WindowClass::GuestList) != nullptr)
                _flashingFlags |= MapFlashingFlags::FlashGuests;

            // Handle staff map flashing
            _flashingFlags &= ~MapFlashingFlags::FlashStaff;
            if (WindowFindByClass(WindowClass::StaffList) != nullptr)
                _flashingFlags |= MapFlashingFlags::FlashStaff;

            if (GetCurrentRotation() != _rotation)
            {
                _rotation = GetCurrentRotation();
                InitMap();
                CentreMapOnViewPoint();
            }

            for (int32_t i = 0; i < 16; i++)
                SetMapPixels();

            Invalidate();

            // Update tab animations
            list_information_type++;
            switch (selected_tab)
            {
                case PAGE_PEEPS:
                    if (list_information_type >= 32)
                    {
                        list_information_type = 0;
                    }
                    break;
                case PAGE_RIDES:
                    if (list_information_type >= 64)
                    {
                        list_information_type = 0;
                    }
                    break;
            }
        }

        void OnToolUpdate(WidgetIndex widgetIndex, const ScreenCoordsXY& screenCoords) override
        {
            switch (widgetIndex)
            {
                case WIDX_SET_LAND_RIGHTS:
                    SetLandRightsToolUpdate(screenCoords);
                    break;
                case WIDX_PEOPLE_STARTING_POSITION:
                    SetPeepSpawnToolUpdate(screenCoords);
                    break;
            }
        }

        void OnToolDown(WidgetIndex widgetIndex, const ScreenCoordsXY& screenCoords) override
        {
            switch (widgetIndex)
            {
                case WIDX_PEOPLE_STARTING_POSITION:
                    SetPeepSpawnToolDown(screenCoords);
                    break;
            }
        }

        void OnToolDrag(WidgetIndex widgetIndex, const ScreenCoordsXY& screenCoords) override
        {
            switch (widgetIndex)
            {
                case WIDX_SET_LAND_RIGHTS:
                    if (gMapSelectFlags & MAP_SELECT_FLAG_ENABLE)
                    {
                        auto landSetRightsAction = LandSetRightsAction(
                            { gMapSelectPositionA.x, gMapSelectPositionA.y, gMapSelectPositionB.x, gMapSelectPositionB.y },
                            LandSetRightSetting::SetOwnershipWithChecks, _activeTool << 4);
                        GameActions::Execute(&landSetRightsAction);
                    }
                    break;
            }
        }

        void OnToolAbort(WidgetIndex widgetIndex) override
        {
            switch (widgetIndex)
            {
                case WIDX_SET_LAND_RIGHTS:
                    Invalidate();
                    HideGridlines();
                    HideLandRights();
                    HideConstructionRights();
                    break;
                case WIDX_PEOPLE_STARTING_POSITION:
                    Invalidate();
                    HideGridlines();
                    HideLandRights();
                    HideConstructionRights();
                    break;
            }
        }

        void SetLandRightsToolUpdate(const ScreenCoordsXY& screenCoords)
        {
            MapInvalidateSelectionRect();
            gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE;

            auto info = GetMapCoordinatesFromPos(
                screenCoords, EnumsToFlags(ViewportInteractionItem::Terrain, ViewportInteractionItem::Water));
            if (info.SpriteType == ViewportInteractionItem::None)
                return;

            auto mapCoords = info.Loc;

            gMapSelectFlags |= MAP_SELECT_FLAG_ENABLE;
            gMapSelectType = MAP_SELECT_TYPE_FULL_LAND_RIGHTS;

            int32_t landRightsToolSize = _landRightsToolSize;
            if (landRightsToolSize == 0)
                landRightsToolSize = 1;

            int32_t size = (landRightsToolSize * 32) - 32;
            int32_t radius = (landRightsToolSize * 16) - 16;
            mapCoords.x -= radius;
            mapCoords.y -= radius;
            mapCoords = mapCoords.ToTileStart();
            gMapSelectPositionA = mapCoords;
            gMapSelectPositionB.x = mapCoords.x + size;
            gMapSelectPositionB.y = mapCoords.y + size;
            MapInvalidateSelectionRect();
        }

        void SetPeepSpawnToolUpdate(const ScreenCoordsXY& screenCoords)
        {
            int32_t direction;
            TileElement* tileElement;
            MapInvalidateSelectionRect();
            gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE;
            gMapSelectFlags &= ~MAP_SELECT_FLAG_ENABLE_ARROW;
            auto mapCoords = FootpathBridgeGetInfoFromPos(screenCoords, &direction, &tileElement);
            if (mapCoords.IsNull())
                return;

            int32_t mapZ = tileElement->GetBaseZ();
            if (tileElement->GetType() == TileElementType::Surface)
            {
                if ((tileElement->AsSurface()->GetSlope() & kTileSlopeRaisedCornersMask) != 0)
                    mapZ += 16;
                if (tileElement->AsSurface()->GetSlope() & kTileSlopeDiagonalFlag)
                    mapZ += 16;
            }

            gMapSelectFlags |= MAP_SELECT_FLAG_ENABLE;
            gMapSelectFlags |= MAP_SELECT_FLAG_ENABLE_ARROW;
            gMapSelectType = MAP_SELECT_TYPE_FULL;
            gMapSelectPositionA = mapCoords;
            gMapSelectPositionB = mapCoords;
            gMapSelectArrowPosition = CoordsXYZ{ mapCoords, mapZ };
            gMapSelectArrowDirection = DirectionReverse(direction);
            MapInvalidateSelectionRect();
        }

        void SetPeepSpawnToolDown(const ScreenCoordsXY& screenCoords)
        {
            // Verify footpath exists at location, and retrieve coordinates
            TileElement* tileElement;
            int32_t direction;
            auto mapCoords = FootpathGetCoordinatesFromPos(screenCoords, &direction, &tileElement);
            if (mapCoords.IsNull())
                return;

            int32_t mapZ = tileElement->GetBaseZ();

            auto gameAction = PeepSpawnPlaceAction({ mapCoords, mapZ, static_cast<Direction>(direction) });
            auto result = GameActions::Execute(&gameAction);
            if (result.Error == GameActions::Status::Ok)
            {
                Audio::Play3D(Audio::SoundId::PlaceItem, result.Position);
            }
        }

        void OnTextInput(WidgetIndex widgetIndex, std::string_view text) override
        {
            if (text.empty())
                return;

            switch (widgetIndex)
            {
                case WIDX_LAND_TOOL:
                {
                    char* end;
                    std::string textStr = std::string(text);
                    int32_t size = strtol(textStr.c_str(), &end, 10);
                    if (*end == '\0')
                    {
                        size = std::clamp<uint16_t>(size, kLandToolMinimumSize, kLandToolMaximumSize);
                        _landRightsToolSize = size;
                        Invalidate();
                    }
                    break;
                }
                case WIDX_MAP_SIZE_SPINNER_Y:
                case WIDX_MAP_SIZE_SPINNER_X:
                {
                    char* end;
                    std::string textStr = std::string(text);
                    int32_t size = strtol(textStr.c_str(), &end, 10);
                    if (*end == '\0')
                    {
                        // The practical size is 2 lower than the technical size
                        size += 2;
                        size = std::clamp(
                            size, static_cast<int>(kMinimumMapSizeTechnical), static_cast<int>(kMaximumMapSizeTechnical));

                        TileCoordsXY newMapSize = GetGameState().MapSize;
                        if (_resizeDirection != ResizeDirection::X)
                            newMapSize.y = size;
                        if (_resizeDirection != ResizeDirection::Y)
                            newMapSize.x = size;

                        auto mapChangeSizeAction = MapChangeSizeAction(newMapSize);
                        GameActions::Execute(&mapChangeSizeAction);
                        Invalidate();
                    }
                    break;
                }
            }
        }

        ScreenSize OnScrollGetSize(int32_t scrollIndex) override
        {
            return ScreenSize(getMiniMapWidth(), getMiniMapWidth());
        }

        void OnScrollMouseDown(int32_t scrollIndex, const ScreenCoordsXY& screenCoords) override
        {
            // Adjust coordinates for any map offset to centre
            auto adjCoords = screenCoords;
            auto mapOffset = getMapOffset(width);
            if (mapOffset > 0)
            {
                adjCoords -= ScreenCoordsXY(mapOffset, mapOffset - SCROLLBAR_SIZE / 2);
            }

            CoordsXY c = ScreenToMap(adjCoords);
            auto mapCoords = CoordsXY{ std::clamp(c.x, 0, getTechnicalMapSizeBig() - 1),
                                       std::clamp(c.y, 0, getTechnicalMapSizeBig() - 1) };
            auto mapZ = TileElementHeight(mapCoords);

            WindowBase* mainWindow = WindowGetMain();
            if (mainWindow != nullptr)
            {
                WindowScrollToLocation(*mainWindow, { mapCoords, mapZ });
            }

            if (isToolActive(WindowClass::Land))
            {
                // Set land terrain
                int32_t landToolSize = std::max<int32_t>(1, gLandToolSize);
                int32_t size = (landToolSize * 32) - 32;
                int32_t radius = (landToolSize * 16) - 16;

                mapCoords = (mapCoords - CoordsXY{ radius, radius }).ToTileStart();
                MapInvalidateSelectionRect();
                gMapSelectFlags |= MAP_SELECT_FLAG_ENABLE;
                gMapSelectType = MAP_SELECT_TYPE_FULL;
                gMapSelectPositionA = mapCoords;
                gMapSelectPositionB = mapCoords + CoordsXY{ size, size };
                MapInvalidateSelectionRect();

                auto surfaceSetStyleAction = SurfaceSetStyleAction(
                    { gMapSelectPositionA.x, gMapSelectPositionA.y, gMapSelectPositionB.x, gMapSelectPositionB.y },
                    gLandToolTerrainSurface, gLandToolTerrainEdge);
                GameActions::Execute(&surfaceSetStyleAction);
            }
            else if (isToolActive(*this, WIDX_SET_LAND_RIGHTS))
            {
                // Set land rights
                int32_t landRightsToolSize = std::max<int32_t>(1, _landRightsToolSize);
                int32_t size = (landRightsToolSize * 32) - 32;
                int32_t radius = (landRightsToolSize * 16) - 16;
                mapCoords = (mapCoords - CoordsXY{ radius, radius }).ToTileStart();

                MapInvalidateSelectionRect();
                gMapSelectFlags |= MAP_SELECT_FLAG_ENABLE;
                gMapSelectType = MAP_SELECT_TYPE_FULL_LAND_RIGHTS;
                gMapSelectPositionA = mapCoords;
                gMapSelectPositionB = mapCoords + CoordsXY{ size, size };
                MapInvalidateSelectionRect();

                auto landSetRightsAction = LandSetRightsAction(
                    { gMapSelectPositionA.x, gMapSelectPositionA.y, gMapSelectPositionB.x, gMapSelectPositionB.y },
                    LandSetRightSetting::SetOwnershipWithChecks, _activeTool << 4);
                GameActions::Execute(&landSetRightsAction);
            }
        }

        void OnScrollMouseDrag(int32_t scrollIndex, const ScreenCoordsXY& screenCoords) override
        {
            OnScrollMouseDown(scrollIndex, screenCoords);
        }

        void OnScrollDraw(int32_t scrollIndex, DrawPixelInfo& dpi) override
        {
            GfxClear(dpi, PALETTE_INDEX_10);

            // Ensure small maps are centred
            auto screenOffset = ScreenCoordsXY(0, 0);
            auto mapOffset = getMapOffset(width);
            if (mapOffset > 0)
                screenOffset += ScreenCoordsXY(mapOffset, mapOffset - SCROLLBAR_SIZE / 2);

            G1Element g1temp = {};
            g1temp.offset = _mapImageData.data();
            g1temp.width = getMiniMapWidth();
            g1temp.height = getMiniMapWidth();
            GfxSetG1Element(SPR_TEMP, &g1temp);
            DrawingEngineInvalidateImage(SPR_TEMP);
            GfxDrawSprite(dpi, ImageId(SPR_TEMP), screenOffset);

            if (selected_tab == PAGE_PEEPS)
            {
                PaintPeepOverlay(dpi, screenOffset);
            }
            else
            {
                PaintTrainOverlay(dpi, screenOffset);
            }
            PaintHudRectangle(dpi, screenOffset);
        }

        void OnPrepareDraw() override
        {
            // Set the pressed widgets
            pressed_widgets = 0;
            SetWidgetPressed(WIDX_MAP_SIZE_LINK, _mapWidthAndHeightLinked);
            pressed_widgets |= (1uLL << (WIDX_PEOPLE_TAB + selected_tab));
            pressed_widgets |= (1uLL << WIDX_LAND_TOOL);

            if (_activeTool & (1 << 3))
                pressed_widgets |= (1uLL << WIDX_LAND_SALE_CHECKBOX);

            if (_activeTool & (1 << 2))
                pressed_widgets |= (1uLL << WIDX_CONSTRUCTION_RIGHTS_SALE_CHECKBOX);

            if (_activeTool & (1 << 1))
                pressed_widgets |= (1uLL << WIDX_LAND_OWNED_CHECKBOX);

            if (_activeTool & (1 << 0))
                pressed_widgets |= (1uLL << WIDX_CONSTRUCTION_RIGHTS_OWNED_CHECKBOX);

            if (WindowFindByClass(WindowClass::EditorParkEntrance))
                pressed_widgets |= (1uLL << WIDX_BUILD_PARK_ENTRANCE);

            // Set disabled widgets
            auto& gameState = GetGameState();
            SetWidgetDisabled(WIDX_MAP_SIZE_LINK, gameState.MapSize.x != gameState.MapSize.y);

            // Resize widgets to window size
            ResizeFrameWithPage();
            ResizeMiniMap();

            widgets[WIDX_MAP_SIZE_SPINNER_Y].top = height - 15;
            widgets[WIDX_MAP_SIZE_SPINNER_Y].bottom = height - 4;
            widgets[WIDX_MAP_SIZE_SPINNER_Y_UP].top = height - 14;
            widgets[WIDX_MAP_SIZE_SPINNER_Y_UP].bottom = height - 5;
            widgets[WIDX_MAP_SIZE_SPINNER_Y_DOWN].top = height - 14;
            widgets[WIDX_MAP_SIZE_SPINNER_Y_DOWN].bottom = height - 5;
            widgets[WIDX_MAP_SIZE_LINK].top = height - 15;
            widgets[WIDX_MAP_SIZE_LINK].bottom = height - 4;
            widgets[WIDX_MAP_SIZE_SPINNER_X].top = height - 15;
            widgets[WIDX_MAP_SIZE_SPINNER_X].bottom = height - 4;
            widgets[WIDX_MAP_SIZE_SPINNER_X_UP].top = height - 14;
            widgets[WIDX_MAP_SIZE_SPINNER_X_UP].bottom = height - 5;
            widgets[WIDX_MAP_SIZE_SPINNER_X_DOWN].top = height - 14;
            widgets[WIDX_MAP_SIZE_SPINNER_X_DOWN].bottom = height - 5;

            widgets[WIDX_SET_LAND_RIGHTS].top = height - 70;
            widgets[WIDX_SET_LAND_RIGHTS].bottom = height - 70 + 23;
            widgets[WIDX_BUILD_PARK_ENTRANCE].top = height - 46;
            widgets[WIDX_BUILD_PARK_ENTRANCE].bottom = height - 46 + 23;
            widgets[WIDX_PEOPLE_STARTING_POSITION].top = height - 46;
            widgets[WIDX_PEOPLE_STARTING_POSITION].bottom = height - 46 + 23;

            widgets[WIDX_LAND_TOOL].top = height - 42;
            widgets[WIDX_LAND_TOOL].bottom = height - 42 + 30;
            widgets[WIDX_LAND_TOOL_SMALLER].top = height - 41;
            widgets[WIDX_LAND_TOOL_SMALLER].bottom = height - 41 + 15;
            widgets[WIDX_LAND_TOOL_LARGER].top = height - 27;
            widgets[WIDX_LAND_TOOL_LARGER].bottom = height - 27 + 15;

            widgets[WIDX_MAP_GENERATOR].top = height - 69;
            widgets[WIDX_MAP_GENERATOR].bottom = height - 69 + 13;

            // Land tool mode (4 checkboxes)
            int checkboxY = height - 55;
            for (int32_t i = WIDX_LAND_OWNED_CHECKBOX; i <= WIDX_CONSTRUCTION_RIGHTS_SALE_CHECKBOX; i++)
            {
                widgets[i].top = checkboxY;
                checkboxY += 11;
                widgets[i].bottom = checkboxY;
                checkboxY += 2;
            }

            // Disable all scenario editor related widgets
            for (int32_t i = WIDX_MAP_SIZE_SPINNER_Y; i <= WIDX_MAP_GENERATOR; i++)
            {
                widgets[i].type = WindowWidgetType::Empty;
            }

            if ((gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) || GetGameState().Cheats.SandboxMode)
            {
                // Always show set land rights button
                widgets[WIDX_SET_LAND_RIGHTS].type = WindowWidgetType::FlatBtn;

                if (isToolActive(WindowClass::Map, WIDX_SET_LAND_RIGHTS))
                {
                    // Show land tool buttons + modes
                    widgets[WIDX_LAND_TOOL].type = WindowWidgetType::ImgBtn;
                    widgets[WIDX_LAND_TOOL_SMALLER].type = WindowWidgetType::TrnBtn;
                    widgets[WIDX_LAND_TOOL_LARGER].type = WindowWidgetType::TrnBtn;

                    for (int32_t i = WIDX_LAND_OWNED_CHECKBOX; i <= WIDX_CONSTRUCTION_RIGHTS_SALE_CHECKBOX; i++)
                        widgets[i].type = WindowWidgetType::Checkbox;

                    widgets[WIDX_LAND_TOOL].image = ImageId(LandTool::SizeToSpriteIndex(_landRightsToolSize));
                }
                else
                {
                    // Show the default scenario editor buttons
                    ShowDefaultScenarioEditorButtons();
                }
            }
            if (_recalculateScrollbars)
            {
                WidgetScrollUpdateThumbs(*this, WIDX_MAP);
                _recalculateScrollbars = false;
            }
        }

        void OnDraw(DrawPixelInfo& dpi) override
        {
            DrawWidgets(dpi);
            DrawTabImages(dpi);

            auto screenCoords = windowPos
                + ScreenCoordsXY{ window_map_widgets[WIDX_LAND_TOOL].midX(), window_map_widgets[WIDX_LAND_TOOL].midY() };

            // Draw land tool size
            if (isToolActive(*this, WIDX_SET_LAND_RIGHTS) && _landRightsToolSize > kLandToolMaximumSizeWithSprite)
            {
                auto ft = Formatter();
                ft.Add<uint16_t>(_landRightsToolSize);
                DrawTextBasic(
                    dpi, screenCoords - ScreenCoordsXY{ 0, 2 }, STR_LAND_TOOL_SIZE_VALUE, ft, { TextAlignment::CENTRE });
            }
            screenCoords.y = windowPos.y + window_map_widgets[WIDX_LAND_TOOL].bottom + 5;

            // People starting position (scenario editor only)
            if (widgets[WIDX_PEOPLE_STARTING_POSITION].type != WindowWidgetType::Empty)
            {
                screenCoords = windowPos
                    + ScreenCoordsXY{ widgets[WIDX_PEOPLE_STARTING_POSITION].left + 12,
                                      widgets[WIDX_PEOPLE_STARTING_POSITION].top + 18 };
                GfxDrawSprite(dpi, ImageId(SPR_6410, COLOUR_BRIGHT_RED, COLOUR_LIGHT_BROWN), screenCoords);
            }

            if (!(gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) && !GetGameState().Cheats.SandboxMode)
            {
                // Render the map legend
                if (selected_tab == PAGE_RIDES)
                {
                    screenCoords = windowPos + ScreenCoordsXY{ 4, widgets[WIDX_MAP].bottom + 2 };

                    static_assert(std::size(RideKeyColours) == std::size(MapLabels));

                    for (uint32_t i = 0; i < std::size(RideKeyColours); i++)
                    {
                        GfxFillRect(
                            dpi, { screenCoords + ScreenCoordsXY{ 0, 2 }, screenCoords + ScreenCoordsXY{ 6, 8 } },
                            RideKeyColours[i]);
                        DrawTextBasic(dpi, screenCoords + ScreenCoordsXY{ kListRowHeight, 0 }, MapLabels[i], {});
                        screenCoords.y += kListRowHeight;
                        if (i == 3)
                        {
                            screenCoords += { _firstColumnWidth, -(kListRowHeight * 4) };
                        }
                    }
                }
            }
            else if (!isToolActive(*this, WIDX_SET_LAND_RIGHTS))
            {
                DrawTextBasic(
                    dpi, windowPos + ScreenCoordsXY{ 4, widgets[WIDX_MAP_SIZE_SPINNER_Y].top + 1 }, STR_MAP_SIZE, {},
                    { colours[1] });
            }
        }

        void OnLanguageChange() override
        {
            CalculateTextLayout();
        }

        void ResetMap()
        {
            InitMap();
            CentreMapOnViewPoint();
        }

    private:
        void InitMap()
        {
            _mapImageData.resize(getMiniMapWidth() * getMiniMapWidth());
            std::fill(_mapImageData.begin(), _mapImageData.end(), PALETTE_INDEX_10);
            _currentLine = 0;
        }

        void CentreMapOnViewPoint()
        {
            WindowBase* mainWindow = WindowGetMain();
            int16_t ax, bx, cx, dx;
            int16_t bp, di;

            if (mainWindow == nullptr || mainWindow->viewport == nullptr)
                return;

            auto offset = MiniMapOffsetFactors[GetCurrentRotation()];

            // calculate centre view point of viewport and transform it to minimap coordinates

            cx = ((mainWindow->viewport->view_width >> 1) + mainWindow->viewport->viewPos.x) >> 5;
            dx = ((mainWindow->viewport->view_height >> 1) + mainWindow->viewport->viewPos.y) >> 4;
            cx += offset.x * getTechnicalMapSize();
            dx += offset.y * getTechnicalMapSize();

            // calculate width and height of minimap

            ax = widgets[WIDX_MAP].width() - 11;
            bx = widgets[WIDX_MAP].height() - 11;
            bp = ax;
            di = bx;

            ax >>= 1;
            bx >>= 1;
            cx = std::max(cx - ax, 0);
            dx = std::max(dx - bx, 0);

            bp = scrolls[0].h_right - bp;
            di = scrolls[0].v_bottom - di;

            if (bp < 0 && (bp - cx) < 0)
                cx = 0;

            if (di < 0 && (di - dx) < 0)
                dx = 0;

            scrolls[0].h_left = cx;
            scrolls[0].v_top = dx;
            WidgetScrollUpdateThumbs(*this, WIDX_MAP);
        }

        void IncreaseMapSize()
        {
            auto newMapSize = GetGameState().MapSize;
            if (IsWidgetPressed(WIDX_MAP_SIZE_LINK) || _resizeDirection == ResizeDirection::Y)
                newMapSize.y++;
            if (IsWidgetPressed(WIDX_MAP_SIZE_LINK) || _resizeDirection == ResizeDirection::X)
                newMapSize.x++;

            auto increaseMapSizeAction = MapChangeSizeAction(newMapSize);
            GameActions::Execute(&increaseMapSizeAction);
        }

        void DecreaseMapSize()
        {
            auto newMapSize = GetGameState().MapSize;
            if (IsWidgetPressed(WIDX_MAP_SIZE_LINK) || _resizeDirection == ResizeDirection::Y)
                newMapSize.y--;
            if (IsWidgetPressed(WIDX_MAP_SIZE_LINK) || _resizeDirection == ResizeDirection::X)
                newMapSize.x--;

            auto decreaseMapSizeAction = MapChangeSizeAction(newMapSize);
            GameActions::Execute(&decreaseMapSizeAction);
        }

        void SetMapPixels()
        {
            int32_t x = 0, y = 0, dx = 0, dy = 0;

            int32_t pos = (_currentLine * (getMiniMapWidth() - 1)) + getTechnicalMapSize() - 1;
            auto destinationPosition = ScreenCoordsXY{ pos % getMiniMapWidth(), pos / getMiniMapWidth() };
            auto destination = _mapImageData.data() + (destinationPosition.y * getMiniMapWidth()) + destinationPosition.x;
            switch (GetCurrentRotation())
            {
                case 0:
                    x = _currentLine * kCoordsXYStep;
                    y = 0;
                    dx = 0;
                    dy = kCoordsXYStep;
                    break;
                case 1:
                    x = getMaxTileStartXY();
                    y = _currentLine * kCoordsXYStep;
                    dx = -kCoordsXYStep;
                    dy = 0;
                    break;
                case 2:
                    x = getTechnicalMapSizeBig() - ((_currentLine + 1) * kCoordsXYStep);
                    y = getMaxTileStartXY();
                    dx = 0;
                    dy = -kCoordsXYStep;
                    break;
                case 3:
                    x = 0;
                    y = getTechnicalMapSizeBig() - ((_currentLine + 1) * kCoordsXYStep);
                    dx = kCoordsXYStep;
                    dy = 0;
                    break;
            }

            for (int32_t i = 0; i < getTechnicalMapSize(); i++)
            {
                if (!MapIsEdge({ x, y }))
                {
                    uint16_t colour = 0;
                    switch (selected_tab)
                    {
                        case PAGE_PEEPS:
                            colour = GetPixelColourPeep({ x, y });
                            break;
                        case PAGE_RIDES:
                            colour = GetPixelColourRide({ x, y });
                            break;
                    }
                    destination[0] = (colour >> 8) & 0xFF;
                    destination[1] = colour;
                }
                x += dx;
                y += dy;

                destinationPosition.x++;
                destinationPosition.y++;
                destination = _mapImageData.data() + (destinationPosition.y * getMiniMapWidth()) + destinationPosition.x;
            }
            _currentLine++;
            if (_currentLine >= static_cast<uint32_t>(getTechnicalMapSize()))
                _currentLine = 0;
        }

        uint16_t GetPixelColourPeep(const CoordsXY& c)
        {
            auto* surfaceElement = MapGetSurfaceElementAt(c);
            if (surfaceElement == nullptr)
                return 0;

            uint16_t colour = MapColour(PALETTE_INDEX_0);
            const auto* surfaceObject = surfaceElement->GetSurfaceObject();
            if (surfaceObject != nullptr)
                colour = MapColour2(surfaceObject->MapColours[0], surfaceObject->MapColours[1]);

            if (surfaceElement->GetWaterHeight() > 0)
                colour = WaterColour;

            if (!(surfaceElement->GetOwnership() & OWNERSHIP_OWNED))
                colour = MapColourUnowned(colour);

            const int32_t maxSupportedTileElementType = static_cast<int32_t>(std::size(ElementTypeAddColour));
            auto tileElement = reinterpret_cast<TileElement*>(surfaceElement);
            while (!(tileElement++)->IsLastForTile())
            {
                if (tileElement->IsGhost())
                {
                    colour = MapColour(PALETTE_INDEX_21);
                    break;
                }

                auto tileElementType = tileElement->GetType();
                if (EnumValue(tileElementType) >= maxSupportedTileElementType)
                {
                    tileElementType = TileElementType::Surface;
                }
                colour &= ElementTypeMaskColour[EnumValue(tileElementType)];
                colour |= ElementTypeAddColour[EnumValue(tileElementType)];
            }

            return colour;
        }

        uint16_t GetPixelColourRide(const CoordsXY& c)
        {
            uint16_t colourA = 0;                           // highlight colour
            uint16_t colourB = MapColour(PALETTE_INDEX_13); // surface colour (dark grey)

            // as an improvement we could use first_element to show underground stuff?
            TileElement* tileElement = reinterpret_cast<TileElement*>(MapGetSurfaceElementAt(c));
            do
            {
                if (tileElement == nullptr)
                    break;

                if (tileElement->IsGhost())
                {
                    colourA = MapColour(PALETTE_INDEX_21);
                    break;
                }

                switch (tileElement->GetType())
                {
                    case TileElementType::Surface:
                        if (tileElement->AsSurface()->GetWaterHeight() > 0)
                            // Why is this a different water colour as above (195)?
                            colourB = MapColour(PALETTE_INDEX_194);
                        if (!(tileElement->AsSurface()->GetOwnership() & OWNERSHIP_OWNED))
                            colourB = MapColourUnowned(colourB);
                        break;
                    case TileElementType::Path:
                        colourA = MapColour(PALETTE_INDEX_14); // lighter grey
                        break;
                    case TileElementType::Entrance:
                    {
                        if (tileElement->AsEntrance()->GetEntranceType() == ENTRANCE_TYPE_PARK_ENTRANCE)
                            break;
                        Ride* targetRide = GetRide(tileElement->AsEntrance()->GetRideIndex());
                        if (targetRide != nullptr)
                        {
                            const auto& colourKey = targetRide->GetRideTypeDescriptor().ColourKey;
                            colourA = RideKeyColours[EnumValue(colourKey)];
                        }
                        break;
                    }
                    case TileElementType::Track:
                    {
                        Ride* targetRide = GetRide(tileElement->AsTrack()->GetRideIndex());
                        if (targetRide != nullptr)
                        {
                            const auto& colourKey = targetRide->GetRideTypeDescriptor().ColourKey;
                            colourA = RideKeyColours[EnumValue(colourKey)];
                        }

                        break;
                    }
                    default:
                        break;
                }
            } while (!(tileElement++)->IsLastForTile());

            if (colourA != 0)
                return colourA;

            return colourB;
        }

        void PaintPeepOverlay(DrawPixelInfo& dpi, const ScreenCoordsXY& offset)
        {
            auto flashColour = GetGuestFlashColour();
            for (auto guest : EntityList<Guest>())
            {
                DrawMapPeepPixel(guest, flashColour, dpi, offset);
            }
            flashColour = GetStaffFlashColour();
            for (auto staff : EntityList<Staff>())
            {
                DrawMapPeepPixel(staff, flashColour, dpi, offset);
            }
        }

        void DrawMapPeepPixel(Peep* peep, const uint8_t flashColour, DrawPixelInfo& dpi, const ScreenCoordsXY& offset)
        {
            if (peep->x == kLocationNull)
                return;

            MapCoordsXY c = TransformToMapCoords({ peep->x, peep->y });
            auto leftTop = ScreenCoordsXY{ c.x, c.y } + offset;
            auto rightBottom = leftTop;
            uint8_t colour = DefaultPeepMapColour;
            if (EntityGetFlashing(peep))
            {
                colour = flashColour;
                // If flashing then map peep pixel size is increased (by moving left top downwards)
                if (flashColour != DefaultPeepMapColour)
                {
                    leftTop.x--;
                }
            }

            GfxFillRect(dpi, { leftTop, rightBottom }, colour);
        }

        uint8_t GetGuestFlashColour() const
        {
            uint8_t colour = DefaultPeepMapColour;
            if ((_flashingFlags & MapFlashingFlags::FlashGuests) != 0)
            {
                colour = GuestMapColour;
                if ((_flashingFlags & MapFlashingFlags::SwitchColour) == 0)
                    colour = GuestMapColourAlternate;
            }
            return colour;
        }

        uint8_t GetStaffFlashColour() const
        {
            uint8_t colour = DefaultPeepMapColour;
            if ((_flashingFlags & MapFlashingFlags::FlashStaff) != 0)
            {
                colour = StaffMapColour;
                if ((_flashingFlags & MapFlashingFlags::SwitchColour) == 0)
                    colour = StaffMapColourAlternate;
            }
            return colour;
        }

        void PaintTrainOverlay(DrawPixelInfo& dpi, const ScreenCoordsXY& offset)
        {
            for (auto train : TrainManager::View())
            {
                for (Vehicle* vehicle = train; vehicle != nullptr; vehicle = GetEntity<Vehicle>(vehicle->next_vehicle_on_train))
                {
                    if (vehicle->x == kLocationNull)
                        continue;

                    auto mapCoord = TransformToMapCoords({ vehicle->x, vehicle->y });
                    auto pixelCoord = ScreenCoordsXY{ mapCoord.x, mapCoord.y } + offset;

                    GfxFillRect(dpi, { pixelCoord, pixelCoord }, PALETTE_INDEX_171);
                }
            }
        }

        /**
         * The call to GfxFillRect was originally wrapped in Sub68DABD which made sure that arguments were ordered correctly,
         * but it doesn't look like it's ever necessary here so the call was removed.
         */
        void PaintHudRectangle(DrawPixelInfo& dpi, const ScreenCoordsXY& widgetOffset)
        {
            WindowBase* mainWindow = WindowGetMain();
            if (mainWindow == nullptr)
                return;

            Viewport* mainViewport = mainWindow->viewport;
            if (mainViewport == nullptr)
                return;

            auto mapOffset = MiniMapOffsetFactors[GetCurrentRotation()];
            mapOffset.x *= getTechnicalMapSize();
            mapOffset.y *= getTechnicalMapSize();

            auto leftTop = widgetOffset + mapOffset
                + ScreenCoordsXY{ (mainViewport->viewPos.x >> 5), (mainViewport->viewPos.y >> 4) };
            auto rightBottom = leftTop + ScreenCoordsXY{ mainViewport->view_width >> 5, mainViewport->view_height >> 4 };
            auto rightTop = ScreenCoordsXY{ rightBottom.x, leftTop.y };
            auto leftBottom = ScreenCoordsXY{ leftTop.x, rightBottom.y };

            // top horizontal lines
            GfxFillRect(dpi, { leftTop, leftTop + ScreenCoordsXY{ 3, 0 } }, PALETTE_INDEX_56);
            GfxFillRect(dpi, { rightTop - ScreenCoordsXY{ 3, 0 }, rightTop }, PALETTE_INDEX_56);

            // left vertical lines
            GfxFillRect(dpi, { leftTop, leftTop + ScreenCoordsXY{ 0, 3 } }, PALETTE_INDEX_56);
            GfxFillRect(dpi, { leftBottom - ScreenCoordsXY{ 0, 3 }, leftBottom }, PALETTE_INDEX_56);

            // bottom horizontal lines
            GfxFillRect(dpi, { leftBottom, leftBottom + ScreenCoordsXY{ 3, 0 } }, PALETTE_INDEX_56);
            GfxFillRect(dpi, { rightBottom - ScreenCoordsXY{ 3, 0 }, rightBottom }, PALETTE_INDEX_56);

            // right vertical lines
            GfxFillRect(dpi, { rightTop, rightTop + ScreenCoordsXY{ 0, 3 } }, PALETTE_INDEX_56);
            GfxFillRect(dpi, { rightBottom - ScreenCoordsXY{ 0, 3 }, rightBottom }, PALETTE_INDEX_56);
        }

        void DrawTabImages(DrawPixelInfo& dpi)
        {
            // Guest tab image (animated)
            uint32_t guestTabImage = SPR_TAB_GUESTS_0;
            if (selected_tab == PAGE_PEEPS)
                guestTabImage += list_information_type / 4;

            GfxDrawSprite(
                dpi, ImageId(guestTabImage),
                windowPos + ScreenCoordsXY{ widgets[WIDX_PEOPLE_TAB].left, widgets[WIDX_PEOPLE_TAB].top });

            // Ride/stall tab image (animated)
            uint32_t rideTabImage = SPR_TAB_RIDE_0;
            if (selected_tab == PAGE_RIDES)
                rideTabImage += list_information_type / 4;

            GfxDrawSprite(
                dpi, ImageId(rideTabImage),
                windowPos + ScreenCoordsXY{ widgets[WIDX_RIDES_TAB].left, widgets[WIDX_RIDES_TAB].top });
        }

        void ShowDefaultScenarioEditorButtons()
        {
            widgets[WIDX_BUILD_PARK_ENTRANCE].type = WindowWidgetType::FlatBtn;
            widgets[WIDX_PEOPLE_STARTING_POSITION].type = WindowWidgetType::FlatBtn;
            widgets[WIDX_MAP_SIZE_SPINNER_Y].type = WindowWidgetType::Spinner;
            widgets[WIDX_MAP_SIZE_SPINNER_Y_UP].type = WindowWidgetType::Button;
            widgets[WIDX_MAP_SIZE_SPINNER_Y_DOWN].type = WindowWidgetType::Button;
            widgets[WIDX_MAP_SIZE_LINK].type = WindowWidgetType::FlatBtn;
            widgets[WIDX_MAP_SIZE_SPINNER_X].type = WindowWidgetType::Spinner;
            widgets[WIDX_MAP_SIZE_SPINNER_X_UP].type = WindowWidgetType::Button;
            widgets[WIDX_MAP_SIZE_SPINNER_X_DOWN].type = WindowWidgetType::Button;

            // Only show this in the scenario editor, even when in sandbox mode.
            if (gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR)
                widgets[WIDX_MAP_GENERATOR].type = WindowWidgetType::Button;

            // Push width (Y) and height (X) to the common formatter arguments for the map size spinners to use
            auto& gameState = GetGameState();
            auto ft = Formatter::Common();
            ft.Add<uint16_t>(gameState.MapSize.y - 2);
            ft.Add<uint16_t>(gameState.MapSize.x - 2);
        }

        void InputLandSize()
        {
            Formatter ft;
            ft.Add<uint16_t>(kLandToolMinimumSize);
            ft.Add<uint16_t>(kLandToolMaximumSize);
            TextInputOpen(WIDX_LAND_TOOL, STR_SELECTION_SIZE, STR_ENTER_SELECTION_SIZE, ft, STR_NONE, STR_NONE, 3);
        }

        void InputMapSize(WidgetIndex callingWidget)
        {
            if (IsWidgetPressed(WIDX_MAP_SIZE_LINK))
                _resizeDirection = ResizeDirection::Both;
            else
                _resizeDirection = (callingWidget == WIDX_MAP_SIZE_SPINNER_Y) ? ResizeDirection::Y : ResizeDirection::X;

            Formatter ft;
            ft.Add<int16_t>(kMinimumMapSizePractical);
            ft.Add<int16_t>(kMaximumMapSizePractical);
            TextInputOpen(callingWidget, STR_MAP_SIZE_2, STR_ENTER_MAP_SIZE, ft, STR_NONE, STR_NONE, 4);
        }

        CoordsXY ScreenToMap(ScreenCoordsXY screenCoords)
        {
            screenCoords.x = ((screenCoords.x + 8) - getTechnicalMapSize()) / 2;
            screenCoords.y = ((screenCoords.y + 8)) / 2;
            auto location = TileCoordsXY(screenCoords.y - screenCoords.x, screenCoords.x + screenCoords.y).ToCoordsXY();

            switch (GetCurrentRotation())
            {
                case 0:
                    return location;
                case 1:
                    return { getTechnicalMapSizeBig() - 1 - location.y, location.x };
                case 2:
                    return { getTechnicalMapSizeBig() - 1 - location.x, getTechnicalMapSizeBig() - 1 - location.y };
                case 3:
                    return { location.y, getTechnicalMapSizeBig() - 1 - location.x };
            }

            return { 0, 0 }; // unreachable
        }

        MapCoordsXY TransformToMapCoords(CoordsXY c)
        {
            int32_t x = c.x, y = c.y;

            switch (GetCurrentRotation())
            {
                case 3:
                    std::swap(x, y);
                    x = getTechnicalMapSizeBig() - 1 - x;
                    break;
                case 2:
                    x = getTechnicalMapSizeBig() - 1 - x;
                    y = getTechnicalMapSizeBig() - 1 - y;
                    break;
                case 1:
                    std::swap(x, y);
                    y = getTechnicalMapSizeBig() - 1 - y;
                    break;
                case 0:
                    break;
            }
            x /= 32;
            y /= 32;

            return { -x + y + getTechnicalMapSize(), x + y };
        }

        uint16_t GetReservedBottomSpace()
        {
            if ((gScreenFlags & SCREEN_FLAGS_SCENARIO_EDITOR) || GetGameState().Cheats.SandboxMode)
                return kScenarioEditorReservedSpace;
            else if (selected_tab == PAGE_RIDES)
                return kRidesTabReservedSpace;
            else
                return kDefaultReservedSpace;
        }

        void SetInitialWindowDimensions()
        {
            // The initial mini map size should be able to show a reasonably sized map
            auto initSize = std::clamp(getTechnicalMapSize(), 100, 254) * 2;
            width = initSize + kReservedHSpace + SCROLLBAR_SIZE;
            height = initSize + kReservedTopSpace + GetReservedBottomSpace() + SCROLLBAR_SIZE;

            auto maxWindowHeight = ContextGetHeight() - 68;
            width = std::min<int16_t>(width, ContextGetWidth());
            height = std::min<int16_t>(height, maxWindowHeight);
        }

        void ResetMaxWindowDimensions()
        {
            max_width = std::clamp(getMiniMapWidth() + kReservedHSpace + SCROLLBAR_SIZE, WW, ContextGetWidth());
            max_height = std::clamp(
                getMiniMapWidth() + kReservedTopSpace + GetReservedBottomSpace() + SCROLLBAR_SIZE, WH, ContextGetHeight() - 68);
        }

        void ResizeMiniMap()
        {
            widgets[WIDX_MAP].right = width - 4;
            widgets[WIDX_MAP].bottom = height - 1 - GetReservedBottomSpace();
        }

        void CalculateTextLayout()
        {
            int32_t textOffset = 4 + kListRowHeight;
            _firstColumnWidth = 118;
            for (uint32_t i = 0; i < 4; i++)
            {
                const auto* labelStr = LanguageGetString(MapLabels[i]);
                _firstColumnWidth = std::max(textOffset + GfxGetStringWidth(labelStr, FontStyle::Medium), _firstColumnWidth);
            }

            textOffset += _firstColumnWidth + 4;
            min_width = WW;
            for (uint32_t i = 4; i < std::size(MapLabels); i++)
            {
                const auto* labelStr = LanguageGetString(MapLabels[i]);
                min_width = std::max(
                    static_cast<int16_t>(textOffset + GfxGetStringWidth(labelStr, FontStyle::Medium)), min_width);
            }
            width = std::max(min_width, width);
            _recalculateScrollbars = true;
        }
    };

    WindowBase* MapOpen()
    {
        try
        {
            WindowBase* w = WindowFocusOrCreate<MapWindow>(WindowClass::Map, 245, 259, WF_10);
            w->selected_tab = 0;
            w->list_information_type = 0;
            return w;
        }
        catch (const std::bad_alloc&)
        {
            return nullptr;
        }
    }

    void WindowMapReset()
    {
        WindowBase* w;

        // Check if window is even opened
        w = WindowBringToFrontByClass(WindowClass::Map);
        if (w == nullptr)
        {
            return;
        }

        auto* mapWindow = static_cast<MapWindow*>(w);
        mapWindow->ResetMap();
    }
} // namespace OpenRCT2::Ui::Windows
