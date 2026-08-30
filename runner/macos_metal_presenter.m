#import "macos_metal_presenter.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include "dkc1_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kDkc1MetalFrameSlots = 3,
  kDkc1MetalFrameBytes =
      kDkc1VideoWidescreenWidth * kDkc1VideoHeight * 4,
};

typedef struct Dkc1MetalFrame {
  uint8_t pixels[kDkc1MetalFrameBytes];
  int width;
  int height;
  int presentationWidth;
  uint64_t sequence;
  Dkc1MacPresentationFrameInfo info;
} Dkc1MetalFrame;

typedef struct Dkc1MetalShaderParameters {
  float sourceSize[2];
  float destinationSize[2];
  uint32_t scaling;
  uint32_t padding;
} Dkc1MetalShaderParameters;

typedef struct Dkc1MetalTraceFrame {
  uint64_t sequence;
  Dkc1MacPresentationFrameInfo info;
} Dkc1MetalTraceFrame;

@interface Dkc1MetalView : NSView
- (void)updateMetalDrawableSize;
@end

@implementation Dkc1MetalView

- (CALayer *)makeBackingLayer {
  return [CAMetalLayer layer];
}

- (void)updateMetalDrawableSize {
  CAMetalLayer *metalLayer = (CAMetalLayer *)self.layer;
  if (![metalLayer isKindOfClass:[CAMetalLayer class]])
    return;
  NSRect backing = [self convertRectToBacking:self.bounds];
  const CGFloat width = MAX(1.0, NSWidth(backing));
  const CGFloat height = MAX(1.0, NSHeight(backing));
  metalLayer.drawableSize = CGSizeMake(width, height);
  metalLayer.contentsScale = self.window.backingScaleFactor ?: 1.0;
}

- (void)layout {
  [super layout];
  [self updateMetalDrawableSize];
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  [self updateMetalDrawableSize];
}

@end

@interface Dkc1MetalPresenter : NSObject <CAMetalDisplayLinkDelegate> {
@public
  Dkc1MetalView *view;
  CAMetalLayer *metalLayer;
  id<MTLDevice> device;
  id<MTLCommandQueue> commandQueue;
  id<MTLRenderPipelineState> pipeline;
  id<MTLTexture> sourceTexture;
  CAMetalDisplayLink *displayLink;
  NSThread *displayThread;
  NSCondition *condition;
  NSLock *frameLock;
  NSLock *traceLock;
  BOOL started;
  BOOL stopRequested;
  BOOL stopped;
  BOOL active;
  Dkc1MetalFrame frames[kDkc1MetalFrameSlots];
  Dkc1MetalFrame currentFrame;
  unsigned head;
  unsigned tail;
  unsigned count;
  BOOL hasCurrentFrame;
  unsigned currentRepeats;
  unsigned repeatGoal;
  unsigned callbacksWithoutFrame;
  uint64_t nextSequence;
  uint64_t uploadedSequence;
  uint64_t callbackNumber;
  uint64_t producerDrops;
  uint64_t consumerSkips;
  uint64_t starvedCallbacks;
  int presentationWidth;
  BOOL fullscreen;
  Dkc1MacFullscreenScaling scaling;
  CFTimeInterval previousTargetPresentation;
  CFTimeInterval previousPresentedTime;
  FILE *trace;
}
- (BOOL)buildPipeline:(NSError **)outError;
- (void)runDisplayThread;
- (void)recordPresentedDrawable:(id<MTLDrawable>)drawable
                 targetTimestamp:(CFTimeInterval)targetTimestamp
     targetPresentationTimestamp:(CFTimeInterval)targetPresentationTimestamp
                  callbackNumber:(uint64_t)presentCallback
                            frame:(Dkc1MetalTraceFrame)frame
                      repeatIndex:(unsigned)repeatIndex
                       repeatGoal:(unsigned)presentRepeatGoal
                       queueDepth:(unsigned)queueDepth
                    producerDrops:(uint64_t)presentProducerDrops
                    consumerSkips:(uint64_t)presentConsumerSkips
                 starvedCallbacks:(uint64_t)presentStarvedCallbacks;
