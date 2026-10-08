//
//  SymbolTableHeaderView.m
//  MacDependency
//
//  Created by Konrad Windszus on 18.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

#import "SymbolTableHeaderView.h"
#import "SymbolTableController.h"

/**
 * How wide the clickable area a filter icon sits in is. The icon is centred in
 * it, so the area is a little wider than the icon itself.
 */
static const CGFloat kFilterAreaWidth = 20.0;

/**
 * What the right edge of a header is left to AppKit, which draws the sort
 * indicator of a sorted column there. Measured: the indicator covers the ten
 * points before the last eight, so the last eighteen are enough to keep the two
 * apart. The band is left free whether or not the column is sorted, so that the
 * filter icon does not move out from under the pointer when the sort changes.
 */
static const CGFloat kSortIndicatorAreaWidth = 18.0;

/** The square the magnifying glass of a filter is drawn in. */
static const CGFloat kFilterIconSize = 9.0;

/** How thick the strokes the magnifying glass is made of are. */
static const CGFloat kFilterIconLineWidth = 1.5;

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
 * The rectangle the filter icon of a column is drawn in and clicked in, or
 * NSZeroRect for a column that does not filter anything and for one that is too
 * narrow to hold both the icon and the sort indicator beside it.
 */
- (NSRect) filterIconRectOfColumn:(NSInteger)column {
	NSTableColumn* tableColumn = [[self tableView] tableColumns][(NSUInteger)column];
	if (![self.filterController hasFilterMenuForTableColumn:tableColumn])
		return NSZeroRect;

	NSRect header = [self headerRectOfColumn:column];
	if (NSWidth(header) < kFilterAreaWidth + kSortIndicatorAreaWidth)
		return NSZeroRect;

	return NSMakeRect(NSMaxX(header) - kSortIndicatorAreaWidth - kFilterAreaWidth, NSMinY(header),
	                  kFilterAreaWidth, NSHeight(header));
}

- (void) drawRect:(NSRect)dirtyRect {
	[super drawRect:dirtyRect];

	if (self.filterController == nil)
		return;

	for (NSInteger column = 0; column < [[self tableView] numberOfColumns]; column++) {
		NSRect area = [self filterIconRectOfColumn:column];
		if (NSEqualRects(area, NSZeroRect))
			continue;

		// A filter that is narrowing the table is worth noticing: the rows that
		// are missing would otherwise look like rows that were never there.
		BOOL active = [self.filterController hasActiveFilterForTableColumn:
		               [[self tableView] tableColumns][(NSUInteger)column]];
		[self drawFilterIconInRect:area active:active];
	}
}

/**
 * A magnifying glass: a lens with a handle running out of its lower right,
 * which is the way round one is held. The view is flipped, so the lower right
 * is where both coordinates grow.
 */
- (void) drawFilterIconInRect:(NSRect)area active:(BOOL)active {
	NSRect icon = NSMakeRect(NSMidX(area) - kFilterIconSize / 2.0,
	                         NSMidY(area) - kFilterIconSize / 2.0,
	                         kFilterIconSize, kFilterIconSize);

	CGFloat diameter = kFilterIconSize * 0.7;
	CGFloat radius = diameter / 2.0 - kFilterIconLineWidth / 2.0;
	NSPoint centre = NSMakePoint(NSMinX(icon) + diameter / 2.0, NSMinY(icon) + diameter / 2.0);
	CGFloat corner = radius * M_SQRT1_2;

	NSBezierPath* glass = [NSBezierPath bezierPath];
	[glass appendBezierPathWithOvalInRect:NSMakeRect(centre.x - radius, centre.y - radius,
	                                                 radius * 2.0, radius * 2.0)];
	// The handle leaves the lens at forty five degrees and ends in the corner of
	// the icon, which is what makes the glass read as one.
	[glass moveToPoint:NSMakePoint(centre.x + corner, centre.y + corner)];
	[glass lineToPoint:NSMakePoint(NSMaxX(icon) - kFilterIconLineWidth / 2.0,
	                               NSMaxY(icon) - kFilterIconLineWidth / 2.0)];
	[glass setLineWidth:kFilterIconLineWidth];
	[glass setLineCapStyle:NSLineCapStyleRound];

	[(active ? [NSColor controlAccentColor] : [NSColor headerTextColor]) setStroke];
	[glass stroke];
}

- (void) mouseDown:(NSEvent*)event {
	NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
	NSInteger column = [self columnAtPoint:point];

	NSRect area = column < 0 ? NSZeroRect : [self filterIconRectOfColumn:column];
	if (NSEqualRects(area, NSZeroRect) || !NSPointInRect(point, area)) {
		// Not the icon: the header does what it always does, which is where the
		// column is sorted from and resized from.
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

	// The menu changed what the table lists, and the icon says whether a filter
	// is on.
	[self setNeedsDisplay:YES];
}

@end
