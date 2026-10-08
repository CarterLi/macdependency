//
//  MyDocument.h
//  MacDependency
//
//  Created by Konrad Windszus on 19.06.09.
//  Copyright Konrad Windszus 2009 . All rights reserved.
//


#import <Cocoa/Cocoa.h>
#import "MachO/macho.h"
#import "MachO/machocache.h"
#import "MachOModel.h"
#import "PrioritySplitViewDelegate.h"
#import "SymbolTableController.h"
#import "SymbolTableEntryModel.h"

#include <map>

class ObjCMetadata;
@class ObjCClassDefinitionWindowController;

@interface MyDocument : NSDocument
{
	MachOCache* cache;
	MachO* machO;
	NSArray* contents;
	NSAttributedString* log;
	PrioritySplitViewDelegate* splitViewDelegate;
	IBOutlet NSTreeController* dependenciesController;
	IBOutlet NSTextField* textFieldFilename;
	IBOutlet NSTextField* textFieldBottomBar;
	IBOutlet NSSplitView* mainSplitView;
	IBOutlet SymbolTableController* symbolTableController;
	IBOutlet NSTableView* symbolsTableView;
	IBOutlet NSTabView* bottomTabView;
	IBOutlet NSTextView* definitionTextView;
	unsigned int numDependencies;
	// Parsed Objective-C metadata, one entry per architecture, built on demand:
	// reading the class list of a system framework is not free.
	std::map<MachOArchitecture*, ObjCMetadata*>* objcMetadata;
	// The class definition panels that are currently open.
	NSMutableArray* definitionWindows;
}
- (NSAttributedString*)log;
- (void)setLog:(NSAttributedString *)newLog;
- (void)appendLogLine:(NSString *)line withModel:(MachOModel*)aModel state:(State)aState;
- (void)clearLog;
- (NSString*) workingDirectory;

- (void)incrementNumDependencies;
- (void)resetNumDependencies;

- (MachOCache*)cache;

- (NSArray*)architectures;
- (IBAction)clickRevealInFinder:(id)sender;
- (NSString*)dependencyStatus;
- (SymbolTableController*)symbolTableController;

/**
 * Shows the declaration of the Objective-C class behind the double clicked
 * symbol table row, in a window of its own. A symbol this image does not define
 * is provided by one of its dependencies, and the double click takes you to
 * that library and to the export it makes of the symbol instead.
 */
- (IBAction)showObjCClassDefinition:(id)sender;

/**
 * Puts the selected row of the symbol table on the pasteboard, written the way
 * the table displays it: "Export _AaaBbbCcc". This is what the Edit menu's Copy
 * item sends, and it does something only while the symbol table has the focus
 * and one of its rows is selected.
 */
- (IBAction)copy:(id)sender;
@end
