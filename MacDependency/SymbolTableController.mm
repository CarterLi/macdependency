//
//  SymbolTableController.m
//  MacDependency
//
//  Created by Konrad Windszus on 18.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

#import "SymbolTableController.h"
#import "SymbolTableEntryTypeFormatter.h"
#import "SymbolTableEntryKindFormatter.h"
#import "MyDocument.h"
#include "MachO/symboltableentry.h"


@interface SymbolTableController()
-(void)setFilter;
- (NSPredicate*) typeFilter;
- (NSPredicate*) kindFilter;
- (NSMenu*) typeFilterMenu;
- (NSMenu*) kindFilterMenu;
- (NSString*) kindOfColumn:(NSTableColumn*)column;
- (BOOL) listsAllTypes;
- (void) updateTypeFilterControl;
- (void) refreshFilterHeaders;
@end

@implementation SymbolTableController


const int TYPE[] = {SymbolTableEntry::TypeExported, SymbolTableEntry::TypeImported};

/** The number of types a symbol can be filtered by. */
static const unsigned int kNumTypes = sizeof(TYPE)/sizeof(*TYPE);

/** The tag of the menu item that leaves every type of symbol listed. */
static const NSInteger kAllTypesTag = -1;

/** The tag of the menu item that leaves every kind of symbol listed. */
static const NSInteger kAllKindsTag = -1;

/**
 * The columns whose header opens a filter, by the identifier the table columns
 * carry. Everything else about the column -- where it sits, how wide it is --
 * is the xib's business and is not repeated here.
 */
static NSString* const kTypeColumnIdentifier = @"Type";
static NSString* const kKindColumnIdentifier = @"Kind";

- (id)initWithCoder:(NSCoder *)decoder {
	self = [super initWithCoder:decoder];
    if (self) {
		demangleNames = true;
		// The symbol table opens on what the image offers to others; the
		// imports are a click away.
		typeFilterMask = 1u << 0;
		kindFilterTag = kAllKindsTag;
		filterHeaders = [NSHashTable weakObjectsHashTable];
	}
    return self;
}

// called when all connections were made
- (void)awakeFromNib {
	[self setFilter];
}

/** Whether the table lists every type of symbol, i.e. nothing is hidden. */
- (BOOL) listsAllTypes {
	return typeFilterMask == (1u << kNumTypes) - 1;
}

/**
 * Shows the filter on the buttons above the table. They are the second view of
 * it, so they follow whatever the menu -- or a jump -- set it to.
 */
- (void) updateTypeFilterControl {
	for (unsigned int index=0; index < kNumTypes; index++) {
		[typeFilterControl setSelected:(typeFilterMask & (1u << index)) != 0
		                    forSegment:(NSInteger)index];
	}
}

- (NSPredicate*) typeFilter {
	NSMutableString* typeFilter = [NSMutableString string];
	
	for (unsigned int index=0; index < kNumTypes; index++) {
		if ((typeFilterMask & (1u << index)) == 0) {
			continue;
		}
		NSString* condition = [NSString stringWithFormat:@"type=%d", TYPE[index]];
		if ([typeFilter length] > 0) {
			[typeFilter appendString:@" or "];
		} 
		[typeFilter appendString:condition];
	}
	
	// select nothing if no filter set
	NSPredicate* predicate;
	if ([typeFilter length] == 0) {
		predicate = [NSPredicate predicateWithValue:NO];
	} else {
		predicate = [NSPredicate predicateWithFormat:typeFilter];
	}
	return predicate;
}

- (NSPredicate*)nameFilter {
	return nameFilter;
}


- (void)setNameFilter:(NSPredicate*) newNameFilter {
	nameFilter = newNameFilter;
	[self setFilter];
}

/**
 * The kind the table is narrowed to, or nil while every kind is listed. The
 * item that filters nothing carries a tag no kind has.
 */
- (NSPredicate*) kindFilter {
	if (kindFilterTag < 0)
		return nil;
	return [NSPredicate predicateWithFormat:@"kind=%d", (int)kindFilterTag];
}

/**
 * The types a symbol can have, with the ones that are listed ticked. The names
 * come from the formatter of the column that shows them.
 */
- (NSMenu*) typeFilterMenu {
	NSMenu* menu = [[NSMenu alloc] init];

	NSMenuItem* allTypes = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"SYMBOL_TYPE_ALL", @"All types")
	                                                  action:@selector(typeFilterChosen:)
	                                           keyEquivalent:@""];
	[allTypes setTarget:self];
	[allTypes setTag:kAllTypesTag];
	[allTypes setState:[self listsAllTypes] ? NSControlStateValueOn : NSControlStateValueOff];
	[menu addItem:allTypes];

	for (unsigned int index=0; index < kNumTypes; index++) {
		NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:[SymbolTableEntryTypeFormatter labelForType:TYPE[index]]
		                                              action:@selector(typeFilterChosen:)
		                                       keyEquivalent:@""];
		[item setTarget:self];
		[item setTag:(NSInteger)index];
		[item setState:typeFilterMask == (1u << index) ? NSControlStateValueOn : NSControlStateValueOff];
		[menu addItem:item];
	}

	return menu;
}

