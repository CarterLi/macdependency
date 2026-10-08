//
//  SymbolTableEntryTypeFormatter.h
//  MacDependency
//
//  Created by Konrad Windszus on 18.07.09.
//  Copyright 2009 Konrad Windszus. All rights reserved.
//

#import <Cocoa/Cocoa.h>


@interface SymbolTableEntryTypeFormatter : NSFormatter {

}

/**
 * What a type of symbol is called. The column that shows the type and the menu
 * that filters by it ask the same place, so the two cannot drift apart.
 */
+ (NSString*) labelForType:(unsigned int)type;

@end
