#include <AircraftTypeClass.h>
#include <SpawnManagerClass.h>
#include <TiberiumClass.h>
#include "Body.h"
#include <Drawing.h>
#include <Surface.h>
#include <cstdio>
#include <cstring>

// IronWall lesson 5: keep adaptive HP readable on short health bars.
namespace
{
struct IronWallDigitFont
{
    int Width;
    int Height;
    int Gap;
};

// Rebuild the seven strokes at the chosen size instead of shrinking a bitmap.
constexpr IronWallDigitFont DigitFonts[] =
{
    {3, 5, 1}, {4, 7, 1}, {5, 9, 1}, {6, 10, 1}, {7, 11, 2}
};
constexpr int HealthTextGap = 4;
constexpr int HealthLineGap = 2;
// Keep the tiny fonts available, but never choose them for the HP overlay.
constexpr int MinimumFontIndex = 2; // 5x9 is the minimum readable HP font.
constexpr int MaximumSideOverhang = 10; // Soft allowance per side, including shadow.

int MeasureIronWallDigits(int count, const IronWallDigitFont& font)
{
    return count > 0 ? count * (font.Width + font.Gap) - font.Gap : 0;
}

int CountIronWallDigits(int value)
{
    int count = 1;
    while (value >= 10)
    {
        value /= 10;
        ++count;
    }
    return count;
}

struct IronWallHealthLayout
{
    IronWallDigitFont Font;
    bool TwoLines;
};

IronWallHealthLayout ChooseIronWallHealthLayout(int currentHealth,
    int maximumHealth, int barPixels)
{
    // Reserve both sides for the maximum digit count. Normal damage therefore
    // never changes the font or switches between one line and two lines.
    const int currentDigits = CountIronWallDigits(currentHealth);
    const int maximumDigits = CountIronWallDigits(maximumHealth);
    const int digits = currentDigits > maximumDigits ? currentDigits : maximumDigits;
    const int barBudget = barPixels + MaximumSideOverhang * 2;
    // Even an 8-pip infantry bar may show 125/125 on one readable line.
    // A 42-pixel minimum budget takes priority over the soft bar-width limit.
    const int minimumBudget = MeasureIronWallDigits(7, DigitFonts[MinimumFontIndex]) + 1;
    const int budget = barBudget > minimumBudget ? barBudget : minimumBudget;
    for (int index = 4; index >= MinimumFontIndex; --index)
    {
        const auto font = DigitFonts[index];
        if (MeasureIronWallDigits(digits * 2 + 1, font) + 1 <= budget)
            return {font, false};
    }

    // Very long HP: preserve exact numbers by placing current/ above maximum.
    for (int index = 4; index >= MinimumFontIndex; --index)
    {
        const auto font = DigitFonts[index];
        if (MeasureIronWallDigits(digits + 1, font) + 1 <= budget)
            return {font, true};
    }

    // Extreme values may exceed the soft width budget; never erase digits
    // or shrink below the readable 5x9 seven-segment font.
    return {DigitFonts[MinimumFontIndex], true};
}

// Bits a,b,c,d,e,f,g: top, upper-right, lower-right, bottom,
// lower-left, upper-left, middle. These are seven-segment digit encodings.
constexpr unsigned char DigitSegments[10] =
{
    0x3F, 0x06, 0x5B, 0x4F, 0x66,
    0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

bool IsIronWallDigitPixel(char character, int x, int y,
    int width = 7, int height = 11)
{
    if (character == '/')
        return x == width - 2 - (y * (width - 2) / (height - 1));

    if (character < '0' || character > '9')
        return false;

    const unsigned char segments = DigitSegments[character - '0'];
    const int middle = (height - 1) / 2;
    // At 3x5, connect the corners so the seven strokes stay recognisable.
    const int inset = width == 3 ? 0 : 1;
    const bool horizontal = x >= inset && x < width - inset;
    const bool upper = y >= inset && y <= middle - inset;
    const bool lower = y >= middle + inset && y < height - inset;
    return ((segments & 0x01) && y == 0 && horizontal)
        || ((segments & 0x02) && x == width - 1 && upper)
        || ((segments & 0x04) && x == width - 1 && lower)
        || ((segments & 0x08) && y == height - 1 && horizontal)
        || ((segments & 0x10) && x == 0 && lower)
        || ((segments & 0x20) && x == 0 && upper)
        || ((segments & 0x40) && y == middle && horizontal);
}

void DrawIronWallCalculatorText(DSurface* pSurface, const char* text,
    Point2D anchor, bool sloped, int color, const IronWallDigitFont& font = DigitFonts[4])
{
    if (!pSurface || !text || !text[0])
        return;

    // YR's native composite surface is 16-bit, even with modern wrappers.
    // Lock once per label instead of making a surface call per lit pixel.
    if (pSurface->GetBytesPerPixel() != 2)
        return;

    const int count = static_cast<int>(strlen(text));
    const int textWidth = MeasureIronWallDigits(count, font);
    const int halfWidth = textWidth / 2;
    const int left = anchor.X - halfWidth;
    RectangleStruct bounds = pSurface->GetRect();
    bounds.Height -= 32; // Match the existing bottom UI exclusion.
    if (bounds.Width <= 0 || bounds.Height <= 0)
        return;

    auto* pixels = static_cast<unsigned char*>(pSurface->Lock(0, 0));
    if (!pixels)
        return;

    const int pitch = pSurface->GetPitch();
    if (pitch <= 0)
    {
        pSurface->Unlock();
        return;
    }

    auto putPixel = [&](int x, int y, unsigned short pixelColor)
    {
        // Clip BEFORE calculating an address, including partly offscreen labels.
        if (x >= bounds.X && x < bounds.X + bounds.Width
            && y >= bounds.Y && y < bounds.Y + bounds.Height)
        {
            auto* row = reinterpret_cast<unsigned short*>(pixels + y * pitch);
            row[x] = pixelColor;
        }
    };

    // Paint all shadows first, then all foreground pixels, so neighbouring
    // strokes cannot overwrite one another with their black shadow.
    for (int pass = 0; pass < 2; ++pass)
    {
        const int shadow = pass == 0 ? 1 : 0;
        const auto pixelColor = static_cast<unsigned short>(pass == 0 ? 0 : color);
        for (int i = 0; i < count; ++i)
        {
            for (int y = 0; y < font.Height; ++y)
            {
                for (int x = 0; x < font.Width; ++x)
                {
                    if (!IsIronWallDigitPixel(text[i], x, y, font.Width, font.Height))
                        continue;

                    const int localX = i * (font.Width + font.Gap) + x;
                    // Screen Y grows downwards: +2 X means -1 Y on buildings.
                    // Apply this to every lit pixel, not just each digit origin.
                    const int rise = sloped ? localX / 2 - halfWidth / 2 : 0;
                    putPixel(left + localX + shadow, anchor.Y + y - rise + shadow,
                        pixelColor);
                }
            }
        }
    }

    pSurface->Unlock();
}

void DrawIronWallSelectedHealth(TechnoClass* pThis, int totalPips)
{
    if (!pThis || !pThis->IsSelected || !pThis->IsAlive
        || pThis->InLimbo || pThis->Health <= 0)
    {
        return;
    }

    if (pThis->VisualCharacter(false, nullptr) == VisualType::Hidden)
        return;

    const auto pType = pThis->GetTechnoType();
    if (!pType || pType->Strength <= 0 || !DSurface::Composite)
        return;

    const bool isBuilding = pThis->WhatAmI() == AbstractType::Building;
    const int pipCount = totalPips > 0 ? totalPips : 1;
    const int barPixels = pipCount * (isBuilding ? 4 : 2);
    const auto layout = ChooseIronWallHealthLayout(pThis->Health, pType->Strength, barPixels);
    const int textHeight = layout.TwoLines
        ? layout.Font.Height * 2 + HealthLineGap : layout.Font.Height;
    char firstLine[32] = {};
    char secondLine[16] = {};
    if (layout.TwoLines)
    {
        sprintf_s(firstLine, "%d/", pThis->Health);
        sprintf_s(secondLine, "%d", pType->Strength);
    }
    else
    {
        sprintf_s(firstLine, "%d/%d", pThis->Health, pType->Strength);
    }

    Point2D position;
    if (isBuilding)
    {
        position = TechnoExt::GetBuildingSelectBracketPosition(
            pThis, BuildingSelectBracketPosition::Top);
        // Existing building pip layout: start at Top + (-6,-3),
        // then move (-4,+2) per pip. Centre over the entire bar.
        const int intervals = pipCount - 1;
        position.X -= 6 + intervals * 2;
        position.Y += intervals - 3;
    }
    else
    {
        position = TechnoExt::GetFootSelectBracketPosition(
            pThis, Anchor(HorizontalPosition::Center, VerticalPosition::Top));
    }

    position.Y += pType->PixelSelectionBracketDelta - textHeight - HealthTextGap;
    const auto pExt = TechnoExt::ExtMap.Find(pThis);
    const auto pShield = pExt ? pExt->Shield.get() : nullptr;
    if (pShield && !pShield->IsBrokenAndNonRespawning())
        position.Y -= 4;

    const double ratio = static_cast<double>(pThis->Health) / pType->Strength;
    int color = Drawing::RGB_To_Int(0, 255, 0);
    if (ratio <= RulesClass::Instance->ConditionRed)
        color = Drawing::RGB_To_Int(255, 0, 0);
    else if (ratio <= RulesClass::Instance->ConditionYellow)
        color = Drawing::RGB_To_Int(255, 255, 0);

    DrawIronWallCalculatorText(DSurface::Composite, firstLine, position,
        isBuilding, color, layout.Font);
    if (layout.TwoLines)
    {
        position.Y += layout.Font.Height + HealthLineGap;
        DrawIronWallCalculatorText(DSurface::Composite, secondLine, position,
            isBuilding, color, layout.Font);
    }
}
}


DEFINE_HOOK(0x6F64A9, TechnoClass_DrawHealthBar_Hide, 0x5)
{
	GET(TechnoClass*, pThis, ECX);
	auto pTypeData = TechnoExt::ExtMap.Find(pThis)->TypeExtData;
	if (pTypeData->HealthBar_Hide)
		return 0x6F6AB6;

	return 0;
}

DEFINE_HOOK(0x6F65D1, TechnoClass_DrawHealthBar_Buildings, 0x6)
{
	GET(BuildingClass*, pThis, ESI);
	GET(int, length, EBX);
	GET_STACK(RectangleStruct*, pBound, STACK_OFFSET(0x4C, 0x8));

	const auto pExt = TechnoExt::ExtMap.Find(pThis);

	if (const auto pShieldData = pExt->Shield.get())
	{
		if (pShieldData->IsAvailable() && !pShieldData->IsBrokenAndNonRespawning())
			pShieldData->DrawShieldBar_Building(length, pBound);
	}

	TechnoExt::ProcessDigitalDisplays(pThis);
	DrawIronWallSelectedHealth(pThis, length); // Building bar length for centring.

	return 0;
}

DEFINE_HOOK(0x6F683C, TechnoClass_DrawHealthBar_Units, 0x7)
{
	GET(FootClass*, pThis, ESI);
	GET_STACK(RectangleStruct*, pBound, STACK_OFFSET(0x4C, 0x8));

	const auto pExt = TechnoExt::ExtMap.Find(pThis);

	if (const auto pShieldData = pExt->Shield.get())
	{
		if (pShieldData->IsAvailable() && !pShieldData->IsBrokenAndNonRespawning())
		{
			const int length = pThis->WhatAmI() == AbstractType::Infantry ? 8 : 17;
			pShieldData->DrawShieldBar_Other(length, pBound);
		}
	}

	TechnoExt::ProcessDigitalDisplays(pThis);
	const int totalHealthPips = pThis->WhatAmI() == AbstractType::Infantry ? 8 : 17;
	DrawIronWallSelectedHealth(pThis, totalHealthPips);

	return 0;
}

DEFINE_HOOK(0x6F534E, TechnoClass_DrawExtras_Insignia, 0x5)
{
	enum { SkipGameCode = 0x6F5388 };

	GET(TechnoClass*, pThis, EBP);
	GET_STACK(Point2D*, pLocation, STACK_OFFSET(0x98, 0x4));
	GET(RectangleStruct*, pBounds, ESI);

	if (pThis->VisualCharacter(false, nullptr) != VisualType::Hidden)
	{
		if (RulesExt::Global()->DrawInsignia_OnlyOnSelected.Get() && !pThis->IsSelected && !pThis->IsMouseHovering)
			return SkipGameCode;
		else
			TechnoExt::DrawInsignia(pThis, pLocation, pBounds);
	}

	return SkipGameCode;
}

DEFINE_HOOK(0x709B2E, TechnoClass_DrawPips_Sizes, 0x5)
{
	GET(TechnoClass*, pThis, ECX);
	REF_STACK(int, pipWidth, STACK_OFFSET(0x74, -0x1C));

	Point2D size;
	bool isBuilding = pThis->WhatAmI() == AbstractType::Building;
	auto const pType = pThis->GetTechnoType();

	if (pType->PipScale == PipScale::Ammo)
	{
		if (isBuilding)
			size = RulesExt::Global()->Pips_Ammo_Buildings_Size;
		else
			size = RulesExt::Global()->Pips_Ammo_Size;

		size = TechnoTypeExt::ExtMap.Find(pType)->AmmoPipSize.Get(size);
	}
	else
	{
		if (isBuilding)
			size = RulesExt::Global()->Pips_Generic_Buildings_Size;
		else
			size = RulesExt::Global()->Pips_Generic_Size;
	}

	pipWidth = size.X;
	R->ESI(size.Y);

	return 0;
}

DEFINE_HOOK(0x709B8B, TechnoClass_DrawPips_Spawns, 0x5)
{
	enum { SkipGameDrawing = 0x709C27 };

	GET(TechnoClass*, pThis, ECX);
	auto const pTypeExt = TechnoExt::ExtMap.Find(pThis)->TypeExtData;

	if (!pTypeExt->ShowSpawnsPips)
		return SkipGameDrawing;

	LEA_STACK(RectangleStruct*, offset, STACK_OFFSET(0x74, -0x24));
	GET_STACK(RectangleStruct*, rect, STACK_OFFSET(0x74, 0xC));
	GET_STACK(SHPStruct*, shape, STACK_OFFSET(0x74, -0x58));
	GET_STACK(bool, isBuilding, STACK_OFFSET(0x74, -0x61));
	GET(int, maxSpawnsCount, EBX);

	int currentSpawnsCount = pThis->SpawnManager->CountDockedSpawns();
	auto const pipOffset = pTypeExt->SpawnsPipOffset.Get();
	Point2D position = { offset->X + pipOffset.X, offset->Y + pipOffset.Y };
	Point2D size;

	if (isBuilding)
		size = pTypeExt->SpawnsPipSize.Get(RulesExt::Global()->Pips_Generic_Buildings_Size);
	else
		size = pTypeExt->SpawnsPipSize.Get(RulesExt::Global()->Pips_Generic_Size);

	for (int i = 0; i < maxSpawnsCount; i++)
	{
		int frame = i < currentSpawnsCount ? pTypeExt->SpawnsPipFrame : pTypeExt->EmptySpawnsPipFrame;

		DSurface::Temp->DrawSHP(FileSystem::PALETTE_PAL, shape, frame,
			&position, rect, BlitterFlags(0x600), 0, 0, ZGradient::Ground, 1000, 0, 0, 0, 0, 0);

		position.X += size.X;
		position.Y += size.Y;
	}

	return SkipGameDrawing;
}

DEFINE_HOOK(0x70A36E, TechnoClass_DrawPips_Ammo, 0x6)
{
	enum { SkipGameDrawing = 0x70A4EC };

	GET(TechnoClass*, pThis, ECX);
	LEA_STACK(RectangleStruct*, offset, STACK_OFFSET(0x74, -0x24));
	GET_STACK(RectangleStruct*, rect, STACK_OFFSET(0x74, 0xC));
	GET(int, pipWrap, EBX);
	GET_STACK(int, pipCount, STACK_OFFSET(0x74, -0x54));
	GET_STACK(int, maxPips, STACK_OFFSET(0x74, -0x60));
	GET(int, yOffset, ESI);

	auto const pTypeExt = TechnoExt::ExtMap.Find(pThis)->TypeExtData;
	auto const pipOffset = pTypeExt->AmmoPipOffset.Get();
	Point2D position = { offset->X + pipOffset.X, offset->Y + pipOffset.Y };

	if (pipWrap > 0)
	{
		int levels = maxPips / pipWrap - 1;

		for (int i = 0; i < pipWrap; i++)
		{
			int frame = pTypeExt->AmmoPipWrapStartFrame;

			if (levels >= 0)
			{
				int counter = i + pipWrap * levels;
				int frameCounter = levels;
				bool calculateFrame = true;

				while (counter >= pThis->Ammo)
				{
					frameCounter--;
					counter -= pipWrap;

					if (frameCounter < 0)
					{
						calculateFrame = false;
						break;
					}
				}

				if (calculateFrame)
					frame = frameCounter + frame + 1;
			}

			position.X += offset->Width;
			position.Y += yOffset;

			DSurface::Temp->DrawSHP(FileSystem::PALETTE_PAL, FileSystem::PIPS2_SHP,
				frame, &position, rect, BlitterFlags(0x600), 0, 0, ZGradient::Ground, 1000, 0, 0, 0, 0, 0);
		}
	}
	else
	{
		int ammoFrame = pTypeExt->AmmoPipFrame;
		int emptyFrame = pTypeExt->EmptyAmmoPipFrame;

		for (int i = 0; i < maxPips; i++)
		{
			if (i >= pipCount && emptyFrame < 0)
				break;

			int frame = i >= pipCount ? emptyFrame : ammoFrame;

			DSurface::Temp->DrawSHP(FileSystem::PALETTE_PAL, FileSystem::PIPS2_SHP,
				frame, &position, rect, BlitterFlags(0x600), 0, 0, ZGradient::Ground, 1000, 0, 0, 0, 0, 0);

			position.X += offset->Width;
			position.Y += yOffset;
		}
	}

	return SkipGameDrawing;
}

DEFINE_HOOK(0x70A1F6, TechnoClass_DrawPips_Tiberium, 0x6)
{
	enum { SkipGameDrawing = 0x70A4EC };

	GET(TechnoClass*, pThis, ECX);
	LEA_STACK(RectangleStruct*, offset, STACK_OFFSET(0x74, -0x24));
	GET_STACK(RectangleStruct*, rect, STACK_OFFSET(0x74, 0xC));
	GET_STACK(SHPStruct*, shape, STACK_OFFSET(0x74, -0x58));
	GET_STACK(int, maxPips, STACK_OFFSET(0x74, -0x60));
	GET(int, yOffset, ESI);

	Point2D position = { offset->X, offset->Y };
	const int totalStorage = pThis->GetTechnoType()->Storage;

	std::vector<int> pipsToDraw;
	pipsToDraw.reserve(maxPips);

	bool isWeeder = false;
	auto const whatAmI = pThis->WhatAmI();

	switch (whatAmI)
	{
	case AbstractType::Building:
		isWeeder = static_cast<BuildingClass*>(pThis)->Type->Weeder;
		break;
	case AbstractType::Unit:
		isWeeder = static_cast<UnitClass*>(pThis)->Type->Weeder;
		break;
	default:
		break;
	}

	if (isWeeder)
	{
		const int fullWeedFrames = whatAmI == AbstractType::Building ?
			static_cast<int>(pThis->Owner->GetWeedStoragePercentage() * maxPips + 0.5) :
			static_cast<int>(pThis->Tiberium.GetTotalAmount() / totalStorage * maxPips + 0.5);

		for (int i = 0; i < maxPips; i++)
		{
			if (i < fullWeedFrames)
				pipsToDraw.push_back(RulesExt::Global()->Pips_Tiberiums_WeedFrame);
			else
				pipsToDraw.push_back(RulesExt::Global()->Pips_Tiberiums_WeedEmptyFrame);
		}
	}
	else
	{
		const int count = TiberiumClass::Array.Count;
		std::vector<int> tiberiumPipCounts(count);

		for (size_t i = 0; i < tiberiumPipCounts.size(); i++)
		{
			tiberiumPipCounts[i] = static_cast<int>(pThis->Tiberium.GetAmount(i) / totalStorage * maxPips + 0.5);
		}

		auto const rawPipOrder = RulesExt::Global()->Pips_Tiberiums_DisplayOrder.empty() ? std::vector<int>{ 0, 2, 3, 1 } : RulesExt::Global()->Pips_Tiberiums_DisplayOrder;
		auto const& pipFrames = RulesExt::Global()->Pips_Tiberiums_Frames;
		int const emptyFrame = RulesExt::Global()->Pips_Tiberiums_EmptyFrame;

		std::vector<int> pipOrder;
		pipOrder.reserve(count);

		// First make a new vector, removing all the duplicate and invalid tiberiums
		for (int index : rawPipOrder)
		{
			if (std::find(pipOrder.begin(), pipOrder.end(), index) == pipOrder.end() &&
				index >= 0 && index < count)
			{
				pipOrder.push_back(index);
			}
		}

		// Then add any tiberium types that are missing
		for (int i = 0; i < count; i++)
		{
			if (std::find(pipOrder.begin(), pipOrder.end(), i) == pipOrder.end())
			{
				pipOrder.push_back(i);
			}
		}

		for (int i = 0; i < maxPips; i++)
		{
			for (const int index : pipOrder)
			{
				if (tiberiumPipCounts[index] > 0)
				{
					tiberiumPipCounts[index]--;

					if (static_cast<size_t>(index) >= pipFrames.size())
						pipsToDraw.push_back(index == 1 ? 5 : 2);
					else
						pipsToDraw.push_back(pipFrames.at(index));

					break;
				}
			}

			if (pipsToDraw.size() <= static_cast<size_t>(i))
				pipsToDraw.push_back(emptyFrame);
		}
	}

	for (int pip : pipsToDraw)
	{
		DSurface::Temp->DrawSHP(FileSystem::PALETTE_PAL, shape, pip,
			&position, rect, BlitterFlags::Centered | BlitterFlags::bf_400, 0, 0,
			ZGradient::Ground, 1000, 0, nullptr, 0, 0, 0);

		position.X += offset->Width;
		position.Y += yOffset;
	}

	return SkipGameDrawing;
}

DEFINE_HOOK(0x70A4FB, TechnoClass_DrawPips_SelfHealGain, 0x5)
{
	enum { SkipGameDrawing = 0x70A6C0 };

	GET(TechnoClass*, pThis, ECX);
	GET_STACK(Point2D*, pLocation, STACK_OFFSET(0x74, 0x4));
	GET_STACK(RectangleStruct*, pBounds, STACK_OFFSET(0x74, 0xC));

	TechnoExt::DrawSelfHealPips(pThis, pLocation, pBounds);

	return SkipGameDrawing;
}