@end

static Dkc1MetalPresenter *s_metal_presenter;

static NSString *const kDkc1MetalShaderSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct VertexOutput { float4 position [[position]]; float2 uv; };\n"
     "struct ShaderParameters {\n"
     "  float2 sourceSize; float2 destinationSize;\n"
     "  uint scaling; uint padding;\n"
     "};\n"
     "vertex VertexOutput dkc1_vertex(uint vertexId [[vertex_id]]) {\n"
     "  const float2 positions[] = {\n"
     "    float2(-1.0, 1.0), float2(-1.0, -1.0),\n"
     "    float2(1.0, 1.0), float2(1.0, -1.0) };\n"
     "  const float2 coordinates[] = {\n"
     "    float2(0.0, 0.0), float2(0.0, 1.0),\n"
     "    float2(1.0, 0.0), float2(1.0, 1.0) };\n"
     "  VertexOutput output;\n"
     "  output.position = float4(positions[vertexId], 0.0, 1.0);\n"
     "  output.uv = coordinates[vertexId]; return output;\n"
     "}\n"
     "fragment float4 dkc1_fragment(\n"
     "    VertexOutput input [[stage_in]],\n"
     "    texture2d<float> source [[texture(0)]],\n"
     "    constant ShaderParameters &parameters [[buffer(0)]]) {\n"
     "  constexpr sampler nearestSampler(coord::normalized,\n"
     "      address::clamp_to_edge, filter::nearest);\n"
     "  constexpr sampler linearSampler(coord::normalized,\n"
     "      address::clamp_to_edge, filter::linear);\n"
     "  if (parameters.scaling == 0)\n"
     "    return source.sample(linearSampler, input.uv);\n"
     "  if (parameters.scaling == 2)\n"
     "    return source.sample(nearestSampler, input.uv);\n"
     "  const float2 texel = input.uv * parameters.sourceSize - 0.5;\n"
     "  const float2 base = floor(texel);\n"
     "  const float2 fraction = fract(texel);\n"
     "  const float2 scale = max(parameters.destinationSize /\n"
     "      parameters.sourceSize, float2(1.0));\n"
     "  const float2 flatRegion = 0.5 - 0.5 / scale;\n"
     "  const float2 adjusted = clamp((fraction - flatRegion) * scale,\n"
     "      float2(0.0), float2(1.0));\n"
     "  const float2 coordinate =\n"
     "      (base + adjusted + 0.5) / parameters.sourceSize;\n"
     "  return source.sample(linearSampler, coordinate);\n"
     "}\n";

@implementation Dkc1MetalPresenter

- (BOOL)buildPipeline:(NSError **)outError {
  id<MTLLibrary> library =
      [device newLibraryWithSource:kDkc1MetalShaderSource
                           options:nil error:outError];
  if (!library)
    return NO;
  id<MTLFunction> vertex = [library newFunctionWithName:@"dkc1_vertex"];
  id<MTLFunction> fragment = [library newFunctionWithName:@"dkc1_fragment"];
  if (!vertex || !fragment) {
    if (outError) {
      *outError = [NSError errorWithDomain:@"DKC1MetalPresenter"
                                      code:1
                                  userInfo:@{
        NSLocalizedDescriptionKey: @"Unable to load the Metal shader functions"
      }];
    }
    [vertex release];
    [fragment release];
    [library release];
    return NO;
  }
  MTLRenderPipelineDescriptor *descriptor =
      [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.vertexFunction = vertex;
  descriptor.fragmentFunction = fragment;
  descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  pipeline = [device newRenderPipelineStateWithDescriptor:descriptor
                                                    error:outError];
  [descriptor release];
  [vertex release];
  [fragment release];
  [library release];
  return pipeline != nil;
}

- (void)runDisplayThread {
  @autoreleasepool {
    [NSThread currentThread].qualityOfService =
        NSQualityOfServiceUserInteractive;
    NSRunLoop *runLoop = [NSRunLoop currentRunLoop];
    [displayLink addToRunLoop:runLoop forMode:NSRunLoopCommonModes];
    [condition lock];
    started = YES;
    [condition signal];
    [condition unlock];

    for (;;) {
      [condition lock];
      BOOL shouldStop = stopRequested;
      [condition unlock];
      if (shouldStop)
        break;
      @autoreleasepool {
        [runLoop runMode:NSDefaultRunLoopMode
              beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.050]];
      }
    }

    [displayLink invalidate];
    [condition lock];
    stopped = YES;
    [condition broadcast];
    [condition unlock];
  }
}