/**
 * The kinds a symbol can have, behind the one that filters nothing. The kinds
 * come from SymbolTableEntry, and their names from the formatter of the column
 * that shows them, so a kind that is added there turns up here without anything
 * else having to change.
 */
- (NSMenu*) kindFilterMenu {
	NSMenu* menu = [[NSMenu alloc] init];

	NSMenuItem* allKinds = [[NSMenuItem alloc] initWithTitle:NSLocalizedString(@"SYMBOL_KIND_ALL", @"All Kinds")
	                                                  action:@selector(kindFilterChanged:)
	                                           keyEquivalent:@""];
	[allKinds setTarget:self];
	[allKinds setTag:kAllKindsTag];
	[allKinds setState:kindFilterTag < 0 ? NSControlStateValueOn : NSControlStateValueOff];
	[menu addItem:allKinds];

	for (unsigned int kind=0; kind < SymbolTableEntry::NumKinds; kind++) {
		NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:[SymbolTableEntryKindFormatter labelForKind:kind]
		                                              action:@selector(kindFilterChanged:)
		                                       keyEquivalent:@""];
		[item setTarget:self];
		[item setTag:(NSInteger)kind];
		[item setState:kindFilterTag == (NSInteger)kind ? NSControlStateValueOn : NSControlStateValueOff];
		[menu addItem:item];
	}

	return menu;
}

- (BOOL)hasFilterMenuForTableColumn:(NSTableColumn*)column {
	return [self kindOfColumn:column] != nil;
}

- (BOOL)hasActiveFilterForTableColumn:(NSTableColumn*)column {
	NSString* identifier = [self kindOfColumn:column];
	if ([identifier isEqual:kTypeColumnIdentifier])
		return ![self listsAllTypes];
	if ([identifier isEqual:kKindColumnIdentifier])
		return kindFilterTag >= 0;
	return NO;
}

- (NSMenu*)filterMenuForTableColumn:(NSTableColumn*)column {
	NSString* identifier = [self kindOfColumn:column];
	if ([identifier isEqual:kTypeColumnIdentifier])
		return [self typeFilterMenu];
	if ([identifier isEqual:kKindColumnIdentifier])
		return [self kindFilterMenu];
	return nil;
}

/**
 * Which filter a column carries, or nil for a column that carries none. The
 * identifier is what says so, which is why the xib gives the two columns one.
 */
- (NSString*) kindOfColumn:(NSTableColumn*)column {
	NSString* identifier = [column identifier];
	if ([identifier isEqual:kTypeColumnIdentifier] || [identifier isEqual:kKindColumnIdentifier])
		return identifier;
	return nil;
}

- (void)addFilterHeaderView:(NSView*)headerView {
	if (headerView != nil)
		[filterHeaders addObject:headerView];
}

- (void)refreshFilterHeaders {
	for (NSView* headerView in filterHeaders)
		[headerView setNeedsDisplay:YES];
}

- (IBAction)typeFilterChanged:(id)sender {
	unsigned int mask = 0;
	for (unsigned int index=0; index < kNumTypes; index++) {
		if ([typeFilterControl isSelectedForSegment:(NSInteger)index])
			mask |= 1u << index;
	}

	// Unticking the last one would empty the table, which is a worse answer
	// than the rows that are still there. The button is put back the way it was.
	if (mask == 0) {
		[self updateTypeFilterControl];
		return;
	}

	typeFilterMask = mask;
	[self setFilter];
}

- (IBAction)typeFilterChosen:(id)sender {
	NSInteger tag = [sender tag];
	if (tag < 0) {
		typeFilterMask = (1u << kNumTypes) - 1;
	} else if ((unsigned int)tag < kNumTypes) {
		typeFilterMask = 1u << tag;
	} else {
		return;
	}
	[self setFilter];
}

- (IBAction)kindFilterChanged:(id)sender {
	kindFilterTag = [sender tag];
	[self setFilter];
}

- (void)showExportedSymbols {
	// TYPE[0] is SymbolTableEntry::TypeExported.
	if (typeFilterMask == (1u << 0))
		return;

	typeFilterMask = 1u << 0;
	[self setFilter];
}

-(void)setFilter {
	NSMutableArray* predicates = [NSMutableArray array];
	if (nameFilter) {
		[predicates addObject:nameFilter];
	}
	// The type filter is always part of it: with no type selected it is the
	// predicate that matches nothing, which is what "no type chosen" means here.
	[predicates addObject:[self typeFilter]];

	NSPredicate* kindFilter = [self kindFilter];
	if (kindFilter) {
		[predicates addObject:kindFilter];
	}

	//NSLog(@"%@", predicates);
	[self setFilterPredicate:[NSCompoundPredicate andPredicateWithSubpredicates:predicates]];

	[self updateTypeFilterControl];
	[self refreshFilterHeaders];
}

- (BOOL)demangleNames {
	return demangleNames;
}

- (BOOL*)demangleNamesPtr {
	return &demangleNames;
}

- (void)setDemangleNames:(BOOL)demangle {
	self->demangleNames = demangle;
	
	// refresh
	[self rearrangeObjects];
}

@end
