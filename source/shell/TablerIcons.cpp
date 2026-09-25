#include "TablerIcons.h"

#include <algorithm>
#include <cmath>

namespace
{
juce::Image cropToVisiblePixels(const juce::Image& image)
{
    if (image.isNull())
        return {};

    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    auto left = image.getWidth();
    auto top = image.getHeight();
    auto right = -1;
    auto bottom = -1;

    for (int y = 0; y < image.getHeight(); ++y)
    {
        for (int x = 0; x < image.getWidth(); ++x)
        {
            if (pixels.getPixelColour(x, y).getAlpha() == 0)
                continue;

            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }

    if (right < left || bottom < top)
        return {};

    return image.getClippedImage({ left, top, right - left + 1, bottom - top + 1 });
}

juce::Image limitVisibleInk(const juce::Image& image)
{
    if (image.isNull())
        return {};

    // Keep dense glyphs from looking larger than sparse glyphs at the same 18-pixel bounds.
    constexpr auto maximumInkFraction = 0.34;
    const auto maximumDimension = std::max(image.getWidth(), image.getHeight());
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    double alphaSum = 0.0;

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            alphaSum += static_cast<double>(pixels.getPixelColour(x, y).getAlpha()) / 255.0;

    const auto paddedDimension = std::max(maximumDimension,
        static_cast<int>(std::ceil(std::sqrt(alphaSum / maximumInkFraction))));
    if (paddedDimension == maximumDimension)
        return image;

    juce::Image padded(juce::Image::ARGB, paddedDimension, paddedDimension, true);
    juce::Graphics graphics(padded);
    graphics.drawImageAt(image, (paddedDimension - image.getWidth()) / 2,
                        (paddedDimension - image.getHeight()) / 2);
    return padded;
}

const char* findIconData(const juce::String& fileName, int& dataSize)
{
    for (int index = 0; index < BinaryData::namedResourceListSize; ++index)
    {
        if (fileName == BinaryData::originalFilenames[index])
            return BinaryData::getNamedResource(BinaryData::namedResourceList[index], dataSize);
    }

    dataSize = 0;
    return nullptr;
}
}

juce::Image loadTablerIcon(const juce::String& iconName, const float pointSize)
{
    int dataSize = 0;
    const auto* data = findIconData(iconName + ".svg", dataSize);
    if (data == nullptr || dataSize <= 0)
        return {};

    auto drawable = juce::Drawable::createFromImageData(data, static_cast<size_t>(dataSize));
    if (drawable == nullptr)
        return {};

    constexpr auto renderScale = 4.0f;
    const auto pixelSize = std::max(1, juce::roundToInt(pointSize * renderScale));
    juce::Image image(juce::Image::ARGB, pixelSize, pixelSize, true);
    juce::Graphics graphics(image);
    drawable->drawWithin(graphics, image.getBounds().toFloat(),
                         juce::RectanglePlacement::centred, 1.0f);
    return limitVisibleInk(cropToVisiblePixels(image));
}
