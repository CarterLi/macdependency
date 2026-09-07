//
//  AutoExpandOutlineView.m
//  MacDependency
//
//  Created by Konrad Windszus on 03.09.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

#import "AutoExpandOutlineView.h"


@implementation AutoExpandOutlineView

- (void)awakeFromNib {
	[super awakeFromNib];
	// The nib does not set a delegate; become our own delegate so that
	// -outlineView:willDisplayCell:forTableColumn:item: runs and we can
	// push the cell title past the disclosure triangle.
	if ([self delegate] == nil) {
		[self setDelegate:self];
	}
}

- (void)reloadData;
{
	[super reloadData];

	// auto expand root item
	NSTreeNode* item = [self itemAtRow:0];
	if (item)
		[self expandItem:item];
}

#pragma mark - Hierarchy indentation

// In source-list style the outline column's cell is drawn at a fixed x
// regardless of row level, and -frameOfCellAtColumn:row: is ignored for
// drawing, so the disclosure button lands on top of the leading characters
// of the name. Instead, we keep the cell frame untouched and use the
// cell's built-in -indentationLevel (set in -outlineView:willDisplayCell::
// forTableColumn:item:) to push the title past the button. The button
// itself is repositioned in -frameOfOutlineCellAtRow: so it indents one
// level per row depth, matching the text.

static const CGFloat kDisclosureButtonWidth = 16.0;

- (NSRect)frameOfOutlineCellAtRow:(NSInteger)row {
	NSInteger outlineColumnIndex = [[self tableColumns] indexOfObject:[self outlineTableColumn]];
	NSRect columnRect = [self rectOfColumn:outlineColumnIndex];
	CGFloat indent = [self indentationPerLevel];
	NSInteger level = [self levelForRow:row];
	NSRect rect = [super frameOfOutlineCellAtRow:row];
	rect.origin.x = columnRect.origin.x + level * indent;
	return rect;
}

- (void)outlineView:(NSOutlineView *)outlineView willDisplayCell:(id)cell forTableColumn:(NSTableColumn *)tableColumn item:(id)item {
	if (tableColumn != [outlineView outlineTableColumn]) {
		// Non-outline columns: keep them flush with the row (reset indent).
		if ([cell respondsToSelector:@selector(setIndentationLevel:)]) {
			[cell setIndentationLevel:0];
		}
		return;
	}
	NSInteger level = [outlineView levelForItem:item];
	CGFloat indent = [outlineView indentationPerLevel];
	// Total left offset = room for the disclosure button at this level + the
	// per-level indent. The cell itself stays where AppKit placed it; the
	// title is shifted by this much via -setIndentationLevel:.
	CGFloat total = kDisclosureButtonWidth + level * indent;
	if ([cell respondsToSelector:@selector(setIndentationLevel:)]) {
		[cell setIndentationLevel:(NSInteger)total];
	}
}

@end