- (void)metalDisplayLink:(CAMetalDisplayLink *)link
             needsUpdate:(CAMetalDisplayLinkUpdate *)update
    API_AVAILABLE(macos(14.0)) {
  (void)link;
  @autoreleasepool {
    const CFTimeInterval targetPresentation =
        update.targetPresentationTimestamp;
    const CFTimeInterval targetTimestamp = update.targetTimestamp;
    const CFTimeInterval callbackTimestamp = CACurrentMediaTime();
    if (previousTargetPresentation > 0.0) {
      const double interval =
          targetPresentation - previousTargetPresentation;
      if (interval > 0.0 && interval < 0.100)
        repeatGoal = interval < (1.0 / 90.0) ? 2u : 1u;
    }
    previousTargetPresentation = targetPresentation;

    [frameLock lock];
    if (!active) {
      [frameLock unlock];
      return;
    }
    callbackNumber++;
    if (getenv("DKC1_FPS_STATS") && callbackNumber <= 8) {
      fprintf(stderr,
              "[metal-callback] callback=%llu deadline_lead_ms=%.3f "
              "presentation_lead_ms=%.3f drawable=%llu size=%lux%lu\n",
              (unsigned long long)callbackNumber,
              (targetTimestamp - callbackTimestamp) * 1000.0,
              (targetPresentation - callbackTimestamp) * 1000.0,
              (unsigned long long)update.drawable.drawableID,
              (unsigned long)update.drawable.texture.width,
              (unsigned long)update.drawable.texture.height);
    }
    if (!hasCurrentFrame) {
      callbacksWithoutFrame++;
      if (count >= 2 || (count && callbacksWithoutFrame >= 2)) {
        memcpy(&currentFrame, &frames[head], sizeof currentFrame);
        head = (head + 1) % kDkc1MetalFrameSlots;
        count--;
        hasCurrentFrame = YES;
        currentRepeats = 0;
      }
    } else if (currentRepeats >= repeatGoal) {
      while (count > 2) {
        head = (head + 1) % kDkc1MetalFrameSlots;
        count--;
        consumerSkips++;
      }
      if (count) {
        memcpy(&currentFrame, &frames[head], sizeof currentFrame);
        head = (head + 1) % kDkc1MetalFrameSlots;
        count--;
        currentRepeats = 0;
      } else {
        starvedCallbacks++;
      }
    }
    if (!hasCurrentFrame) {
      [frameLock unlock];
      return;
    }
    currentRepeats++;
    const unsigned presentRepeatIndex = currentRepeats;
    const unsigned presentRepeatGoal = repeatGoal;
    const unsigned presentQueueDepth = count;
    const uint64_t presentCallback = callbackNumber;
    const uint64_t presentProducerDrops = producerDrops;
    const uint64_t presentConsumerSkips = consumerSkips;
    const uint64_t presentStarvedCallbacks = starvedCallbacks;
    const BOOL presentFullscreen = fullscreen;
    const Dkc1MacFullscreenScaling presentScaling = scaling;
    Dkc1MetalFrame *presentFrame = &currentFrame;
    if (!sourceTexture || sourceTexture.width != (NSUInteger)presentFrame->width ||
        sourceTexture.height != (NSUInteger)presentFrame->height) {
      [sourceTexture release];
      MTLTextureDescriptor *textureDescriptor =
          [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
              MTLPixelFormatBGRA8Unorm
                                                   width:presentFrame->width
                                                  height:presentFrame->height
                                               mipmapped:NO];
      textureDescriptor.storageMode = MTLStorageModeShared;
      textureDescriptor.usage = MTLTextureUsageShaderRead;
      sourceTexture = [device newTextureWithDescriptor:textureDescriptor];
    }
    if (!sourceTexture) {
      [frameLock unlock];
      return;
    }
    if (uploadedSequence != presentFrame->sequence) {
      [sourceTexture replaceRegion:MTLRegionMake2D(
                                       0, 0, presentFrame->width,
                                       presentFrame->height)
                           mipmapLevel:0
                             withBytes:presentFrame->pixels
                           bytesPerRow:(NSUInteger)presentFrame->width * 4];
      uploadedSequence = presentFrame->sequence;
    }
    const int sourceWidth = presentFrame->width;
    const int sourceHeight = presentFrame->height;
    const int framePresentationWidth = presentFrame->presentationWidth;
    const Dkc1MetalTraceFrame traceFrame = {
      .sequence = presentFrame->sequence,
      .info = presentFrame->info,
    };
    [frameLock unlock];

    id<CAMetalDrawable> drawable = update.drawable;
    const NSUInteger outputWidth = drawable.texture.width;
    const NSUInteger outputHeight = drawable.texture.height;
    int fittedWidth = (int)outputWidth;
    int fittedHeight = (int)outputHeight;
    if ((uint64_t)outputWidth * (uint64_t)sourceHeight <=
        (uint64_t)outputHeight *
            (uint64_t)framePresentationWidth) {
      fittedHeight = (int)(((uint64_t)outputWidth * sourceHeight +
                            framePresentationWidth / 2) /
                           framePresentationWidth);
    } else {
      fittedWidth = (int)(((uint64_t)outputHeight *
                           framePresentationWidth +
                           sourceHeight / 2) /
                          sourceHeight);
    }
    const int fittedX = ((int)outputWidth - fittedWidth) / 2;
    const int fittedY = ((int)outputHeight - fittedHeight) / 2;

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:pipeline];
    [encoder setViewport:(MTLViewport){
      .originX = fittedX, .originY = fittedY,
      .width = fittedWidth, .height = fittedHeight,
      .znear = 0.0, .zfar = 1.0
    }];
    [encoder setFragmentTexture:sourceTexture atIndex:0];
    Dkc1MetalShaderParameters parameters = {
      .sourceSize = {(float)sourceWidth, (float)sourceHeight},
      .destinationSize = {(float)fittedWidth, (float)fittedHeight},
      .scaling = (uint32_t)(presentFullscreen ? presentScaling
                                             : kDkc1MacFullscreenPixelSharp),
      .padding = 0,
    };
    [encoder setFragmentBytes:&parameters
                       length:sizeof parameters atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0 vertexCount:4];
    [encoder endEncoding];

    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
      if (completed.status == MTLCommandBufferStatusError) {
        fprintf(stderr,
                "[metal-error] callback=%llu status=%ld error=%s\n",
                (unsigned long long)presentCallback,
                (long)completed.status,
                completed.error.localizedDescription.UTF8String ?: "unknown");
      }
    }];

    Dkc1MetalPresenter *presenter = self;
    [drawable addPresentedHandler:^(id<MTLDrawable> presentedDrawable) {
      [presenter recordPresentedDrawable:presentedDrawable
                         targetTimestamp:targetTimestamp
             targetPresentationTimestamp:targetPresentation
                          callbackNumber:presentCallback
                                    frame:traceFrame
                              repeatIndex:presentRepeatIndex
                               repeatGoal:presentRepeatGoal
                               queueDepth:presentQueueDepth
                            producerDrops:presentProducerDrops
                            consumerSkips:presentConsumerSkips
                         starvedCallbacks:presentStarvedCallbacks];
    }];
    /* The display link supplies the presentation target. Queue the ordinary
     * drawable presentation on the command buffer; only explicitly timed
     * presentAtTime:/minimum-duration variants are invalid on this path. */
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
  }
}

