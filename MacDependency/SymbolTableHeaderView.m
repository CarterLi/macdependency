//
//  SymbolTableHeaderView.m
//  MacDependency
//
//  Created by Konrad Windszus on 18.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

#import "SymbolTableHeaderView.h"
#import "SymbolTableController.h"

/** How wide the clickable chevron is, and how wide the resize edge stays. */
static const CGFloat kChevronAreaWidth = 24.0;
static const CGFloat kResizeEdgeWidth = 4.0;

/** The size of the chevron itself. */
static const CGFloat kChevronWidth = 7.0;
static const CGFloat kChevronHeight = 4.0;

@implementation SymbolTableHeaderView

/**
 * The controller is what knows what a filter is set to, and the controller is
 * told about this header once the nib has connected the outlet. It holds the
 * header weakly, so registering here does not tie the two lifetimes together.
 */
- (void) awakeFromNib {
	[super awakeFromNib];
	[self.filterController addFilterHeaderView:self];
}

/**
 * The rectangle the chevron of a column is drawn in and clicked in, or
 * NSZeroRect for a column that does not filter anything and for one that is
 * too narrow to hold both the chevron and the edge to resize it by.
 */
- (NSRect) chevronRectOfColumn:(NSInteger)column {
	NSTableColumn* tableColumn = [[self tableView] tableColumns][(NSUInteger)column];
	if (![self.filterController hasFilterMenuForTableColumn:tableColumn])
		return NSZeroRect;

	NSRect header = [self headerRectOfColumn:column];
	if (NSWidth(header) < kChevronAreaWidth + kResizeEdgeWidth)
		return NSZeroRect;

	return NSMakeRect(NSMaxX(header) - kChevronAreaWidth - kResizeEdgeWidth, NSMinY(header),
	                  kChevronAreaWidth, NSHeight(header));
}

- (void) drawRect:(NSRect)dirtyRect {
	[super drawRect:dirtyRect];

	if (self.filterController == nil)
		return;

	for (NSInteger column = 0; column < [[self tableView] numberOfColumns]; column++) {
		NSRect area = [self chevronRectOfColumn:column];
		if (NSEqualRects(area, NSZeroRect))
			continue;

		CGFloat x = NSMidX(area) - kChevronWidth / 2.0;
		CGFloat y = NSMidY(area) - kChevronHeight / 2.0;

		NSBezierPath* chevron = [NSBezierPath bezierPath];
		[chevron moveToPoint:NSMakePoint(x, y + kChevronHeight)];
		[chevron lineToPoint:NSMakePoint(x + kChevronWidth / 2.0, y)];
		[chevron lineToPoint:NSMakePoint(x + kChevronWidth, y + kChevronHeight)];
		[chevron setLineWidth:1.5];
		[chevron setLineCapStyle:NSLineCapStyleRound];

		// A filter that is narrowing the table is worth noticing: the rows that
		// are missing would otherwise look like rows that were never there.
		BOOL active = [self.filterController hasActiveFilterForTableColumn:
		               [[self tableView] tableColumns][(NSUInteger)column]];
		[(active ? [NSColor controlAccentColor] : [NSColor headerTextColor]) setStroke];
		[chevron stroke];
	}
}

- (void) mouseDown:(NSEvent*)event {
	NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
	NSInteger column = [self columnAtPoint:point];

	NSRect area = column < 0 ? NSZeroRect : [self chevronRectOfColumn:column];
	if (NSEqualRects(area, NSZeroRect) || !NSPointInRect(point, area)) {
		// Not the chevron: the header does what it always does, which is where
		// the column is resized from.
		[super mouseDown:event];
		return;
	}

	NSMenu* menu = [self.filterController filterMenuForTableColumn:
	                [[self tableView] tableColumns][(NSUInteger)column]];
	if (menu == nil) {
		[super mouseDown:event];
		return;
	}

	[menu popUpMenuPositioningItem:nil
	                    atLocation:NSMakePoint(NSMinX(area), NSMinY(area))
	                      inView:self];

	// The menu changed what the table lists, and the chevron says whether a
	// filter is on.
	[self setNeedsDisplay:YES];
}

@end
