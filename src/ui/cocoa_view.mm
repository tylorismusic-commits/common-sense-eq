// Common Sense EQ - Cocoa view host
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws the cairo Panel into an NSView and forwards mouse, wheel and keyboard
// events to it.  Coordinates are mapped from the view bounds to the panel's
// 1120x560 logical space, keeping the aspect ratio and centring like the X11
// editor does.

#include "ui/cocoa_view.hpp"

#include <algorithm>
#include <cmath>

#include <cairo.h>

#import <Cocoa/Cocoa.h>

#include "ui/panel.hpp"

using namespace cse::ui;

namespace {

struct ViewState {
  Panel* panel = nullptr;
  UiState* state = nullptr;
  CocoaView::Setter set;
  CocoaView::Sync sync;
};

// Panel-space transform: the panel keeps its aspect ratio and is centred.
struct Placement {
  double scale = 1.0;
  double offsetX = 0.0;
  double offsetY = 0.0;
};

Placement placementFor(double width, double height) {
  Placement p;
  p.scale = std::min(width / static_cast<double>(kWidth),
                     height / static_cast<double>(kHeight));
  if (!(p.scale > 0.0)) p.scale = 1.0;
  p.offsetX = (width - kWidth * p.scale) * 0.5;
  p.offsetY = (height - kHeight * p.scale) * 0.5;
  return p;
}

} // namespace

@interface CseCocoaView : NSView {
 @public
  ViewState* viewState;
  cairo_surface_t* surface;
  cairo_t* context;
  int surfaceWidth;
  int surfaceHeight;
  NSTimer* refreshTimer;
}
- (void)tick:(NSTimer*)timer;
@end

@implementation CseCocoaView

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (BOOL)isOpaque {
  return YES;
}

- (BOOL)isFlipped {
  return NO;
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  [refreshTimer invalidate];
  refreshTimer = nil;
  if (self.window) {
    refreshTimer = [NSTimer timerWithTimeInterval:1.0 / 30.0
                                           target:self
                                         selector:@selector(tick:)
                                         userInfo:nil
                                          repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:refreshTimer
                                 forMode:NSRunLoopCommonModes];
  }
}

- (void)tick:(NSTimer*)timer {
  (void)timer;
  if (viewState && viewState->sync && viewState->state) {
    viewState->sync(*viewState->state);
    [self setNeedsDisplay:YES];
  }
}

- (void)dealloc {
  [refreshTimer invalidate];
  refreshTimer = nil;
  if (context) cairo_destroy(context);
  if (surface) cairo_surface_destroy(surface);
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  if (!viewState || !viewState->panel || !viewState->state) return;

  const double width = self.bounds.size.width;
  const double height = self.bounds.size.height;
  if (width < 1.0 || height < 1.0) return;

  const double backing = self.window ? self.window.backingScaleFactor : 1.0;
  const int pixelWidth = static_cast<int>(std::lround(width * backing));
  const int pixelHeight = static_cast<int>(std::lround(height * backing));
  if (pixelWidth <= 0 || pixelHeight <= 0) return;

  if (!surface || surfaceWidth != pixelWidth || surfaceHeight != pixelHeight) {
    if (context) cairo_destroy(context);
    if (surface) cairo_surface_destroy(surface);
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pixelWidth,
                                         pixelHeight);
    context = cairo_create(surface);
    surfaceWidth = pixelWidth;
    surfaceHeight = pixelHeight;
  }
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) return;

  const Placement place = placementFor(width, height);
  cairo_set_source_rgb(context, 0.031, 0.043, 0.067);
  cairo_paint(context);
  cairo_save(context);
  cairo_translate(context, place.offsetX * backing, place.offsetY * backing);
  cairo_scale(context, place.scale * backing, place.scale * backing);
  viewState->panel->draw(context, *viewState->state);
  cairo_restore(context);
  cairo_surface_flush(surface);

  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGDataProviderRef provider = CGDataProviderCreateWithData(
      nullptr, cairo_image_surface_get_data(surface),
      static_cast<size_t>(pixelWidth) * pixelHeight * 4, nullptr);
  CGImageRef image = CGImageCreate(
      pixelWidth, pixelHeight, 8, 32, cairo_image_surface_get_stride(surface),
      space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
      provider, nullptr, false, kCGRenderingIntentDefault);
  if (image) {
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, 0.0, height);
    CGContextScaleCTM(ctx, 1.0, -1.0);
    CGContextDrawImage(ctx, CGRectMake(0.0, 0.0, width, height), image);
    CGContextRestoreGState(ctx);
    CGImageRelease(image);
  }
  CGDataProviderRelease(provider);
  CGColorSpaceRelease(space);
}