- (void)recordPresentedDrawable:(id<MTLDrawable>)drawable
                 targetTimestamp:(CFTimeInterval)targetTimestamp
     targetPresentationTimestamp:(CFTimeInterval)targetPresentationTimestamp
                  callbackNumber:(uint64_t)presentCallback
                            frame:(Dkc1MetalTraceFrame)frame
                      repeatIndex:(unsigned)repeatIndex
                       repeatGoal:(unsigned)presentRepeatGoal
                       queueDepth:(unsigned)queueDepth
                    producerDrops:(uint64_t)presentProducerDrops
                    consumerSkips:(uint64_t)presentConsumerSkips
                 starvedCallbacks:(uint64_t)presentStarvedCallbacks {
  if (!trace)
    return;
  [traceLock lock];
  const CFTimeInterval presented = drawable.presentedTime;
  const double intervalMs = previousPresentedTime > 0.0 && presented > 0.0
      ? (presented - previousPresentedTime) * 1000.0 : 0.0;
  if (presented > 0.0)
    previousPresentedTime = presented;
  fprintf(trace,
          "{\"display_callback\":%llu,\"drawable_id\":%llu,"
          "\"target_timestamp\":%.9f,"
          "\"target_presentation_timestamp\":%.9f,"
          "\"presented_time\":%.9f,\"scanout_interval_ms\":%.6f,"
          "\"source_sequence\":%llu,\"host_frame\":%lld,"
          "\"repeat_index\":%u,\"repeat_goal\":%u,"
          "\"queue_depth\":%u,\"producer_drops\":%llu,"
          "\"consumer_skips\":%llu,\"starved_callbacks\":%llu,"
          "\"camera_x\":%u,\"camera_y\":%u,"
          "\"bg1_hscroll\":%u,\"bg1_vscroll\":%u,"
          "\"bg2_hscroll\":%u,\"bg2_vscroll\":%u,"
          "\"bg3_hscroll\":%u,\"bg3_vscroll\":%u}\n",
          (unsigned long long)presentCallback,
          (unsigned long long)drawable.drawableID,
          targetTimestamp, targetPresentationTimestamp, presented, intervalMs,
          (unsigned long long)frame.sequence,
          (long long)frame.info.host_frame, repeatIndex, presentRepeatGoal,
          queueDepth, (unsigned long long)presentProducerDrops,
          (unsigned long long)presentConsumerSkips,
          (unsigned long long)presentStarvedCallbacks,
          frame.info.camera_x, frame.info.camera_y,
          frame.info.bg_hscroll[0], frame.info.bg_vscroll[0],
          frame.info.bg_hscroll[1], frame.info.bg_vscroll[1],
          frame.info.bg_hscroll[2], frame.info.bg_vscroll[2]);
  if ((presentCallback % 120) == 0)
    fflush(trace);
  [traceLock unlock];
}

