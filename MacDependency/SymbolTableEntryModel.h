//
//  SymbolEntryModel.h
//  MacDependency
//
//  Created by Konrad Windszus on 13.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#include "MachO/machofile.h"
#include "MachO/machoarchitecture.h"
#include "MachO/symboltableentry.h"
@class MyDocument;

@interface SymbolTableEntryModel : NSObject {
	const SymbolTableEntry* entry;
	const BOOL* demangleNames;
	MyDocument* document;
	MachOArchitecture* architecture;
}

- (id) initWithEntry:(const SymbolTableEntry*)entry demangleNamesPtr:(BOOL*)demangleNames document:(MyDocument*)document architecture:(MachOArchitecture*)architecture;
- (NSString*) name;
- (NSNumber*) type;

/**
 * What the symbol is -- C, C++, Swift, an Objective-C class and so on -- as
 * the number of a SymbolTableEntry::Kind. The column that shows it carries a
 * formatter, the way the one for the type does.
 */
- (NSNumber*) kind;

/**
 * The symbol name as the string table holds it, without demangling.
 *
 * Demangling turns a name into something to read, but the mangled form is what
 * identifies the symbol -- the Objective-C class symbols are recognised by it.
 */
- (NSString*) rawName;

/** YES for a symbol this image imports, i.e. one another image defines. */
- (BOOL) isImported;

/**
 * The library an imported symbol is bound to, as an ordinal into the load
 * commands of the image: 1 is the first library, 2 the second, and so on.
 * Zero when the symbol is not bound to one library.
 */
- (unsigned int) libraryOrdinal;

/** The image this symbol was read from. */
- (MachOArchitecture*) architecture;
@end
