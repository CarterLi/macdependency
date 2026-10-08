//
//  SymbolTableEntryTypeFormatter.m
//  MacDependency
//
//  Created by Konrad Windszus on 18.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//
//  Formatter for types of symbol table entries. We need a formatter to provide a valid sort order.
#import "SymbolTableEntryTypeFormatter.h"
#include "MachO/symboltableentry.h"

@implementation SymbolTableEntryTypeFormatter

+ (NSString*) labelForType:(unsigned int)type {
	switch (type) {
		case SymbolTableEntry::TypeExported:
			return NSLocalizedString(@"SYMBOL_TYPE_EXPORT", @"Export");
		case SymbolTableEntry::TypeImported:
			return NSLocalizedString(@"SYMBOL_TYPE_IMPORT", @"Import");
	}
	return NSLocalizedString(@"UNKNOWN", @"Unknown");
}

// conversion to string
- (NSString*) stringForObjectValue:(id)obj {
	// must be a NSNumber
	if (![obj isKindOfClass:[NSNumber class]]) {
		return nil;
	}

	// NSNumber contains the type as unsigned int
	return [SymbolTableEntryTypeFormatter labelForType:[obj unsignedIntValue]];
}


// conversion from string (not necessary)
- (BOOL) getObjectValue:(id*)obj forString:(NSString*)string errorDescription:(NSString**)errorString {
	return NO;
}
@end