- (void)dealloc {
  if (trace) {
    fflush(trace);
    fclose(trace);
  }
  [sourceTexture release];
  [pipeline release];
  [commandQueue release];
  [device release];
  [displayLink release];
  [displayThread release];
  [condition release];
  [frameLock release];
  [traceLock release];
  [view release];
  [super dealloc];
}

@end

int Dkc1MacMetalPresenterStart(void *native_window, double preferred_hz,
                               Dkc1MacFullscreenScaling initialScaling,
                               int initialFullscreen) {
  @autoreleasepool {
    if (s_metal_presenter)
      return 1;
    if (!native_window || preferred_hz <= 0.0)
      return 0;
    if (@available(macOS 14.0, *)) {
      NSWindow *window = (NSWindow *)native_window;
      NSView *contentView = window.contentView;
      if (!contentView)
        return 0;
      Dkc1MetalPresenter *presenter = [[Dkc1MetalPresenter alloc] init];
      presenter->condition = [[NSCondition alloc] init];
      presenter->frameLock = [[NSLock alloc] init];
      presenter->traceLock = [[NSLock alloc] init];
      presenter->repeatGoal = preferred_hz >= 90.0 ? 2u : 1u;
      presenter->active = YES;
      presenter->scaling = initialScaling;
      presenter->fullscreen = initialFullscreen != 0;
      presenter->device = MTLCreateSystemDefaultDevice();
      presenter->commandQueue = [presenter->device newCommandQueue];
      if (!presenter->device || !presenter->commandQueue) {
        fprintf(stderr, "warning: Metal presentation device unavailable\n");
        [presenter release];
        return 0;
      }
      NSError *pipelineError = nil;
      if (![presenter buildPipeline:&pipelineError]) {
        fprintf(stderr, "warning: Metal presentation pipeline failed: %s\n",
                pipelineError.localizedDescription.UTF8String ?: "unknown");
        [presenter release];
        return 0;
      }

      presenter->view = [[Dkc1MetalView alloc] initWithFrame:contentView.bounds];
      presenter->view.autoresizingMask =
          NSViewWidthSizable | NSViewHeightSizable;
      presenter->view.wantsLayer = YES;
      presenter->metalLayer = (CAMetalLayer *)presenter->view.layer;
      presenter->metalLayer.device = presenter->device;
      presenter->metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
      presenter->metalLayer.framebufferOnly = YES;
      presenter->metalLayer.opaque = YES;
      presenter->metalLayer.backgroundColor = NSColor.blackColor.CGColor;
      presenter->metalLayer.maximumDrawableCount = 3;
      /* CAMetalDisplayLink is the sole scanout cadence authority. */
      presenter->metalLayer.displaySyncEnabled = NO;
      presenter->metalLayer.presentsWithTransaction = NO;
      [contentView addSubview:presenter->view
                  positioned:NSWindowAbove relativeTo:nil];
      [presenter->view updateMetalDrawableSize];
      [window makeKeyAndOrderFront:nil];
      [NSApp activateIgnoringOtherApps:YES];

      presenter->displayLink =
          [[CAMetalDisplayLink alloc]
              initWithMetalLayer:presenter->metalLayer];
      presenter->displayLink.delegate = presenter;
      presenter->displayLink.preferredFrameLatency = 1.0f;
      const float rate = (float)preferred_hz;
      presenter->displayLink.preferredFrameRateRange =
          CAFrameRateRangeMake(rate, rate, rate);
      presenter->displayLink.paused = NO;

      const char *tracePath = getenv("DKC1_SCANOUT_LOG");
      if (tracePath && *tracePath) {
        presenter->trace = fopen(tracePath, "wb");
        if (!presenter->trace) {
          fprintf(stderr, "warning: unable to open scanout log: %s\n",
                  tracePath);
        } else {
          fprintf(presenter->trace,
                  "{\"schema\":\"dkc1.scanout.v1\","
                  "\"platform\":\"macos\","
                  "\"emulation_hz\":60.0,"
                  "\"requested_display_hz\":%.6f,"
                  "\"frame_slots\":%d}\n",
                  preferred_hz, kDkc1MetalFrameSlots);
          fflush(presenter->trace);
        }
      }

      presenter->displayThread = [[NSThread alloc]
          initWithTarget:presenter
                selector:@selector(runDisplayThread)
                  object:nil];
      s_metal_presenter = presenter;
      [presenter->displayThread start];
      [presenter->condition lock];
      NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:1.0];
      while (!presenter->started && !presenter->stopped) {
        if (![presenter->condition waitUntilDate:deadline])
          break;
      }
      const BOOL running = presenter->started && !presenter->stopped;
      [presenter->condition unlock];
      if (running) {
        fprintf(stderr,
                "[metal-presenter] active=1 requested_hz=%.3f slots=%d\n",
                preferred_hz, kDkc1MetalFrameSlots);
        return 1;
      }
      Dkc1MacMetalPresenterStop();
    }
    return 0;
  }
}

