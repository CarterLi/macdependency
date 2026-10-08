//
//  SymbolTableController.h
//  MacDependency
//
//  Created by Konrad Windszus on 18.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

@class MyDocument;

/**
 * The symbols of one image, filtered by what is typed into the search field and
 * by the two filters that hang off the headers of the table: the type of a
 * symbol and its kind. Both are kept here rather than in a control of their
 * own, because the header of a column is where they belong -- the column shows
 * what the filter is about, and a row that is missing is one the column would
 * have shown.
 *
 * The type has a second control, the Export and Import buttons above the table:
 * a filter that is used on almost every visit to the window is worth a click
 * that does not have to go through a menu first. The two are views of one
 * filter, so whichever is used, the other follows.
 */
@interface SymbolTableController : NSArrayController {
	NSPredicate* nameFilter;
	IBOutlet NSSegmentedControl* typeFilterControl;
	IBOutlet NSButton* demangleNamesControl;
	BOOL demangleNames;
	IBOutlet MyDocument* document;

	/** One bit per SymbolTableEntry::Type, the ones that are listed. */
	unsigned int typeFilterMask;
	/** The kind that is listed, or a tag no kind has while all are. */
	NSInteger kindFilterTag;
	/** The headers that show the filters, held weakly. */
	NSHashTable* filterHeaders;
}

- (void)setNameFilter:(NSPredicate*) nameFilter;
- (NSPredicate*)nameFilter;

/**
 * Reads the Export and Import buttons into the filter. Turning off the last one
 * left on would leave a table with nothing in it, so it is put back: an empty
 * table says less than one that still shows something.
 */
- (IBAction)typeFilterChanged:(id)sender;

/**
 * Narrows the table to the symbols of one type, or to all of them. The tag of
 * the menu item is the type, and the one that filters nothing carries a tag no
 * type has.
 */
- (IBAction)typeFilterChosen:(id)sender;

/**
 * Narrows the table to the symbols of one kind, or to all of them. The menu is
 * filled from the kinds the model knows about, so it cannot fall out of step
 * with SymbolTableEntry.
 */
- (IBAction)kindFilterChanged:(id)sender;

/**
 * Turns the type filter into "exports only": the exported symbols of the
 * library that was jumped to are what is being looked for, and the imports of
 * that library would only be in the way. The kind filter is left alone -- the
 * export being looked for carries the same name as the import that was clicked,
 * so it has the same kind and cannot be hidden by what is set.
 */
- (void)showExportedSymbols;

/**
 * Whether the header of this column carries a chevron that opens a filter. The
 * type column and the kind column do, the name column does not.
 */
- (BOOL)hasFilterMenuForTableColumn:(NSTableColumn*)column;

/**
 * Whether the filter of this column is hiding rows right now, which is what
 * makes its chevron stand out.
 */
- (BOOL)hasActiveFilterForTableColumn:(NSTableColumn*)column;

/**
 * What this column's filter offers, with what is in effect ticked. A new menu
 * on every call, so what it shows is what the filter is at that moment.
 */
- (NSMenu*)filterMenuForTableColumn:(NSTableColumn*)column;

/**
 * The headers that draw the chevrons of those filters, so that they can be
 * asked to draw again once a filter changes. They are held weakly: the table
 * owns its header, and a controller has no business keeping one alive.
 */
- (void)addFilterHeaderView:(NSView*)headerView;

- (BOOL)demangleNames;
- (BOOL*)demangleNamesPtr;
- (void)setDemangleNames:(BOOL)demangle;

@end
