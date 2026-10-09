//
//  SymbolTableHeaderView.h
//  MacDependency
//
//  The header of the symbol table. The headers of the columns that filter the
//  table -- the type of a symbol and its kind -- carry a funnel and open the
//  list of what can be chosen when it is clicked. The rest of the header keeps
//  the behaviour of a plain table header, sorting and resizing included; the
//  funnel stays clear of the sort indicator AppKit draws at the right of a
//  sorted column.
//

#import <Cocoa/Cocoa.h>

@class SymbolTableController;

@interface SymbolTableHeaderView : NSTableHeaderView

/** The controller that knows which columns filter and what they filter by. */
@property (nonatomic, strong) IBOutlet SymbolTableController* filterController;

@end
