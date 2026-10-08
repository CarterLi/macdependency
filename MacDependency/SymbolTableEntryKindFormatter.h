//
//  SymbolTableEntryKindFormatter.h
//  MacDependency
//
//  Formatter for the kind of a symbol table entry -- C, C++, Swift, an
//  Objective-C class and so on. The model carries the kind as the number of a
//  SymbolTableEntry::Kind, and a formatter is what turns it into the text the
//  column shows, the way SymbolTableEntryTypeFormatter does for the type.
//

#import <Cocoa/Cocoa.h>


@interface SymbolTableEntryKindFormatter : NSFormatter {

}

/**
 * The text a kind is shown as. The menu that filters the symbol table by kind
 * carries the same labels, so that the column and the filter cannot end up
 * naming the same kind in two different ways.
 */
+ (NSString*) labelForKind:(unsigned int)kind;

@end
