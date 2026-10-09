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

/**
 * The square the funnel of a filter is drawn in.
 *
 * The icon is a filled shape rather than a stroke, and its lines are thin for
 * its size -- a sixteenth of it, where the magnifying glass it replaces was
 * stroked a sixth of its own size. A slightly larger square is what keeps the
 * amount of ink, and with it the weight in the header, about the same.
 */
static const CGFloat kFilterIconSize = 11.0;

/**
 * Where the funnel's own path sits in the square above.
 *
 * The path is written in a box of 1024 by 1024 and covers 817.5 by 828.6 of it
 * around (512, 542.3), so that is what it is scaled by and what it is centred
 * on: a path drawn from its own box would sit off centre in the square.
 */
static const CGFloat kFilterPathWidth = 817.536;
static const CGFloat kFilterPathHeight = 828.608;
static const CGFloat kFilterPathCentreX = 512.0;
static const CGFloat kFilterPathCentreY = 542.304;

/**
 * The funnel a filter is drawn as.
 *
 * It is one SVG path, written in a box of 1024 by 1024 with the y axis running
 * downwards, which is the way this view is laid out as well. Two of its corners
 * are arcs, and those are written below as the curves that go through the same
 * points, Core Graphics having no arc of the kind SVG describes. The shape is
 * built once and only scaled afterwards.
 */
static CGPathRef FilterIconPath(void) {
	static CGPathRef path = NULL;
	if (path != NULL)
		return path;

	CGMutablePathRef funnel = CGPathCreateMutable();
	CGPathMoveToPoint(funnel, NULL, 384.000, 523.392);
	CGPathAddLineToPoint(funnel, NULL, 384.000, 928.000);
	CGPathAddCurveToPoint(funnel, NULL, 384.000, 939.450, 389.447, 949.212, 399.189, 955.227);
	CGPathAddCurveToPoint(funnel, NULL, 408.931, 961.242, 420.100, 961.737, 430.336, 956.608);
	CGPathAddLineToPoint(funnel, NULL, 622.336, 860.608);
	CGPathAddCurveToPoint(funnel, NULL, 633.463, 855.032, 640.000, 844.446, 640.000, 832.000);
	CGPathAddLineToPoint(funnel, NULL, 640.000, 523.392);
	CGPathAddLineToPoint(funnel, NULL, 920.768, 180.288);
	CGPathAddCurveToPoint(funnel, NULL, 928.238, 171.169, 930.117, 159.723, 925.954, 148.694);
	CGPathAddCurveToPoint(funnel, NULL, 921.791, 137.666, 912.818, 130.316, 901.186, 128.406);
	CGPathAddCurveToPoint(funnel, NULL, 889.553, 126.497, 878.702, 130.593, 871.232, 139.712);
	CGPathAddLineToPoint(funnel, NULL, 583.232, 491.712);
	CGPathAddCurveToPoint(funnel, NULL, 578.342, 497.690, 575.994, 504.276, 576.000, 512.000);
	CGPathAddLineToPoint(funnel, NULL, 576.000, 812.224);
	CGPathAddLineToPoint(funnel, NULL, 448.000, 876.224);
	CGPathAddLineToPoint(funnel, NULL, 448.000, 512.000);
	CGPathAddCurveToPoint(funnel, NULL, 448.006, 504.276, 445.658, 497.690, 440.768, 491.712);
	CGPathAddLineToPoint(funnel, NULL, 195.520, 192.000);
	CGPathAddLineToPoint(funnel, NULL, 704.000, 192.000);
	CGPathAddCurveToPoint(funnel, NULL, 721.673, 192.000, 736.000, 177.673, 736.000, 160.000);
	CGPathAddCurveToPoint(funnel, NULL, 736.000, 142.327, 721.673, 128.000, 704.000, 128.000);
	CGPathAddLineToPoint(funnel, NULL, 128.000, 128.000);
	CGPathAddCurveToPoint(funnel, NULL, 115.297, 127.992, 104.497, 134.821, 99.059, 146.301);
	CGPathAddCurveToPoint(funnel, NULL, 93.621, 157.782, 95.178, 170.464, 103.232, 180.288);
	CGPathCloseSubpath(funnel);

	path = funnel;
	return path;
}

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
 * The funnel of a filter, filled.
 *
 * The shape is a path rather than an image, so it is drawn afresh at whatever
 * the screen asks for and stays sharp on a screen with more than one pixel to
 * the point. The path is written with the y axis running downwards, which is
 * the way this view is laid out, so nothing has to be turned around.
 */
- (void) drawFilterIconInRect:(NSRect)area active:(BOOL)active {
	NSRect icon = NSMakeRect(NSMidX(area) - kFilterIconSize / 2.0,
	                         NSMidY(area) - kFilterIconSize / 2.0,
	                         kFilterIconSize, kFilterIconSize);

	CGFloat scale = kFilterIconSize / MAX(kFilterPathWidth, kFilterPathHeight);
	CGAffineTransform transform = CGAffineTransformMake(scale, 0.0, 0.0, scale,
	                                                    NSMidX(icon) - scale * kFilterPathCentreX,
	                                                    NSMidY(icon) - scale * kFilterPathCentreY);
	CGPathRef scaled = CGPathCreateMutableCopyByTransformingPath(FilterIconPath(), &transform);
	NSBezierPath* funnel = [NSBezierPath bezierPathWithCGPath:scaled];
	CGPathRelease(scaled);

	[(active ? [NSColor controlAccentColor] : [NSColor headerTextColor]) setFill];
	[funnel fill];
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
