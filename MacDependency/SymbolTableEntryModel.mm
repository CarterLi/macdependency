//
//  SymbolEntryModel.mm
//  MacDependency
//
//  Created by Konrad Windszus on 13.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

#import "SymbolTableEntryModel.h"
#import "MachOModel.h"
#import "ConversionStdString.h"
#import "MyDocument.h"
#include "MachO/machodemangleexception.h"

/**
 * The name of a library as the symbol table shows it: the file name alone,
 * without the directories it sits in and without what the file is called on
 * top of the library's name.
 *
 * The install name of a library spells out where it is to be found and what
 * version of it this is -- "/usr/lib/libSystem.B.dylib" -- and none of that is
 * what the column is about: the library is "libSystem". So the path goes, and
 * so does every suffix that is left, the version letter and the extension both
 * being one.
 */
static NSString* ShortLibraryName(NSString* installName) {
	if ([installName length] == 0)
		return @"";

	NSString* name = [installName lastPathComponent];
	NSString* shorter = [name stringByDeletingPathExtension];
	while ([shorter length] > 0 && ![shorter isEqualToString:name]) {
		name = shorter;
		shorter = [name stringByDeletingPathExtension];
	}
	return name;
}

@implementation SymbolTableEntryModel

- (id) initWithEntry:(const SymbolTableEntry*)anEntry demangleNamesPtr:(BOOL*)demangle document:(MyDocument*)aDocument architecture:(MachOArchitecture*)anArchitecture {
	self = [super init];
    if (self) {
		entry = anEntry;
		self->demangleNames = demangle;
		document = aDocument;
		architecture = anArchitecture;
	}
	return self;
}

- (NSString*) name {
	try {
		return [NSString stringWithStdString: entry->getName(*demangleNames)];
	} catch (MachODemangleException& e) {
        // in case of demangling problems (probably c++filt not installed)
		NSString* error = NSLocalizedString(@"ERR_NO_DEMANGLER", nil);
		[document appendLogLine:error withModel:nil state:StateError];
        // disable name demangling
		[[document symbolTableController] setDemangleNames:NO];
	}
	return [NSString stringWithStdString:entry->getName(false)];
}

- (NSNumber*) type {
	return [NSNumber numberWithUnsignedInt:entry->getType()];
}

- (NSNumber*) kind {
	return [NSNumber numberWithUnsignedInt:entry->getKind()];
}

- (NSString*) rawName {
	return [NSString stringWithStdString:entry->getName(false)];
}

- (BOOL) isImported {
	return entry->getType() == SymbolTableEntry::TypeImported;
}

- (unsigned int) libraryOrdinal {
	return entry->getLibraryOrdinal();
}

- (NSString*) from {
	// An image does not import what it exports itself, and a local symbol is
	// not bound to any library either.
	if (![self isImported])
		return @"";

	// The library the row would be taken to by a double click: the one named
	// by the ordinal the symbol carries, or, for a symbol that names none, the
	// one this process has it in.
	MachOModel* provider = [document providerOfImportedSymbol:self];
	return ShortLibraryName([provider name]);
}

- (MachOArchitecture*) architecture {
	return architecture;
}

@end