void Dkc1MacMetalPresenterQueueFrame(
    const uint32_t *pixels, int width, int height, int framePresentationWidth,
    const Dkc1MacPresentationFrameInfo *info) {
  Dkc1MetalPresenter *presenter = s_metal_presenter;
  if (!presenter || !pixels || !info || width <= 0 || height <= 0 ||
      width > kDkc1VideoWidescreenWidth || height > kDkc1VideoHeight)
    return;
  const size_t bytes = (size_t)width * (size_t)height * 4;
  [presenter->frameLock lock];
  if (!presenter->active) {
    [presenter->frameLock unlock];
    return;
  }
  if (presenter->count == kDkc1MetalFrameSlots) {
    presenter->head = (presenter->head + 1) % kDkc1MetalFrameSlots;
    presenter->count--;
    presenter->producerDrops++;
  }
  Dkc1MetalFrame *frame = &presenter->frames[presenter->tail];
  memcpy(frame->pixels, pixels, bytes);
  frame->width = width;
  frame->height = height;
  frame->presentationWidth = framePresentationWidth;
  frame->sequence = ++presenter->nextSequence;
  frame->info = *info;
  presenter->tail = (presenter->tail + 1) % kDkc1MetalFrameSlots;
  presenter->count++;
  [presenter->frameLock unlock];
}

