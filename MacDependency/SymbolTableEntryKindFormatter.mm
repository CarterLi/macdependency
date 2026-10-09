//
//  SymbolTableEntryKindFormatter.mm
//  MacDependency
//
//  Created by Konrad Windszus on 18.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

#import "SymbolTableEntryKindFormatter.h"
#include "MachO/symboltableentry.h"

@implementation SymbolTableEntryKindFormatter

+ (NSString*) labelForKind:(unsigned int)kind {
	switch (kind) {
		case SymbolTableEntry::KindC:
			return NSLocalizedString(@"SYMBOL_KIND_C", @"C");
		case SymbolTableEntry::KindCXX:
			return NSLocalizedString(@"SYMBOL_KIND_CXX", @"C++");
		case SymbolTableEntry::KindSwift:
			return NSLocalizedString(@"SYMBOL_KIND_SWIFT", @"Swift");
		case SymbolTableEntry::KindObjCClass:
			return NSLocalizedString(@"SYMBOL_KIND_OBJC_CLASS", @"ObjC class");
		case SymbolTableEntry::KindObjCMetaclass:
			return NSLocalizedString(@"SYMBOL_KIND_OBJC_METACLASS", @"ObjC metaclass");
		case SymbolTableEntry::KindObjCProtocol:
			return NSLocalizedString(@"SYMBOL_KIND_OBJC_PROTOCOL", @"ObjC protocol");
		case SymbolTableEntry::KindObjCIvar:
			return NSLocalizedString(@"SYMBOL_KIND_OBJC_IVAR", @"ObjC ivar");
		case SymbolTableEntry::KindObjCOther:
			return NSLocalizedString(@"SYMBOL_KIND_OBJC_OTHER", @"ObjC");
	}
	return NSLocalizedString(@"SYMBOL_KIND_UNKNOWN", @"Unknown");
}

// conversion to string
- (NSString*) stringForObjectValue:(id)obj {
	// must be a NSNumber
	if (![obj isKindOfClass:[NSNumber class]]) {
		return nil;
	}

	return [SymbolTableEntryKindFormatter labelForKind:[obj unsignedIntValue]];
}

// conversion from string (not necessary)
- (BOOL) getObjectValue:(id*)obj forString:(NSString*)string errorDescription:(NSString**)errorString {
	return NO;
}
@end
