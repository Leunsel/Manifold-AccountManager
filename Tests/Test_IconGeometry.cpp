#include "TestRunner.h"

#include "UI/IconGeometry.h"

using namespace Bunker::UI;

namespace
{
    bool Near(float a, float b, float tolerance = 0.001f)
    {
        const float d = a - b;
        return (d < 0 ? -d : d) <= tolerance;
    }
}

TEST_CASE(IconGeom_CentresSymmetricInk)
{
    const GlyphInk ink{ -5.0f, -5.0f, 5.0f, 5.0f };
    const Point p = CentredIconPos(0.0f, 0.0f, 40.0f, 30.0f, ink);
    CHECK(Near(p.X, 20.0f));
    CHECK(Near(p.Y, 15.0f));
}

TEST_CASE(IconGeom_TheInkEndsUpCentredWhateverTheOffset)
{
    const float minX = 100.0f, minY = 50.0f, maxX = 140.0f, maxY = 80.0f;
    const float wantX = (minX + maxX) * 0.5f;
    const float wantY = (minY + maxY) * 0.5f;

    const GlyphInk inks[] = {
        { 0.0f, 0.0f, 12.0f, 12.0f },
        { -3.0f, -11.0f, 9.0f, 1.0f },
        { 2.0f, 1.0f, 14.0f, 13.0f },
        { -6.0f, -6.0f, 6.0f, 6.0f },
        { 1.875f, 0.5f, 13.125f, 12.5f },
    };

    for (const GlyphInk& ink : inks)
    {
        const Point p = CentredIconPos(minX, minY, maxX, maxY, ink);
        const float inkCentreX = p.X + (ink.X0 + ink.X1) * 0.5f;
        const float inkCentreY = p.Y + (ink.Y0 + ink.Y1) * 0.5f;
        CHECK_MSG(Near(inkCentreX, wantX), "the ink is not centred horizontally");
        CHECK_MSG(Near(inkCentreY, wantY), "the ink is not centred vertically");
    }
}

TEST_CASE(IconGeom_AOnePixelDownshiftIsUndone)
{
    const GlyphInk plain{ -6.0f, -12.0f, 6.0f, 0.0f };
    const GlyphInk shifted{ -6.0f, -11.0f, 6.0f, 1.0f };

    const Point a = CentredIconPos(0.0f, 0.0f, 30.0f, 30.0f, plain);
    const Point b = CentredIconPos(0.0f, 0.0f, 30.0f, 30.0f, shifted);

    CHECK_MSG(Near(b.Y, a.Y - 1.0f),
              "the font's offset was not compensated");
    CHECK(Near(a.Y + (plain.Y0 + plain.Y1) * 0.5f,
               b.Y + (shifted.Y0 + shifted.Y1) * 0.5f));
}

TEST_CASE(IconGeom_LopsidedSideBearingsAreUndoneToo)
{
    const float advance = 18.0f;
    const GlyphInk ink{ 3.0f, -13.0f, 18.0f, 0.0f };

    const Point p = CentredIconPos(0.0f, 0.0f, 30.0f, 30.0f, ink);
    const float inkCentre = p.X + (ink.X0 + ink.X1) * 0.5f;
    CHECK_MSG(Near(inkCentre, 15.0f), "the uneven side bearing remained");

    const float byAdvance = (30.0f - advance) * 0.5f + (ink.X0 + ink.X1) * 0.5f;
    CHECK_MSG(!Near(byAdvance, 15.0f, 0.5f),
              "advance and ink coincide here, the test shows nothing");
}

TEST_CASE(IconGeom_SnapsToWholePixels)
{
    CHECK(Near(SnapToPixels(Point{ 10.4f, 20.6f }).X, 10.0f));
    CHECK(Near(SnapToPixels(Point{ 10.4f, 20.6f }).Y, 21.0f));

    CHECK(Near(SnapToPixels(Point{ -10.4f, -20.6f }).X, -10.0f));
    CHECK(Near(SnapToPixels(Point{ -10.4f, -20.6f }).Y, -21.0f));
    CHECK(Near(SnapToPixels(Point{ -10.6f, 0.0f }).X, -11.0f));
}

TEST_CASE(IconGeom_ATieRoundsUpwardsOnScreen)
{
    CHECK_MSG(Near(SnapToPixels(Point{ 10.5f, 178.5f }).Y, 178.0f),
              "a tie was pushed down");
    CHECK_MSG(Near(SnapToPixels(Point{ 10.5f, 178.5f }).X, 10.0f),
              "a tie was pushed right");

    const GlyphInk eye{ 1.0f, 1.0f, 15.0f, 15.0f };
    const Point p = SnapToPixels(CentredIconPos(0.0f, 173.0f, 35.0f, 200.0f, eye));
    CHECK_EQ(p.Y, 178.0f);
    const float top = p.Y + eye.Y0 - 173.0f;
    const float bottom = 200.0f - (p.Y + eye.Y1);
    CHECK_MSG(top + 1.0f == bottom,
              "the space above and below differs by more than one pixel");
}

TEST_CASE(IconGeom_AnEmptyRectStillHasACentre)
{
    const GlyphInk ink{ -4.0f, -8.0f, 4.0f, 0.0f };
    const Point p = CentredIconPos(7.0f, 9.0f, 7.0f, 9.0f, ink);
    CHECK(Near(p.X, 7.0f));
    CHECK(Near(p.Y, 13.0f));
}