void Dkc1MacMetalPresenterSetGeometry(int newPresentationWidth,
                                      int newFullscreen) {
  Dkc1MetalPresenter *presenter = s_metal_presenter;
  if (!presenter)
    return;
  [presenter->frameLock lock];
  presenter->presentationWidth = newPresentationWidth;
  presenter->fullscreen = newFullscreen != 0;
  presenter->head = presenter->tail = presenter->count = 0;
  presenter->hasCurrentFrame = NO;
  presenter->currentRepeats = 0;
  presenter->callbacksWithoutFrame = 0;
  [presenter->frameLock unlock];
}

void Dkc1MacMetalPresenterSetScaling(Dkc1MacFullscreenScaling newScaling) {
  Dkc1MetalPresenter *presenter = s_metal_presenter;
  if (!presenter)
    return;
  [presenter->frameLock lock];
  presenter->scaling = newScaling;
  [presenter->frameLock unlock];
}

void Dkc1MacMetalPresenterSetActive(int newActive) {
  Dkc1MetalPresenter *presenter = s_metal_presenter;
  if (!presenter)
    return;
  const BOOL active = newActive != 0;
  [presenter->frameLock lock];
  if (presenter->active != active) {
    presenter->active = active;
    presenter->head = presenter->tail = presenter->count = 0;
    presenter->hasCurrentFrame = NO;
    presenter->currentRepeats = 0;
    presenter->callbacksWithoutFrame = 0;
  }
  [presenter->frameLock unlock];
  /* Toggling paused on visibility changes restarts a link that Core Animation
   * suspended after a fully occluded window exhausted its drawable budget. */
  presenter->displayLink.paused = !active;
}

void Dkc1MacMetalPresenterStop(void) {
  @autoreleasepool {
    Dkc1MetalPresenter *presenter = s_metal_presenter;
    if (!presenter)
      return;
    s_metal_presenter = nil;
    [presenter->condition lock];
    presenter->stopRequested = YES;
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:1.0];
    while (!presenter->stopped) {
      if (![presenter->condition waitUntilDate:deadline])
        break;
    }
    [presenter->condition unlock];
    [presenter->view removeFromSuperview];
    [presenter release];
  }
}