- (void)sendPoint:(NSPoint)point {
  if (!viewState || !viewState->panel || !viewState->state) return;
  const Placement place = placementFor(self.bounds.size.width,
                                        self.bounds.size.height);
  const double y = self.bounds.size.height - point.y;  // panel is y-down
  const double lx = (point.x - place.offsetX) / place.scale;
  const double ly = (y - place.offsetY) / place.scale;
  const bool shift =
      ([NSEvent modifierFlags] & NSEventModifierFlagShift) != 0;
  viewState->panel->pointerMove(*viewState->state, lx, ly, shift,
                                viewState->set);
  [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event {
  if (!viewState || !viewState->panel || !viewState->state) return;
  [self.window makeFirstResponder:self];
  const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const Placement place = placementFor(self.bounds.size.width,
                                       self.bounds.size.height);
  const double y = self.bounds.size.height - point.y;
  const double lx = (point.x - place.offsetX) / place.scale;
  const double ly = (y - place.offsetY) / place.scale;
  const bool shift = (event.modifierFlags & NSEventModifierFlagShift) != 0;
  if (event.clickCount == 2) {
    viewState->panel->doubleClick(*viewState->state, lx, ly, viewState->set);
  } else {
    viewState->panel->pointerDown(*viewState->state, lx, ly, shift,
                                  viewState->set);
  }
  [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event {
  const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  [self sendPoint:point];
}

- (void)mouseUp:(NSEvent*)event {
  (void)event;
  if (viewState && viewState->panel) {
    viewState->panel->pointerUp(*viewState->state);
    [self setNeedsDisplay:YES];
  }
}

- (void)rightMouseDown:(NSEvent*)event {
  if (!viewState || !viewState->panel || !viewState->state) return;
  const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const Placement place = placementFor(self.bounds.size.width,
                                       self.bounds.size.height);
  const double y = self.bounds.size.height - point.y;
  const double lx = (point.x - place.offsetX) / place.scale;
  const double ly = (y - place.offsetY) / place.scale;
  viewState->panel->rightClick(*viewState->state, lx, ly);
  [self setNeedsDisplay:YES];
}

- (void)scrollWheel:(NSEvent*)event {
  if (!viewState || !viewState->panel || !viewState->state) return;
  const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const Placement place = placementFor(self.bounds.size.width,
                                       self.bounds.size.height);
  const double y = self.bounds.size.height - point.y;
  const double lx = (point.x - place.offsetX) / place.scale;
  const double ly = (y - place.offsetY) / place.scale;
  double dy = event.scrollingDeltaY;
  if (event.isDirectionInvertedFromDevice) dy = -dy;
  const bool shift = (event.modifierFlags & NSEventModifierFlagShift) != 0;
  const bool ctrl = (event.modifierFlags & NSEventModifierFlagControl) != 0;
  viewState->panel->wheel(*viewState->state, lx, ly, dy > 0.0 ? 1.0 : -1.0,
                          shift, ctrl, viewState->set);
  [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent*)event {
  if (!viewState || !viewState->panel || !viewState->state) return;
  int key = 0;
  switch (event.keyCode) {
    case 53: key = 27; break;   // Escape
    case 36: key = 13; break;   // Return
    case 76: key = 13; break;   // keypad Enter
    case 51: key = 8; break;    // Backspace
    default: break;
  }
  const char* text = [event.characters UTF8String];
  viewState->panel->key(*viewState->state, key, text, viewState->set);
  [self setNeedsDisplay:YES];
}

@end

namespace cse {
namespace ui {

struct CocoaView::Impl {
  CseCocoaView* view = nil;
  ViewState state;
};

CocoaView::CocoaView() : impl_(std::make_unique<Impl>()) {}

CocoaView::~CocoaView() { detach(); }

void CocoaView::setContent(Panel* panel, UiState* state, Setter set, Sync sync) {
  impl_->state.panel = panel;
  impl_->state.state = state;
  impl_->state.set = std::move(set);
  impl_->state.sync = std::move(sync);
  if (impl_->view) impl_->view->viewState = &impl_->state;
}

void CocoaView::attach(void* parentNsView) {
  if (!impl_->view) {
    impl_->view = [[CseCocoaView alloc]
        initWithFrame:NSMakeRect(0.0, 0.0, kWidth, kHeight)];
    impl_->view->viewState = &impl_->state;
    impl_->view->surface = nullptr;
    impl_->view->context = nullptr;
    impl_->view->surfaceWidth = 0;
    impl_->view->surfaceHeight = 0;
    impl_->view->refreshTimer = nil;
    impl_->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  }
  NSView* parent = (__bridge NSView*)parentNsView;
  if (parent) {
    [parent addSubview:impl_->view];
    [impl_->view setFrame:[parent bounds]];
    [impl_->view setNeedsDisplay:YES];
  }
}

void CocoaView::detach() {
  if (!impl_->view) return;
  [impl_->view removeFromSuperview];
  impl_->view = nil;
}

void CocoaView::resize(int width, int height) {
  if (!impl_->view) return;
  [impl_->view
      setFrame:NSMakeRect(0.0, 0.0, std::max(1, width), std::max(1, height))];
  [impl_->view setNeedsDisplay:YES];
}

void CocoaView::refresh() {
  if (impl_->view) [impl_->view setNeedsDisplay:YES];
}

} // namespace ui
} // namespace cse
