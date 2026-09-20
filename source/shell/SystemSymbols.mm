#import <AppKit/AppKit.h>

#include "SystemSymbols.h"

namespace {
juce::Image decodePngData(NSData *data) {
  if (data == nil || data.length == 0)
    return {};

  return juce::ImageFileFormat::loadFrom(data.bytes,
                                         static_cast<size_t>(data.length));
}

juce::Image normaliseSymbolAlpha(juce::Image image) {
  if (!image.isValid())
    return {};

  int maximumAlpha = 0;

  for (int y = 0; y < image.getHeight(); ++y)
    for (int x = 0; x < image.getWidth(); ++x)
      maximumAlpha = juce::jmax(maximumAlpha,
                                static_cast<int>(image.getPixelAt(x, y).getAlpha()));

  if (maximumAlpha <= 0 || maximumAlpha >= 255)
    return image;

  auto normalised = image.createCopy();

  for (int y = 0; y < normalised.getHeight(); ++y) {
    for (int x = 0; x < normalised.getWidth(); ++x) {
      const auto alpha = static_cast<int>(image.getPixelAt(x, y).getAlpha());
      const auto scaledAlpha = static_cast<juce::uint8>(
          juce::jlimit(0, 255, (alpha * 255 + maximumAlpha / 2) / maximumAlpha));
      normalised.setPixelAt(x, y,
                            juce::Colour::fromRGBA(255, 255, 255, scaledAlpha));
    }
  }

  return normalised;
}
}

juce::Image loadSystemSymbolImage(const char *symbolName,
                                  const float pointSize) {
  if (symbolName == nullptr || pointSize <= 0.0f)
    return {};

  @autoreleasepool {
    auto *name = [NSString stringWithUTF8String:symbolName];

    auto *configuration = [NSImageSymbolConfiguration
        configurationWithPointSize:pointSize
                            weight:NSFontWeightRegular];
    auto *symbol = [[NSImage imageWithSystemSymbolName:name
                              accessibilityDescription:nil]
        imageWithSymbolConfiguration:configuration];

    if (symbol == nil)
      return {};

    auto *canvas =
        [[NSImage alloc] initWithSize:NSMakeSize(pointSize, pointSize)];
    [canvas lockFocus];
    [NSColor.whiteColor set];
    const auto symbolSize = symbol.size;
    const auto target = NSMakeRect((pointSize - symbolSize.width) * 0.5,
                                   (pointSize - symbolSize.height) * 0.5,
                                   symbolSize.width, symbolSize.height);
    [symbol drawInRect:target
              fromRect:NSZeroRect
             operation:NSCompositingOperationSourceOver
              fraction:1.0
        respectFlipped:YES
                 hints:nil];
    [NSColor.whiteColor set];
    NSRectFillUsingOperation(NSMakeRect(0.0, 0.0, pointSize, pointSize),
                             NSCompositingOperationSourceAtop);
    [canvas unlockFocus];

    auto *tiff = canvas.TIFFRepresentation;

    if (tiff == nil)
      return {};

    auto *bitmap = [NSBitmapImageRep imageRepWithData:tiff];

    if (bitmap == nil)
      return {};

    auto *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG
                                     properties:@{}];
    return normaliseSymbolAlpha(decodePngData(png));

  }
}
