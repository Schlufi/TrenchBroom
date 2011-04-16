//
//  InputManager.h
//  TrenchBroom
//
//  Created by Kristian Duske on 26.01.11.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@class Ray3D;
@class PickingHitList;
@class MapWindowController;
@class CameraTool;
@class SelectionTool;
@class BrushTool;
@class FaceTool;
@class ClipTool;
@protocol Tool;

@interface InputManager : NSObject {
    @private 
    MapWindowController* windowController;

    NSEvent* lastEvent;
    Ray3D* lastRay;
    PickingHitList* lastHits;
    
    CameraTool* cameraTool;
    SelectionTool* selectionTool;
    BrushTool* brushTool;
    FaceTool* faceTool;
    ClipTool* clipTool;
    id <Tool> activeTool;
    id <Tool> cursorOwner;
    BOOL drag;
    BOOL hasMouse;
}

- (id)initWithWindowController:(MapWindowController *)theWindowController;

- (BOOL)handleKeyDown:(NSEvent *)event sender:(id)sender;
- (void)handleFlagsChanged:(NSEvent *)event sender:(id)sender;
- (void)handleLeftMouseDragged:(NSEvent *)event sender:(id)sender;
- (void)handleMouseMoved:(NSEvent *)event sender:(id)sender;
- (void)handleMouseEntered:(NSEvent *)event sender:(id)sender;
- (void)handleMouseExited:(NSEvent *)event sender:(id)sender;
- (void)handleLeftMouseDown:(NSEvent *)event sender:(id)sender;
- (void)handleLeftMouseUp:(NSEvent *)event sender:(id)sender;
- (void)handleRightMouseDragged:(NSEvent *)event sender:(id)sender;
- (void)handleRightMouseDown:(NSEvent *)event sender:(id)sender;
- (void)handleRightMouseUp:(NSEvent *)event sender:(id)sender;
- (void)handleScrollWheel:(NSEvent *)event sender:(id)sender;
- (void)handleBeginGesture:(NSEvent *)event sender:(id)sender;
- (void)handleEndGesture:(NSEvent *)event sender:(id)sender;
- (void)handleMagnify:(NSEvent *)event sender:(id)sender;

- (ClipTool *)clipTool;

@end
